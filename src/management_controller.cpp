#include "qlog/detail/management_controller.hpp"

#include <filesystem>
#include <limits>
#include <new>
#include <utility>

#include "qlog/logger_config.hpp"
namespace qlog::detail {
ManagementController::ManagementController(ControlMailbox& mailbox,
                                           std::vector<AppenderConfig> snapshot,
                                           std::size_t categories, ConsoleOutputGate& gate)
    : mailbox_(mailbox), acknowledged_(std::move(snapshot)), categories_(categories), gate_(gate) {}
SubmitStatus ManagementController::admission() const noexcept {
    if (stopped_) return SubmitStatus::stopped;
    if (mailbox_.state.load(std::memory_order_acquire) != CommandState::empty)
        return SubmitStatus::busy;
    if (next_id_ == 0U) return SubmitStatus::identity_exhausted;
    return SubmitStatus::submitted;
}
SubmitResult ManagementController::reset(const AppenderConfig* p, std::size_t n, ResetMode mode) {
    return submit(CommandKind::reset_appenders, FlushMode::buffered, p, n, mode);
}
SubmitResult ManagementController::flush(FlushMode mode) {
    return submit(CommandKind::flush_batches, mode, nullptr, 0, ResetMode::reuse_compatible);
}
SubmitResult ManagementController::drain(FlushMode mode) {
    return submit(CommandKind::drain, mode, nullptr, 0, ResetMode::reuse_compatible);
}
SubmitResult ManagementController::submit(CommandKind kind, FlushMode mode, const AppenderConfig* p,
                                          std::size_t count, ResetMode reset_mode) {
    SubmitResult result{};
    result.status = admission();
    if (result.status != SubmitStatus::submitted) return result;
    if (mode != FlushMode::buffered && mode != FlushMode::durable) {
        result.status = SubmitStatus::invalid_config;
        result.reason = ConfigError::invalid_output_config;
        return result;
    }
    try {
        auto command = std::make_unique<PreparedCommand>();
        command->kind = kind;
        command->flush_mode = mode;
        command->reset_mode = reset_mode;
        if (kind == CommandKind::reset_appenders) {
            command->reset = prepare_reset(p, count, acknowledged_.data(), acknowledged_.size(),
                                           categories_, reset_mode, gate_);
            auto targets = acknowledged_;
            const auto& added = command->reset->new_config_snapshot;
            if (added.size() > targets.max_size() - targets.size())
                throw std::length_error("too many reset report targets");
            targets.insert(targets.end(), added.begin(), added.end());
            command->report = prepare_io_report(targets.data(), targets.size());
            command->failure_report = prepare_io_report(targets.data(), targets.size());
        } else {
            command->report = prepare_io_report(acknowledged_.data(), acknowledged_.size());
            command->failure_report = prepare_io_report(acknowledged_.data(), acknowledged_.size());
        }
        const auto id = next_id_;
        next_id_ = id == std::numeric_limits<std::uint64_t>::max() ? 0U : id + 1U;
        command->request_id = id;
        command->completion.request_id = id;
        mailbox_.command = std::move(command);
        mailbox_.state.store(CommandState::pending, std::memory_order_release);
        result.request_id = id;
    } catch (const ConfigValidationError& e) {
        result.status = SubmitStatus::invalid_config;
        result.reason = e.reason;
        result.config_index = e.config_index;
    } catch (AppenderOpenError& e) {
        result.status = SubmitStatus::open_failed;
        result.error.emplace(std::move(e.failure));
    } catch (const std::filesystem::filesystem_error&) {
        result.status = SubmitStatus::invalid_config;
        result.reason = ConfigError::invalid_path;
    } catch (const std::bad_alloc&) {
        result.status = SubmitStatus::resource_exhausted;
    } catch (const std::length_error&) {
        result.status = SubmitStatus::resource_exhausted;
    }
    return result;
}
PollResult ManagementController::poll(std::uint64_t id) {
    const auto state = mailbox_.state.load(std::memory_order_acquire);
    if (state == CommandState::empty) return {PollStatus::unknown_request, std::nullopt};
    // request_id is immutable after publication, including while pending.
    if (mailbox_.command->request_id != id) return {PollStatus::unknown_request, std::nullopt};
    if (state == CommandState::pending) return {PollStatus::pending, std::nullopt};
    auto& command = *mailbox_.command;
    command.completion.errors = command.report.take_errors();
    if (command.reset_applied) acknowledged_.swap(command.reset->new_config_snapshot);
    PollResult result{PollStatus::completed, std::move(command.completion)};
    mailbox_.command.reset();  // resource destruction belongs to management
    mailbox_.state.store(CommandState::empty, std::memory_order_release);
    return result;
}
}  // namespace qlog::detail
