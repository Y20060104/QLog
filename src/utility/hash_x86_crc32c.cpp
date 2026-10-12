#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)

#include <nmmintrin.h>

#include "hash_core.hpp"

namespace qlog::utility::hash_impl {
struct HardwareOps {
    static std::uint32_t u8(std::uint32_t crc, std::uint8_t v) noexcept {
        return _mm_crc32_u8(crc, v);
    }

    static std::uint32_t u16(std::uint32_t crc, std::uint16_t v) noexcept {
        return _mm_crc32_u16(crc, v);
    }

    static std::uint32_t u32(std::uint32_t crc, std::uint32_t v) noexcept {
        return _mm_crc32_u32(crc, v);
    }

    static std::uint32_t u64(std::uint32_t crc, std::uint64_t v) noexcept {
#if defined(__x86_64__) || defined(_M_X64)
        return static_cast<std::uint32_t>(_mm_crc32_u64(crc, v));
#else
        crc = u32(crc, static_cast<std::uint32_t>(v));
        return u32(crc, static_cast<std::uint32_t>(v >> 32U));
#endif
    }
};

std::uint64_t x86_hash_raw(const std::byte* src, std::size_t len) noexcept {
    return hash_core<false, HardwareOps>(src, nullptr, len);
}
std::uint64_t x86_copy_raw(const std::byte* src, std::byte* dst, std::size_t len) noexcept {
    return hash_core<true, HardwareOps>(src, dst, len);
}
}  // namespace qlog::utility::hash_impl
#endif
