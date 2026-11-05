#include "feinstat/application/amount.hpp"

#include <limits>
#include <stdexcept>

namespace feinstat::application {

Amount parse_amount(const std::string& text) {
    if (text.empty()) throw std::invalid_argument("amount cannot be empty");
    const auto separator = text.find('.');
    if (separator != std::string::npos && text.find('.', separator + 1) != std::string::npos) {
        throw std::invalid_argument("amount contains more than one decimal point");
    }
    const std::string whole_text = separator == std::string::npos ? text : text.substr(0, separator);
    const std::string fraction_text = separator == std::string::npos ? "" : text.substr(separator + 1);
    if (whole_text.empty() || fraction_text.size() > 8) {
        throw std::invalid_argument("amount must have a whole part and at most 8 decimal places");
    }
    Amount whole = 0;
    for (const char c : whole_text) {
        if (c < '0' || c > '9') throw std::invalid_argument("amount must contain only decimal digits");
        const Amount digit = static_cast<Amount>(c - '0');
        if (whole > (UINT64_MAX - digit) / 10) throw std::overflow_error("amount is too large");
        whole = whole * 10 + digit;
    }
    Amount fraction = 0;
    for (const char c : fraction_text) {
        if (c < '0' || c > '9') throw std::invalid_argument("amount must contain only decimal digits");
        fraction = fraction * 10 + static_cast<Amount>(c - '0');
    }
    for (std::size_t i = fraction_text.size(); i < 8; ++i) fraction *= 10;
    if (whole > UINT64_MAX / kAtomsPerCoin) throw std::overflow_error("amount is too large");
    const Amount whole_atoms = whole * kAtomsPerCoin;
    if (whole_atoms > UINT64_MAX - fraction) throw std::overflow_error("amount is too large");
    return whole_atoms + fraction;
}

std::string format_amount(Amount amount) {
    const Amount whole = amount / kAtomsPerCoin;
    Amount fraction = amount % kAtomsPerCoin;
    if (fraction == 0) return std::to_string(whole);
    std::string fraction_text = std::to_string(fraction + kAtomsPerCoin).substr(1);
    while (!fraction_text.empty() && fraction_text.back() == '0') fraction_text.pop_back();
    return std::to_string(whole) + "." + fraction_text;
}

} // namespace feinstat::application
