#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <qlog/detail/ring_geometry.hpp>
#include <thread>

#include "spsc_ring_buffer_test_access.hpp"

namespace {

using qlog::detail::FrameHeader;
using qlog::detail::SpscRingBuffer;
using qlog::detail::SpscRingBufferConfig;
using qlog::detail::SpscRingBufferTestAccess;

constexpr SpscRingBufferConfig kSmallConfig{64U, 32U};

[[nodiscard]] FrameHeader read_header(const SpscRingBuffer& ring, std::uint64_t logical_cursor) {
    const auto config = SpscRingBufferTestAccess::config(ring);
    const auto offset =
        static_cast<std::size_t>(logical_cursor & static_cast<std::uint64_t>(config.capacity_mask));

    FrameHeader header{};
    std::memcpy(&header, SpscRingBufferTestAccess::storage(ring) + offset, sizeof(header));
    return header;
}

void commit_frame(SpscRingBuffer& ring, std::size_t payload_bytes) {
    auto handle = ring.try_reserve(payload_bytes);
    ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    ring.commit(handle);
}

TEST(SpscRingBufferCapacity, ExactCapacityCanBeFilledAndReusedAfterPublish) {
    SpscRingBuffer ring{kSmallConfig};

    for (std::size_t index = 0; index < 4U; ++index) {
        commit_frame(ring, 8U);
    }
    ASSERT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 64U);

    auto full = ring.try_reserve(8U);
    ring.commit(full);

    EXPECT_FALSE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_write_cursor(ring), 64U);
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 64U);

    auto first = ring.try_read();
    ASSERT_TRUE(first);
    ring.release(first);

    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 0U);
    ring.publish_reclaimed();
    ASSERT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 16U);

    auto reused = ring.try_reserve(8U);
    ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::cached_read_cursor(ring), 16U);
    ring.commit(reused);

    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 80U);
}

TEST(SpscRingBufferWrap, FrameThatExactlyFitsTailDoesNotWrap) {
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);
    commit_frame(ring, 8U);
    commit_frame(ring, 8U);

    auto tail = ring.try_reserve(8U);
    ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));

    const auto header = read_header(ring, 48U);
    EXPECT_EQ(header.frame_bytes, 16U);
    EXPECT_EQ(header.payload_bytes, 8U);

    ring.commit(tail);
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 64U);
}

TEST(SpscRingBufferWrap, TailWasteCountsTowardSpaceAndWrappedFrameCanBeReleased) {
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);
    commit_frame(ring, 8U);
    commit_frame(ring, 8U);
    ASSERT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 48U);

    auto insufficient = ring.try_reserve(16U);
    ring.commit(insufficient);
    EXPECT_FALSE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 48U);

    auto first = ring.try_read();
    ASSERT_TRUE(first);
    ring.release(first);
    ring.publish_reclaimed();
    ASSERT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 16U);

    auto wrapped = ring.try_reserve(16U);
    ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));

    const auto header = read_header(ring, 48U);
    EXPECT_EQ(header.frame_bytes, 32U);
    EXPECT_EQ(header.payload_bytes, 16U);

    ring.commit(wrapped);
    ASSERT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 80U);

    auto second = ring.try_read();
    ASSERT_TRUE(second);
    ring.release(second);
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 32U);

    auto third = ring.try_read();
    ASSERT_TRUE(third);
    ring.release(third);
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 48U);

    auto wrapped_read = ring.try_read();
    ASSERT_TRUE(wrapped_read);
    ring.release(wrapped_read);

    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 80U);
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 80U);
}

TEST(SpscRingBufferConcurrency, VariableFramesCrossManyWrapsWithoutLosingProgress) {
    SpscRingBuffer ring{{256U, 32U}};
    constexpr std::size_t kRecordCount = 20'000U;

    std::atomic<bool> stop{false};
    std::size_t produced = 0U;

    std::thread producer([&] {
        while (produced < kRecordCount && !stop.load(std::memory_order_relaxed)) {
            const std::size_t payload_bytes = 1U + (produced % 32U);
            auto write = ring.try_reserve(payload_bytes);

            if (SpscRingBufferTestAccess::reservation_pending(ring)) {
                ring.commit(write);
                ++produced;
            } else {
                std::this_thread::yield();
            }
        }
    });

    std::size_t consumed = 0U;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};

    while (consumed < kRecordCount && std::chrono::steady_clock::now() < deadline) {
        auto read = ring.try_read();

        if (read) {
            ring.release(read);
            ++consumed;
        } else {
            std::this_thread::yield();
        }
    }

    stop.store(true, std::memory_order_relaxed);
    producer.join();

    EXPECT_EQ(produced, kRecordCount);
    EXPECT_EQ(consumed, kRecordCount);
    EXPECT_EQ(SpscRingBufferTestAccess::current_write_cursor(ring),
              SpscRingBufferTestAccess::current_read_cursor(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring),
              SpscRingBufferTestAccess::read_cursor(ring));
}

}  // namespace
