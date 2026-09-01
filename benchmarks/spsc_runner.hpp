#pragma once

#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "benchmark_types.hpp"
#include "linux_support.hpp"

namespace qlog::bench {

namespace detail {

using Clock = std::chrono::steady_clock;

inline constexpr std::uint64_t kCanarySalt = 0x9E3779B97F4A7C15ULL;
inline constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
inline constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
inline constexpr std::uint64_t kStopCheckMask = 1023U;

struct alignas(64) RunControl {
    std::atomic<bool> stop{false};
    std::atomic<bool> producer_done{false};
    std::atomic<bool> fatal{false};
    std::atomic<bool> setup_ok{true};
    std::atomic<std::uint64_t> accepted_final{0U};
};

[[nodiscard]] inline constexpr std::uint8_t payload_source_byte(std::size_t index) noexcept {
    return static_cast<std::uint8_t>((index * 131U + 17U) & 0xFFU);
}

[[nodiscard]] inline std::uint64_t elapsed_ns(Clock::time_point begin,
                                              Clock::time_point end) noexcept {
    if (end <= begin) {
        return 0U;
    }
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
}

[[nodiscard]] inline std::vector<std::byte> make_payload_source(std::size_t payload_bytes) {
    std::vector<std::byte> source(payload_bytes);
    for (std::size_t index = 0; index < source.size(); ++index) {
        source[index] = static_cast<std::byte>(payload_source_byte(index));
    }
    return source;
}

inline void stamp_payload(std::byte* destination, const std::vector<std::byte>& source,
                          std::uint64_t sequence) noexcept {
    if (source.empty()) {
        return;
    }

    std::memcpy(destination, source.data(), source.size());
    if (source.size() >= 2U * sizeof(std::uint64_t)) {
        const std::uint64_t canary = sequence ^ kCanarySalt;
        std::memcpy(destination, &sequence, sizeof(sequence));
        std::memcpy(destination + source.size() - sizeof(canary), &canary, sizeof(canary));
    }
    compiler_memory_barrier(destination);
}

struct ConsumerState {
    std::uint64_t consumed{};
    std::uint64_t empty_reads{};
    std::uint64_t read_calls{};
    std::uint64_t unexpected_failures{};
    std::uint64_t validation_failures{};
    std::uint64_t checksum{kFnvOffset};
    std::uint64_t next_expected_sequence{};
    std::uint64_t last_sequence{};
    bool has_last_sequence{};
};

[[nodiscard]] inline constexpr bool requires_contiguous_sequence(Scenario scenario) noexcept {
    return scenario == Scenario::transfer_retry || scenario == Scenario::write_success ||
           scenario == Scenario::prefilled_read;
}

inline void validate_payload(const std::byte* payload, std::size_t payload_bytes, Scenario scenario,
                             ConsumerState& state) noexcept {
    if (payload_bytes == 0U) {
        state.checksum = (state.checksum ^ state.consumed) * kFnvPrime;
        return;
    }

    if (payload == nullptr) {
        ++state.validation_failures;
        return;
    }

    if (payload_bytes < 2U * sizeof(std::uint64_t)) {
        bool payload_matches = true;
        for (std::size_t index = 0; index < payload_bytes; ++index) {
            const std::uint8_t value = static_cast<std::uint8_t>(payload[index]);
            payload_matches = payload_matches && value == payload_source_byte(index);
            state.checksum ^= value;
            state.checksum *= kFnvPrime;
        }
        if (!payload_matches) {
            ++state.validation_failures;
        }
        return;
    }

    std::uint64_t sequence{};
    std::uint64_t canary{};
    std::memcpy(&sequence, payload, sizeof(sequence));
    std::memcpy(&canary, payload + payload_bytes - sizeof(canary), sizeof(canary));

    if (canary != (sequence ^ kCanarySalt)) {
        ++state.validation_failures;
    }

    if (requires_contiguous_sequence(scenario)) {
        if (sequence != state.next_expected_sequence) {
            ++state.validation_failures;
        }
        ++state.next_expected_sequence;
    } else {
        if (state.has_last_sequence && sequence <= state.last_sequence) {
            ++state.validation_failures;
        }
        state.last_sequence = sequence;
        state.has_last_sequence = true;
    }

    if (scenario == Scenario::drop_new) {
        for (std::size_t index = 0; index < payload_bytes; ++index) {
            state.checksum ^= static_cast<std::uint8_t>(payload[index]);
            state.checksum *= kFnvPrime;
        }
    } else {
        state.checksum ^= sequence;
        state.checksum *= kFnvPrime;
        state.checksum ^= canary;
        state.checksum *= kFnvPrime;
    }
}

template <typename Adapter>
[[nodiscard]] bool preflight(const BenchmarkOptions& options, std::string& error) {
    Adapter adapter(options.capacity_bytes, options.payload_bytes);
    const std::vector<std::byte> source = make_payload_source(options.payload_bytes);
    constexpr std::uint64_t kSequence = 7U;

    auto write_handle = adapter.reserve(options.payload_bytes);
    if (Adapter::write_status(write_handle) != AttemptStatus::success) {
        adapter.commit(write_handle);
        error = "preflight reserve did not succeed";
        return false;
    }

    stamp_payload(Adapter::write_data(write_handle), source, kSequence);
    adapter.commit(write_handle);

    auto read_handle = adapter.read();
    if (Adapter::read_status(read_handle) != AttemptStatus::success) {
        adapter.consume(read_handle);
        error = "preflight read did not succeed";
        return false;
    }

    bool valid = Adapter::read_size(read_handle) == options.payload_bytes;
    if (valid && options.payload_bytes != 0U) {
        std::vector<std::byte> expected = source;
        stamp_payload(expected.data(), source, kSequence);
        valid = std::memcmp(Adapter::read_data(read_handle), expected.data(), expected.size()) == 0;
    }

    adapter.consume(read_handle);
    if (!valid) {
        error = "preflight full-payload validation failed";
    }
    return valid;
}

inline void append_error(std::string& destination, const std::string& error) {
    if (error.empty()) {
        return;
    }
    if (!destination.empty()) {
        destination += "; ";
    }
    destination += error;
}

[[nodiscard]] inline bool validate_invariants(BenchmarkResult& result) {
    const Counters& counters = result.counters;
    bool valid = counters.unexpected_failures == 0U && counters.validation_failures == 0U;

    switch (result.scenario) {
        case Scenario::transfer_retry:
            valid = valid && counters.reserve_calls == counters.accepted + counters.full_retries &&
                    counters.attempted_messages == counters.accepted &&
                    counters.dropped_full == 0U && counters.accepted == counters.consumed &&
                    counters.read_calls == counters.consumed + counters.empty_reads;
            break;
        case Scenario::drop_new:
            valid = valid && counters.reserve_calls == counters.attempted_messages &&
                    counters.attempted_messages == counters.accepted + counters.dropped_full &&
                    counters.full_retries == 0U && counters.accepted == counters.consumed &&
                    counters.read_calls == counters.consumed + counters.empty_reads;
            break;
        case Scenario::full_fast_fail:
            valid = valid && counters.reserve_calls == counters.attempted_messages &&
                    counters.accepted == 0U && counters.consumed == 0U &&
                    counters.read_calls == 0U && counters.full_retries == 0U &&
                    counters.attempted_messages == counters.dropped_full;
            break;
        case Scenario::write_success:
        case Scenario::prefilled_read: {
            const std::uint64_t expected_records =
                result.cycle_count * result.target_records_per_cycle;
            valid = valid && result.cycle_count > 0U && result.target_records_per_cycle > 0U &&
                    counters.reserve_calls == expected_records &&
                    counters.attempted_messages == expected_records &&
                    counters.accepted == expected_records &&
                    counters.read_calls == expected_records &&
                    counters.consumed == expected_records && counters.full_retries == 0U &&
                    counters.dropped_full == 0U && counters.empty_reads == 0U;
            break;
        }
    }

    if (!valid) {
        append_error(result.error, "counter or payload invariant failed");
    }
    return valid;
}

struct IsolatedCyclePlan {
    std::uint64_t target_records{};
    std::uint64_t padding_records{};
};

template <typename Adapter>
void write_record_or_throw(Adapter& adapter, std::size_t payload_bytes,
                           const std::vector<std::byte>& source, std::uint64_t sequence) {
    auto handle = adapter.reserve(payload_bytes);
    const AttemptStatus status = Adapter::write_status(handle);
    if (status != AttemptStatus::success) {
        adapter.commit(handle);
        throw std::runtime_error("isolated benchmark expected a successful reserve");
    }
    stamp_payload(Adapter::write_data(handle), source, sequence);
    adapter.commit(handle);
}

template <typename Adapter>
void read_record_or_throw(Adapter& adapter, std::size_t expected_payload_bytes, Scenario scenario,
                          ConsumerState* consumer_state) {
    auto handle = adapter.read();
    const AttemptStatus status = Adapter::read_status(handle);
    if (status != AttemptStatus::success) {
        adapter.consume(handle);
        throw std::runtime_error("isolated benchmark expected a readable record");
    }

    const bool size_matches = Adapter::read_size(handle) == expected_payload_bytes;
    if (consumer_state != nullptr) {
        ++consumer_state->read_calls;
        if (!size_matches) {
            ++consumer_state->validation_failures;
        } else {
            validate_payload(Adapter::read_data(handle), expected_payload_bytes, scenario,
                             *consumer_state);
        }
        adapter.consume(handle);
        ++consumer_state->consumed;
        return;
    }

    adapter.consume(handle);
    if (!size_matches) {
        throw std::runtime_error("isolated benchmark observed an unexpected payload length");
    }
}

template <typename Adapter>
void expect_empty_or_throw(Adapter& adapter) {
    auto handle = adapter.read();
    const AttemptStatus status = Adapter::read_status(handle);
    adapter.consume(handle);
    if (status != AttemptStatus::empty) {
        throw std::runtime_error("isolated benchmark expected an empty ring");
    }
}

template <typename Adapter>
void expect_full_or_throw(Adapter& adapter) {
    auto handle = adapter.reserve(0U);
    const AttemptStatus status = Adapter::write_status(handle);
    adapter.commit(handle);
    if (status != AttemptStatus::full) {
        throw std::runtime_error("isolated benchmark expected a full ring");
    }
}

template <typename Adapter>
[[nodiscard]] IsolatedCyclePlan build_isolated_cycle_plan(const BenchmarkOptions& options) {
    Adapter adapter(options.capacity_bytes, options.payload_bytes);
    const std::vector<std::byte> source = make_payload_source(options.payload_bytes);
    const std::vector<std::byte> empty_source;
    const std::uint64_t maximum_records =
        static_cast<std::uint64_t>(options.capacity_bytes / 8U) + 1U;

    IsolatedCyclePlan plan;
    std::uint64_t sequence = 0U;
    while (true) {
        auto handle = adapter.reserve(options.payload_bytes);
        const AttemptStatus status = Adapter::write_status(handle);
        if (status == AttemptStatus::success) {
            stamp_payload(Adapter::write_data(handle), source, sequence++);
            adapter.commit(handle);
            ++plan.target_records;
            if (plan.target_records > maximum_records) {
                throw std::runtime_error("isolated target calibration did not make progress");
            }
            continue;
        }

        adapter.commit(handle);
        if (status != AttemptStatus::full) {
            throw std::runtime_error("isolated target calibration failed unexpectedly");
        }
        break;
    }

    if (plan.target_records == 0U) {
        throw std::runtime_error("isolated benchmark cannot fit one target record");
    }

    while (true) {
        auto handle = adapter.reserve(0U);
        const AttemptStatus status = Adapter::write_status(handle);
        if (status == AttemptStatus::success) {
            stamp_payload(Adapter::write_data(handle), empty_source, 0U);
            adapter.commit(handle);
            ++plan.padding_records;
            if (plan.target_records + plan.padding_records > maximum_records) {
                throw std::runtime_error("isolated padding calibration did not make progress");
            }
            continue;
        }

        adapter.commit(handle);
        if (status != AttemptStatus::full) {
            throw std::runtime_error("isolated padding calibration failed unexpectedly");
        }
        break;
    }

    for (std::uint64_t index = 0; index < plan.target_records; ++index) {
        read_record_or_throw(adapter, options.payload_bytes, Scenario::write_success, nullptr);
    }
    for (std::uint64_t index = 0; index < plan.padding_records; ++index) {
        read_record_or_throw(adapter, 0U, Scenario::write_success, nullptr);
    }
    expect_empty_or_throw(adapter);

    for (std::uint64_t index = 0; index < plan.padding_records; ++index) {
        write_record_or_throw(adapter, 0U, empty_source, 0U);
    }
    for (std::uint64_t index = 0; index < plan.target_records; ++index) {
        write_record_or_throw(adapter, options.payload_bytes, source, index);
    }
    expect_full_or_throw(adapter);
    for (std::uint64_t index = 0; index < plan.padding_records; ++index) {
        read_record_or_throw(adapter, 0U, Scenario::write_success, nullptr);
    }
    for (std::uint64_t index = 0; index < plan.target_records; ++index) {
        read_record_or_throw(adapter, options.payload_bytes, Scenario::write_success, nullptr);
    }
    expect_empty_or_throw(adapter);
    return plan;
}

template <typename Adapter>
[[nodiscard]] BenchmarkResult run_isolated(Implementation implementation, Scenario scenario,
                                           const BenchmarkOptions& options,
                                           std::size_t repetition) {
    BenchmarkResult result;
    result.implementation = implementation;
    result.scenario = scenario;
    result.options = options;
    result.repetition = repetition;

    std::string preflight_error;
    if (!preflight<Adapter>(options, preflight_error)) {
        result.error = std::move(preflight_error);
        return result;
    }

    const IsolatedCyclePlan plan = build_isolated_cycle_plan<Adapter>(options);
    Adapter adapter(options.capacity_bytes, options.payload_bytes);
    const std::vector<std::byte> source = make_payload_source(options.payload_bytes);
    const std::vector<std::byte> empty_source;
    const std::uint64_t requested_active_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(options.duration).count());

