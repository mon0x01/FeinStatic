#include "feinstat/application/terminal.hpp"

#include <openssl/crypto.h>

#include <csignal>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <termios.h>
#include <unistd.h>

namespace feinstat::application {
namespace {

volatile std::sig_atomic_t secret_input_signal = 0;

extern "C" void handle_secret_input_signal(int signal_number) {
    secret_input_signal = signal_number;
}

class SignalGuard {
public:
    SignalGuard() {
        secret_input_signal = 0;
        struct sigaction action {};
        action.sa_handler = handle_secret_input_signal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0; // Deliberately omit SA_RESTART so input can unwind.
        if (::sigaction(SIGINT, &action, &old_int_) != 0) {
            throw std::runtime_error("unable to install secret-input SIGINT handler");
        }
        int_installed_ = true;
        if (::sigaction(SIGTERM, &action, &old_term_) != 0) {
            (void)::sigaction(SIGINT, &old_int_, nullptr);
            int_installed_ = false;
            throw std::runtime_error("unable to install secret-input SIGTERM handler");
        }
        term_installed_ = true;
    }

    ~SignalGuard() {
        if (term_installed_) (void)::sigaction(SIGTERM, &old_term_, nullptr);
        if (int_installed_) (void)::sigaction(SIGINT, &old_int_, nullptr);
    }

    SignalGuard(const SignalGuard&) = delete;
    SignalGuard& operator=(const SignalGuard&) = delete;

private:
    struct sigaction old_int_ {};
    struct sigaction old_term_ {};
    bool int_installed_{false};
    bool term_installed_{false};
};

class TerminalEchoGuard {
public:
    explicit TerminalEchoGuard(int fd) : fd_(fd) {
        if (::tcgetattr(fd_, &original_) != 0) {
            throw std::runtime_error("unable to read terminal settings for secret input");
        }
        termios hidden = original_;
        hidden.c_lflag &= static_cast<tcflag_t>(~(ECHO | ECHONL));
        if (::tcsetattr(fd_, TCSAFLUSH, &hidden) != 0) {
            throw std::runtime_error("unable to disable terminal echo for secret input");
        }
        active_ = true;
    }

    ~TerminalEchoGuard() {
        (void)restore();
    }

    [[nodiscard]] bool restore() noexcept {
        if (!active_) return true;
        if (::tcsetattr(fd_, TCSANOW, &original_) != 0) return false;
        active_ = false;
        return true;
    }

    TerminalEchoGuard(const TerminalEchoGuard&) = delete;
    TerminalEchoGuard& operator=(const TerminalEchoGuard&) = delete;

private:
    int fd_;
    termios original_{};
    bool active_{false};
};

} // namespace

std::string read_secret(const std::string& prompt) {
    if (::isatty(STDIN_FILENO) != 1) {
        throw std::runtime_error("secret input requires an interactive terminal; refusing echoed input");
    }

    SignalGuard signal_guard;
    TerminalEchoGuard echo_guard(STDIN_FILENO);
    std::cout << prompt << std::flush;

    std::string secret;
    bool read_ok = false;
    bool reached_eof = false;
    for (;;) {
        char value = '\0';
        const ssize_t count = ::read(STDIN_FILENO, &value, 1);
        if (count == 1) {
            if (value == '\n' || value == '\r') {
                read_ok = true;
                break;
            }
            if (value == '\0') {
                clear_secret(secret);
                (void)echo_guard.restore();
                throw std::invalid_argument("secret input contains a NUL byte");
            }
            if (secret.size() >= 4096) {
                clear_secret(secret);
                (void)echo_guard.restore();
                throw std::length_error("secret exceeds the 4096-byte input limit");
            }
            secret.push_back(value);
            continue;
        }
        if (count == 0) {
            reached_eof = true;
            break;
        }
        if (errno == EINTR) {
            if (secret_input_signal != 0) break;
            continue;
        }
        const int read_error = errno;
        clear_secret(secret);
        (void)echo_guard.restore();
        throw std::runtime_error(std::string("unable to read secret from terminal: ") +
                                 std::strerror(read_error));
    }

    const std::sig_atomic_t interrupted_by = secret_input_signal;
    const bool terminal_restored = echo_guard.restore();
    std::cout << '\n' << std::flush;
    if (!terminal_restored) {
        clear_secret(secret);
        throw std::runtime_error("failed to restore terminal echo after secret input");
    }

    if (interrupted_by != 0) {
        clear_secret(secret);
        throw std::runtime_error("secret input interrupted; terminal echo restored");
    }
    if (!read_ok || reached_eof) {
        clear_secret(secret);
        throw std::runtime_error("unable to read secret from terminal");
    }
    if (secret.empty()) {
        clear_secret(secret);
        throw std::invalid_argument("secret cannot be empty");
    }
    if (secret.size() > 4096) {
        clear_secret(secret);
        throw std::length_error("secret exceeds the 4096-byte input limit");
    }
    return secret;
}

std::string read_confirmed_secret(const std::string& prompt,
                                  const std::string& confirmation_prompt) {
    std::string first = read_secret(prompt);
    try {
        std::string second = read_secret(confirmation_prompt);
        const bool matches = first == second;
        clear_secret(second);
        if (!matches) {
            clear_secret(first);
            throw std::invalid_argument("passphrases do not match");
        }
    } catch (...) {
        clear_secret(first);
        throw;
    }
    return first;
}

void clear_secret(std::string& secret) noexcept {
    if (!secret.empty()) OPENSSL_cleanse(secret.data(), secret.size());
    secret.clear();
}

} // namespace feinstat::application
