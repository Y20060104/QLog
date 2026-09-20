#include "qlog/detail/appender.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

#include "qlog/detail/appender_prepare.hpp"

namespace qlog::detail {
namespace {
constexpr std::size_t kMaxLineBytes = 65536U;
constexpr std::size_t kRecoveryPathExtra = sizeof(".recovery.") - 1U + 20U;
constexpr auto kConsoleRetryInterval = std::chrono::milliseconds(100);

void add_saturated(std::uint64_t& to, std::uint64_t value) noexcept {
    const auto max = std::numeric_limits<std::uint64_t>::max();
    to += std::min(value, max - to);
}

// source must not alias destination.output_path; all storage is cold-prepared.
void set_failure(IoFailure& destination, IoStage stage, int error, std::uint64_t unwritten,
                 bool uncertain, std::string_view path) noexcept {
    if (path.size() > destination.output_path.capacity()) std::terminate();
    destination.output_path.resize(path.size());
    if (!path.empty()) std::memcpy(destination.output_path.data(), path.data(), path.size());
    destination.stage = stage;
    destination.system_error = error;
    destination.unwritten_bytes = unwritten;
    destination.durability_uncertain = uncertain;
}

void emit(IoReport& report, std::size_t index, const IoFailure& failure) noexcept {
    report.record(index, failure.stage, failure.system_error, failure.unwritten_bytes,
                  failure.durability_uncertain, failure.output_path.data(),
                  failure.output_path.size());
}
}  // namespace

Appender::Appender(AppenderConfig prepared)
    : config_(std::move(prepared)), batch_(config_.text.batch_bytes) {
    const auto now = std::chrono::steady_clock::now();
    flush_due_ = now + std::chrono::microseconds(config_.text.flush_interval_us);
    retry_due_ = now;
    for (auto* failure : {&last_error_, &sync_error_, &history_.first}) {
        failure->appender_name = config_.name;
        if (config_.type == AppenderType::TextFile) {
            const auto base = config_.file.path.size();
            const auto limit = failure->output_path.max_size();
            if (limit < kRecoveryPathExtra || base > limit - kRecoveryPathExtra)
                throw std::length_error("Appender error path is too long");
            failure->output_path.reserve(base + kRecoveryPathExtra);
        }
    }
}

Appender::~Appender() noexcept = default;
CalendarCache& Appender::calendar_cache() noexcept {
    return calendar_cache_;
}
const AppenderConfig& Appender::config() const noexcept {
    return config_;
}

bool Appender::selects(std::uint32_t category, std::uint8_t level) const noexcept {
    return config_.enabled && level < 6U && category < config_.filter.category_enabled.size() &&
           config_.filter.category_enabled[category] != 0U &&
           (config_.filter.levels & (std::uint32_t{1} << level)) != 0U;
}

bool Appender::ready_for_record() const noexcept {
    return state_ == OutputState::active && batch_.append_capacity() >= kMaxLineBytes;
}

bool Appender::accept_line(const std::byte* data, std::size_t size) noexcept {
    return state_ == OutputState::active && size <= kMaxLineBytes && batch_.append(data, size);
}

IoWriteResult Appender::write_all(const std::byte* data, std::size_t size) noexcept {
    std::size_t total = 0U;
    while (total < size) {
        const auto request = std::min(size - total, static_cast<std::size_t>(SSIZE_MAX));
        const auto r = write(data + total, request);
        if (r.written > request || (r.error != 0 && r.written != 0U)) std::terminate();
        if (r.error == EINTR) continue;
        if (r.error != 0) return {total, r.error};
        total += r.written;
        if (r.written == 0U) break;
    }
    return {total, 0};
}

void Appender::schedule_retry(std::chrono::steady_clock::time_point now) noexcept {
    retry_due_ = config_.type == AppenderType::TextFile
                     ? now + std::chrono::microseconds(config_.file.retry_interval_us)
                     : now + kConsoleRetryInterval;
}

std::chrono::steady_clock::time_point Appender::next_service_time() const noexcept {
    if (state_ == OutputState::closed) return std::chrono::steady_clock::time_point::max();
    return state_ == OutputState::active ? flush_due_ : retry_due_;
}

void Appender::note_io_failure(IoReport& report, std::size_t index, IoStage stage, int error,
                               std::uint64_t unwritten, bool uncertain, std::string_view path,
                               bool blocking) noexcept {
    if (error == 0 || history_merged_) std::terminate();
    report.record(index, stage, error, unwritten, uncertain, path.data(), path.size());
    add_saturated(history_.event_count, 1U);
    if (!history_.first_present) {
        set_failure(history_.first, stage, error, unwritten, uncertain, path);
        history_.first_present = true;
    }
    if (blocking) {
        set_failure(last_error_, stage, error, unwritten, uncertain, path);
        has_last_error_ = true;
    }
    if (stage == IoStage::sync) {
        set_failure(sync_error_, stage, error, unwritten, true, path);
        sync_failed_ = true;
    }
}

void Appender::report_unresolved(IoReport& report, std::size_t index) const noexcept {
    if (has_last_error_) emit(report, index, last_error_);
    if (sync_failed_) emit(report, index, sync_error_);
    if (!has_last_error_ && !sync_failed_) {
        // Already closed: an unavailable target, not an invented OS failure.
        const auto path = output_path();
        report.record(index, IoStage::close, 0, batch_.pending_size(), false, path.data(),
                      path.size());
    }
}

void Appender::discard_pending_as_lost() noexcept {
    add_saturated(history_.lost_bytes, static_cast<std::uint64_t>(batch_.discard_pending()));
}

void Appender::close_and_report(IoReport& report, std::size_t index) noexcept {
    const auto r = close_output();
    if (r.error != 0)
        note_io_failure(report, index, IoStage::close, r.error, 0U, sync_failed_, output_path(),
                        false);
}

FlushResult Appender::flush(FlushMode mode, IoReport& report, std::size_t index) noexcept {
    return flush_impl(mode, report, index, false);
}

FlushResult Appender::flush_impl(FlushMode mode, IoReport& report, std::size_t index,
                                 bool final_attempt) noexcept {
    if (state_ == OutputState::closed || state_ == OutputState::reopen_pending) {
        report_unresolved(report, index);
        return {FlushStatus::failed, batch_.pending_size(), sync_failed_};
    }

    const auto r = write_all(batch_.pending_data(), batch_.pending_size());
    batch_.consume(r.written);  // Successful prefixes must never be replayed.
    const auto now = std::chrono::steady_clock::now();

    if (r.error != 0) {
        note_io_failure(report, index, IoStage::write, r.error, batch_.pending_size(), sync_failed_,
                        output_path(), true);
        if (sync_failed_) emit(report, index, sync_error_);
        schedule_retry(now);
        if (r.error == ENOSPC || r.error == EDQUOT) {
            state_ = OutputState::disk_full;
        } else if (r.error == EAGAIN || r.error == EWOULDBLOCK) {
            state_ = OutputState::retry_same_fd;
        } else if (!final_attempt) {
            discard_pending_as_lost();
            if (config_.type == AppenderType::TextFile) {
                close_and_report(report, index);
                state_ = OutputState::reopen_pending;
            } else {
                state_ = OutputState::retry_same_fd;
            }
        }
        return {FlushStatus::failed, batch_.pending_size(), sync_failed_};
    }

    if (batch_.pending_size() != 0U) {
        state_ = OutputState::retry_same_fd;
        schedule_retry(now);
        set_failure(last_error_, IoStage::write, 0, batch_.pending_size(), sync_failed_,
                    output_path());
        has_last_error_ = true;
        emit(report, index, last_error_);  // No OS event for zero progress.
        if (sync_failed_) emit(report, index, sync_error_);
        return {FlushStatus::incomplete, batch_.pending_size(), sync_failed_};
    }

    state_ = OutputState::active;
    has_last_error_ = false;
    flush_due_ = now + std::chrono::microseconds(config_.text.flush_interval_us);
    if (mode == FlushMode::durable && config_.type == AppenderType::TextFile) {
        const auto sync = sync_output();  // One call, including EINTR failures.
        if (sync.error != 0) {
            note_io_failure(report, index, IoStage::sync, sync.error, 0U, true, output_path(),
                            false);
            return {FlushStatus::failed, 0U, true};
        }
        sync_failed_ = false;
    }
    // A previous sync fault remains unresolved after a buffered flush.
    if (sync_failed_) emit(report, index, sync_error_);
    return {FlushStatus::completed, 0U, sync_failed_};
}

void Appender::service_recovery(std::chrono::steady_clock::time_point now, IoReport& report,
                                std::size_t index) noexcept {
    if (state_ == OutputState::active || state_ == OutputState::closed || now < retry_due_) return;
    if (state_ != OutputState::reopen_pending) {
        (void)flush(FlushMode::buffered, report, index);
        return;
    }
    const auto r = reopen_output(report, index);
    const auto finished = std::chrono::steady_clock::now();
    schedule_retry(finished);
    if (r.error != 0) return;  // Derived method already recorded actual failures.
    state_ = OutputState::active;
    has_last_error_ = false;
    sync_failed_ = false;  // New output target; old uncertainty remains in history.
    flush_due_ = finished + std::chrono::microseconds(config_.text.flush_interval_us);
}

void Appender::service(std::chrono::steady_clock::time_point now, IoReport& report,
                       std::size_t index) noexcept {
    if (state_ != OutputState::active) {
        service_recovery(now, report, index);
    } else if (now >= flush_due_) {
        if (batch_.pending_size() != 0U)
            (void)flush(FlushMode::buffered, report, index);
        else
            flush_due_ = now + std::chrono::microseconds(config_.text.flush_interval_us);
    }
}

void Appender::final_close(FlushMode mode, IoReport& report, std::size_t index) noexcept {
    if (state_ == OutputState::closed) return;
    (void)flush_impl(mode, report, index, true);
    discard_pending_as_lost();
    close_and_report(report, index);
    state_ = OutputState::closed;
}

void Appender::capture_first_error(RetiredIoSummary& destination) noexcept {
    if (!destination.first_error && history_.first_present)
        destination.first_error.emplace(std::move(history_.first));
}

void Appender::merge_history_into(RetiredIoSummary& destination) noexcept {
    if (history_merged_) return;
    if (state_ != OutputState::closed) std::terminate();
    add_saturated(destination.event_count, history_.event_count);
    add_saturated(destination.lost_bytes, history_.lost_bytes);
    capture_first_error(destination);
    history_merged_ = true;
}

void Appender::apply_compatible_config(AppenderConfig& prepared) noexcept {
    if (!compatible_appender(config_, prepared) || state_ == OutputState::closed) std::terminate();
    const bool timezone_changed =
        config_.text.time_zone.offset_minutes != prepared.text.time_zone.offset_minutes;
    std::swap(config_, prepared);
    if (timezone_changed) calendar_cache_.valid = false;
    const auto now = std::chrono::steady_clock::now();
    flush_due_ = now + std::chrono::microseconds(config_.text.flush_interval_us);
    schedule_retry(now);
}
void Appender::abandon_after_worker_failure(IoReport& report, std::size_t index) noexcept {
    if (state_ == OutputState::closed) return;
    if (has_last_error_ || sync_failed_) report_unresolved(report, index);
    if (batch_.pending_size() != 0U) {
        const auto path = output_path();
        report.record(index, IoStage::write, 0, batch_.pending_size(), sync_failed_, path.data(),
                      path.size());
    }
    discard_pending_as_lost();
    close_and_report(report, index);
    state_ = OutputState::closed;
}
}  // namespace qlog::detail