    std::atomic<bool> setup_ok{true};
    std::atomic<bool> fatal{false};
    std::atomic<bool> stop_after_cycle{false};
    std::barrier setup_barrier(2);
    std::barrier phase_barrier(2);
    Counters producer_counters;
    ConsumerState consumer_state;
    std::string producer_error;
    std::string consumer_error;
    std::uint64_t producer_active_ns{};
    std::uint64_t consumer_active_ns{};
    std::uint64_t drain_elapsed_ns{};
    std::uint64_t write_cycles{};
    std::uint64_t read_cycles{};
    Clock::time_point wall_begin{};
    Clock::time_point wall_end{};

    std::thread producer([&]() {
        if (!pin_current_thread(options.producer_cpu, producer_error)) {
            setup_ok.store(false, std::memory_order_release);
        }
        setup_barrier.arrive_and_wait();
        if (!setup_ok.load(std::memory_order_acquire)) {
            return;
        }

        std::uint64_t sequence = 0U;
        wall_begin = Clock::now();
        while (true) {
            try {
                if (scenario == Scenario::write_success) {
                    for (std::uint64_t index = 0; index < plan.padding_records; ++index) {
                        write_record_or_throw(adapter, 0U, empty_source, 0U);
                    }

                    const Clock::time_point active_begin = Clock::now();
                    for (std::uint64_t index = 0; index < plan.target_records; ++index) {
                        write_record_or_throw(adapter, options.payload_bytes, source, sequence++);
                        ++producer_counters.reserve_calls;
                        ++producer_counters.attempted_messages;
                        ++producer_counters.accepted;
                    }
                    const Clock::time_point active_end = Clock::now();
                    producer_active_ns += elapsed_ns(active_begin, active_end);
                    if (producer_active_ns >= requested_active_ns) {
                        stop_after_cycle.store(true, std::memory_order_release);
                    }
                } else {
                    for (std::uint64_t index = 0; index < plan.target_records; ++index) {
                        write_record_or_throw(adapter, options.payload_bytes, source, sequence++);
                        ++producer_counters.reserve_calls;
                        ++producer_counters.attempted_messages;
                        ++producer_counters.accepted;
                    }
                    for (std::uint64_t index = 0; index < plan.padding_records; ++index) {
                        write_record_or_throw(adapter, 0U, empty_source, 0U);
                    }
                }
            } catch (const std::exception& exception) {
                ++producer_counters.unexpected_failures;
                append_error(producer_error, exception.what());
                fatal.store(true, std::memory_order_release);
            }

            phase_barrier.arrive_and_wait();
            phase_barrier.arrive_and_wait();

            if (!fatal.load(std::memory_order_acquire)) {
                if (scenario == Scenario::write_success) {
                    ++write_cycles;
                }
            }
            if (fatal.load(std::memory_order_acquire) ||
                stop_after_cycle.load(std::memory_order_acquire)) {
                break;
            }
        }
        wall_end = Clock::now();
    });

