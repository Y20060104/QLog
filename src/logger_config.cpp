#include "qlog/logger_config.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "qlog/async_logger.hpp"
#include "qlog/backend_config.hpp"

namespace qlog {
namespace {

constexpr std::size_t kNoConfigIndex = std::numeric_limits<std::size_t>::max();

void validate_logger_categories(const LoggerConfig& config) {
    const auto count = config.category_names.size();
    if (count == 0U || count > std::numeric_limits<std::uint32_t>::max() ||
        config.category_enabled.size() != count) {
        throw ConfigValidationError(ConfigError::category_count_mismatch, kNoConfigIndex,
                                    "invalid logger category count");
    }
    for (const auto value : config.category_enabled) {
        if (value > 1U) {
            throw ConfigValidationError(ConfigError::invalid_category_value, kNoConfigIndex,
                                        "logger category value must be 0 or 1");
        }
    }
}

void validate_logger_ring(const LoggerConfig& config) {
    const auto capacity = config.ring.capacity_bytes;
    const auto quota = config.ring.max_payload_bytes;
    if (capacity < 16U || capacity > (std::size_t{1} << 31U) ||
        (capacity & (capacity - 1U)) != 0U) {
        throw std::invalid_argument("invalid ring capacity");
    }
    if (quota < 32U || quota > capacity / 2U) {
        throw std::invalid_argument("invalid record payload quota");
    }
}

void validate_backend_config(const BackendConfig& config) {
    switch (config.thread_mode) {
        case ThreadMode::async:
        case ThreadMode::independent:
            break;
        default:
            throw ConfigValidationError(ConfigError::invalid_backend_config, kNoConfigIndex,
                                        "unknown backend thread mode");
    }
    if (config.records_per_channel == 0U || config.bytes_per_channel == 0U) {
        throw ConfigValidationError(ConfigError::invalid_backend_config, kNoConfigIndex,
                                    "backend quota must be nonzero");
    }
}

void validate_appender_config(const AppenderConfig& config, std::size_t category_count,
                              std::size_t config_index) {
    switch (config.type) {
        case AppenderType::Console:
        case AppenderType::TextFile:
            break;
        default:
            throw ConfigValidationError(ConfigError::invalid_type, config_index,
                                        "unknown appender type");
    }
    if ((config.filter.levels & ~std::uint32_t{0x3F}) != 0U) {
        throw ConfigValidationError(ConfigError::invalid_level_bits, config_index,
                                    "unknown appender level bits");
    }
    if (config.filter.category_enabled.size() != category_count) {
        throw ConfigValidationError(ConfigError::category_count_mismatch, config_index,
                                    "appender category count mismatch");
    }
    for (const auto value : config.filter.category_enabled) {
        if (value > 1U) {
            throw ConfigValidationError(ConfigError::invalid_category_value, config_index,
                                        "appender category value must be 0 or 1");
        }
    }
    const auto& text = config.text;
    if (text.batch_bytes < 65536U || text.batch_bytes > 16U * 1024U * 1024U ||
        text.flush_interval_us == 0U || text.flush_interval_us > 1000000U ||
        text.time_zone.offset_minutes < -840 || text.time_zone.offset_minutes > 840) {
        throw ConfigValidationError(ConfigError::invalid_output_config, config_index,
                                    "invalid text output config");
    }
    if (config.type == AppenderType::Console) {
        switch (config.console.stream) {
            case ConsoleStream::stdout_stream:
            case ConsoleStream::stderr_stream:
                break;
            default:
                throw ConfigValidationError(ConfigError::invalid_output_config, config_index,
                                            "unknown console stream");
        }
    } else {
        if (config.file.path.empty() || config.file.path.find('\0') != std::string::npos) {
            throw ConfigValidationError(ConfigError::invalid_path, config_index,
                                        "file path must be nonempty and contain no NUL");
        }
        if (config.file.retry_interval_us < 1000U || config.file.retry_interval_us > 60000000U) {
            throw ConfigValidationError(ConfigError::invalid_output_config, config_index,
                                        "invalid file retry interval");
        }
    }
}

void normalize_validate_appenders_in_place(std::vector<AppenderConfig>& configs,
                                           std::size_t category_count) {
    if (configs.empty()) {
        AppenderConfig console;
        console.name = "console";
        console.type = AppenderType::Console;
        configs.push_back(std::move(console));
    }
    for (std::size_t i = 0; i < configs.size(); ++i) {
        auto& config = configs[i];
        if (config.filter.category_enabled.empty()) {
            config.filter.category_enabled.assign(category_count, std::uint8_t{1});
        }
        if (config.name.empty()) {
            throw ConfigValidationError(ConfigError::empty_name, i, "empty appender name");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (config.name == configs[j].name) {
                throw ConfigValidationError(ConfigError::duplicate_name, i,
                                            "duplicate appender name");
            }
        }
        validate_appender_config(config, category_count, i);
        if (config.type == AppenderType::TextFile) {
            const auto path = std::filesystem::absolute(std::filesystem::path(config.file.path))
                                  .lexically_normal();
            if (!std::filesystem::is_directory(path.parent_path())) {
                throw ConfigValidationError(ConfigError::invalid_path, i,
                                            "file parent must be an existing directory");
            }
            config.file.path = path.string();
        }
    }
}

}  // namespace

LoggerConfig prepare_logger_config(const LoggerConfig& input) {
    validate_logger_categories(input);
    validate_logger_ring(input);
    validate_backend_config(input.backend);
    LoggerConfig result = input;
    normalize_validate_appenders_in_place(result.appenders, result.category_names.size());
    return result;
}

std::vector<AppenderConfig> prepare_appender_configs(const AppenderConfig* input, std::size_t count,
                                                     std::size_t category_count) {
    if (input == nullptr && count != 0U) {
        throw ConfigValidationError(ConfigError::invalid_pointer, kNoConfigIndex,
                                    "null appender input with nonzero count");
    }
    if (category_count == 0U || category_count > std::numeric_limits<std::uint32_t>::max()) {
        throw ConfigValidationError(ConfigError::category_count_mismatch, kNoConfigIndex,
                                    "invalid logger category count");
    }
    std::vector<AppenderConfig> result;
    if (count != 0U) {
        result.assign(input, input + count);
    }
    normalize_validate_appenders_in_place(result, category_count);
    return result;
}

std::uint32_t merge_appender_levels(const AppenderConfig* configs, std::size_t count) noexcept {
    std::uint32_t merged = 0U;
    for (std::size_t i = 0; i < count; ++i) {
        merged |= configs[i].filter.levels;
    }
    return merged;
}

}  // namespace qlog
