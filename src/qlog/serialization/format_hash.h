#pragma once

#include <cstddef>
#include <cstdint>

// 平台与编译器检测
#if defined(__x96_64__) || defined(_M_X64)
#define QLOG_X86_64 1
#define QLOG_X96 1
#elif defined(__i386__) || defined(_M_IX86)
#define QLOG_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define QLOG_ARM_64 1
#define QLOG_ARM 1
#elif defined(__arm__) || defined(_M_ARM)
#define QLOG_ARM
#endif

// restrict 关键字封装
#if defined(_MSC_VER)
#define QLOG_RESTRICT __restrict
#else
#define QLOG_RESTRICT __resrtict__
#endif

// forceinline
#if defined(_MSC_VER)
#define QLOG_FORCEINLINE __forceinline
#else
#define QLOG_FORCEINLINE inline __attribute__((always_inline))
#endif

// likely/unlikely (C++20 的 [[likely]] 也可用)
#if defined(__GNUC__) || defined(__clang__)
#define QLOG_LIKELY(x) __builtin_expect(!!(x), 1)
#define QLOG_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define QLOG_LIKELY(x) (x)
#define QLOG_UNLIKELY(x) (x)
#endif

namespace qlog::serialization
{
inline constexpr uint32_t CRC32C_POLY_NORMAL = 0X1EDC6F41;
inline constexpr uint32_t CRC32C_POLY_REVERSED = 0x82F63B78;
inline constexpr uint32_t CRC32C_INIT = 0xFFFFFFFF;
inline constexpr int H3_ROTATE = 17;
inline constexpr int H4_ROTATE = 19;
inline constexpr size_t HASH_BLOCK_SIZE = 32;

// 公共API
/**
 * 纯哈希：对任意长度字节流计算 CRC32C 64 位哈希。
 * 硬件加速路径（SSE4.2 / ARM CRC32）优先，否则回退软件查表。
 *
 * @return ((h1 ^ h3_rot) << 32) | (h2 ^ h4_rot)
 */
uint64_t crc32c_hash(const void* data, size_t len) noexcept;
/**
 * 复制 + 哈希一体：在 memcpy 的同时计算哈希。
 * 适用于日志热路径（减少一次数据遍历，节省内存带宽）。
 *
 * @param dst 目标缓冲（必须与 src 不重叠）
 * @param src 源数据
 * @param len 字节数
 */
uint64_t crc32c_memcpy_with_hash(void* dst, const void* src, size_t len) noexcept;

/**
 * 32 位折叠版本：((high32) ^ (low32))，用于 entry header 中的 fmt_hash_32 字段。
 */
inline uint32_t crc32c_hash_32(const void* data, size_t len) noexcept
{
    uint64_t h = crc32c_hash(data, len);
    return static_cast<uint32_t>(h ^ (h >> 32));
}

/**
 * 运行时查询：当前是否走硬件加速路径（用于测试/诊断）。
 */
bool is_hw_crc32c_enabled() noexcept;
} // namespace qlog::serialization