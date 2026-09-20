#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "manual_worker_fixture.hpp"
#include "qlog/async_logger.hpp"

namespace {
void require(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << '\n';
        std::abort();
    }
}
auto submit(qlog::AsyncLogger& logger) {
    return logger.try_log(0U, qlog::LogLevel::info, qlog::runtime_format("x={} y={}", 9U), 12, 34);
}
void check(const qlog::LogResult& result, qlog::LogStatus status, qlog::FailureStage stage,
           qlog::FailureReason reason, std::size_t index, std::size_t bytes) {
    require(result.status() == status, "status");
    const auto* failure = result.failure();
    require(failure != nullptr, "failure present");
    require(failure->stage == stage && failure->reason == reason, "stage/reason");
    require(failure->argument_index == index && failure->byte_count == bytes, "index/bytes");
}
}  // namespace

int main() {
    qlog::test::install_manual_worker();
    using namespace qlog;
    using namespace qlog::detail;
    const ContextError context_errors[]{
        ContextError::allocation_failed, ContextError::tls_capacity_exhausted,
        ContextError::registration_closed, ContextError::producer_token_exhausted};
    const LogStatus context_status[]{LogStatus::resource_exhausted, LogStatus::resource_exhausted,
                                     LogStatus::registration_closed, LogStatus::identity_exhausted};
    const FailureReason context_reason[]{
        FailureReason::context_allocation_failed, FailureReason::tls_capacity_exhausted,
        FailureReason::registration_closed, FailureReason::producer_token_exhausted};
    for (std::size_t i = 0; i < 4; ++i)
        check(map_context_error(context_errors[i]), context_status[i], FailureStage::context,
              context_reason[i], 0xFFU, 0);
    const MeasureError measure_errors[]{MeasureError::invalid_limits,
                                        MeasureError::invalid_format_metadata,
                                        MeasureError::invalid_string_metadata,
                                        MeasureError::format_too_large,
                                        MeasureError::argument_length_out_of_range,
                                        MeasureError::args_length_out_of_range,
                                        MeasureError::size_overflow,
                                        MeasureError::record_length_out_of_range,
                                        MeasureError::payload_too_large};
    const FailureReason measure_reason[]{FailureReason::invalid_limits,
                                         FailureReason::invalid_format_metadata,
                                         FailureReason::invalid_string_metadata,
                                         FailureReason::format_too_large,
                                         FailureReason::argument_length_out_of_range,
                                         FailureReason::args_length_out_of_range,
                                         FailureReason::size_overflow,
                                         FailureReason::record_length_out_of_range,
                                         FailureReason::payload_too_large};
    for (std::size_t i = 0; i < 9; ++i) {
        const auto status = i == 0  ? LogStatus::internal_error
                            : i < 3 ? LogStatus::invalid_input
                                    : LogStatus::too_large;
        check(map_measure_failure({measure_errors[i], 3U, 97U}), status, FailureStage::measure,
              measure_reason[i], 3U, 97U);
    }
    const ReserveStatus reserve_errors[]{ReserveStatus::full, ReserveStatus::payload_too_large,
                                         ReserveStatus::reservation_pending};
    const FailureReason reserve_reason[]{FailureReason::full, FailureReason::payload_too_large,
                                         FailureReason::reservation_pending};
    for (std::size_t i = 0; i < 3; ++i)
        check(map_reserve_error(reserve_errors[i], 96U),
              i == 0 ? LogStatus::full : LogStatus::internal_error, FailureStage::reserve,
              reserve_reason[i], 0xFFU, 96U);
    const EncodeError encode_errors[]{EncodeError::invalid_destination_metadata,
                                      EncodeError::destination_size_mismatch,
                                      EncodeError::invalid_level,
                                      EncodeError::unknown_flags,
                                      EncodeError::reserved_timestamp_status,
                                      EncodeError::invalid_time_value,
                                      EncodeError::fallback_timestamp_not_configured};
    const FailureReason encode_reason[]{FailureReason::invalid_destination_metadata,
                                        FailureReason::destination_size_mismatch,
                                        FailureReason::invalid_level,
                                        FailureReason::unknown_flags,
                                        FailureReason::reserved_timestamp_status,
                                        FailureReason::invalid_time_value,
                                        FailureReason::fallback_timestamp_not_configured};
    for (std::size_t i = 0; i < 7; ++i)
        check(map_encode_error(encode_errors[i], 96U), LogStatus::internal_error,
              FailureStage::encode, encode_reason[i], 0xFFU, 96U);

    LoggerConfig cfg;
    cfg.name = "runtime-probe";
    cfg.ring = {256U, 128U};
    AsyncLogger a(cfg), b(cfg);
    check(a.try_log(0U, static_cast<LogLevel>(255U), runtime_format("", 0U)),
          LogStatus::invalid_level, FailureStage::validation, FailureReason::invalid_level, 0xFFU,
          0);
    check(a.try_log(1U, LogLevel::info, runtime_format("", 0U)), LogStatus::invalid_category,
          FailureStage::validation, FailureReason::invalid_category, 0xFFU, 0);
    require(a.set_category_enabled(0U, false), "disable category");
    const auto filtered = submit(a);
    require(filtered.status() == LogStatus::filtered && !filtered.failure(), "filtered result");
    require(a.set_category_enabled(0U, true), "enable category");
    const auto invalid =
        a.try_log(0U, LogLevel::info, runtime_format(static_cast<const char*>(nullptr), 1U));
    require(invalid.status() == LogStatus::invalid_input, "invalid format metadata");
    require(submit(a).accepted(), "accepted after measure failure");
    bool full = false;
    for (unsigned i = 0; i < 100; ++i) {
        const auto result = submit(a);
        if (result.status() == LogStatus::full) {
            full = true;
            break;
        }
        require(result.accepted(), "fill ring");
    }
    require(full, "bounded ring becomes full");
    require(submit(b).accepted(), "B independent of full A");
    require(submit(a).status() == LogStatus::full, "A remains full after B");
    require(submit(a).status() == LogStatus::full, "A fast cache remains A");

    AsyncLogger closed(cfg);
    closed.close_registration();
    check(submit(closed), LogStatus::registration_closed, FailureStage::context,
          FailureReason::registration_closed, 0xFFU, 0);
    LoggerConfig concurrent_cfg;
    concurrent_cfg.name = "multi";
    AsyncLogger shared(concurrent_cfg);
    std::atomic<unsigned> accepted{0};
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 4; ++i)
        threads.emplace_back([&] {
            for (unsigned j = 0; j < 20; ++j)
                if (submit(shared).accepted()) ++accepted;
        });
    for (auto& thread : threads) thread.join();
    require(accepted.load() == 80U, "concurrent first-use and writes");

    alignas(AsyncLogger) std::byte storage[sizeof(AsyncLogger)];
    for (unsigned i = 0; i < 40; ++i) {
        auto* logger = std::construct_at(reinterpret_cast<AsyncLogger*>(storage), cfg);
        require(submit(*logger).accepted(), "same address new logger identity");
        std::destroy_at(logger);
    }
    std::cout << "PASS: 23 error mappings; public runtime "
                 "validation/filter/full/cache/concurrency/address-reuse scenarios\n";
}
