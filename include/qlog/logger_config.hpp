#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "qlog/appender_config.hpp"
#include "qlog/management_result.hpp"

namespace qlog {
struct LoggerConfig;

class ConfigValidationError final : public std::invalid_argument {
   public:
    ConfigValidationError(ConfigError error_reason, std::size_t index, const char* message)
        : std::invalid_argument(message), reason(error_reason), config_index(index) {}

    ConfigError reason;
    std::size_t config_index;
};

[[nodiscard]] LoggerConfig prepare_logger_config(const LoggerConfig& input);
[[nodiscard]] std::vector<AppenderConfig> prepare_appender_configs(const AppenderConfig* input,
                                                                   std::size_t count,
                                                                   std::size_t category_count);
[[nodiscard]] std::uint32_t merge_appender_levels(const AppenderConfig* configs,
                                                  std::size_t count) noexcept;

}  // namespace qlog
