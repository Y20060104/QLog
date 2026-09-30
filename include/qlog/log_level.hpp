#pragma once
#include <cstdint>
namespace qlog {
enum class LogLevel : std::int32_t {
    verbose = 0,
    debug = 1,
    info = 2,
    warning = 3,
    error = 4,
    fatal = 5,
    log_level_max = 32,
};

[[nodiscard]] constexpr bool valid_level(LogLevel level) noexcept {
    const auto value = static_cast<std::int32_t>(level);
    return value >= 0 && value <= 5;
}

}  // namespace qlog