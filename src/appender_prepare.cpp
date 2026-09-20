#include "qlog/detail/appender_prepare.hpp"

#include <stdexcept>

#include "qlog/logger_config.hpp"
namespace qlog::detail {
bool compatible_appender(const AppenderConfig& a, const AppenderConfig& b) noexcept {
    return a.name == b.name && a.type == b.type && a.text.batch_bytes == b.text.batch_bytes &&
           (a.type == AppenderType::TextFile ? a.file.path == b.file.path
                                             : a.console.stream == b.console.stream);
}
std::unique_ptr<PreparedReset> prepare_reset(const AppenderConfig* input, std::size_t count,
                                             const AppenderConfig* old, std::size_t old_count,
                                             std::size_t categories, ResetMode mode,
                                             ConsoleOutputGate& gate) {
    if (!old && old_count != 0U)
        throw ConfigValidationError(ConfigError::invalid_pointer, 0, "null old snapshot");
    if (mode != ResetMode::reuse_compatible && mode != ResetMode::recreate_all)
        throw ConfigValidationError(ConfigError::invalid_output_config, 0, "invalid reset mode");
    auto prepared = std::make_unique<PreparedReset>();
    auto& p = *prepared;
    p.new_config_snapshot = prepare_appender_configs(input, count, categories);
    p.changes = p.new_config_snapshot;
    const auto size = p.new_config_snapshot.size();
    p.reuse_old_index.assign(size, kNoReuse);
    p.old_reused.assign(old_count, 0U);
    p.next_appenders.resize(size);
    p.retired.resize(old_count);
    p.next_selection.assign(size, 0U);
    p.next_shutdown_report = prepare_io_report(p.new_config_snapshot.data(), size);
    p.next_periodic_report = prepare_io_report(p.new_config_snapshot.data(), size);
    p.prepared_merged_levels = merge_appender_levels(p.new_config_snapshot.data(), size);
    for (std::size_t i = 0; i < size; ++i) {
        if (mode == ResetMode::reuse_compatible) {
            for (std::size_t j = 0; j < old_count; ++j) {
                if (compatible_appender(p.new_config_snapshot[i], old[j])) {
                    p.reuse_old_index[i] = j;
                    p.old_reused[j] = 1U;
                    break;
                }
            }
        }
        if (p.reuse_old_index[i] == kNoReuse)
            p.next_appenders[i] = make_appender(p.new_config_snapshot[i], gate);
    }
    return prepared;
}
}  // namespace qlog::detail
