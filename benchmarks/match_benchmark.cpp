#include "feinstat/exchange/matching_engine.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

std::uint64_t percentile(const std::vector<std::uint64_t>& sorted_values, double percentile_value) {
    if (sorted_values.empty()) return 0;
    const auto index = static_cast<std::size_t>(percentile_value * static_cast<double>(sorted_values.size() - 1));
    return sorted_values[index];
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::size_t operations = 100'000;
        if (argc > 2) throw std::invalid_argument("usage: feinstatic_bench [order-operations]");
        if (argc == 2) {
            const std::string_view argument(argv[1]);
            const auto [end, error] = std::from_chars(argument.data(), argument.data() + argument.size(), operations);
            if (error != std::errc{} || end != argument.data() + argument.size()) {
                throw std::invalid_argument("operation count must be an unsigned integer");
            }
        }
        if (operations < 2 || operations > 2'000'000) {
            throw std::invalid_argument("operation count must be between 2 and 2,000,000");
        }
        const std::size_t pairs = operations / 2;
        if (pairs > (UINT64_MAX - 1) / 100) throw std::overflow_error("benchmark funds overflow");

        feinstat::exchange::MatchingEngine engine;
        engine.create_account("benchmark-buyer", 0, static_cast<feinstat::Amount>(pairs * 100 + 1));
        engine.create_account("benchmark-seller", static_cast<feinstat::Amount>(pairs + 1), 0);

        std::vector<std::uint64_t> latency_ns;
        latency_ns.reserve(operations);
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < operations; ++i) {
            const auto operation_start = std::chrono::steady_clock::now();
            if (i % 2 == 0) {
                (void)engine.submit_limit("benchmark-buyer", feinstat::exchange::Side::Buy, 100, 1);
            } else {
                (void)engine.submit_limit("benchmark-seller", feinstat::exchange::Side::Sell, 100, 1);
            }
            const auto operation_end = std::chrono::steady_clock::now();
            latency_ns.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(operation_end - operation_start).count()));
        }
        const auto end = std::chrono::steady_clock::now();
        std::sort(latency_ns.begin(), latency_ns.end());
        const double elapsed_seconds = std::chrono::duration<double>(end - start).count();
        const double average_ns = static_cast<double>(
            std::accumulate(latency_ns.begin(), latency_ns.end(), std::uint64_t{0})) /
            static_cast<double>(latency_ns.size());

        std::cout << "FeinStatic Match benchmark / synthetic same-price crossing workload\n"
                  << "Compiler             : " << __VERSION__ << '\n'
                  << "Hardware threads    : " << std::thread::hardware_concurrency() << '\n'
                  << "Order operations    : " << operations << '\n'
                  << "Completed trades    : " << engine.trades().size() << '\n'
                  << "Elapsed seconds     : " << std::fixed << std::setprecision(6) << elapsed_seconds << '\n'
                  << "Orders / second     : " << std::setprecision(2)
                  << (static_cast<double>(operations) / elapsed_seconds) << '\n'
                  << "Average latency (us): " << average_ns / 1'000.0 << '\n'
                  << "p50 latency (us)    : " << static_cast<double>(percentile(latency_ns, 0.50)) / 1'000.0 << '\n'
                  << "p95 latency (us)    : " << static_cast<double>(percentile(latency_ns, 0.95)) / 1'000.0 << '\n'
                  << "p99 latency (us)    : " << static_cast<double>(percentile(latency_ns, 0.99)) / 1'000.0 << '\n'
                  << "Note: one process, in-memory book, no network or persistence in timed loop.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "benchmark error: " << exception.what() << '\n';
        return 1;
    }
}
