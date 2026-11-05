#include "test.hpp"
#include "feinstat/application/verification.hpp"

#include "feinstat/application/amount.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <system_error>

using namespace feinstat;

FS_TEST(amount_parser_uses_exact_integer_atoms) {
    FS_REQUIRE(application::parse_amount("1") == kAtomsPerCoin);
    FS_REQUIRE(application::parse_amount("0.00000001") == 1);
    FS_REQUIRE(application::parse_amount("12.34567890") == 1'234'567'890ULL);
    FS_REQUIRE(application::format_amount(1'234'567'890ULL) == "12.3456789");
    FS_REQUIRE(application::format_amount(kAtomsPerCoin) == "1");
}

FS_TEST(amount_parser_rejects_ambiguous_or_unsafe_input) {
    FS_REQUIRE_THROWS(application::parse_amount("-1"));
    FS_REQUIRE_THROWS(application::parse_amount("1.000000001"));
    FS_REQUIRE_THROWS(application::parse_amount("1.2.3"));
    FS_REQUIRE_THROWS(application::parse_amount("18446744073709551615"));
}


FS_TEST(database_verification_reports_missing_database_without_creating_it) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = (std::filesystem::temp_directory_path() /
        ("feinstatic-missing-verification-" + std::to_string(stamp) + ".db")).string();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    const auto report = feinstat::application::verify_database(path);
    FS_REQUIRE(!report.success());
    FS_REQUIRE(!report.ledger_opened);
#ifdef FEINSTATIC_TEST_EXECUTABLE
    const std::string cli_command = std::string("\"") + FEINSTATIC_TEST_EXECUTABLE +
        "\" verify \"" + path + "\" > /dev/null 2>&1";
    FS_REQUIRE(std::system(cli_command.c_str()) != 0);
#endif
    FS_REQUIRE(!std::filesystem::exists(path));
}
