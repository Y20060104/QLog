#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <utility>

#include "qlog/detail/producer_context.hpp"
#include "qlog/detail/producer_result_map.hpp"
#include "qlog/detail/record_encoder.hpp"
#include "qlog/detail/record_measure.hpp"

namespace qlog {

template <typename... Args>
    requires(sizeof...(Args) <= detail::kMaxArgCount) && (detail::SupportedArgument<Args> && ...)
[[nodiscard]] LogResult AsyncLogger::try_log(std::uint32_t category_id, LogLevel level,
                                             FormatView format, Args&&... args) const noexcept {
    switch (classify_call(category_id, level)) {
        case detail::CallGate::invalid_level:
#if QLOG_ENABLE_DIAGNOSTICS
            record_call_result(
                detail::LogResultAccess::make_failed(
                    LogStatus::invalid_level,
                    LogFailure{FailureStage::validation, FailureReason::invalid_level, 0xFFU, 0}),
                0U);
#endif
            return detail::LogResultAccess::make_failed(
                LogStatus::invalid_level,
                LogFailure{FailureStage::validation, FailureReason::invalid_level, 0xFFU, 0});
        case detail::CallGate::invalid_category:
#if QLOG_ENABLE_DIAGNOSTICS
            record_call_result(detail::LogResultAccess::make_failed(
                                   LogStatus::invalid_category,
                                   LogFailure{FailureStage::validation,
                                              FailureReason::invalid_category, 0xFFU, 0}),
                               0U);
#endif
            return detail::LogResultAccess::make_failed(
                LogStatus::invalid_category,
                LogFailure{FailureStage::validation, FailureReason::invalid_category, 0xFFU, 0});
        case detail::CallGate::filtered:
#if QLOG_ENABLE_DIAGNOSTICS
            record_call_result(detail::LogResultAccess::make_filtered(), 0U);
#endif
            return detail::LogResultAccess::make_filtered();
        case detail::CallGate::proceed:
            break;
        default:
            assert(false);
            std::terminate();
    }

    const auto acquired = acquire_context_for_thread();
    auto* context = acquired.context();
    if (context == nullptr) {
        assert(acquired.error() != nullptr);
#if QLOG_ENABLE_DIAGNOSTICS
        record_call_result(detail::map_context_error(*acquired.error()), 0U);
#endif
        return detail::map_context_error(*acquired.error());
    }

    auto& channel = context->channel_;
    auto& ring = channel.ring_;
    const auto& dependencies = channel.cold_.dependencies;
    const detail::FormatInput input{format.data(), format.size(), format.stored_hash()};
    const auto measured =
        detail::measure_record(input, channel.cold_.payload_quota, std::forward<Args>(args)...);
    if (!measured.succeeded()) {
#if QLOG_ENABLE_DIAGNOSTICS
        record_call_result(detail::map_measure_failure(*measured.failure()), 0U);
#endif
        return detail::map_measure_failure(*measured.failure());
    }
    const auto& prepared = *measured.prepared();
    auto write = ring.try_reserve(prepared.payload_size());
    if (!write) {
        if (write.status() == detail::ReserveStatus::full) notify_backend();
#if QLOG_ENABLE_DIAGNOSTICS
        record_call_result(detail::map_reserve_error(write.status(), prepared.payload_size()), 0U);
#endif
        return detail::map_reserve_error(write.status(), prepared.payload_size());
    }

    const auto timestamp = detail::sample_admission_timestamp(dependencies.clock);
    const detail::RecordMetadata metadata{timestamp.time_value, category_id,
                                          static_cast<std::uint8_t>(level), timestamp.flags};
    const auto encoded = detail::encode_v1(write.data(), write.size(), prepared, metadata,
                                           dependencies.policy, dependencies.hash_dispatch);
    if (!encoded.succeeded()) {
        const auto target_size = write.size();
        ring.abort(write);
#if QLOG_ENABLE_DIAGNOSTICS
        record_call_result(detail::map_encode_error(*encoded.failure(), target_size), 0U);
#endif
        return detail::map_encode_error(*encoded.failure(), target_size);
    }
    ring.commit(write);
    if (ring.writer_at_least_half_full()) notify_backend();
#if QLOG_ENABLE_DIAGNOSTICS
    record_call_result(detail::LogResultAccess::make_accepted(), prepared.payload_size());
#endif
    return detail::LogResultAccess::make_accepted();
}

template <std::size_t N, typename... Args>
    requires(N > 0U) && (sizeof...(Args) <= detail::kMaxArgCount) &&
            (detail::SupportedArgument<Args> && ...)
[[nodiscard]] LogResult AsyncLogger::try_log(std::uint32_t category_id, LogLevel level,
                                             const char (&format)[N],
                                             Args&&... args) const noexcept {
    const std::size_t format_size = format[N - 1U] == '\0' ? N - 1U : N;
    return try_log(category_id, level, runtime_format(format, format_size),
                   std::forward<Args>(args)...);
}
}  // namespace qlog
