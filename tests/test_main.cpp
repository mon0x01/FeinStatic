#include "test.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    const char* filter_env = std::getenv("FEINSTATIC_TEST_FILTER");
    const std::string filter = argc > 1 ? argv[1] : (filter_env == nullptr ? "" : filter_env);
    std::size_t selected = 0;
    std::size_t passed = 0;
    std::size_t failed = 0;
    for (const auto& test_case : test::registry()) {
        if (!filter.empty() && test_case.name.find(filter) == std::string::npos) continue;
        ++selected;
        try {
            test_case.run();
            ++passed;
            std::cout << "[PASS] " << test_case.name << std::endl;
        } catch (const std::exception& exception) {
            ++failed;
            std::cout << "[FAIL] " << test_case.name << " — " << exception.what() << std::endl;
        } catch (...) {
            ++failed;
            std::cout << "[FAIL] " << test_case.name << " — unknown exception" << std::endl;
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed, "
              << selected << " selected, " << test::registry().size() << " registered" << std::endl;
    if (selected == 0) {
        std::cerr << "No tests matched filter: " << filter << '\n';
        return 2;
    }
    return failed == 0 ? 0 : 1;
}
