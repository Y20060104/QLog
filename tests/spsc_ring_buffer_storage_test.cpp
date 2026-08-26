#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <qlog/detail/spsc_ring_buffer.hpp>
#include <stdexcept>

namespace qlog::detail {

struct SpscRingBufferTestAccess {
   public:
    struct ConfigSnapshot {
        std::size_t capacity_bytes;
        std::size_t capacity_mask;
        std::size_t max_payload_bytes;
    };

    [[nodiscard]] static ConfigSnapshot validate(SpscRingBufferConfig config) {
        const auto validated = SpscRingBuffer::validate_config(config);
        return {validated.capacity_bytes, validated.capacity_mask, validated.max_payload_bytes};
    }

    [[nodiscard]] static ConfigSnapshot config(const SpscRingBuffer& ring) noexcept {
        const auto& validated = ring.cold_state_.config_;
        return {validated.capacity_bytes, validated.capacity_mask, validated.max_payload_bytes};
    }

    [[nodiscard]] static const std::byte* storage(const SpscRingBuffer& ring) noexcept {
        return ring.cold_state_.storage_.get();
    }

    [[nodiscard]] static constexpr std::size_t storage_alignment() noexcept {
        return SpscRingBuffer::kStorageAlignment;
    }
};

}  // namespace qlog::detail

namespace {

using qlog::detail::SpscRingBuffer;
using qlog::detail::SpscRingBufferConfig;
using qlog::detail::SpscRingBufferTestAccess;

constexpr std::size_t kMaximumCapacity = std::size_t{1} << 31U;

TEST(SpscRingBufferValidation, AcceptsPayloadBoundaries) {
    struct TestCase {
        SpscRingBufferConfig input;
        std::size_t expected_mask;
    };

    constexpr std::array test_cases{
        TestCase{{16U, 1U}, 15U},
        TestCase{{16U, 8U}, 15U},
        TestCase{{64U, 31U}, 63U},
        TestCase{{64U, 32U}, 63U},
    };

    for (const auto& test_case : test_cases) {
        SCOPED_TRACE(test_case.input.capacity_bytes);
        SCOPED_TRACE(test_case.input.max_payload_bytes);
        const auto validated = SpscRingBufferTestAccess::validate(test_case.input);
        EXPECT_EQ(validated.capacity_bytes, test_case.input.capacity_bytes);
        EXPECT_EQ(validated.capacity_mask, test_case.expected_mask);
        EXPECT_EQ(validated.max_payload_bytes, test_case.input.max_payload_bytes);
    }
}

TEST(SpscRingBufferValidation, RejectsInvalidCapacity) {
    constexpr std::array invalid_capacities{
        std::size_t{0},  std::size_t{1},  std::size_t{8},        std::size_t{15},
        std::size_t{17}, std::size_t{24}, kMaximumCapacity + 1U,
    };

    for (const auto capacity : invalid_capacities) {
        SCOPED_TRACE(capacity);
        EXPECT_THROW(
            static_cast<void>(SpscRingBufferTestAccess::validate({capacity, std::size_t{1}})),
            std::invalid_argument);
    }
}

TEST(SpscRingBufferValidation, RejectsPowerOfTwoAboveMaximumCapacity) {
    if constexpr (std::numeric_limits<std::size_t>::digits > 32) {
        constexpr auto capacity = static_cast<std::size_t>(std::uint64_t{1} << 32U);
        EXPECT_THROW(
            static_cast<void>(SpscRingBufferTestAccess::validate({capacity, std::size_t{1}})),
            std::invalid_argument);
    } else {
        GTEST_SKIP() << "size_t cannot represent a power of two above 2^31";
    }
}

TEST(SpscRingBufferValidation, RejectsInvalidPayloadLength) {
    constexpr std::array invalid_configs{
        SpscRingBufferConfig{16U, 0U},
        SpscRingBufferConfig{16U, 9U},
        SpscRingBufferConfig{64U, 33U},
        SpscRingBufferConfig{64U, std::numeric_limits<std::size_t>::max()},
    };

    for (const auto config : invalid_configs) {
        SCOPED_TRACE(config.capacity_bytes);
        SCOPED_TRACE(config.max_payload_bytes);
        EXPECT_THROW(static_cast<void>(SpscRingBufferTestAccess::validate(config)),
                     std::invalid_argument);
    }
}

TEST(SpscRingBufferValidation, AcceptsMaximumCapacityWithoutAllocatingIt) {
    const auto validated =
        SpscRingBufferTestAccess::validate({kMaximumCapacity, kMaximumCapacity / 2U});
    EXPECT_EQ(validated.capacity_bytes, kMaximumCapacity);
    EXPECT_EQ(validated.capacity_mask, kMaximumCapacity - 1U);
    EXPECT_EQ(validated.max_payload_bytes, kMaximumCapacity / 2U);
}

TEST(SpscRingBufferConstruction, RejectsSmallInvalidConfigurationsBeforeUse) {
    constexpr std::array invalid_configs{
        SpscRingBufferConfig{0U, 1U},
        SpscRingBufferConfig{24U, 1U},
        SpscRingBufferConfig{16U, 0U},
        SpscRingBufferConfig{16U, 9U},
        SpscRingBufferConfig{16U, std::numeric_limits<std::size_t>::max()},
    };

    for (const auto config : invalid_configs) {
        SCOPED_TRACE(config.capacity_bytes);
        SCOPED_TRACE(config.max_payload_bytes);
        EXPECT_THROW({ SpscRingBuffer ring{config}; }, std::invalid_argument);
    }
}

TEST(SpscRingBufferConstruction, DefaultConfigIsPreservedAndStorageIsAligned) {
    SpscRingBuffer ring;
    const auto config = SpscRingBufferTestAccess::config(ring);
    const auto* const storage = SpscRingBufferTestAccess::storage(ring);
    constexpr auto alignment = SpscRingBufferTestAccess::storage_alignment();

    EXPECT_EQ(config.capacity_bytes, 64U * 1024U);
    EXPECT_EQ(config.capacity_mask, (64U * 1024U) - 1U);
    EXPECT_EQ(config.max_payload_bytes, 8U * 1024U);
    ASSERT_NE(storage, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(storage) % alignment, 0U);
}

TEST(SpscRingBufferConstruction, ExplicitConfigAndStorageAddressRemainStable) {
    SpscRingBuffer ring{{128U, 32U}};
    const auto initial_config = SpscRingBufferTestAccess::config(ring);
    const auto* const initial_storage = SpscRingBufferTestAccess::storage(ring);
    const auto later_config = SpscRingBufferTestAccess::config(ring);
    const auto* const later_storage = SpscRingBufferTestAccess::storage(ring);

    EXPECT_EQ(initial_config.capacity_bytes, 128U);
    EXPECT_EQ(initial_config.capacity_mask, 127U);
    EXPECT_EQ(initial_config.max_payload_bytes, 32U);
    EXPECT_EQ(later_config.capacity_bytes, initial_config.capacity_bytes);
    EXPECT_EQ(later_config.capacity_mask, initial_config.capacity_mask);
    EXPECT_EQ(later_config.max_payload_bytes, initial_config.max_payload_bytes);
    ASSERT_NE(initial_storage, nullptr);
    EXPECT_EQ(later_storage, initial_storage);
}

TEST(SpscRingBufferConstruction, RepeatedConstructionSupportsLeakChecking) {
    for (std::size_t iteration = 0; iteration < 512U; ++iteration) {
        SpscRingBuffer ring{{16U, 8U}};
        ASSERT_NE(SpscRingBufferTestAccess::storage(ring), nullptr);
    }
}

}  // namespace
