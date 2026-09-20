#pragma once
#include <cassert>
#include <exception>

#include "qlog/detail/context_result.hpp"
#include "qlog/detail/record_measure.hpp"
#include "qlog/detail/record_types.hpp"
#include "qlog/detail/spsc_ring_buffer.hpp"
#include "qlog/log_result.hpp"

namespace qlog::detail {
[[nodiscard]] inline LogResult map_context_error(ContextError error) noexcept {
    switch (error) {
        case ContextError::allocation_failed:
            return LogResultAccess::make_failed(
                LogStatus::resource_exhausted,
                LogFailure{FailureStage::context, FailureReason::context_allocation_failed, 0xFFU,
                           0});

        case ContextError::tls_capacity_exhausted:
            return LogResultAccess::make_failed(
                LogStatus::resource_exhausted,
                LogFailure{FailureStage::context, FailureReason::tls_capacity_exhausted, 0xFFU, 0});
        case ContextError::registration_closed:
            return LogResultAccess::make_failed(
                LogStatus::registration_closed,
                LogFailure{FailureStage::context, FailureReason::registration_closed, 0xFFU, 0});
        case ContextError::producer_token_exhausted:
            return LogResultAccess::make_failed(
                LogStatus::identity_exhausted,
                LogFailure{FailureStage::context, FailureReason::producer_token_exhausted, 0xFFU,
                           0});
    }
    assert(false);
    std::terminate();
}
[[nodiscard]] inline LogResult map_measure_failure(const MeasureFailure& failure) noexcept {
    switch (failure.error) {
        case MeasureError::invalid_limits:
            return LogResultAccess::make_failed(
                LogStatus::internal_error,
                LogFailure{FailureStage::measure, FailureReason::invalid_limits,
                           failure.argument_index, failure.byte_count});

        case MeasureError::invalid_format_metadata:
            return LogResultAccess::make_failed(
                LogStatus::invalid_input,
                LogFailure{FailureStage::measure, FailureReason::invalid_format_metadata,
                           failure.argument_index, failure.byte_count});
        case MeasureError::invalid_string_metadata:
            return LogResultAccess::make_failed(
                LogStatus::invalid_input,
                LogFailure{FailureStage::measure, FailureReason::invalid_string_metadata,
                           failure.argument_index, failure.byte_count});
        case MeasureError::format_too_large:
            return LogResultAccess::make_failed(
                LogStatus::too_large,
                LogFailure{FailureStage::measure, FailureReason::format_too_large,
                           failure.argument_index, failure.byte_count});
        case MeasureError::argument_length_out_of_range:
            return LogResultAccess::make_failed(
                LogStatus::too_large,
                LogFailure{FailureStage::measure, FailureReason::argument_length_out_of_range,
                           failure.argument_index, failure.byte_count});
        case MeasureError::args_length_out_of_range:
            return LogResultAccess::make_failed(
                LogStatus::too_large,
                LogFailure{FailureStage::measure, FailureReason::args_length_out_of_range,
                           failure.argument_index, failure.byte_count});
        case MeasureError::size_overflow:
            return LogResultAccess::make_failed(
                LogStatus::too_large,
                LogFailure{FailureStage::measure, FailureReason::size_overflow,
                           failure.argument_index, failure.byte_count});
        case MeasureError::record_length_out_of_range:
            return LogResultAccess::make_failed(
                LogStatus::too_large,
                LogFailure{FailureStage::measure, FailureReason::record_length_out_of_range,
                           failure.argument_index, failure.byte_count});
        case MeasureError::payload_too_large:
            return LogResultAccess::make_failed(
                LogStatus::too_large,
                LogFailure{FailureStage::measure, FailureReason::payload_too_large,
                           failure.argument_index, failure.byte_count});
    }
    assert(false);
    std::terminate();
}
[[nodiscard]] inline LogResult map_reserve_error(ReserveStatus status,
                                                 std::size_t requested) noexcept {
    switch (status) {
        case ReserveStatus::full:
            return LogResultAccess::make_failed(
                LogStatus::full,
                LogFailure{FailureStage::reserve, FailureReason::full, 0xFFU, requested});

        case ReserveStatus::payload_too_large:
            return LogResultAccess::make_failed(
                LogStatus::internal_error,
                LogFailure{FailureStage::reserve, FailureReason::payload_too_large, 0xFFU,
                           requested});
        case ReserveStatus::reservation_pending:
            return LogResultAccess::make_failed(
                LogStatus::internal_error,
                LogFailure{FailureStage::reserve, FailureReason::reservation_pending, 0xFFU,
                           requested});
        case ReserveStatus::ok:
            assert(false);
            std::terminate();
    }
    assert(false);
    std::terminate();
}

[[nodiscard]] inline LogResult map_encode_error(EncodeError error,
                                                std::size_t target_size) noexcept {
    switch (error) {
        case EncodeError::invalid_destination_metadata:
            return LogResultAccess::make_failed(
                LogStatus::internal_error,
                LogFailure{FailureStage::encode, FailureReason::invalid_destination_metadata, 0xFFU,
                           target_size});

        case EncodeError::destination_size_mismatch:
            return LogResultAccess::make_failed(
                LogStatus::internal_error,
                LogFailure{FailureStage::encode, FailureReason::destination_size_mismatch, 0xFFU,
                           target_size});
        case EncodeError::invalid_level:
            return LogResultAccess::make_failed(
                LogStatus::internal_error,
                LogFailure{FailureStage::encode, FailureReason::invalid_level, 0xFFU, target_size});
        case EncodeError::unknown_flags:
            return LogResultAccess::make_failed(
                LogStatus::internal_error,
                LogFailure{FailureStage::encode, FailureReason::unknown_flags, 0xFFU, target_size});
        case EncodeError::reserved_timestamp_status:
            return LogResultAccess::make_failed(
                LogStatus::internal_error,
                LogFailure{FailureStage::encode, FailureReason::reserved_timestamp_status, 0xFFU,
                           target_size});
        case EncodeError::invalid_time_value:
            return LogResultAccess::make_failed(
                LogStatus::internal_error,
                LogFailure{FailureStage::encode, FailureReason::invalid_time_value, 0xFFU,
                           target_size});
        case EncodeError::fallback_timestamp_not_configured:
            return LogResultAccess::make_failed(
                LogStatus::internal_error,
                LogFailure{FailureStage::encode, FailureReason::fallback_timestamp_not_configured,
                           0xFFU, target_size});
    }
    assert(false);
    std::terminate();
}

}  // namespace qlog::detail