    std::thread consumer([&]() {
        if (!pin_current_thread(options.consumer_cpu, consumer_error)) {
            setup_ok.store(false, std::memory_order_release);
        }
        setup_barrier.arrive_and_wait();
        if (!setup_ok.load(std::memory_order_acquire)) {
            return;
        }

        while (true) {
            phase_barrier.arrive_and_wait();
            if (!fatal.load(std::memory_order_acquire)) {
                try {
                    if (scenario == Scenario::write_success) {
                        const Clock::time_point drain_begin = Clock::now();
                        for (std::uint64_t index = 0; index < plan.padding_records; ++index) {
                            read_record_or_throw(adapter, 0U, scenario, nullptr);
                        }
                        for (std::uint64_t index = 0; index < plan.target_records; ++index) {
                            read_record_or_throw(adapter, options.payload_bytes, scenario,
                                                 &consumer_state);
                        }
                        expect_empty_or_throw(adapter);
                        drain_elapsed_ns += elapsed_ns(drain_begin, Clock::now());
                    } else {
                        const Clock::time_point active_begin = Clock::now();
                        for (std::uint64_t index = 0; index < plan.target_records; ++index) {
                            read_record_or_throw(adapter, options.payload_bytes, scenario,
                                                 &consumer_state);
                        }
                        const Clock::time_point active_end = Clock::now();
                        consumer_active_ns += elapsed_ns(active_begin, active_end);

                        const Clock::time_point drain_begin = Clock::now();
                        for (std::uint64_t index = 0; index < plan.padding_records; ++index) {
                            read_record_or_throw(adapter, 0U, scenario, nullptr);
                        }
                        expect_empty_or_throw(adapter);
                        drain_elapsed_ns += elapsed_ns(drain_begin, Clock::now());
                        if (consumer_active_ns >= requested_active_ns) {
                            stop_after_cycle.store(true, std::memory_order_release);
                        }
                    }
                } catch (const std::exception& exception) {
                    ++consumer_state.unexpected_failures;
                    append_error(consumer_error, exception.what());
                    fatal.store(true, std::memory_order_release);
                }
            }

            if (!fatal.load(std::memory_order_acquire) && scenario == Scenario::prefilled_read) {
                ++read_cycles;
            }
            phase_barrier.arrive_and_wait();

            if (fatal.load(std::memory_order_acquire) ||
                stop_after_cycle.load(std::memory_order_acquire)) {
                break;
            }
        }
    });

