#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace qlog::utility::hash_impl {
std::uint64_t software_hash_raw(const std::byte* src, std::size_t len) noexcept;
std::uint64_t software_copy_raw(const std::byte* src, std::byte* dst, std::size_t len) noexcept;
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
std::uint64_t x86_hash_raw(const std::byte* src, std::size_t len) noexcept;
std::uint64_t x86_copy_raw(const std::byte* src, std::byte* dst, std::size_t len) noexcept;
#endif

template <std::size_t Width, bool Copy, class Ops>
std::uint32_t consume_word(std::uint32_t crc, const std::byte* src, std::byte* dst,
                           std::size_t offset) noexcept {
    static_assert(Width == 1U || Width == 2U || Width == 4U || Width == 8U);
    if constexpr (Width == 1U) {
        std::uint8_t v;
        std::memcpy(&v, src + offset, Width);
        if constexpr (Copy) {
            std::memcpy(dst + offset, &v, Width);
        }
        return Ops::u8(crc, v);
    } else if constexpr (Width == 2U) {
        std::uint16_t v;
        std::memcpy(&v, src + offset, Width);
        if constexpr (Copy) {
            std::memcpy(dst + offset, &v, Width);
        }
        return Ops::u16(crc, v);
    } else if constexpr (Width == 4U) {
        std::uint32_t v;
        std::memcpy(&v, src + offset, Width);
        if constexpr (Copy) {
            std::memcpy(dst + offset, &v, Width);
        }
        return Ops::u32(crc, v);

    } else {
        std::uint64_t v;
        std::memcpy(&v, src + offset, Width);
        if constexpr (Copy) {
            std::memcpy(dst + offset, &v, Width);
        }
        return Ops::u64(crc, v);
    }
}
template <bool Copy, class Ops>
std::uint64_t hash_core(const std::byte* src, std::byte* dst, std::size_t len) noexcept {
    std::uint64_t raw = 0;
    if (len == 0) {
        return raw;
    }

    std::uint32_t h1 = 0xFFFFFFFFU;
    std::uint32_t h2 = 0xFFFFFFFFU;
    std::uint32_t h3 = 0xFFFFFFFFU;
    std::uint32_t h4 = 0xFFFFFFFFU;

    if (len < 4U) {
        const auto len_mix = static_cast<std::uint32_t>(len);
        std::size_t offset = 0U;
        if ((len & 2U) != 0U) {
            h1 = consume_word<2U, Copy, Ops>(h1 ^ len_mix, src, dst, 0U);
            offset = 2U;
        }
        if ((len & 1U) != 0U) {
            h2 = consume_word<1U, Copy, Ops>(h2 ^ len_mix, src, dst, offset);
        }
    } else if (len < 8U) {
        const auto len_mix = static_cast<std::uint32_t>(len);
        h1 = consume_word<4U, Copy, Ops>(h1, src, dst, 0U);
        h2 = consume_word<4U, Copy, Ops>(h2 ^ len_mix, src, dst, len - 4U);
    } else if (len < 16U) {
        const auto len_mix = static_cast<std::uint32_t>(len);
        h1 = consume_word<8U, Copy, Ops>(h1, src, dst, 0U);
        h2 = consume_word<8U, Copy, Ops>(h2 ^ len_mix, src, dst, len - 8U);
    } else if (len < 32U) {
        const auto len_mix = static_cast<std::uint32_t>(len);
        h1 = consume_word<8U, Copy, Ops>(h1, src, dst, 0U);
        h2 = consume_word<8U, Copy, Ops>(h2, src, dst, 8U);
        h3 = consume_word<8U, Copy, Ops>(h3 ^ len_mix, src, dst, len - 16U);
        h4 = consume_word<8U, Copy, Ops>(h4 ^ len_mix, src, dst, len - 8U);
    } else {
        std::size_t offset = 0U;
        while (offset <= len - 32U) {
            h1 = consume_word<8U, Copy, Ops>(h1, src, dst, offset);
            h2 = consume_word<8U, Copy, Ops>(h2, src, dst, offset + 8U);
            h3 = consume_word<8U, Copy, Ops>(h3, src, dst, offset + 16U);
            h4 = consume_word<8U, Copy, Ops>(h4, src, dst, offset + 24U);
            offset += 32U;
        }
        // The overlapping final window is part of crc32c4x64_v1.
        if (offset != len) {
            const std::size_t tail = len - 32U;
            h1 = consume_word<8U, Copy, Ops>(h1, src, dst, tail);
            h2 = consume_word<8U, Copy, Ops>(h2, src, dst, tail + 8U);
            h3 = consume_word<8U, Copy, Ops>(h3, src, dst, tail + 16U);
            h4 = consume_word<8U, Copy, Ops>(h4, src, dst, tail + 24U);
        }
    }

    std::uint32_t low = h1 ^ std::rotl(h3, 17);
    std::uint32_t high = h2 ^ std::rotl(h4, 19);
    return ((std::uint64_t(high)) << 32) | (std::uint64_t)(low);
}
}  // namespace qlog::utility::hash_impl