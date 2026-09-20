#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "qlog/detail/io_report.hpp"
#include "qlog/detail/io_result.hpp"
#include "qlog/detail/output_batch.hpp"
#include "qlog/detail/text_formatter.hpp"

namespace qlog::detail {
enum class OutputState : std::uint8_t {
    active,
    disk_full,
    retry_same_fd,
    reopen_pending,
    closed,
};

// Single backend owner. Never invoke I/O while a Ring frame is borrowed.
class Appender {
   public:
    virtual ~Appender() noexcept;
    bool selects(std::uint32_t category, std::uint8_t level) const noexcept;
    bool ready_for_record() const noexcept;
    bool accept_line(const std::byte*, std::size_t) noexcept;
    FlushResult flush(FlushMode, IoReport&, std::size_t report_index) noexcept;
    // Only after the worker has exited and released all Session borrows.
    void abandon_after_worker_failure(IoReport&, std::size_t) noexcept;
    void final_close(FlushMode, IoReport&, std::size_t report_index) noexcept;
    void service_recovery(std::chrono::steady_clock::time_point, IoReport&,
                          std::size_t report_index) noexcept;
    void service(std::chrono::steady_clock::time_point, IoReport&,
                 std::size_t report_index) noexcept;
    void apply_compatible_config(AppenderConfig& prepared) noexcept;
    CalendarCache& calendar_cache() noexcept;
    const AppenderConfig& config() const noexcept;
    // Session calls this immediately after each target operation to preserve
    // cross-target observation order; always use the same Session summary.
    void capture_first_error(RetiredIoSummary&) noexcept;
    // Only after final_close; moves the first error without allocating, once.
    void merge_history_into(RetiredIoSummary&) noexcept;
    OutputState state() const noexcept {
        return state_;
    }
    // closed has no deadline; caller must exclude it from wakeup scheduling.
    std::chrono::steady_clock::time_point next_service_time() const noexcept;

   protected:
    explicit Appender(AppenderConfig prepared);
    // One syscall: an error result must have written == 0.
    virtual IoWriteResult write(const std::byte*, std::size_t) noexcept = 0;
    virtual IoCallResult sync_output() noexcept = 0;
    // Idempotent; invalidate fd before close; keep output_path available.
    virtual IoCallResult close_output() noexcept = 0;
    // Reports every actual failure via note_io_failure below. No duplicate
    // reporting in service_recovery. A failed open uses blocking=true;
    // a secondary cleanup close failure uses blocking=false.
    virtual IoCallResult reopen_output(IoReport&, std::size_t) noexcept = 0;
    virtual std::string_view output_path() const noexcept = 0;
    void note_io_failure(IoReport&, std::size_t, IoStage, int error, std::uint64_t unwritten,
                         bool uncertain, std::string_view path, bool blocking) noexcept;

   private:
    AppenderConfig config_;
    OutputBatch batch_;
    OutputState state_{OutputState::active};
    CalendarCache calendar_cache_;
    IoHistory history_;
    IoFailure last_error_;
    IoFailure sync_error_;  // independent of a later write/open fault
    bool has_last_error_{false};
    bool sync_failed_{false};
    bool history_merged_{false};
    std::chrono::steady_clock::time_point flush_due_{};
    std::chrono::steady_clock::time_point retry_due_{};

    FlushResult flush_impl(FlushMode, IoReport&, std::size_t, bool final_attempt) noexcept;
    IoWriteResult write_all(const std::byte*, std::size_t) noexcept;
    void schedule_retry(std::chrono::steady_clock::time_point) noexcept;
    void report_unresolved(IoReport&, std::size_t) const noexcept;
    void discard_pending_as_lost() noexcept;
    void close_and_report(IoReport&, std::size_t) noexcept;
};
}  // namespace qlog::detail
