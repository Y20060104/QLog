#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <qlog/detail/ring_geometry.hpp>
using namespace qlog::detail;

constexpr std::size_t oracle_align_up_8(std::size_t value) {
    return ((value + 7) / 8) * 8;
}
TEST(GeometryPowerOfTwo, RecognizesValidValues) {
    EXPECT_FALSE(is_power_of_two(0));
    EXPECT_TRUE(is_power_of_two(1));
    EXPECT_TRUE(is_power_of_two(2));
    EXPECT_FALSE(is_power_of_two(3));
    EXPECT_TRUE(is_power_of_two(64));
}

TEST(GeometryAlignment, NormalAlignment) {
    EXPECT_EQ(align_up_8(0), 0u);
    EXPECT_EQ(align_up_8(1), 8u);
    EXPECT_EQ(align_up_8(7), 8u);
    EXPECT_EQ(align_up_8(8), 8u);
    EXPECT_EQ(align_up_8(9), 16u);
    EXPECT_EQ(align_up_8(15), 16u);
    EXPECT_EQ(align_up_8(16), 16u);
}

TEST(GeometryWrapped, NormalWrapped) {
    FrameLayout layout_ = compute_frame_layout(112, 9, 128);
    EXPECT_EQ(layout_.tail_waste_bytes, 8u);
}

TEST(GeometryWrapped, WrappedPosition) {
    FrameLayout layout_ = compute_frame_layout(0, 8, 64);
    EXPECT_EQ(layout_.header_offset, 0u);
    EXPECT_EQ(layout_.payload_offset, 8u);
    EXPECT_EQ(layout_.frame_bytes, 16u);
    EXPECT_EQ(layout_.tail_waste_bytes, 0u);
    EXPECT_EQ(layout_.wrapped, false);
    FrameLayout layout1_ = compute_frame_layout(48, 8, 64);
    EXPECT_EQ(layout1_.header_offset, 48u);
    EXPECT_EQ(layout1_.payload_offset, 56u);
    EXPECT_EQ(layout1_.frame_bytes, 16u);
    EXPECT_EQ(layout1_.tail_waste_bytes, 0u);
    EXPECT_EQ(layout1_.wrapped, false);
    FrameLayout layout2_ = compute_frame_layout(48, 9, 64);
    EXPECT_EQ(layout2_.header_offset, 48u);
    EXPECT_EQ(layout2_.payload_offset, 0u);
    EXPECT_EQ(layout2_.frame_bytes, 32u);
    EXPECT_EQ(layout2_.tail_waste_bytes, 8u);
    EXPECT_EQ(layout2_.wrapped, true);
    FrameLayout layout3_ = compute_frame_layout(56, 1, 64);
    EXPECT_EQ(layout3_.header_offset, 56u);
    EXPECT_EQ(layout3_.payload_offset, 0u);
    EXPECT_EQ(layout3_.frame_bytes, 16u);
    EXPECT_EQ(layout3_.tail_waste_bytes, 0u);
    EXPECT_EQ(layout3_.wrapped, true);
}

TEST(GeometryPayload, PayloadSize) {
    FrameLayout layout0_ = compute_frame_layout(0, 0, 64);
    EXPECT_EQ(layout0_.payload_offset, 8u);
    EXPECT_EQ(layout0_.frame_bytes, 8u);
    FrameLayout layout1_ = compute_frame_layout(0, 1, 64);
    EXPECT_EQ(layout1_.payload_offset, 8u);
    EXPECT_EQ(layout1_.frame_bytes, 16u);
    FrameLayout layout2_ = compute_frame_layout(0, 7, 64);
    EXPECT_EQ(layout2_.payload_offset, 8u);
    EXPECT_EQ(layout2_.frame_bytes, 16u);
    FrameLayout layout3_ = compute_frame_layout(0, 8, 64);
    EXPECT_EQ(layout3_.payload_offset, 8u);
    EXPECT_EQ(layout3_.frame_bytes, 16u);
    constexpr auto near_max = std::numeric_limits<std::uint64_t>::max() - 7;
    FrameLayout layout4_ = compute_frame_layout(near_max, 8, 64);
    EXPECT_EQ(layout4_.payload_offset, 0u);
    EXPECT_EQ(layout4_.frame_bytes, 16u);
}

TEST(GeometryPayload, PayloadLength) {
    constexpr std::size_t kOracleAlignment = 8;
    constexpr std::size_t kOracleHeaderBytes = 8;

    // 穷举 16/32/64/128/256/512B Ring。
    for (std::size_t capacity = 16; capacity <= 512; capacity *= 2) {
        // 满足 V1：capacity >= 2 * aligned_max_payload。
        const std::size_t max_payload_bytes = capacity / 2;

        // 遍历 Ring 中所有合法的 8B 对齐起点。
        for (std::size_t position = 0; position < capacity; position += kOracleAlignment) {
            // 增加三圈，顺便验证逻辑游标多圈映射。
            const std::uint64_t logical_position =
                static_cast<std::uint64_t>(capacity) * 3U + static_cast<std::uint64_t>(position);

            // 遍历所有合法 Payload 长度，包括 0 和最大值。
            for (std::size_t payload_bytes = 0; payload_bytes <= max_payload_bytes;
                 ++payload_bytes) {
                SCOPED_TRACE(::testing::Message()
                             << "capacity=" << capacity << ", position=" << position
                             << ", payload_bytes=" << payload_bytes);

                // Oracle 必须独立于生产实现：
                // 对齐使用除法，物理位置使用取模。
                const std::size_t aligned_payload = oracle_align_up_8(payload_bytes);

                const std::size_t physical_position = static_cast<std::size_t>(
                    logical_position % static_cast<std::uint64_t>(capacity));

                const std::size_t tail_bytes = capacity - physical_position;

                const std::size_t normal_frame_bytes = kOracleHeaderBytes + aligned_payload;

                const bool expected_wrapped = normal_frame_bytes > tail_bytes;

                const std::size_t expected_header_offset = physical_position;

                const std::size_t expected_payload_offset =
                    expected_wrapped ? 0 : physical_position + kOracleHeaderBytes;

                const std::size_t expected_frame_bytes =
                    expected_wrapped ? tail_bytes + aligned_payload : normal_frame_bytes;

                const std::size_t expected_tail_waste =
                    expected_wrapped ? tail_bytes - kOracleHeaderBytes : 0;

                const FrameLayout actual =
                    compute_frame_layout(logical_position, payload_bytes, capacity);

                // 比较全部布局字段。
                EXPECT_EQ(actual.header_offset, expected_header_offset);
                EXPECT_EQ(actual.payload_offset, expected_payload_offset);
                EXPECT_EQ(actual.frame_bytes, expected_frame_bytes);
                EXPECT_EQ(actual.tail_waste_bytes, expected_tail_waste);
                EXPECT_EQ(actual.wrapped, expected_wrapped);

                // 检查与具体公式相互独立的不变量。
                EXPECT_LE(actual.header_offset + kOracleHeaderBytes, capacity);

                EXPECT_LE(actual.payload_offset + aligned_payload, capacity);

                EXPECT_GT(actual.frame_bytes, 0U);
                EXPECT_LE(actual.frame_bytes, capacity);

                EXPECT_EQ(actual.frame_bytes % kOracleAlignment, 0U);

                EXPECT_EQ((logical_position + actual.frame_bytes) % kOracleAlignment, 0U);

                if (actual.wrapped) {
                    EXPECT_EQ(actual.payload_offset, 0U);
                }
            }
        }
    }
}