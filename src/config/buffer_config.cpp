#include "qlog/config/buffer_config.hpp"

#include <limits>

#include "qlog/utility/string_utils.hpp"

namespace qlog::config {
qlog::buffer::LogBufferConfig get_log_buffer_config_by_config(
    const std::string& log_name, const std::vector<std::string>& log_categories_name,
    const PropertyValue& log_config, bool memory_map_supported) {
    qlog::buffer::LogBufferConfig buffer_config;
    buffer_config.log_name = log_name;
    buffer_config.log_categories_name = log_categories_name;

    if (log_config.is_object()) {
        if (log_config["buffer_size"].is_integral()) {
            buffer_config.default_buffer_size =
                static_cast<std::uint32_t>(log_config["buffer_size"]);
        }

        if (log_config["recovery"].is_bool()) {
            buffer_config.need_recovery =
                static_cast<bool>(log_config["recovery"]) && memory_map_supported;
        }

        if (log_config["buffer_policy_when_full"]
                .is_string()) {  // CR:直接用 buffer_policy_when_full
                                 // ？如果枚举的不是这个呢？logconfig也有嘛？
            const auto policy_config =
                static_cast<std::string>(log_config["buffer_policy_when_full"]);

            if (qlog::utility::equals_ignore_case(policy_config, "discard")) {
                buffer_config.policy = qlog::buffer::LogMemoryPolicy::discard_when_full;
            } else if (qlog::utility::equals_ignore_case(policy_config, "block")) {
                buffer_config.policy = qlog::buffer::LogMemoryPolicy::block_when_full;
            } else if (qlog::utility::equals_ignore_case(policy_config, "expand")) {
                buffer_config.policy = qlog::buffer::LogMemoryPolicy::auto_expand_when_full;
            }
        }

        if (log_config["high_perform_mode_freq_threshold_per_second"].is_integral()) {
            buffer_config.high_frequency_threshold_per_second =
                static_cast<std::uint64_t>(static_cast<std::int64_t>(
                    log_config["high_perform_mode_freq_threshold_per_second"]));

            if (buffer_config.high_frequency_threshold_per_second == 0) {
                buffer_config.high_frequency_threshold_per_second =
                    std::numeric_limits<std::uint64_t>::max();
            }
        }
    }
    return buffer_config;
}
}  // namespace qlog::config