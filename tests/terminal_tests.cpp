#include "test.hpp"

#include "feinstat/application/terminal.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace {

struct PtyProbeResult {
    std::string output;
    int status{0};
};

PtyProbeResult run_secret_probe(int interruption_signal) {
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) throw std::runtime_error("posix_openpt failed");
    if (::grantpt(master) != 0 || ::unlockpt(master) != 0) {
        ::close(master);
        throw std::runtime_error("unable to prepare pseudo-terminal");
    }
    const char* slave_name = ::ptsname(master);
    if (slave_name == nullptr) {
        ::close(master);
        throw std::runtime_error("ptsname failed");
    }

    const pid_t child = ::fork();
    if (child < 0) {
        ::close(master);
        throw std::runtime_error("fork failed");
    }
    if (child == 0) {
        const int slave = ::open(slave_name, O_RDWR);
        if (slave < 0) _exit(20);
        (void)::dup2(slave, STDIN_FILENO);
        (void)::dup2(slave, STDOUT_FILENO);
        (void)::dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO) ::close(slave);
        ::close(master);

        try {
            std::string secret = feinstat::application::read_secret("SECRET_PROMPT> ");
            termios attributes{};
            const bool restored = ::tcgetattr(STDIN_FILENO, &attributes) == 0 &&
                                  (attributes.c_lflag & ECHO) != 0;
            const bool correct = secret == "UnEchoedTestSecret-123";
            feinstat::application::clear_secret(secret);
            std::cout << "READ_OK=" << (correct ? 1 : 0)
                      << " ECHO_RESTORED=" << (restored ? 1 : 0) << '\n' << std::flush;
            _exit(correct && restored ? 0 : 21);
        } catch (const std::exception& exception) {
            termios attributes{};
            const bool restored = ::tcgetattr(STDIN_FILENO, &attributes) == 0 &&
                                  (attributes.c_lflag & ECHO) != 0;
            std::cout << "READ_ERROR=" << exception.what()
                      << " ECHO_RESTORED=" << (restored ? 1 : 0) << '\n' << std::flush;
            _exit(restored ? 0 : 22);
        }
    }

    PtyProbeResult result;
    bool action_sent = false;
    bool child_exited = false;
    int child_status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        pollfd descriptor{master, POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, 100);
        if (ready > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0) {
            char buffer[512];
            const ssize_t count = ::read(master, buffer, sizeof(buffer));
            if (count > 0) result.output.append(buffer, static_cast<std::size_t>(count));
        }

        if (!action_sent && result.output.find("SECRET_PROMPT> ") != std::string::npos) {
            action_sent = true;
            if (interruption_signal != 0) {
                (void)::kill(child, interruption_signal);
            } else {
                constexpr char input[] = "UnEchoedTestSecret-123\n";
                const ssize_t written = ::write(master, input, sizeof(input) - 1);
                if (written != static_cast<ssize_t>(sizeof(input) - 1)) {
                    (void)::kill(child, SIGKILL);
                    (void)::waitpid(child, &child_status, 0);
                    ::close(master);
                    throw std::runtime_error("unable to write pseudo-terminal test input");
                }
            }
        }

        const pid_t waited = ::waitpid(child, &child_status, WNOHANG);
        if (waited == child) {
            child_exited = true;
            // Drain the final line written just before child exit.
            for (;;) {
                char buffer[512];
                const ssize_t count = ::read(master, buffer, sizeof(buffer));
                if (count <= 0) break;
                result.output.append(buffer, static_cast<std::size_t>(count));
            }
            break;
        }
        if (waited < 0 && errno != EINTR) break;
    }

    if (!child_exited) {
        (void)::kill(child, SIGKILL);
        (void)::waitpid(child, &child_status, 0);
        ::close(master);
        throw std::runtime_error("pseudo-terminal secret input test timed out");
    }
    ::close(master);
    result.status = WIFEXITED(child_status) ? WEXITSTATUS(child_status) : 128;
    return result;
}

} // namespace

FS_TEST(secret_input_is_not_echoed_and_terminal_echo_is_restored) {
    const PtyProbeResult result = run_secret_probe(0);
    FS_REQUIRE(result.status == 0);
    FS_REQUIRE(result.output.find("READ_OK=1 ECHO_RESTORED=1") != std::string::npos);
    FS_REQUIRE(result.output.find("UnEchoedTestSecret-123") == std::string::npos);
}

FS_TEST(secret_input_interruption_restores_terminal_echo) {
    const PtyProbeResult result = run_secret_probe(SIGINT);
    FS_REQUIRE(result.status == 0);
    FS_REQUIRE(result.output.find("secret input interrupted") != std::string::npos);
    FS_REQUIRE(result.output.find("ECHO_RESTORED=1") != std::string::npos);
}

FS_TEST(secret_input_sigterm_restores_terminal_echo) {
    const PtyProbeResult result = run_secret_probe(SIGTERM);
    FS_REQUIRE(result.status == 0);
    FS_REQUIRE(result.output.find("secret input interrupted") != std::string::npos);
    FS_REQUIRE(result.output.find("ECHO_RESTORED=1") != std::string::npos);
}

FS_TEST(secret_input_refuses_non_interactive_stdin) {
    int descriptors[2]{};
    FS_REQUIRE(::pipe(descriptors) == 0);
    const pid_t child = ::fork();
    FS_REQUIRE(child >= 0);
    if (child == 0) {
        ::close(descriptors[1]);
        (void)::dup2(descriptors[0], STDIN_FILENO);
        ::close(descriptors[0]);
        try {
            (void)feinstat::application::read_secret("SECRET_PROMPT> ");
            _exit(30);
        } catch (const std::exception& exception) {
            const std::string message = exception.what();
            _exit(message.find("requires an interactive terminal") != std::string::npos ? 0 : 31);
        }
    }
    ::close(descriptors[0]);
    ::close(descriptors[1]);
    int status = 0;
    FS_REQUIRE(::waitpid(child, &status, 0) == child);
    FS_REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
