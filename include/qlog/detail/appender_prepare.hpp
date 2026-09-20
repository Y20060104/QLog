#pragma once
#include <limits>
#include <memory>
#include <vector>

#include "qlog/detail/appender_factory.hpp"
namespace qlog::detail {
inline constexpr std::size_t kNoReuse = std::numeric_limits<std::size_t>::max();
bool compatible_appender(const AppenderConfig&, const AppenderConfig&) noexcept;
struct PreparedReset final {
    std::vector<AppenderConfig> new_config_snapshot;
    std::vector<AppenderConfig> changes;
    std::vector<std::size_t> reuse_old_index;
    std::vector<std::uint8_t> old_reused;
    std::vector<std::unique_ptr<Appender>> next_appenders;
    std::vector<std::unique_ptr<Appender>> retired;
    std::vector<std::uint8_t> next_selection;
    IoReport next_shutdown_report;
    IoReport next_periodic_report;
    std::uint32_t prepared_merged_levels{};
};
std::unique_ptr<PreparedReset> prepare_reset(const AppenderConfig*, std::size_t,
                                             const AppenderConfig*, std::size_t,
                                             std::size_t category_count, ResetMode,
                                             ConsoleOutputGate&);
}  // namespace qlog::detail
