#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "spsc_ring_buffer_test_access.hpp"

namespace {

using qlog::detail::SpscRingBuffer;
using qlog::detail::SpscRingBufferTestAccess;

constexpr std::size_t kCacheLineSize = SpscRingBufferTestAccess::cache_line_size();

static_assert(SpscRingBufferTestAccess::writer_state_size() == kCacheLineSize);
static_assert(SpscRingBufferTestAccess::writer_state_alignment() == kCacheLineSize);
static_assert(SpscRingBufferTestAccess::cursor_set_size() == 2U * kCacheLineSize);
static_assert(SpscRingBufferTestAccess::cursor_set_alignment() == kCacheLineSize);
static_assert(SpscRingBufferTestAccess::reader_state_size() == kCacheLineSize);
static_assert(SpscRingBufferTestAccess::reader_state_alignment() == kCacheLineSize);

TEST(SpscRingBufferLayout, HotStateUsesFourContiguousCacheLines) {
    SpscRingBuffer ring{{128U, 32U}};

    const auto writer_state = SpscRingBufferTestAccess::writer_state_address(ring);
    const auto write_cursor = SpscRingBufferTestAccess::write_cursor_address(ring);
    const auto read_cursor = SpscRingBufferTestAccess::read_cursor_address(ring);
    const auto reader_state = SpscRingBufferTestAccess::reader_state_address(ring);

    constexpr std::array<std::size_t, 4U> kExpectedLineOffsets{
        0U,
        kCacheLineSize,
        2U * kCacheLineSize,
        3U * kCacheLineSize,
    };
    const std::array addresses{writer_state, write_cursor, read_cursor, reader_state};

    for (std::size_t index = 0; index < addresses.size(); ++index) {
        EXPECT_EQ(addresses[index] % kCacheLineSize, 0U);
        EXPECT_EQ(addresses[index] - writer_state, kExpectedLineOffsets[index]);
    }
}

TEST(SpscRingBufferCursors, PublishedCursorsStartAtZero) {
    SpscRingBuffer ring{{128U, 32U}};

    EXPECT_EQ(SpscRingBufferTestAccess::write_cursor(ring), 0U);
    EXPECT_EQ(SpscRingBufferTestAccess::read_cursor(ring), 0U);
}

}  // namespace
