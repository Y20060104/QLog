#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace qlog::record::hash_impl {
std::uint64_t software_hash_raw(const std::byte*, std::size_t) noexcept;
std::uint64_t software_copy_raw(const std::byte*, std::byte*, std::size_t) noexcept;
std::uint64_t x86_hash_raw(const std::byte*, std::size_t) noexcept;
std::uint64_t x86_copy_raw(const std::byte*, std::byte*, std::size_t) noexcept;
bool x86_crc32c_available() noexcept;

template <std::size_t Width, bool Copy, class Ops>
std::uint32_t consume_word(std::uint32_t state, const std::byte* source, std::byte* destination,
                           std::size_t offset) noexcept {
    static_assert(Width == 1U || Width == 2U || Width == 4U || Width == 8U);
    if constexpr (Width == 1U) {
        std::uint8_t value;
        std::memcpy(&value, source + offset, Width);
        if constexpr (Copy) {
            std::memcpy(destination + offset, &value, Width);
        }
        return Ops::u8(state, value);
    } else if constexpr (Width == 2U) {
        std::uint16_t value;
        std::memcpy(&value, source + offset, Width);
        if constexpr (Copy) {
            std::memcpy(destination + offset, &value, Width);
        }
        return Ops::u16(state, value);
    } else if constexpr (Width == 4U) {
        std::uint32_t value;
        std::memcpy(&value, source + offset, Width);
        if constexpr (Copy) {
            std::memcpy(destination + offset, &value, Width);
        }
        return Ops::u32(state, value);

    } else {
        std::uint64_t value;
        std::memcpy(&value, source + offset, Width);
        if constexpr (Copy) {
            std::memcpy(destination + offset, &value, Width);
        }
        return Ops::u64(state, value);
    }
}
template <bool Copy, class Ops>
std::uint64_t hash_core(const std::byte* source, std::byte* destination,
                        std::size_t size) noexcept {
    std::uint64_t raw = 0;
    if (size == 0) {
        return raw;
    }

    uint32_t h1 = 0xFFFFFFFFU;
    uint32_t h2 = 0xFFFFFFFFU;
    uint32_t h3 = 0xFFFFFFFFU;
    uint32_t h4 = 0xFFFFFFFFU;

    if (size < 4U) {
        const auto len = static_cast<std::uint32_t>(size);
        std::size_t offset = 0U;
        if ((size & 2U) != 0U) {
            h1 = consume_word<2U, Copy, Ops>(h1 ^ len, source, destination, 0U);
            offset = 2U;
        }
        if ((size & 1U) != 0U) {
            h2 = consume_word<1U, Copy, Ops>(h2 ^ len, source, destination, offset);
        }
    } else if (size < 8U) {
        const auto len = static_cast<std::uint32_t>(size);
        h1 = consume_word<4U, Copy, Ops>(h1, source, destination, 0U);
        h2 = consume_word<4U, Copy, Ops>(h2 ^ len, source, destination, size - 4U);
    } else if (size < 16U) {
        const auto len = static_cast<std::uint32_t>(size);
        h1 = consume_word<8U, Copy, Ops>(h1, source, destination, 0U);
        h2 = consume_word<8U, Copy, Ops>(h2 ^ len, source, destination, size - 8U);
    } else if (size < 32U) {
        const auto len = static_cast<std::uint32_t>(size);
        h1 = consume_word<8U, Copy, Ops>(h1, source, destination, 0U);
        h2 = consume_word<8U, Copy, Ops>(h2, source, destination, 8U);
        h3 = consume_word<8U, Copy, Ops>(h3 ^ len, source, destination, size - 16U);
        h4 = consume_word<8U, Copy, Ops>(h4 ^ len, source, destination, size - 8U);
    } else {
        std::size_t offset = 0U;
        while (offset <= size - 32U) {
            h1 = consume_word<8U, Copy, Ops>(h1, source, destination, offset);
            h2 = consume_word<8U, Copy, Ops>(h2, source, destination, offset + 8U);
            h3 = consume_word<8U, Copy, Ops>(h3, source, destination, offset + 16U);
            h4 = consume_word<8U, Copy, Ops>(h4, source, destination, offset + 24U);
            offset += 32U;
        }
        // The overlapping final window is part of crc32c4x64_v1.
        if (offset != size) {
            const std::size_t tail = size - 32U;
            h1 = consume_word<8U, Copy, Ops>(h1, source, destination, tail);
            h2 = consume_word<8U, Copy, Ops>(h2, source, destination, tail + 8U);
            h3 = consume_word<8U, Copy, Ops>(h3, source, destination, tail + 16U);
            h4 = consume_word<8U, Copy, Ops>(h4, source, destination, tail + 24U);
        }
    }

    std::uint32_t low = h1 ^ std::rotl(h3, 17);
    std::uint32_t high = h2 ^ std::rotl(h4, 19);
    return ((std::uint64_t(high)) << 32) | (std::uint64_t)(low);
}
}  // namespace qlog::record::hash_impl