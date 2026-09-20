#include "qlog/detail/backend_session.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

#include "qlog/detail/filter_config_access.hpp"
#include "qlog/detail/record_decoder.hpp"

namespace qlog::detail {
namespace {
class FrameLease final {
   public:
    FrameLease(SpscRingBuffer& ring, ReadHandle frame) noexcept : ring_(ring), frame_(frame) {}
    ~FrameLease() {
        ring_.release(frame_);
    }
    FrameLease(const FrameLease&) = delete;
    FrameLease& operator=(const FrameLease&) = delete;

   private:
    SpscRingBuffer& ring_;
    ReadHandle frame_;
};
#if QLOG_ENABLE_DIAGNOSTICS
void increment(std::uint64_t& value) noexcept {
    if (value != std::numeric_limits<std::uint64_t>::max()) ++value;
}
#define QLOG_BACKEND_COUNT(field) increment(diagnostics_.field)
#else
#define QLOG_BACKEND_COUNT(field) ((void)0)
#endif
}  // namespace
BackendSession::BackendSession(SessionBindings bindings, BackendConfig config,
                               std::unique_ptr<PreparedReset> initial)
    : bindings_(bindings),
      config_(config),
      appenders_(std::move(initial->next_appenders)),
      selected_(std::move(initial->next_selection)),
      shutdown_report_(std::move(initial->next_shutdown_report)),
      periodic_report_(std::move(initial->next_periodic_report)) {
    if (config_.records_per_channel == 0 || config_.bytes_per_channel == 0) std::terminate();
    for (const auto& a : appenders_)
        if (!a) std::terminate();
}
void BackendSession::discover() noexcept {
    if (!discovering_) {
        snapshot_head_ = bindings_.published_context.load(std::memory_order_acquire);
        discover_cursor_ = snapshot_head_;
        discovering_ = discover_cursor_ != known_head_;
    }
    for (unsigned count = 0; discovering_ && count < 64U; ++count) {
        auto* node = discover_cursor_;
        if (!node) std::terminate();  // immutable append-only publication chain
        discover_cursor_ = node->published_next;
        node->consumer_next = active_head_;
        active_head_ = node;
        // A prefix added during a sweep has not necessarily been visited yet.
        // Force another full sweep before reporting global emptiness.
        sweep_had_record_ = true;
        if (discover_cursor_ == known_head_) {
            known_head_ = snapshot_head_;
            discovering_ = false;
        }
    }
}
void BackendSession::start_sweep() noexcept {
    cursor_ = nullptr;
    sweep_had_record_ = false;
}
bool BackendSession::needs_capacity() const noexcept {
    for (const auto& a : appenders_)
        if (a->config().enabled && a->state() == OutputState::active && !a->ready_for_record())
            return true;
    return false;
}
void BackendSession::service_output(std::chrono::steady_clock::time_point now,
                                    IoReport& report) noexcept {
    if (appenders_.empty()) return;
    const auto index = next_output_;
    next_output_ = (next_output_ + 1U) % appenders_.size();
    auto& a = *appenders_[index];
    if (a.state() == OutputState::active && !a.ready_for_record())
        (void)a.flush(FlushMode::buffered, report, index);
    else if (stop_phase_ != StopPhase::drain)
        a.service(now, report, index);
    a.capture_first_error(shutdown_result_.retired_io);
}
void BackendSession::render_record(ProducerContext& context, const ReadHandle& frame,
                                   BackendWorkspace& workspace) noexcept {
    QLOG_BACKEND_COUNT(records);
    const auto decoded = decode_v1(frame.data(), frame.size(), workspace.args.data(),
                                   workspace.args.size(), bindings_.policy);
    if (!decoded.succeeded()) {
        QLOG_BACKEND_COUNT(decode_failed);
        return;
    }
    const auto& record = *decoded.record();
    const auto& header = record.header();
    if (header.category_id >= context.channel_.cold_.dependencies.category_names.size()) {
        QLOG_BACKEND_COUNT(decode_failed);
        return;
    }
    bool any = false;
    for (std::size_t i = 0; i < appenders_.size(); ++i) {
        auto& a = *appenders_[i];
        const bool selects = a.selects(header.category_id, header.level);
        const bool ready = a.ready_for_record();
        selected_[i] = static_cast<std::uint8_t>(selects && ready);
        if (selects && !ready) QLOG_BACKEND_COUNT(unavailable_deliveries);
        any = any || selected_[i] != 0U;
    }
    if (!any) return;
    const auto body =
        render_message_utf8(record.format_data(), record.format_size(), record.arguments_data(),
                            record.argument_count(), workspace.message.get(), 65536);
    if (body.failure) {
        QLOG_BACKEND_COUNT(body_failed);
        return;
    }
    for (std::size_t i = 0; i < appenders_.size(); ++i) {
        if (!selected_[i]) continue;
        auto& a = *appenders_[i];
        const auto line = compose_line(context.channel_.cold_, header, a.config().text.time_zone,
                                       a.calendar_cache(), workspace.message.get(), body.size,
                                       workspace.line.get(), 65536);
        if (line.failure) {
            QLOG_BACKEND_COUNT(line_failed);
            continue;
        }
        if (!a.accept_line(workspace.line.get(), line.size)) std::terminate();
        QLOG_BACKEND_COUNT(delivered);
    }
}
bool BackendSession::consume_visit(BackendWorkspace& workspace) noexcept {
    discover();
    if (!cursor_) cursor_ = active_head_;
    auto* node = cursor_;
    if (node && !node->consumer_faulted) {
        auto& ring = node->channel_.ring_;
        std::size_t bytes = 0;
        for (std::uint32_t count = 0; count < config_.records_per_channel; ++count) {
            if (needs_capacity()) break;  // no frame is borrowed here
            if (count != 0U && bytes >= config_.bytes_per_channel) break;
            auto frame = ring.try_read();
            if (!frame) {
                if (frame.status() != ReadStatus::empty) {
                    node->consumer_faulted = true;
                    shutdown_result_.drain_incomplete = true;
                    QLOG_BACKEND_COUNT(channel_faults);
                }
                break;
            }
            if (count != 0U && frame.size() > config_.bytes_per_channel - bytes) {
                ring.abandon(frame);
                break;  // budget boundary, not a dropped record
            }
            sweep_had_record_ = true;
            bytes += frame.size();
            {
                FrameLease lease(ring, frame);
                render_record(*node, frame, workspace);  // memory only
            }
        }
        ring.publish_reclaimed();
    }
    cursor_ = node ? node->consumer_next : nullptr;
    if (cursor_) return false;
    const bool empty = !sweep_had_record_ && !discovering_ && !needs_capacity() &&
                       known_head_ == bindings_.published_context.load(std::memory_order_acquire);
    sweep_had_record_ = false;
    return empty;
}
void BackendSession::complete_command(PreparedCommand& cmd, CompletionStatus status) noexcept {
    cmd.completion.status = status;
    cmd.phase = CommandPhase::finish;
    bindings_.mailbox.state.store(CommandState::completed, std::memory_order_release);
    // No command access after publishing completed.
}
SessionProgress BackendSession::service_command(
    BackendWorkspace& workspace, std::chrono::steady_clock::time_point now) noexcept {
    auto& cmd = *bindings_.mailbox.command;
    if (cmd.phase == CommandPhase::begin) {
        cmd.target_index = 0;
        if (cmd.kind == CommandKind::drain) {
            start_sweep();
            cmd.phase = CommandPhase::drain_records;
        } else
            cmd.phase = cmd.kind == CommandKind::reset_appenders ? CommandPhase::retire_targets
                                                                 : CommandPhase::flush_targets;
    }
    if (cmd.phase == CommandPhase::drain_records) {
        service_output(now, cmd.report);
        if (consume_visit(workspace)) cmd.phase = CommandPhase::flush_targets;
        return SessionProgress::runnable;
    }
    if (cmd.phase == CommandPhase::flush_targets) {
        if (cmd.target_index < appenders_.size()) {
            const auto i = cmd.target_index++;
            (void)appenders_[i]->flush(cmd.flush_mode, cmd.report, i);
            appenders_[i]->capture_first_error(shutdown_result_.retired_io);
            return SessionProgress::runnable;
        }
        const auto status = cmd.kind == CommandKind::drain && shutdown_result_.drain_incomplete
                                ? CompletionStatus::backend_failed
                            : cmd.report.has_errors() ? CompletionStatus::completed_with_io_error
                                                      : CompletionStatus::completed;
        complete_command(cmd, status);
        return SessionProgress::runnable;
    }
    auto& reset = *cmd.reset;
    if (cmd.phase == CommandPhase::retire_targets) {
        unsigned scanned = 0;
        while (cmd.target_index < appenders_.size() && scanned++ < 64U) {
            const auto i = cmd.target_index++;
            if (reset.old_reused[i]) continue;
            auto& a = *appenders_[i];
            a.final_close(FlushMode::buffered, cmd.report, i);
            a.capture_first_error(shutdown_result_.retired_io);
            a.merge_history_into(shutdown_result_.retired_io);
            reset.retired[i] = std::move(appenders_[i]);
            return SessionProgress::runnable;
        }
        if (cmd.target_index == appenders_.size()) cmd.phase = CommandPhase::apply_reset;
        return SessionProgress::runnable;
    }
    if (cmd.phase != CommandPhase::apply_reset) std::terminate();
    for (std::size_t i = 0; i < reset.next_appenders.size(); ++i) {
        const auto old = reset.reuse_old_index[i];
        if (old == kNoReuse) continue;
        reset.next_appenders[i] = std::move(appenders_[old]);
        reset.next_appenders[i]->apply_compatible_config(reset.changes[i]);
    }
    appenders_.swap(reset.next_appenders);
    selected_.swap(reset.next_selection);
    std::swap(shutdown_report_, reset.next_shutdown_report);
    std::swap(periodic_report_, reset.next_periodic_report);
    next_output_ = 0;
    FilterConfigAccess::set_levels(bindings_.filter, reset.prepared_merged_levels);
    cmd.reset_applied = true;
    complete_command(cmd, cmd.report.has_errors() ? CompletionStatus::applied_with_io_error
                                                  : CompletionStatus::applied);
    return SessionProgress::runnable;
}
SessionProgress BackendSession::service(BackendWorkspace& workspace,
                                        std::chrono::steady_clock::time_point now) noexcept {
    if (stop_phase_ == StopPhase::ready) return SessionProgress::detach_ready;
    periodic_report_.clear();
    if (bindings_.mailbox.state.load(std::memory_order_acquire) == CommandState::pending)
        return service_command(workspace, now);
    if (stop_phase_ == StopPhase::running && stop_requested_.load(std::memory_order_acquire)) {
        start_sweep();
        stop_phase_ = StopPhase::drain;
    }
    if (stop_phase_ == StopPhase::close) {
        if (close_index_ < appenders_.size()) {
            const auto i = close_index_++;
            auto& a = *appenders_[i];
            a.final_close(stop_mode_, shutdown_report_, i);
            a.capture_first_error(shutdown_result_.retired_io);
            a.merge_history_into(shutdown_result_.retired_io);
            return SessionProgress::runnable;
        }
        stop_phase_ = StopPhase::ready;
        return SessionProgress::detach_ready;
    }
    // During final drain, retain failures in the final operation report.
    auto& report = stop_phase_ == StopPhase::drain ? shutdown_report_ : periodic_report_;
    service_output(now, report);
    const bool empty = consume_visit(workspace);
    if (stop_phase_ == StopPhase::drain) {
        if (empty) stop_phase_ = StopPhase::close;
        return SessionProgress::runnable;
    }
    if (!empty) return SessionProgress::runnable;
    for (const auto& a : appenders_)
        if (a->state() != OutputState::active) return SessionProgress::waiting_io;
    return SessionProgress::idle;
}
void BackendSession::request_stop(FlushMode mode) noexcept {
    if (stop_requested_.load(std::memory_order_relaxed)) return;
    stop_mode_ = mode;
    stop_requested_.store(true, std::memory_order_release);
}
void BackendSession::acknowledge_detached() noexcept {
    if (stop_phase_ != StopPhase::ready) std::terminate();
    detached_.store(true, std::memory_order_release);
}
const ShutdownResult& BackendSession::shutdown_result() noexcept {
    if (!detached()) std::terminate();
    if (!result_taken_) {
        shutdown_result_.errors = shutdown_report_.take_errors();
        result_taken_ = true;
    }
    return shutdown_result_;
}
std::chrono::steady_clock::time_point BackendSession::next_deadline() const noexcept {
    auto deadline = std::chrono::steady_clock::time_point::max();
    for (const auto& a : appenders_)
        if (a) deadline = std::min(deadline, a->next_service_time());
    return deadline;
}
void BackendSession::abandon_after_worker_failure() noexcept {
    if (detached()) return;
    shutdown_result_.backend_failed = true;
    shutdown_result_.drain_incomplete = true;
    const auto state = bindings_.mailbox.state.load(std::memory_order_acquire);
    PreparedCommand* cmd = state == CommandState::empty ? nullptr : bindings_.mailbox.command.get();
    auto& report = cmd ? cmd->failure_report : shutdown_report_;
    const auto cleanup = [&](std::unique_ptr<Appender>& a, std::size_t i) {
        if (!a) return;
        a->abandon_after_worker_failure(report, i);
        a->capture_first_error(shutdown_result_.retired_io);
        a->merge_history_into(shutdown_result_.retired_io);
    };
    const auto old_count = cmd && cmd->reset ? cmd->reset->old_reused.size() : 0U;
    for (std::size_t i = 0; i < appenders_.size(); ++i)
        cleanup(appenders_[i], cmd && cmd->reset_applied ? old_count + i : i);
    if (cmd && cmd->reset) {
        auto& reset = *cmd->reset;
        for (std::size_t i = 0; i < reset.retired.size(); ++i) cleanup(reset.retired[i], i);
        for (std::size_t i = 0; i < reset.next_appenders.size(); ++i)
            cleanup(reset.next_appenders[i], cmd->reset_applied ? i : old_count + i);
    }
    if (cmd) {
        cmd->report.merge_from(report);
        shutdown_report_ = std::move(cmd->failure_report);
        if (state == CommandState::pending)
            complete_command(*cmd, CompletionStatus::backend_failed);
    }
    stop_phase_ = StopPhase::ready;
    acknowledge_detached();
}
#undef QLOG_BACKEND_COUNT
}  // namespace qlog::detail
