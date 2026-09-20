#pragma once
#include <atomic>

#include "qlog/detail/appender_prepare.hpp"
namespace qlog::detail {
enum class CommandState : std::uint8_t { empty, pending, completed };
enum class CommandKind : std::uint8_t { reset_appenders, flush_batches, drain };
enum class CommandPhase : std::uint8_t {
    begin,
    drain_records,
    flush_targets,
    retire_targets,
    apply_reset,
    finish
};
struct PreparedCommand final {
    std::uint64_t request_id{};
    CommandKind kind{};
    CommandPhase phase{CommandPhase::begin};
    FlushMode flush_mode{FlushMode::buffered};
    ResetMode reset_mode{ResetMode::reuse_compatible};
    std::size_t target_index{};
    std::unique_ptr<PreparedReset> reset;
    IoReport report;
    // Same target layout, for no-allocation cleanup after confirmed worker exit.
    IoReport failure_report;
    ManagementCompletion completion;
    bool reset_applied{false};
};
struct ControlMailbox final {
    alignas(64) std::atomic<CommandState> state{CommandState::empty};
    std::unique_ptr<PreparedCommand> command;
};
}  // namespace qlog::detail