    producer.join();
    consumer.join();

    result.counters = producer_counters;
    result.counters.consumed = consumer_state.consumed;
    result.counters.read_calls = consumer_state.read_calls;
    result.counters.unexpected_failures += consumer_state.unexpected_failures;
    result.counters.validation_failures = consumer_state.validation_failures;
    result.counters.checksum = consumer_state.checksum;
    result.active_elapsed_ns =
        scenario == Scenario::write_success ? producer_active_ns : consumer_active_ns;
    result.drain_elapsed_ns = drain_elapsed_ns;
    result.total_elapsed_ns = elapsed_ns(wall_begin, wall_end);
    result.cycle_count = scenario == Scenario::write_success ? write_cycles : read_cycles;
    result.target_records_per_cycle = plan.target_records;
    result.padding_records_per_cycle = plan.padding_records;
    result.affinity_applied = setup_ok.load(std::memory_order_acquire);

    append_error(result.error, producer_error);
    append_error(result.error, consumer_error);
    result.valid = result.affinity_applied && validate_invariants(result);
    return result;
}

template <typename Adapter>
[[nodiscard]] BenchmarkResult run_transfer(Implementation implementation, Scenario scenario,
                                           const BenchmarkOptions& options,
                                           std::size_t repetition) {
    BenchmarkResult result;
    result.implementation = implementation;
    result.scenario = scenario;
    result.options = options;
    result.repetition = repetition;

    std::string preflight_error;
    if (!preflight<Adapter>(options, preflight_error)) {
        result.error = std::move(preflight_error);
        return result;
    }

    Adapter adapter(options.capacity_bytes, options.payload_bytes);
    const std::vector<std::byte> source = make_payload_source(options.payload_bytes);
    RunControl control;
    Counters producer_counters;
    ConsumerState consumer_state;
    std::string producer_error;
    std::string consumer_error;
    Clock::time_point start_time{};
    Clock::time_point producer_end{};
    Clock::time_point consumer_end{};

    std::barrier start_barrier(3, [&start_time]() noexcept { start_time = Clock::now(); });

    std::thread producer([&]() {
        if (!pin_current_thread(options.producer_cpu, producer_error)) {
            control.setup_ok.store(false, std::memory_order_release);
        }
        start_barrier.arrive_and_wait();

        if (!control.setup_ok.load(std::memory_order_acquire)) {
            producer_end = Clock::now();
            control.producer_done.store(true, std::memory_order_release);
            return;
        }

        std::uint64_t sequence = 0U;
        while (true) {
            if ((producer_counters.reserve_calls & kStopCheckMask) == 0U &&
                control.stop.load(std::memory_order_relaxed)) {
                break;
            }

            auto handle = adapter.reserve(options.payload_bytes);
            ++producer_counters.reserve_calls;
            if (scenario == Scenario::drop_new) {
                ++producer_counters.attempted_messages;
            }

            const AttemptStatus status = Adapter::write_status(handle);
            if (status == AttemptStatus::success) {
                stamp_payload(Adapter::write_data(handle), source, sequence);
                adapter.commit(handle);
                ++producer_counters.accepted;
                ++sequence;
                continue;
            }

            adapter.commit(handle);
            if (status == AttemptStatus::full) {
                if (scenario == Scenario::transfer_retry) {
                    ++producer_counters.full_retries;
                    cpu_relax();
                } else {
                    ++producer_counters.dropped_full;
                    ++sequence;
                }
                continue;
            }

            ++producer_counters.unexpected_failures;
            control.fatal.store(true, std::memory_order_release);
            break;
        }

        if (scenario == Scenario::transfer_retry) {
            producer_counters.attempted_messages = producer_counters.accepted;
        }

        producer_end = Clock::now();
        control.accepted_final.store(producer_counters.accepted, std::memory_order_release);
        control.producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        if (!pin_current_thread(options.consumer_cpu, consumer_error)) {
            control.setup_ok.store(false, std::memory_order_release);
        }
        start_barrier.arrive_and_wait();

        if (!control.setup_ok.load(std::memory_order_acquire)) {
            consumer_end = Clock::now();
            return;
        }

        while (true) {
            ++consumer_state.read_calls;
            auto handle = adapter.read();
            const AttemptStatus status = Adapter::read_status(handle);
            if (status == AttemptStatus::success) {
                if (Adapter::read_size(handle) != options.payload_bytes) {
                    ++consumer_state.validation_failures;
                } else {
                    validate_payload(Adapter::read_data(handle), options.payload_bytes, scenario,
                                     consumer_state);
                }
                adapter.consume(handle);
                ++consumer_state.consumed;
                continue;
            }

            adapter.consume(handle);
            if (status == AttemptStatus::empty) {
                ++consumer_state.empty_reads;
                if (control.producer_done.load(std::memory_order_acquire) &&
                    consumer_state.consumed >=
                        control.accepted_final.load(std::memory_order_acquire)) {
                    break;
                }
                cpu_relax();
                continue;
            }

            ++consumer_state.unexpected_failures;
            control.fatal.store(true, std::memory_order_release);
            break;
        }
        consumer_end = Clock::now();
    });

    start_barrier.arrive_and_wait();
    const Clock::time_point deadline = start_time + options.duration;
    while (Clock::now() < deadline && !control.fatal.load(std::memory_order_acquire) &&
           control.setup_ok.load(std::memory_order_acquire)) {
        const Clock::time_point next_check = Clock::now() + std::chrono::milliseconds(10);
        std::this_thread::sleep_until(next_check < deadline ? next_check : deadline);
    }
    control.stop.store(true, std::memory_order_release);

    producer.join();
    consumer.join();

    result.counters = producer_counters;
    result.counters.consumed = consumer_state.consumed;
    result.counters.empty_reads = consumer_state.empty_reads;
    result.counters.read_calls = consumer_state.read_calls;
    result.counters.unexpected_failures += consumer_state.unexpected_failures;
    result.counters.validation_failures = consumer_state.validation_failures;
    result.counters.checksum = consumer_state.checksum;
    result.active_elapsed_ns = elapsed_ns(start_time, producer_end);
    result.drain_elapsed_ns = elapsed_ns(producer_end, consumer_end);
    result.total_elapsed_ns = elapsed_ns(start_time, consumer_end);
    result.affinity_applied = control.setup_ok.load(std::memory_order_acquire);

    append_error(result.error, producer_error);
    append_error(result.error, consumer_error);
    result.valid = result.affinity_applied && validate_invariants(result);
    return result;
}

template <typename Adapter>
[[nodiscard]] BenchmarkResult run_full_fast_fail(Implementation implementation,
                                                 const BenchmarkOptions& options,
                                                 std::size_t repetition) {
    BenchmarkResult result;
    result.implementation = implementation;
    result.scenario = Scenario::full_fast_fail;
    result.options = options;
    result.repetition = repetition;

    std::string preflight_error;
    if (!preflight<Adapter>(options, preflight_error)) {
        result.error = std::move(preflight_error);
        return result;
    }

    Adapter adapter(options.capacity_bytes, options.payload_bytes);
    const std::vector<std::byte> source = make_payload_source(options.payload_bytes);
    RunControl control;
    Counters counters;
    std::string producer_error;
    Clock::time_point start_time{};
    Clock::time_point producer_end{};

    std::barrier start_barrier(2, [&start_time]() noexcept { start_time = Clock::now(); });

    std::thread producer([&]() {
        if (!pin_current_thread(options.producer_cpu, producer_error)) {
            control.setup_ok.store(false, std::memory_order_release);
        }

        std::uint64_t sequence = 0U;
        while (control.setup_ok.load(std::memory_order_acquire)) {
            auto handle = adapter.reserve(options.payload_bytes);
            const AttemptStatus status = Adapter::write_status(handle);
            if (status == AttemptStatus::success) {
                stamp_payload(Adapter::write_data(handle), source, sequence++);
                adapter.commit(handle);
                continue;
            }

            adapter.commit(handle);
            if (status != AttemptStatus::full) {
                ++counters.unexpected_failures;
                control.setup_ok.store(false, std::memory_order_release);
            }
            break;
        }

        start_barrier.arrive_and_wait();
        if (!control.setup_ok.load(std::memory_order_acquire)) {
            producer_end = Clock::now();
            return;
        }

        while (true) {
            if ((counters.reserve_calls & kStopCheckMask) == 0U &&
                control.stop.load(std::memory_order_relaxed)) {
                break;
            }

            auto handle = adapter.reserve(options.payload_bytes);
            ++counters.reserve_calls;
            ++counters.attempted_messages;
            const AttemptStatus status = Adapter::write_status(handle);
            adapter.commit(handle);

            if (status == AttemptStatus::full) {
                ++counters.dropped_full;
                continue;
            }

            ++counters.unexpected_failures;
            control.fatal.store(true, std::memory_order_release);
            break;
        }
        producer_end = Clock::now();
    });

    start_barrier.arrive_and_wait();
    const Clock::time_point deadline = start_time + options.duration;
    while (Clock::now() < deadline && !control.fatal.load(std::memory_order_acquire) &&
           control.setup_ok.load(std::memory_order_acquire)) {
        const Clock::time_point next_check = Clock::now() + std::chrono::milliseconds(10);
        std::this_thread::sleep_until(next_check < deadline ? next_check : deadline);
    }
    control.stop.store(true, std::memory_order_release);
    producer.join();

    result.counters = counters;
    result.active_elapsed_ns = elapsed_ns(start_time, producer_end);
    result.total_elapsed_ns = result.active_elapsed_ns;
    result.affinity_applied = control.setup_ok.load(std::memory_order_acquire);
    append_error(result.error, producer_error);
    result.valid = result.affinity_applied && validate_invariants(result);
    return result;
}

}  // namespace detail

template <typename Adapter>
[[nodiscard]] BenchmarkResult run_adapter_benchmark(Implementation implementation,
                                                    Scenario scenario,
                                                    const BenchmarkOptions& options,
                                                    std::size_t repetition) {
    try {
        if (scenario == Scenario::full_fast_fail) {
            return detail::run_full_fast_fail<Adapter>(implementation, options, repetition);
        }
        if (scenario == Scenario::write_success || scenario == Scenario::prefilled_read) {
            return detail::run_isolated<Adapter>(implementation, scenario, options, repetition);
        }
        return detail::run_transfer<Adapter>(implementation, scenario, options, repetition);
    } catch (const std::exception& exception) {
        BenchmarkResult result;
        result.implementation = implementation;
        result.scenario = scenario;
        result.options = options;
        result.repetition = repetition;
        result.error = exception.what();
        return result;
    }
}

}  // namespace qlog::bench
