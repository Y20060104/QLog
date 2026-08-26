#include <gtest/gtest.h>

#include <atomic>
#include <concepts>
#include <cstdint>
#include <qlog/detail/spsc_ring_buffer.hpp>
#include <type_traits>
#include <utility>

namespace {
using qlog::detail::ReadStatus;
using qlog::detail::ReadView;
using qlog::detail::ReserveStatus;
using qlog::detail::SpscReadHandle;
using qlog::detail::SpscRingBuffer;
using qlog::detail::SpscRingBufferConfig;
using qlog::detail::SpscWriteHandle;
using qlog::detail::WriteReservation;

// 1. 检查枚举底层类型
static_assert(std::same_as<std::underlying_type_t<ReserveStatus>, std::uint8_t>);

static_assert(std::same_as<std::underlying_type_t<ReadStatus>, std::uint8_t>);

// 2. 检查 Ring 不可复制、不可移动
static_assert(!std::is_copy_constructible_v<SpscRingBuffer>);
static_assert(!std::is_copy_assignable_v<SpscRingBuffer>);
static_assert(!std::is_move_constructible_v<SpscRingBuffer>);
static_assert(!std::is_move_assignable_v<SpscRingBuffer>);

// 3. 检查 WriteHandle 不可复制、不可移动
static_assert(!std::is_copy_constructible_v<SpscWriteHandle>);
static_assert(!std::is_copy_assignable_v<SpscWriteHandle>);
static_assert(!std::is_move_constructible_v<SpscWriteHandle>);
static_assert(!std::is_move_assignable_v<SpscWriteHandle>);

// ReadHandle 同样检查
static_assert(!std::is_copy_constructible_v<SpscReadHandle>);
static_assert(!std::is_copy_assignable_v<SpscReadHandle>);
static_assert(!std::is_move_constructible_v<SpscReadHandle>);
static_assert(!std::is_move_assignable_v<SpscReadHandle>);

// 4. 检查左值 Ring 返回的精确类型
static_assert(
    std::same_as<decltype(std::declval<SpscRingBuffer&>().write_handle()), SpscWriteHandle&>);

static_assert(
    std::same_as<decltype(std::declval<SpscRingBuffer&>().read_handle()), SpscReadHandle&>);

// 5. 检测某种 Ring 表达式能否取得 Handle
template <typename T>
concept CanGetWriteHandle = requires(T&& ring) {
    { std::forward<T>(ring).write_handle() } -> std::same_as<SpscWriteHandle&>;
};

template <typename T>
concept CanGetReadHandle = requires(T&& ring) {
    { std::forward<T>(ring).read_handle() } -> std::same_as<SpscReadHandle&>;
};

// 普通左值可以
static_assert(CanGetWriteHandle<SpscRingBuffer&>);
static_assert(CanGetReadHandle<SpscRingBuffer&>);

// const 左值不可以
static_assert(!CanGetWriteHandle<const SpscRingBuffer&>);
static_assert(!CanGetReadHandle<const SpscRingBuffer&>);

// 右值不可以
static_assert(!CanGetWriteHandle<SpscRingBuffer>);
static_assert(!CanGetReadHandle<SpscRingBuffer>);

constexpr SpscRingBufferConfig kDefaultConfig{};

static_assert(kDefaultConfig.capacity_bytes == 64U * 1024U);
static_assert(kDefaultConfig.max_payload_bytes == 8U * 1024U);

static_assert(sizeof(SpscWriteHandle) == 64U);
static_assert(alignof(SpscWriteHandle) == 64U);
static_assert(sizeof(SpscReadHandle) == 64U);
static_assert(alignof(SpscReadHandle) == 64U);

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

static_assert(std::is_final_v<WriteReservation>);
static_assert(!std::is_default_constructible_v<WriteReservation>);
static_assert(!std::is_copy_constructible_v<WriteReservation>);
static_assert(!std::is_copy_assignable_v<WriteReservation>);
static_assert(std::is_nothrow_move_constructible_v<WriteReservation>);
static_assert(std::is_nothrow_move_assignable_v<WriteReservation>);
static_assert(std::is_nothrow_destructible_v<WriteReservation>);

static_assert(std::is_final_v<ReadView>);
static_assert(!std::is_default_constructible_v<ReadView>);
static_assert(!std::is_copy_constructible_v<ReadView>);
static_assert(!std::is_copy_assignable_v<ReadView>);
static_assert(std::is_nothrow_move_constructible_v<ReadView>);
static_assert(std::is_nothrow_move_assignable_v<ReadView>);
static_assert(std::is_nothrow_destructible_v<ReadView>);

static_assert(noexcept(std::declval<SpscWriteHandle&>().try_reserve(std::size_t{})));
static_assert(noexcept(std::declval<SpscReadHandle&>().try_peek()));

static_assert(std::same_as<decltype(std::declval<SpscWriteHandle&>().try_reserve(std::size_t{})),
                           WriteReservation>);

static_assert(std::same_as<decltype(std::declval<SpscReadHandle&>().try_peek()), ReadView>);
TEST(SpscRingBufferContract, CompileTimeContract) {
    SUCCEED();
}

}  // namespace