#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace qlog::bench {

enum class Implementation : std::uint8_t {
    qlog,
    bqlog,
};

enum class Scenario : std::uint8_t {
    transfer_retry,
    drop_new,
    full_fast_fail,
    write_success,
    prefilled_read,
};

enum class AttemptStatus : std::uint8_t {
    success,
    full,
    empty,
    unexpected,
};

enum class OutputFormat : std::uint8_t {
    table,
    jsonl,
};

struct BenchmarkOptions {
    std::size_t capacity_bytes{64U * 1024U};
    std::size_t payload_bytes{64U};
    std::chrono::milliseconds warmup{250};
    std::chrono::milliseconds duration{1000};
    std::size_t repeat{1U};
    int producer_cpu{-1};
    int consumer_cpu{-1};
};

struct Counters {
    std::uint64_t reserve_calls{};
    std::uint64_t attempted_messages{};
    std::uint64_t accepted{};
    std::uint64_t consumed{};
    std::uint64_t full_retries{};
    std::uint64_t dropped_full{};
    std::uint64_t unexpected_failures{};
    std::uint64_t empty_reads{};
    std::uint64_t read_calls{};
    std::uint64_t validation_failures{};
    std::uint64_t checksum{};
};

struct BenchmarkResult {
    Implementation implementation{Implementation::qlog};
    Scenario scenario{Scenario::transfer_retry};
    BenchmarkOptions options{};
    std::size_t repetition{};
    Counters counters{};
    std::uint64_t active_elapsed_ns{};
    std::uint64_t drain_elapsed_ns{};
    std::uint64_t total_elapsed_ns{};
    std::uint64_t cycle_count{};
    std::uint64_t target_records_per_cycle{};
    std::uint64_t padding_records_per_cycle{};
    bool affinity_applied{};
    bool valid{};
    std::string error{};

    [[nodiscard]] double accepted_per_second() const noexcept {
        if (active_elapsed_ns == 0U) {
            return 0.0;
        }
        return static_cast<double>(counters.accepted) * 1'000'000'000.0 /
               static_cast<double>(active_elapsed_ns);
    }

    [[nodiscard]] double read_per_second() const noexcept {
        if (active_elapsed_ns == 0U) {
            return 0.0;
        }
        return static_cast<double>(counters.consumed) * 1'000'000'000.0 /
               static_cast<double>(active_elapsed_ns);
    }

    [[nodiscard]] double primary_per_second() const noexcept {
        if (scenario == Scenario::full_fast_fail) {
            return full_calls_per_second();
        }
        if (scenario == Scenario::prefilled_read) {
            return read_per_second();
        }
        return accepted_per_second();
    }

    [[nodiscard]] double nanoseconds_per_operation() const noexcept {
        const double rate = primary_per_second();
        if (rate == 0.0) {
            return 0.0;
        }
        return 1'000'000'000.0 / rate;
    }

    [[nodiscard]] double processed_per_second() const noexcept {
        if (total_elapsed_ns == 0U) {
            return 0.0;
        }
        return static_cast<double>(counters.consumed) * 1'000'000'000.0 /
               static_cast<double>(total_elapsed_ns);
    }

    [[nodiscard]] double payload_mib_per_second() const noexcept {
        constexpr double kBytesPerMebibyte = 1024.0 * 1024.0;
        return primary_per_second() * static_cast<double>(options.payload_bytes) /
               kBytesPerMebibyte;
    }

    [[nodiscard]] double full_calls_per_second() const noexcept {
        if (active_elapsed_ns == 0U) {
            return 0.0;
        }
        const std::uint64_t full_calls = counters.full_retries + counters.dropped_full;
        return static_cast<double>(full_calls) * 1'000'000'000.0 /
               static_cast<double>(active_elapsed_ns);
    }
};

struct BenchmarkCase {
    Scenario scenario{Scenario::transfer_retry};
    std::size_t capacity_bytes{64U * 1024U};
    std::size_t payload_bytes{64U};
};

[[nodiscard]] const char* to_string(Implementation implementation) noexcept;
[[nodiscard]] const char* to_string(Scenario scenario) noexcept;

BenchmarkResult run_qlog_benchmark(Scenario scenario, const BenchmarkOptions& options,
                                   std::size_t repetition);

#if QLOG_BENCH_WITH_BQLOG
BenchmarkResult run_bqlog_benchmark(Scenario scenario, const BenchmarkOptions& options,
                                    std::size_t repetition);
#endif

}  // namespace qlog::bench
