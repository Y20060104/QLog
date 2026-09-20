#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace qlog {
enum class LogStatus : std::uint8_t {
    accepted,
    filtered,
    invalid_level,
    invalid_category,
    invalid_input,
    too_large,
    full,
    internal_error,
    resource_exhausted,
    registration_closed,
    identity_exhausted,
};
enum class FailureStage : std::uint8_t {
    validation,
    measure,
    reserve,
    encode,
    context,
};
enum class FailureReason : std::uint8_t {
    invalid_level,
    invalid_category,
    invalid_limits,
    invalid_format_metadata,
    invalid_string_metadata,
    format_too_large,
    argument_length_out_of_range,
    args_length_out_of_range,
    size_overflow,
    record_length_out_of_range,
    payload_too_large,
    full,
    reservation_pending,
    invalid_destination_metadata,
    destination_size_mismatch,
    unknown_flags,
    reserved_timestamp_status,
    invalid_time_value,
    fallback_timestamp_not_configured,
    context_allocation_failed,
    tls_capacity_exhausted,
    registration_closed,
    producer_token_exhausted,
};
struct LogFailure {
    FailureStage stage;
    FailureReason reason;
    std::size_t argument_index{0xFFU};
    std::size_t byte_count{0};
};
namespace detail {
struct LogResultAccess;
}
class LogResult final {
   public:
    [[nodiscard]] LogStatus status() const noexcept {
        return status_;
    }
    [[nodiscard]] bool accepted() const noexcept {
        return status_ == LogStatus::accepted;
    }
    [[nodiscard]] const LogFailure* failure() const noexcept {
        return failure_ ? &*failure_ : nullptr;
    }

   private:
    friend struct detail::LogResultAccess;
    LogResult(LogStatus status, std::optional<LogFailure> failure) noexcept
        : status_(status), failure_(failure) {}
    LogStatus status_;
    std::optional<LogFailure> failure_;
};
namespace detail {
struct LogResultAccess final {
    [[nodiscard]] static LogResult make_accepted() noexcept {
        return LogResult(LogStatus::accepted, std::nullopt);
    }
    [[nodiscard]] static LogResult make_filtered() noexcept {
        return LogResult(LogStatus::filtered, std::nullopt);
    }
    [[nodiscard]] static LogResult make_failed(LogStatus status, LogFailure failure) noexcept {
        assert(status != LogStatus::accepted && status != LogStatus::filtered);
        return LogResult(status, std::optional<LogFailure>{failure});
    }
};
}  // namespace detail
}  // namespace qlog
