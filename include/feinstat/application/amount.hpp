#pragma once

#include "feinstat/common/types.hpp"

#include <string>

namespace feinstat::application {

[[nodiscard]] Amount parse_amount(const std::string& text);
[[nodiscard]] std::string format_amount(Amount amount);

} // namespace feinstat::application
