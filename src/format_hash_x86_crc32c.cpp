#include <nmmintrin.h>

#include "format_hash_core.hpp"

namespace qlog::detail::hash_impl {
struct HardwareOps {
    static std::uint32_t u8(std::uint32_t state, std::uint8_t value) noexcept {
        return _mm_crc32_u8(state, value);
    }

    static std::uint32_t u16(std::uint32_t state, std::uint16_t value) noexcept {
        return _mm_crc32_u16(state, value);
    }

    static std::uint32_t u32(std::uint32_t state, std::uint32_t value) noexcept {
        return _mm_crc32_u32(state, value);
    }

    static std::uint32_t u64(std::uint32_t state, std::uint64_t value) noexcept {
        return static_cast<std::uint32_t>(_mm_crc32_u64(state, value));
    }
};

std::uint64_t x86_hash_raw(const std::byte* source, std::size_t size) noexcept {
    return hash_core<false, HardwareOps>(source, nullptr, size);
}
std::uint64_t x86_copy_raw(const std::byte* source, std::byte* destination,
                           std::size_t size) noexcept {
    return hash_core<true, HardwareOps>(source, destination, size);
}
}  // namespace qlog::detail::hash_impl