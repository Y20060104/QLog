#include "manual_worker_fixture.hpp"
// Compile the unchanged production implementation into this test executable so
// Impl is complete for the friend accessor. libqlog supplies the other objects;
// its async_logger object is not extracted (no duplicate definition).
#include <array>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "../src/async_logger.cpp"

#ifndef QLOG_ENABLE_DIAGNOSTICS
#error QLog::qlog must propagate QLOG_ENABLE_DIAGNOSTICS to consumers
#endif
static_assert(QLOG_ENABLE_DIAGNOSTICS == QLOG_TEST_EXPECT_DIAGNOSTICS);

namespace qlog::detail {
struct AsyncLoggerTestAccess {
    template <class T>
    static constexpr bool has_diagnostics = requires(T& value) { value.diagnostics; };
    static constexpr bool enabled() {
        return has_diagnostics<AsyncLogger::Impl>;
    }
    static ProducerContext* head(AsyncLogger& logger) {
        return logger.impl_->published_context.load(std::memory_order_acquire);
    }
    static std::array<std::uint64_t, 7> counts(AsyncLogger& logger) {
#if QLOG_ENABLE_DIAGNOSTICS
        auto& d = logger.impl_->diagnostics;
        return {d.call.calls.load(),
                d.call.filtered.load(),
                d.call.rejected_pre_admission.load(),
                d.admission.attempted.load(),
                d.admission.accepted.load(),
                d.admission.dropped_full.load(),
                d.admission.failed_after_attempt.load()};
#else
        (void)logger;
        return {};
#endif
    }
    static void account(AsyncLogger& logger, const LogResult& result) {
#if QLOG_ENABLE_DIAGNOSTICS
        logger.record_call_result(result, 0U);
#else
        (void)logger;
        (void)result;
#endif
    }
};
static_assert(AsyncLoggerTestAccess::enabled() == (QLOG_TEST_EXPECT_DIAGNOSTICS != 0));
}  // namespace qlog::detail

qlog::LogResult diagnostics_literal_consumer(qlog::AsyncLogger&);
qlog::LogResult diagnostics_view_consumer(qlog::AsyncLogger&);
int diagnostics_consumer_mode();

