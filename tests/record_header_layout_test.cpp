#include <gtest/gtest.h>

#include <cstddef>
#include <qlog/detail/record_header.hpp>
#include <type_traits>

namespace {

using qlog::detail::RecordHeader;

static_assert(std::is_standard_layout_v<RecordHeader>);
static_assert(std::is_trivially_copyable_v<RecordHeader>);

TEST(RecordHeaderLayout, MatchesFrozenV1Abi) {
    EXPECT_EQ(sizeof(RecordHeader), 32U);
    EXPECT_EQ(alignof(RecordHeader), 8U);

    EXPECT_EQ(offsetof(RecordHeader, time_value), 0U);
    EXPECT_EQ(offsetof(RecordHeader, format_hash), 8U);
    EXPECT_EQ(offsetof(RecordHeader, format_bytes), 16U);
    EXPECT_EQ(offsetof(RecordHeader, args_bytes), 20U);
    EXPECT_EQ(offsetof(RecordHeader, category_id), 24U);
    EXPECT_EQ(offsetof(RecordHeader, arg_count), 28U);
    EXPECT_EQ(offsetof(RecordHeader, level), 30U);
    EXPECT_EQ(offsetof(RecordHeader, flags), 31U);
}

}  // namespace
