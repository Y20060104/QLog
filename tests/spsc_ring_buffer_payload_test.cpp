#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>
#include <type_traits>
#include <utility>

#include "qlog/detail/spsc_ring_buffer.hpp"
#include "spsc_ring_buffer_test_access.hpp"

namespace {

using qlog::detail::ReadHandle;
using qlog::detail::ReadStatus;
using qlog::detail::ReserveStatus;
using qlog::detail::SpscRingBuffer;
using qlog::detail::SpscRingBufferConfig;
using qlog::detail::SpscRingBufferTestAccess;
using qlog::detail::WriteHandle;

constexpr SpscRingBufferConfig kSmallConfig{64U, 32U};

static_assert(std::is_constructible_v<bool, const WriteHandle&>);
static_assert(std::is_constructible_v<bool, const ReadHandle&>);
static_assert(!std::is_convertible_v<const WriteHandle&, bool>);
static_assert(!std::is_convertible_v<const ReadHandle&, bool>);
static_assert(noexcept(static_cast<bool>(std::declval<const WriteHandle&>())));
static_assert(noexcept(static_cast<bool>(std::declval<const ReadHandle&>())));
static_assert(std::is_same_v<decltype(std::declval<WriteHandle&>().data()), std::byte*>);
static_assert(std::is_same_v<decltype(std::declval<const ReadHandle&>().data()), const std::byte*>);
static_assert(std::is_same_v<decltype(std::declval<const WriteHandle&>().status()), ReserveStatus>);
static_assert(std::is_same_v<decltype(std::declval<const ReadHandle&>().status()), ReadStatus>);
static_assert(std::is_same_v<decltype(std::declval<const WriteHandle&>().size()), std::uint32_t>);
static_assert(std::is_same_v<decltype(std::declval<const ReadHandle&>().size()), std::uint32_t>);
static_assert(noexcept(std::declval<WriteHandle&>().data()));
static_assert(noexcept(std::declval<const ReadHandle&>().data()));
static_assert(noexcept(std::declval<const WriteHandle&>().status()));
static_assert(noexcept(std::declval<const ReadHandle&>().status()));
static_assert(noexcept(std::declval<const WriteHandle&>().size()));
static_assert(noexcept(std::declval<const ReadHandle&>().size()));

template <std::size_t Size>
[[nodiscard]] constexpr std::array<std::byte, Size> make_payload(std::uint8_t seed) noexcept {
    std::array<std::byte, Size> payload{};
    for (std::size_t index = 0; index < Size; ++index) {
        payload[index] = std::byte{static_cast<unsigned char>(seed + index)};
    }
    return payload;
}

template <std::size_t Size>
void write_record(SpscRingBuffer& ring, const std::array<std::byte, Size>& payload) {
    auto write = ring.try_reserve(Size);
    ASSERT_TRUE(static_cast<bool>(write));
    ASSERT_EQ(write.status(), ReserveStatus::ok);
    ASSERT_NE(write.data(), nullptr);
    ASSERT_EQ(write.size(), Size);

    if constexpr (Size != 0U) {
        std::memcpy(write.data(), payload.data(), Size);
    }

    write.commit();
    EXPECT_FALSE(static_cast<bool>(write));
    EXPECT_EQ(write.status(), ReserveStatus::ok);
    EXPECT_EQ(write.data(), nullptr);
    EXPECT_EQ(write.size(), 0U);
}

template <std::size_t Size>
void expect_record_and_consume(SpscRingBuffer& ring, const std::array<std::byte, Size>& expected) {
    auto read = ring.try_peek();
    ASSERT_TRUE(static_cast<bool>(read));
    ASSERT_EQ(read.status(), ReadStatus::ok);
    ASSERT_NE(read.data(), nullptr);
    ASSERT_EQ(read.size(), Size);

    if constexpr (Size != 0U) {
        EXPECT_EQ(std::memcmp(read.data(), expected.data(), Size), 0);
    }

    read.consume();
    EXPECT_FALSE(static_cast<bool>(read));
    EXPECT_EQ(read.status(), ReadStatus::ok);
    EXPECT_EQ(read.data(), nullptr);
    EXPECT_EQ(read.size(), 0U);
}

TEST(SpscHandleApi, NormalPayloadRoundTripsAndTerminalStateIsCanonical) {
    SpscRingBuffer ring{kSmallConfig};
    constexpr auto payload = make_payload<13U>(0x20U);

    auto write = ring.try_reserve(payload.size());
    ASSERT_TRUE(static_cast<bool>(write));
    EXPECT_EQ(write.status(), ReserveStatus::ok);
    ASSERT_NE(write.data(), nullptr);
    EXPECT_EQ(write.size(), payload.size());

    std::memcpy(write.data(), payload.data(), payload.size());
    write.commit();

    EXPECT_FALSE(static_cast<bool>(write));
    EXPECT_EQ(write.status(), ReserveStatus::ok);
    EXPECT_EQ(write.data(), nullptr);
    EXPECT_EQ(write.size(), 0U);

    auto read = ring.try_peek();
    ASSERT_TRUE(static_cast<bool>(read));
    EXPECT_EQ(read.status(), ReadStatus::ok);
    ASSERT_NE(read.data(), nullptr);
    EXPECT_EQ(read.size(), payload.size());
    EXPECT_EQ(std::memcmp(read.data(), payload.data(), payload.size()), 0);

    read.consume();
    EXPECT_FALSE(static_cast<bool>(read));
    EXPECT_EQ(read.status(), ReadStatus::ok);
    EXPECT_EQ(read.data(), nullptr);
    EXPECT_EQ(read.size(), 0U);
}

TEST(SpscHandleApi, ZeroLengthPayloadIsAnActiveRecord) {
    SpscRingBuffer ring{kSmallConfig};

    auto write = ring.try_reserve(0U);
    ASSERT_TRUE(static_cast<bool>(write));
    EXPECT_EQ(write.status(), ReserveStatus::ok);
    EXPECT_NE(write.data(), nullptr);
    EXPECT_EQ(write.size(), 0U);
    write.commit();

    auto read = ring.try_peek();
    ASSERT_TRUE(static_cast<bool>(read));
    EXPECT_EQ(read.status(), ReadStatus::ok);
    EXPECT_NE(read.data(), nullptr);
    EXPECT_EQ(read.size(), 0U);
    read.consume();
}

TEST(SpscHandleApi, FailureHandlesExposePreciseStatusWithoutPayload) {
    SpscRingBuffer ring{kSmallConfig};

    auto too_large = ring.try_reserve(33U);
    EXPECT_FALSE(static_cast<bool>(too_large));
    EXPECT_EQ(too_large.status(), ReserveStatus::payload_too_large);
    EXPECT_EQ(too_large.data(), nullptr);
    EXPECT_EQ(too_large.size(), 0U);

    auto empty = ring.try_peek();
    EXPECT_FALSE(static_cast<bool>(empty));
    EXPECT_EQ(empty.status(), ReadStatus::empty);
    EXPECT_EQ(empty.data(), nullptr);
    EXPECT_EQ(empty.size(), 0U);

    auto active = ring.try_reserve(8U);
    ASSERT_TRUE(static_cast<bool>(active));

    auto pending = ring.try_reserve(33U);
    EXPECT_FALSE(static_cast<bool>(pending));
    EXPECT_EQ(pending.status(), ReserveStatus::reservation_pending);
    EXPECT_EQ(pending.data(), nullptr);
    EXPECT_EQ(pending.size(), 0U);
    active.abort();
}

TEST(SpscHandleApi, FullAndReadPendingFailuresRemainInactive) {
    SpscRingBuffer ring{kSmallConfig};
    constexpr auto payload = make_payload<8U>(0x10U);

    for (std::size_t index = 0; index < 4U; ++index) {
        write_record(ring, payload);
    }

    auto full = ring.try_reserve(payload.size());
    EXPECT_FALSE(static_cast<bool>(full));
    EXPECT_EQ(full.status(), ReserveStatus::full);
    EXPECT_EQ(full.data(), nullptr);
    EXPECT_EQ(full.size(), 0U);

    auto first = ring.try_peek();
    ASSERT_TRUE(static_cast<bool>(first));

    auto pending = ring.try_peek();
    EXPECT_FALSE(static_cast<bool>(pending));
    EXPECT_EQ(pending.status(), ReadStatus::read_pending);
    EXPECT_EQ(pending.data(), nullptr);
    EXPECT_EQ(pending.size(), 0U);
    first.abandon();
}

TEST(SpscHandleApi, MoveConstructionCanonicalizesSourceAndTransfersPayload) {
    SpscRingBuffer ring{kSmallConfig};
    constexpr auto payload = make_payload<7U>(0x30U);

    auto source = ring.try_reserve(payload.size());
    ASSERT_TRUE(static_cast<bool>(source));
    auto* const payload_address = source.data();

    WriteHandle destination{std::move(source)};

    EXPECT_FALSE(static_cast<bool>(source));
    EXPECT_EQ(source.status(), ReserveStatus::ok);
    EXPECT_EQ(source.data(), nullptr);
    EXPECT_EQ(source.size(), 0U);
    ASSERT_TRUE(static_cast<bool>(destination));
    EXPECT_EQ(destination.status(), ReserveStatus::ok);
    EXPECT_EQ(destination.data(), payload_address);
    EXPECT_EQ(destination.size(), payload.size());

    std::memcpy(destination.data(), payload.data(), payload.size());
    destination.commit();

    auto read_source = ring.try_peek();
    ASSERT_TRUE(static_cast<bool>(read_source));
    const auto* const read_address = read_source.data();

    ReadHandle read_destination{std::move(read_source)};

    EXPECT_FALSE(static_cast<bool>(read_source));
    EXPECT_EQ(read_source.status(), ReadStatus::ok);
    EXPECT_EQ(read_source.data(), nullptr);
    EXPECT_EQ(read_source.size(), 0U);
    ASSERT_TRUE(static_cast<bool>(read_destination));
    EXPECT_EQ(read_destination.data(), read_address);
    EXPECT_EQ(read_destination.size(), payload.size());
    EXPECT_EQ(std::memcmp(read_destination.data(), payload.data(), payload.size()), 0);
    read_destination.consume();
}

TEST(SpscHandleApi, MoveAssignmentReleasesOldReservationBeforeTransfer) {
    SpscRingBuffer source_ring{kSmallConfig};
    SpscRingBuffer destination_ring{kSmallConfig};
    constexpr auto payload = make_payload<6U>(0x40U);

    auto source = source_ring.try_reserve(payload.size());
    ASSERT_TRUE(static_cast<bool>(source));
    std::memcpy(source.data(), payload.data(), payload.size());

    auto destination = destination_ring.try_reserve(5U);
    ASSERT_TRUE(static_cast<bool>(destination));
    destination = std::move(source);

    EXPECT_FALSE(static_cast<bool>(source));
    EXPECT_EQ(source.data(), nullptr);
    EXPECT_EQ(source.size(), 0U);
    ASSERT_TRUE(static_cast<bool>(destination));
    EXPECT_EQ(destination.size(), payload.size());

    auto retry = destination_ring.try_reserve(5U);
    ASSERT_TRUE(static_cast<bool>(retry));
    retry.abort();

    destination.commit();
    expect_record_and_consume(source_ring, payload);
}

TEST(SpscRingBufferPayload, AbortAndAbandonPreserveVisibilityContract) {
    SpscRingBuffer ring{kSmallConfig};
    constexpr auto unpublished = make_payload<8U>(0x50U);
    constexpr auto published = make_payload<8U>(0x70U);

    auto aborted = ring.try_reserve(unpublished.size());
    ASSERT_TRUE(static_cast<bool>(aborted));
    std::memcpy(aborted.data(), unpublished.data(), unpublished.size());
    aborted.abort();

    write_record(ring, published);

    auto first = ring.try_peek();
    ASSERT_TRUE(static_cast<bool>(first));
    ASSERT_NE(first.data(), nullptr);
    const auto* const first_address = first.data();
    EXPECT_EQ(std::memcmp(first.data(), published.data(), published.size()), 0);
    first.abandon();

    auto retry = ring.try_peek();
    ASSERT_TRUE(static_cast<bool>(retry));
    EXPECT_EQ(retry.data(), first_address);
    EXPECT_EQ(retry.size(), published.size());
    EXPECT_EQ(std::memcmp(retry.data(), published.data(), published.size()), 0);
    retry.consume();
}

TEST(SpscRingBufferPayload, WrappedPayloadIsContiguousAndPreservesAdjacentFrames) {
    SpscRingBuffer ring{kSmallConfig};
    constexpr auto first_payload = make_payload<8U>(0x10U);
    constexpr auto second_payload = make_payload<8U>(0x20U);
    constexpr auto third_payload = make_payload<8U>(0x30U);
    constexpr auto wrapped_payload = make_payload<16U>(0x80U);

    write_record(ring, first_payload);
    write_record(ring, second_payload);
    write_record(ring, third_payload);

    expect_record_and_consume(ring, first_payload);
    ring.publish_reclaimed();

    auto wrapped = ring.try_reserve(wrapped_payload.size());
    ASSERT_TRUE(static_cast<bool>(wrapped));
    ASSERT_EQ(wrapped.status(), ReserveStatus::ok);
    ASSERT_EQ(wrapped.data(), SpscRingBufferTestAccess::storage(ring));
    ASSERT_EQ(wrapped.size(), wrapped_payload.size());
    std::memcpy(wrapped.data(), wrapped_payload.data(), wrapped_payload.size());
    wrapped.commit();

    expect_record_and_consume(ring, second_payload);
    expect_record_and_consume(ring, third_payload);
    expect_record_and_consume(ring, wrapped_payload);
}

TEST(SpscRingBufferPayload, ReleaseAcquirePublishesPayloadAcrossThreads) {
    SpscRingBuffer ring{{1024U, 32U}};
    constexpr std::size_t kRecordCount = 20'000U;

    std::atomic<bool> stop{false};
    std::atomic<bool> producer_failed{false};
    std::size_t produced = 0U;

    std::thread producer([&] {
        while (produced < kRecordCount && !stop.load(std::memory_order_relaxed)) {
            const std::size_t payload_bytes = 1U + (produced % 32U);
            auto write = ring.try_reserve(payload_bytes);

            if (write.status() == ReserveStatus::full) {
                std::this_thread::yield();
                continue;
            }
            if (!static_cast<bool>(write) || write.data() == nullptr ||
                write.size() != payload_bytes) {
                producer_failed.store(true, std::memory_order_relaxed);
                break;
            }

            for (std::size_t index = 0; index < payload_bytes; ++index) {
                write.data()[index] = std::byte{static_cast<unsigned char>(produced + index)};
            }
            write.commit();
            ++produced;
        }
    });

    bool payload_matches = true;
    std::size_t consumed = 0U;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};

    while (consumed < kRecordCount && std::chrono::steady_clock::now() < deadline) {
        auto read = ring.try_peek();
        if (read.status() == ReadStatus::empty) {
            std::this_thread::yield();
            continue;
        }
        if (!static_cast<bool>(read)) {
            payload_matches = false;
            break;
        }

        const std::size_t expected_size = 1U + (consumed % 32U);
        if (read.size() != expected_size) {
            payload_matches = false;
        } else {
            for (std::size_t index = 0; index < expected_size; ++index) {
                const auto expected = std::byte{static_cast<unsigned char>(consumed + index)};
                if (read.data()[index] != expected) {
                    payload_matches = false;
                    break;
                }
            }
        }

        read.consume();
        ++consumed;
        if (!payload_matches) {
            break;
        }
    }

    stop.store(true, std::memory_order_relaxed);
    producer.join();

    EXPECT_FALSE(producer_failed.load(std::memory_order_relaxed));
    EXPECT_TRUE(payload_matches);
    EXPECT_EQ(produced, kRecordCount);
    EXPECT_EQ(consumed, kRecordCount);
}

}  // namespace
