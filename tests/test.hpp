#pragma once

#include <exception>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace test {

struct Case {
    std::string name;
    std::function<void()> run;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(std::string name, std::function<void()> function) {
        registry().push_back({std::move(name), std::move(function)});
    }
};

inline void require(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        std::ostringstream message;
        message << file << ':' << line << ": requirement failed: " << expression;
        throw std::runtime_error(message.str());
    }
}

} // namespace test

#define FS_TEST(name) \
    static void name(); \
    static ::test::Registrar name##_registrar(#name, name); \
    static void name()

#define FS_REQUIRE(expression) \
    ::test::require(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

#define FS_REQUIRE_THROWS(expression) \
    do { \
        bool fs_threw_ = false; \
        try { (void)(expression); } catch (const std::exception&) { fs_threw_ = true; } \
        ::test::require(fs_threw_, "throws: " #expression, __FILE__, __LINE__); \
    } while (false)
