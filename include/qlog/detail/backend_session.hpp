#pragma once
#include <array>
#include <atomic>
#include <chrono>

#include "qlog/backend_config.hpp"
#include "qlog/detail/control_mailbox.hpp"
#include "qlog/detail/producer_context.hpp"
namespace qlog::detail {
enum class SessionProgress : std::uint8_t { idle, runnable, waiting_io, detach_ready };
struct BackendWorkspace final {
    std::array<DecodedArg, 32> args{};
    std::unique_ptr<std::byte[]> message{std::make_unique_for_overwrite<std::byte[]>(65536)};
    std::unique_ptr<std::byte[]> line{std::make_unique_for_overwrite<std::byte[]>(65536)};
};
struct SessionBindings final {
    FilterState& filter;
    std::atomic<ProducerContext*>& published_context;
    ControlMailbox& mailbox;
    const RecordValidationPolicy& policy;
};
#if QLOG_ENABLE_DIAGNOSTICS
struct BackendDiagnostics final {
    std::uint64_t records{}, decode_failed{}, body_failed{}, line_failed{}, delivered{},
        unavailable_deliveries{}, channel_faults{};
};
#endif
// One worker owns service(). Management only accesses release/acquire controls.
class BackendSession final {
   public:
    BackendSession(SessionBindings, BackendConfig, std::unique_ptr<PreparedReset> initial);
    SessionProgress service(BackendWorkspace&, std::chrono::steady_clock::time_point) noexcept;
    void request_stop(FlushMode) noexcept;  // once, producers already stopped
    void acknowledge_detached() noexcept;   // worker's LAST access after unlinking
    bool detached() const noexcept {
        return detached_.load(std::memory_order_acquire);
    }
    const ShutdownResult& shutdown_result() noexcept;  // management, after detached/join
    void abandon_after_worker_failure() noexcept;      // only after confirmed exit/no borrows
    std::chrono::steady_clock::time_point next_deadline() const noexcept;  // worker only
#if QLOG_ENABLE_DIAGNOSTICS
    const BackendDiagnostics& diagnostics() const noexcept {
        return diagnostics_;
    }
#endif
   private:
    enum class StopPhase : std::uint8_t { running, drain, close, ready };
    SessionBindings bindings_;
    BackendConfig config_;
    std::vector<std::unique_ptr<Appender>> appenders_;
    std::vector<std::uint8_t> selected_;
    IoReport shutdown_report_, periodic_report_;
    ShutdownResult shutdown_result_;
    bool result_taken_{false};
    std::atomic<bool> stop_requested_{false}, detached_{false};
    FlushMode stop_mode_{FlushMode::buffered};
    StopPhase stop_phase_{StopPhase::running};
    std::size_t close_index_{0}, next_output_{0};
    ProducerContext* active_head_{nullptr};
    ProducerContext* cursor_{nullptr};
    ProducerContext* known_head_{nullptr};
    ProducerContext* discover_cursor_{nullptr};
    ProducerContext* snapshot_head_{nullptr};
    bool discovering_{false}, sweep_had_record_{false};
#if QLOG_ENABLE_DIAGNOSTICS
    BackendDiagnostics diagnostics_;
#endif
    void discover() noexcept;
    bool consume_visit(BackendWorkspace&) noexcept;  // true after a complete empty sweep
    void render_record(ProducerContext&, const ReadHandle&, BackendWorkspace&) noexcept;
    void service_output(std::chrono::steady_clock::time_point, IoReport&) noexcept;
    bool needs_capacity() const noexcept;
    void start_sweep() noexcept;
    SessionProgress service_command(BackendWorkspace&,
                                    std::chrono::steady_clock::time_point) noexcept;
    void complete_command(PreparedCommand&, CompletionStatus) noexcept;
};
}  // namespace qlog::detail
