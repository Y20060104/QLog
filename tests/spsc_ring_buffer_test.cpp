#include <gtest/gtest.h>

#include <atomic>
#include <concepts>
#include <cstdint>
#include <qlog/detail/spsc_ring_buffer.hpp>
#include <type_traits>
#include <utility>

namespace {
using qlog::detail::ReadHandle;
using qlog::detail::ReadStatus;
using qlog::detail::ReserveStatus;
using qlog::detail::SpscRingBuffer;
using qlog::detail::SpscRingBufferConfig;
using qlog::detail::WriteHandle;

// 1. 检查枚举底层类型
static_assert(std::same_as<std::underlying_type_t<ReserveStatus>, std::uint8_t>);

static_assert(std::same_as<std::underlying_type_t<ReadStatus>, std::uint8_t>);

// 2. 检查 Ring 不可复制、不可移动
static_assert(!std::is_copy_constructible_v<SpscRingBuffer>);
static_assert(!std::is_copy_assignable_v<SpscRingBuffer>);
static_assert(!std::is_move_constructible_v<SpscRingBuffer>);
static_assert(!std::is_move_assignable_v<SpscRingBuffer>);

constexpr SpscRingBufferConfig kDefaultConfig{};

static_assert(kDefaultConfig.capacity_bytes == 64U * 1024U);
static_assert(kDefaultConfig.max_payload_bytes == 8U * 1024U);

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

static_assert(std::is_final_v<WriteHandle>);
static_assert(!std::is_default_constructible_v<WriteHandle>);
static_assert(std::is_copy_constructible_v<WriteHandle>);
static_assert(std::is_copy_assignable_v<WriteHandle>);
static_assert(std::is_nothrow_move_constructible_v<WriteHandle>);
static_assert(std::is_nothrow_move_assignable_v<WriteHandle>);
static_assert(std::is_nothrow_destructible_v<WriteHandle>);
static_assert(std::is_trivially_copyable_v<WriteHandle>);
static_assert(std::is_trivially_destructible_v<WriteHandle>);
static_assert(sizeof(WriteHandle) == 16U);

static_assert(std::is_final_v<ReadHandle>);
static_assert(!std::is_default_constructible_v<ReadHandle>);
static_assert(std::is_copy_constructible_v<ReadHandle>);
static_assert(std::is_copy_assignable_v<ReadHandle>);
static_assert(std::is_nothrow_move_constructible_v<ReadHandle>);
static_assert(std::is_nothrow_move_assignable_v<ReadHandle>);
static_assert(std::is_nothrow_destructible_v<ReadHandle>);
static_assert(std::is_trivially_copyable_v<ReadHandle>);
static_assert(std::is_trivially_destructible_v<ReadHandle>);
static_assert(sizeof(ReadHandle) == 16U);

static_assert(noexcept(std::declval<SpscRingBuffer&>().try_reserve(std::size_t{})));
static_assert(noexcept(std::declval<SpscRingBuffer&>().commit(std::declval<const WriteHandle&>())));
static_assert(noexcept(std::declval<SpscRingBuffer&>().abort(std::declval<const WriteHandle&>())));
static_assert(noexcept(std::declval<SpscRingBuffer&>().try_read()));
static_assert(noexcept(std::declval<SpscRingBuffer&>().release(std::declval<const ReadHandle&>())));
static_assert(noexcept(std::declval<SpscRingBuffer&>().abandon(std::declval<const ReadHandle&>())));
static_assert(noexcept(std::declval<SpscRingBuffer&>().publish_reclaimed()));

static_assert(std::same_as<decltype(std::declval<SpscRingBuffer&>().try_reserve(std::size_t{})),
                           WriteHandle>);

static_assert(std::same_as<decltype(std::declval<SpscRingBuffer&>().try_read()), ReadHandle>);
TEST(SpscRingBufferContract, CompileTimeContract) {
    SUCCEED();
}

}  // namespace
