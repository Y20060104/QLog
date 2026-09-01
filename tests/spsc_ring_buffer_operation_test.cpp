#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <qlog/detail/ring_geometry.hpp>
#include <utility>

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
    handle.commit();
}

TEST(SpscRingBufferWrite, ReservationRemainsInvisibleUntilCommit) {
    SpscRingBuffer ring{kSmallConfig};

    auto write = ring.try_reserve(8U);

    EXPECT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_write_cursor(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 0U);

    auto read = ring.try_peek();
    EXPECT_FALSE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 0U);

    write.commit();
}

TEST(SpscRingBufferWrite, CommitPublishesExactlyOneFrameAndIsIdempotent) {
    SpscRingBuffer ring{kSmallConfig};

    auto write = ring.try_reserve(8U);
    ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));

    write.commit();

    EXPECT_FALSE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_write_cursor(ring), 16U);
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 16U);

    const auto header = read_header(ring, 0U);
    EXPECT_EQ(header.frame_bytes, 16U);
    EXPECT_EQ(header.payload_bytes, 8U);

    write.commit();
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 16U);
}

TEST(SpscRingBufferWrite, SecondReservationCannotReplaceActiveReservation) {
    SpscRingBuffer ring{kSmallConfig};

    auto first = ring.try_reserve(8U);
    ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));

    auto second = ring.try_reserve(8U);
    second.commit();

    EXPECT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 0U);

    first.commit();
    EXPECT_FALSE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 16U);
}

TEST(SpscRingBufferWrite, AbortClearsPendingWithoutPublishing) {
    SpscRingBuffer ring{kSmallConfig};

    auto write = ring.try_reserve(8U);
    ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    write.abort();

    EXPECT_FALSE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_write_cursor(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 0U);

    auto retry = ring.try_reserve(8U);
    EXPECT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    retry.commit();
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 16U);
}

TEST(SpscRingBufferWrite, HandleDestructorAbortsActiveReservation) {
    SpscRingBuffer ring{kSmallConfig};

    {
        auto write = ring.try_reserve(8U);
        ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    }

    EXPECT_FALSE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 0U);
}

TEST(SpscRingBufferWrite, MoveConstructionTransfersReservationOwnership) {
    SpscRingBuffer ring{kSmallConfig};

    auto source = ring.try_reserve(8U);
    ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));

    auto destination = std::move(source);
    source.abort();

    EXPECT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    destination.commit();
    EXPECT_FALSE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 16U);
}

TEST(SpscRingBufferWrite, PayloadBoundariesDoNotMutateRingOnFailure) {
    SpscRingBuffer ring{kSmallConfig};

    auto too_large = ring.try_reserve(33U);
    too_large.commit();

    EXPECT_FALSE(SpscRingBufferTestAccess::reservation_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 0U);

    auto zero = ring.try_reserve(0U);
    ASSERT_TRUE(SpscRingBufferTestAccess::reservation_pending(ring));
    zero.commit();

    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 8U);
    const auto header = read_header(ring, 0U);
    EXPECT_EQ(header.frame_bytes, 8U);
    EXPECT_EQ(header.payload_bytes, 0U);
}

TEST(SpscRingBufferRead, ConsumeAdvancesAndPublishesDrainedSnapshot) {
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);

    auto read = ring.try_peek();
    ASSERT_TRUE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 0U);

    read.consume();

    EXPECT_FALSE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 16U);
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 16U);
    EXPECT_EQ(SpscRingBufferTestAccess::records_since_publish(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::bytes_since_publish(ring), 0U);

    read.consume();
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 16U);
}

TEST(SpscRingBufferRead, SecondPeekCannotReplaceActiveRead) {
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);

    auto first = ring.try_peek();
    ASSERT_TRUE(SpscRingBufferTestAccess::read_pending(ring));

    auto second = ring.try_peek();
    second.consume();

    EXPECT_TRUE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 0U);

    first.consume();
    EXPECT_FALSE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 16U);
}

TEST(SpscRingBufferRead, AbandonKeepsRecordAndClearsPending) {
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);

    auto read = ring.try_peek();
    ASSERT_TRUE(SpscRingBufferTestAccess::read_pending(ring));
    read.abandon();

    EXPECT_FALSE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 0U);

    auto retry = ring.try_peek();
    EXPECT_TRUE(SpscRingBufferTestAccess::read_pending(ring));
    retry.consume();
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 16U);
}

TEST(SpscRingBufferRead, HandleDestructorAbandonsActiveRead) {
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);

    {
        auto read = ring.try_peek();
        ASSERT_TRUE(SpscRingBufferTestAccess::read_pending(ring));
    }

    EXPECT_FALSE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 0U);
}

TEST(SpscRingBufferRead, ReclaimIsBatchedUntilSnapshotIsDrained) {
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);
    commit_frame(ring, 8U);

    auto first = ring.try_peek();
    ASSERT_TRUE(SpscRingBufferTestAccess::read_pending(ring));
    first.consume();

    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 16U);
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::records_since_publish(ring), 1U);
    EXPECT_EQ(SpscRingBufferTestAccess::bytes_since_publish(ring), 16U);

    auto second = ring.try_peek();
    ASSERT_TRUE(SpscRingBufferTestAccess::read_pending(ring));
    second.consume();

    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 32U);
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 32U);
    EXPECT_EQ(SpscRingBufferTestAccess::records_since_publish(ring), 0U);
}

TEST(SpscRingBufferRead, ExplicitPublishReturnsConsumedPrefix) {
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);
    commit_frame(ring, 8U);

    auto first = ring.try_peek();
    ASSERT_TRUE(SpscRingBufferTestAccess::read_pending(ring));
    first.consume();
    ASSERT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 0U);

    ring.publish_reclaimed();

    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 16U);
    EXPECT_EQ(SpscRingBufferTestAccess::records_since_publish(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::bytes_since_publish(ring), 0U);
}

TEST(SpscRingBufferRead, CorruptedFrameDoesNotAdvanceReader) {
    SpscRingBuffer ring{kSmallConfig};
    commit_frame(ring, 8U);

    const FrameHeader corrupted{8U, 8U};
    std::memcpy(SpscRingBufferTestAccess::storage(ring), &corrupted, sizeof(corrupted));

    auto read = ring.try_peek();
    read.consume();

    EXPECT_FALSE(SpscRingBufferTestAccess::read_pending(ring));
    EXPECT_EQ(SpscRingBufferTestAccess::current_read_cursor(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 0U);
}

}  // namespace