namespace {
using namespace qlog;
using namespace qlog::detail;
using Access = AsyncLoggerTestAccess;
using Counts = std::array<std::uint64_t, 7>;
void require(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::abort();
    }
}
void check(AsyncLogger& logger, Counts expected) {
    const auto actual = Access::counts(logger);
#if QLOG_ENABLE_DIAGNOSTICS
    if (actual != expected) {
        for (std::size_t i = 0; i < actual.size(); ++i)
            std::fprintf(stderr, "counter %zu: actual=%llu expected=%llu\n", i,
                         static_cast<unsigned long long>(actual[i]),
                         static_cast<unsigned long long>(expected[i]));
        std::abort();
    }
    require(actual[0] == actual[1] + actual[2] + actual[3], "call conservation");
    require(actual[3] == actual[4] + actual[5] + actual[6], "admission conservation");
#else
    (void)expected;
    require(actual == Counts{}, "OFF has no diagnostics field");
#endif
}
LoggerConfig config() {
    LoggerConfig value;
    value.name = "diagnostics";
    return value;
}
void public_paths() {
    AsyncLogger logger(config());
    check(logger, {});
    require(logger.try_log(0U, static_cast<LogLevel>(255), "").status() == LogStatus::invalid_level,
            "invalid level");
    check(logger, {1, 0, 1, 0, 0, 0, 0});
    require(logger.try_log(1U, LogLevel::info, "").status() == LogStatus::invalid_category,
            "invalid category");
    check(logger, {2, 0, 2, 0, 0, 0, 0});
    require(logger.set_category_enabled(0, false), "disable category");
    require(logger.try_log(0, LogLevel::info, runtime_format(static_cast<const char*>(nullptr), 1))
                    .status() == LogStatus::filtered,
            "filter precedes bad format");
    check(logger, {3, 1, 2, 0, 0, 0, 0});
    require(Access::head(logger) == nullptr, "validation and filtering create no context");
    require(logger.set_category_enabled(0, true), "enable category");
    require(logger.try_log(0, LogLevel::info, runtime_format(static_cast<const char*>(nullptr), 1))
                    .status() == LogStatus::invalid_input,
            "measure invalid format");
    check(logger, {4, 1, 3, 0, 0, 0, 0});
    const std::string oversized(8193, 'x');
    require(logger.try_log(0, LogLevel::info, runtime_format(oversized.data(), oversized.size()))
                    .status() == LogStatus::too_large,
            "format limit");
    check(logger, {5, 1, 4, 0, 0, 0, 0});
    require(diagnostics_literal_consumer(logger).accepted(), "literal consumer");
    check(logger, {6, 1, 4, 1, 1, 0, 0});
    require(diagnostics_view_consumer(logger).accepted(), "view consumer");
    check(logger, {7, 1, 4, 2, 2, 0, 0});
    const char raw[2] = {'{', '}'};
    require(logger.try_log(0, LogLevel::info, raw, 3).accepted(), "nonterminated array");
    check(logger, {8, 1, 4, 3, 3, 0, 0});
    require(diagnostics_consumer_mode() == QLOG_TEST_EXPECT_DIAGNOSTICS,
            "cross-TU macro consistency");
    AsyncLogger closed(config());
    closed.close_registration();
    require(closed.try_log(0, LogLevel::info, "x").status() == LogStatus::registration_closed,
            "context failure");
    require(Access::head(closed) == nullptr, "closed registration creates no context");
    check(closed, {1, 0, 1, 0, 0, 0, 0});
    // Existing TLS contexts remain usable: close_registration is not shutdown.
    logger.close_registration();
    require(logger.try_log(0, LogLevel::info, "existing").accepted(),
            "existing context after registration close");
    check(logger, {9, 1, 4, 4, 4, 0, 0});
}
void full_path() {
    auto cfg = config();
    cfg.ring = {256U, 128U};
    AsyncLogger logger(cfg);
    std::uint64_t accepted = 0;
    for (unsigned i = 0; i < 32; ++i) {
        const auto result = logger.try_log(0, LogLevel::info, "{}", 1);
        if (result.status() == LogStatus::full) break;
        require(result.accepted(), "fill accepts or full");
        ++accepted;
    }
    require(accepted > 0 && accepted < 32, "controlled ring filled");
    check(logger, {accepted + 1, 0, 0, accepted + 1, accepted, 1, 0});
    require(logger.try_log(0, LogLevel::info, "x").status() == LogStatus::full, "full persists");
    check(logger, {accepted + 2, 0, 0, accepted + 2, accepted, 2, 0});
}
void internal_failures() {
    auto cfg = config();
    cfg.ring = {4096U, 128U};
    AsyncLogger logger(cfg);
    require(logger.try_log(0, LogLevel::info, runtime_format(static_cast<const char*>(nullptr), 1))
                    .status() == LogStatus::invalid_input,
            "create context without frame");
    auto* context = Access::head(logger);
    require(context != nullptr, "context available");
    auto& channel = context->channel_;
    auto pending = channel.ring_.try_reserve(32);
    require(static_cast<bool>(pending), "external controlled reservation");
    auto result = logger.try_log(0, LogLevel::info, "x");
    require(result.failure() && result.failure()->reason == FailureReason::reservation_pending,
            "non-full reserve failure");
    check(logger, {2, 0, 1, 1, 0, 0, 1});
    channel.ring_.abort(pending);
    channel.cold_.payload_quota = 1024;
    const std::string large(150, 'x');
    result = logger.try_log(0, LogLevel::info, runtime_format(large.data(), large.size()));
    require(result.failure() && result.failure()->stage == FailureStage::reserve &&
                result.failure()->reason == FailureReason::payload_too_large,
            "reserve quota mismatch");
    check(logger, {3, 0, 1, 2, 0, 0, 2});
    channel.cold_.payload_quota = 128;
    // Rebind the test-owned dependency view to a valid policy denying all levels.
    // Do not const_cast the logger's immutable production policy.
    auto& dependencies = channel.cold_.dependencies;
    const auto original = dependencies;
    const RecordValidationPolicy deny{{0, 0, 0, 0}, true};
    std::destroy_at(&dependencies);
    std::construct_at(
        &dependencies,
        ChannelDependencies{original.filter, original.clock, deny, original.hash_dispatch,
                            original.logger_name, original.category_names});
    result = logger.try_log(0, LogLevel::info, "x");
    std::destroy_at(&dependencies);
    std::construct_at(&dependencies, original);
    require(result.failure() && result.failure()->stage == FailureStage::encode,
            "controlled encoder rejection");
    check(logger, {4, 0, 1, 3, 0, 0, 3});
    require(!channel.ring_.try_read(), "failed encode never commits");
    require(logger.try_log(0, LogLevel::info, "recovered").accepted(),
            "encode failure aborted reservation");
    check(logger, {5, 0, 1, 4, 1, 0, 3});
}
void mapped_failures() {
    AsyncLogger logger(config());
    std::uint64_t rejected = 0, failed = 0;
    for (const auto error :
         {ContextError::allocation_failed, ContextError::tls_capacity_exhausted,
          ContextError::registration_closed, ContextError::producer_token_exhausted}) {
        Access::account(logger, map_context_error(error));
        ++rejected;
        check(logger, {rejected, 0, rejected, 0, 0, 0, 0});
    }
    for (const auto error :
         {EncodeError::invalid_destination_metadata, EncodeError::destination_size_mismatch,
          EncodeError::invalid_level, EncodeError::unknown_flags,
          EncodeError::reserved_timestamp_status, EncodeError::invalid_time_value,
          EncodeError::fallback_timestamp_not_configured}) {
        Access::account(logger, map_encode_error(error, 32));
        ++failed;
        check(logger, {rejected + failed, 0, rejected, failed, 0, 0, failed});
    }
}
void concurrent_counts() {
    auto cfg = config();
    cfg.ring = {262144U, 8192U};
    AsyncLogger logger(cfg), other(cfg);
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 8; ++i)
        threads.emplace_back([&] {
            for (unsigned j = 0; j < 500; ++j) {
                require(logger.try_log(0, LogLevel::info, "{}", j).accepted(),
                        "concurrent accepted");
                require(
                    logger.try_log(1, LogLevel::info, "").status() == LogStatus::invalid_category,
                    "concurrent rejected");
                require(other.try_log(0, LogLevel::info, "{}", j).accepted(),
                        "other logger accepted");
            }
        });
    for (auto& thread : threads) thread.join();
    check(logger, {8000, 0, 4000, 4000, 4000, 0, 0});
    check(other, {4000, 0, 0, 4000, 4000, 0, 0});
}
}  // namespace
int main() {
    qlog::test::install_manual_worker();
    public_paths();
    full_path();
    internal_failures();
    mapped_failures();
    concurrent_counts();
    std::printf(
        "PASS: diagnostics=%d; 8 return sites, synthetic error mappings, cross-TU consumers, "
        "8-thread conservation\n",
        QLOG_ENABLE_DIAGNOSTICS);
}
