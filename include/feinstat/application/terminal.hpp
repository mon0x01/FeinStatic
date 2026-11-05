#pragma once

#include <string>

namespace feinstat::application {

// Read a secret from an interactive terminal without echoing it. Fails closed
// when stdin is not a terminal rather than silently falling back to plaintext.
[[nodiscard]] std::string read_secret(const std::string& prompt);

// Read and confirm a secret. Both temporary strings are cleansed before return
// or on mismatch; the returned value is owned by the caller.
[[nodiscard]] std::string read_confirmed_secret(const std::string& prompt,
                                                const std::string& confirmation_prompt);

// Best-effort overwrite of a secret string's active bytes before releasing it.
void clear_secret(std::string& secret) noexcept;

} // namespace feinstat::application
