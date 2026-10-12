#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

#include "qlog/platform/atomic.hpp"

namespace p = qlog::platform;

namespace {
std::size_t checks = 0;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename T, typename Operation>
void check_operation(Operation operation, T expected_return, T expected_value, const char* name) {
    p::atomic_trivially_constructible<T> value{};
    value.store_seq_cst(T{5});
    require(operation(value) == expected_return, name);
    require(value.load_seq_cst() == expected_value, "stored arithmetic value mismatch");
}

#define CHECK_ARITHMETIC_FAMILY(name, expected_return, expected_value)                             \
    do {                                                                                           \
        for (auto order : orders) {                                                                \
            check_operation<T>([order](auto& value) { return value.name(T{3}, order); },           \
                               T{expected_return}, T{expected_value}, #name " return mismatch");   \
        }                                                                                          \
        check_operation<T>([](auto& value) { return value.name##_raw(T{3}); }, T{expected_return}, \
                           T{expected_value}, #name "_raw mismatch");                              \
        check_operation<T>([](auto& value) { return value.name##_acquire(T{3}); },                 \
                           T{expected_return}, T{expected_value}, #name "_acquire mismatch");      \
        check_operation<T>([](auto& value) { return value.name##_release(T{3}); },                 \
                           T{expected_return}, T{expected_value}, #name "_release mismatch");      \
        check_operation<T>([](auto& value) { return value.name##_relaxed(T{3}); },                 \
                           T{expected_return}, T{expected_value}, #name "_relaxed mismatch");      \
        check_operation<T>([](auto& value) { return value.name##_acq_rel(T{3}); },                 \
                           T{expected_return}, T{expected_value}, #name "_acq_rel mismatch");      \
        check_operation<T>([](auto& value) { return value.name##_seq_cst(T{3}); },                 \
                           T{expected_return}, T{expected_value}, #name "_seq_cst mismatch");      \
    } while (false)

template <typename T>
void check_all_variants() {
    const p::memory_order orders[]{p::memory_order::relaxed, p::memory_order::acquire,
                                   p::memory_order::release, p::memory_order::acq_rel,
                                   p::memory_order::seq_cst};
    CHECK_ARITHMETIC_FAMILY(add_fetch, 8, 8);
    CHECK_ARITHMETIC_FAMILY(fetch_add, 5, 8);
    CHECK_ARITHMETIC_FAMILY(sub_fetch, 2, 2);
    CHECK_ARITHMETIC_FAMILY(fetch_sub, 5, 2);
    CHECK_ARITHMETIC_FAMILY(xor_fetch, 6, 6);
    CHECK_ARITHMETIC_FAMILY(fetch_xor, 5, 6);
    CHECK_ARITHMETIC_FAMILY(or_fetch, 7, 7);
    CHECK_ARITHMETIC_FAMILY(fetch_or, 5, 7);
    CHECK_ARITHMETIC_FAMILY(and_fetch, 1, 1);
    CHECK_ARITHMETIC_FAMILY(fetch_and, 5, 1);
}

#undef CHECK_ARITHMETIC_FAMILY

template <typename T>
void check_signed_decrements() {
    p::atomic_trivially_constructible<T> value{};
    value.store_seq_cst(T{2});
    require(value.fetch_add_seq_cst(T{-1}) == T{2}, "negative fetch_add old value");
    require(value.load_seq_cst() == T{1}, "negative fetch_add stored value");
    require(value.add_fetch_seq_cst(T{-1}) == T{0}, "negative add_fetch new value");
    require(value.load_seq_cst() == T{0}, "negative add_fetch stored value");
    require(value.sub_fetch_seq_cst(T{-1}) == T{1}, "negative sub_fetch new value");
    require(value.load_seq_cst() == T{1}, "negative sub_fetch stored value");
    require(value.fetch_sub_seq_cst(T{-1}) == T{1}, "negative fetch_sub old value");
    require(value.load_seq_cst() == T{2}, "negative fetch_sub stored value");
}

template <typename T>
void check_unsigned_boundaries() {
    p::atomic_trivially_constructible<T> value{};
    const T maximum = std::numeric_limits<T>::max();
    value.store_seq_cst(maximum);
    require(value.fetch_add_seq_cst(T{1}) == maximum, "wrapping fetch_add old value");
    require(value.load_seq_cst() == T{0}, "wrapping fetch_add stored value");
    value.store_seq_cst(maximum);
    require(value.add_fetch_seq_cst(T{1}) == T{0}, "wrapping add_fetch new value");
    require(value.load_seq_cst() == T{0}, "wrapping add_fetch stored value");
    value.store_seq_cst(T{0});
    require(value.fetch_sub_seq_cst(T{1}) == T{0}, "wrapping fetch_sub old value");
    require(value.load_seq_cst() == maximum, "wrapping fetch_sub stored value");
    value.store_seq_cst(T{0});
    require(value.sub_fetch_seq_cst(T{1}) == maximum, "wrapping sub_fetch new value");
    require(value.load_seq_cst() == maximum, "wrapping sub_fetch stored value");

    const T high_bit = static_cast<T>(T{1} << (std::numeric_limits<T>::digits - 1));
    value.store_seq_cst(T{0});
    require(value.or_fetch_seq_cst(high_bit) == high_bit, "high-bit or_fetch new value");
    require(value.load_seq_cst() == high_bit, "high-bit or_fetch stored value");
    require(value.fetch_and_seq_cst(T{0}) == high_bit, "high-bit fetch_and old value");
    require(value.load_seq_cst() == T{0}, "high-bit fetch_and stored value");
    require(value.fetch_or_seq_cst(high_bit) == T{0}, "high-bit fetch_or old value");
    require(value.load_seq_cst() == high_bit, "high-bit fetch_or stored value");
    require(value.xor_fetch_seq_cst(high_bit) == T{0}, "high-bit xor_fetch new value");
    require(value.load_seq_cst() == T{0}, "high-bit xor_fetch stored value");
}

void check_concurrent_tickets(bool return_new_value) {
    constexpr std::size_t worker_count = 4;
    constexpr std::size_t tickets_per_worker = 1000;
    constexpr std::size_t ticket_count = worker_count * tickets_per_worker;
    p::atomic_trivially_constructible<std::uint32_t> value{};
    value.store_seq_cst(0);
    std::array<std::uint32_t, ticket_count> tickets{};
    std::array<std::thread, worker_count> workers;
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers[worker] = std::thread([&, worker] {
            for (std::size_t i = 0; i < tickets_per_worker; ++i) {
                tickets[worker * tickets_per_worker + i] =
                    return_new_value ? value.add_fetch_relaxed(1) : value.fetch_add_relaxed(1);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    require(value.load_relaxed() == ticket_count, "concurrent arithmetic lost updates");
    std::sort(tickets.begin(), tickets.end());
    for (std::size_t i = 0; i < ticket_count; ++i) {
        require(tickets[i] == i + (return_new_value ? 1 : 0), "concurrent ticket return mismatch");
    }
}
}  // namespace

int main() {
    try {
        check_all_variants<std::int8_t>();
        check_all_variants<std::uint8_t>();
        check_all_variants<std::int16_t>();
        check_all_variants<std::uint16_t>();
        check_all_variants<std::int32_t>();
        check_all_variants<std::uint32_t>();
        check_all_variants<std::int64_t>();
        check_all_variants<std::uint64_t>();
        check_signed_decrements<std::int8_t>();
        check_signed_decrements<std::int16_t>();
        check_signed_decrements<std::int32_t>();
        check_signed_decrements<std::int64_t>();
        check_unsigned_boundaries<std::uint8_t>();
        check_unsigned_boundaries<std::uint16_t>();
        check_unsigned_boundaries<std::uint32_t>();
        check_unsigned_boundaries<std::uint64_t>();
        check_concurrent_tickets(false);
        check_concurrent_tickets(true);
        std::cout << "Atomic arithmetic: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
