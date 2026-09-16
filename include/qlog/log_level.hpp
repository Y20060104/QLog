#pragma once
#include <cstdint>
namespace qlog {
enum class LogLevel : std::uint8_t {
    verbose = 0,
    debug = 1,
    info = 2,
    warning = 3,
    error = 4,
    fatal = 5,
};

[[nodiscard]] constexpr bool valid_level(LogLevel level) noexcept {
    return static_cast<std::uint8_t>(level) <= 5U;
}

}  // namespace qlog