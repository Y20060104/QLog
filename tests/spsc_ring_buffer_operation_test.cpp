#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <qlog/detail/ring_geometry.hpp>
#include <utility>

#include "spsc_ring_buffer_test_access.hpp"

namespace {

using qlog::detail::FrameHeader;
using qlog::detail::ReadStatus;
using qlog::detail::SpscRingBuffer;
using qlog::detail::SpscRingBufferConfig;
using qlog::detail::SpscRingBufferTestAccess;

#ifndef QLOG_TEST_RING_VALIDATION
#error "QLOG_TEST_RING_VALIDATION must be provided by tests/CMakeLists.txt"
#endif

#if QLOG_TEST_RING_VALIDATION != 0 && QLOG_TEST_RING_VALIDATION != 1
#error "QLOG_TEST_RING_VALIDATION must be 0 or 1"
#endif

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

TEST(SpscRingBufferWrite, ReservationRemainsInvisibleUntilCommit) {
    SpscRingBuffer ring{kSmallConfig};

    auto write = ring.try_reserve(8U);

    EXPECT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_write_cursor(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 0U);

    auto read = ring.try_read();
    EXPECT_FALSE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 0U);

    ring.commit(write);
}

TEST(SpscRingBufferWrite, CommitPublishesExactlyOneFrame) {
    SpscRingBuffer ring{kSmallConfig};

    auto write = ring.try_reserve(8U);
    ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));

    ring.commit(write);

    EXPECT_FALSE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_write_cursor(ring), 16U);
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 16U);

    const auto header = read_header(ring, 0U);
    EXPECT_EQ(header.frame_bytes, 16U);
    EXPECT_EQ(header.payload_bytes, 8U);
}

TEST(SpscRingBufferWrite, SecondReservationCannotReplaceActiveReservation) {
    SpscRingBuffer ring{kSmallConfig};

    auto first = ring.try_reserve(8U);
    ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));

    auto second = ring.try_reserve(8U);
    ring.commit(second);

    EXPECT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 0U);

    ring.commit(first);
    EXPECT_FALSE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 16U);
}

TEST(SpscRingBufferWrite, AbortClearsPendingWithoutPublishing) {
    SpscRingBuffer ring{kSmallConfig};

    auto write = ring.try_reserve(8U);
    ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    ring.abort(write);

    EXPECT_FALSE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_write_cursor(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 0U);

    auto retry = ring.try_reserve(8U);
    EXPECT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    ring.commit(retry);
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 16U);
}

TEST(SpscRingBufferWrite, HandleDestructorDoesNotMutateRingState) {
    SpscRingBuffer ring{kSmallConfig};

    {
        auto write = ring.try_reserve(8U);
        ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    }

    EXPECT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 0U);
}

TEST(SpscRingBufferWrite, MoveConstructionCopiesPassiveReservationToken) {
    SpscRingBuffer ring{kSmallConfig};

    auto source = ring.try_reserve(8U);
    ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));

    auto* const payload_address = source.data();
    auto destination = std::move(source);

    EXPECT_TRUE(source);
    EXPECT_EQ(source.data(), payload_address);
    EXPECT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    ring.commit(destination);
    EXPECT_FALSE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 16U);
}

TEST(SpscRingBufferWrite, PayloadBoundariesDoNotMutateRingOnFailure) {
    SpscRingBuffer ring{kSmallConfig};

    auto too_large = ring.try_reserve(33U);
    ring.commit(too_large);

    EXPECT_FALSE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 0U);

    auto zero = ring.try_reserve(0U);
    ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    ring.commit(zero);

    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 8U);
    const auto header = read_header(ring, 0U);
    EXPECT_EQ(header.frame_bytes, 8U);
    EXPECT_EQ(header.payload_bytes, 0U);
}

TEST(SpscRingBufferRead, ReleaseAdvancesAndPublishesDrainedSnapshot) {
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);

    auto read = ring.try_read();
    ASSERT_TRUE(read);
    EXPECT_TRUE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 0U);

    ring.release(read);

    EXPECT_FALSE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 16U);
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 16U);
    EXPECT_EQ(SpscRingBufferTestAccess::records_since_publish(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::bytes_since_publish(ring), 0U);
}

TEST(SpscRingBufferRead, SecondReadCannotReplaceActiveRead) {
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);

    auto first = ring.try_read();
    ASSERT_TRUE(first);
    ASSERT_TRUE(SpscRingBufferTestAccess::read_pending(ring));

    auto second = ring.try_read();
    EXPECT_FALSE(second);
    EXPECT_EQ(second.status(), ReadStatus::read_pending);
    ring.release(second);

    EXPECT_TRUE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 0U);

    ring.release(first);
    EXPECT_FALSE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 16U);
}

TEST(SpscRingBufferRead, AbandonKeepsRecordAndClearsPending) {
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);

    auto read = ring.try_read();
    ASSERT_TRUE(read);
    EXPECT_TRUE(SpscRingBufferTestAccess::read_pending(ring));
    ring.abandon(read);

    EXPECT_FALSE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 0U);

    auto retry = ring.try_read();
    ASSERT_TRUE(retry);
    EXPECT_TRUE(SpscRingBufferTestAccess::read_pending(ring));
    ring.release(retry);
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 16U);
}

TEST(SpscRingBufferRead, HandleDestructorDoesNotMutateRingState) {
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);

    {
        auto read = ring.try_read();
        ASSERT_TRUE(read);
        EXPECT_TRUE(SpscRingBufferTestAccess::read_pending(ring));
    }

    EXPECT_TRUE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 0U);
}

TEST(SpscRingBufferRead, ReclaimIsBatchedUntilSnapshotIsDrained) {
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);
    commit_frame(ring, 8U);

    auto first = ring.try_read();
    ASSERT_TRUE(first);
    ring.release(first);

    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 16U);
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::records_since_publish(ring), 1U);
    EXPECT_EQ(SpscRingBufferTestAccess::bytes_since_publish(ring), 16U);

    auto second = ring.try_read();
    ASSERT_TRUE(second);
    ring.release(second);

    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 32U);
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 32U);
    EXPECT_EQ(SpscRingBufferTestAccess::records_since_publish(ring), 0U);
}

TEST(SpscRingBufferRead, ExplicitPublishReturnsReleasedPrefix) {
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);
    commit_frame(ring, 8U);

    auto first = ring.try_read();
    ASSERT_TRUE(first);
    ring.release(first);
    ASSERT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 0U);

    ring.publish_reclaimed();

    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 16U);
    EXPECT_EQ(SpscRingBufferTestAccess::records_since_publish(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::bytes_since_publish(ring), 0U);
}

TEST(SpscRingBufferRead, CorruptedFrameDoesNotAdvanceReader) {
#if QLOG_TEST_RING_VALIDATION
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);

    const FrameHeader corrupted{8U, 8U};
    std::memcpy(SpscRingBufferTestAccess::storage(ring), &corrupted, sizeof(corrupted));

    auto read = ring.try_read();
    EXPECT_FALSE(read);
    EXPECT_EQ(read.status(), ReadStatus::corrupted);
    ring.release(read);

    EXPECT_FALSE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 0U);
#else
    GTEST_SKIP() << "trusted Release fast path does not validate deliberately corrupted frames";
#endif
}

}  // namespace
