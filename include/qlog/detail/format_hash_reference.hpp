#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace qlog::detail {

namespace format_hash_reference {
constexpr uint32_t crc_bytes_ref(uint32_t state, uint8_t byte) noexcept {
    state ^= static_cast<std::uint32_t>(byte);
    for (std::size_t i = 0; i < 8; ++i) {
        bool need_xor = (state & 1U) != 0U;

        state >>= 1U;
        if (need_xor) {
            state ^= 0x82F63B78U;
        }
    }

    return state;
}

static_assert(crc_bytes_ref(0U, 0U) == 0U);
static_assert(crc_bytes_ref(0U, 1U) == 0xF26B8303U);

template <class Byte>
constexpr uint8_t byte_value(Byte value) noexcept {
    if constexpr (std::is_same_v<std::byte, Byte>) {
        return std::to_integer<uint8_t>(value);
    } else if constexpr (std::is_same_v<char, Byte> || std::is_same_v<char8_t, Byte>) {
        return static_cast<uint8_t>(static_cast<unsigned char>(value));
    } else {
        static_assert(std::is_same_v<char, Byte> || std::is_same_v<char8_t, Byte> ||
                          std::is_same_v<std::byte, Byte>,
                      "Invalid Type");
    }
}

template <class Byte>
constexpr std::uint32_t crc_bytes_ref(std::uint32_t state, const Byte* data, std::size_t offset,
                                      std::size_t count) noexcept {
    for (std::size_t i = 0; i < count; ++i) {
        state = crc_bytes_ref(state, byte_value(data[offset + i]));
    }

    return state;
}

template <class Byte>
constexpr std::uint64_t hash_raw_ref(const Byte* data, std::size_t size) noexcept {
    if (size == 0) {
        return 0U;
    }

    uint32_t h1 = 0xFFFFFFFFU;
    uint32_t h2 = 0xFFFFFFFFU;
    uint32_t h3 = 0xFFFFFFFFU;
    uint32_t h4 = 0xFFFFFFFFU;

    if (size < 4U) {
        const auto len = static_cast<std::uint32_t>(size);
        std::size_t offset = 0U;
        if ((size & 2U) != 0U) {
            h1 = crc_bytes_ref(h1 ^ len, data, 0U, 2U);
            offset = 2U;
        }
        if ((size & 1U) != 0U) {
            h2 = crc_bytes_ref(h2 ^ len, data, offset, 1U);
        }
    } else if (size < 8U) {
        const auto len = static_cast<std::uint32_t>(size);
        h1 = crc_bytes_ref(h1, data, 0U, 4U);
        h2 = crc_bytes_ref(h2 ^ len, data, size - 4U, 4U);
    } else if (size < 16U) {
        const auto len = static_cast<std::uint32_t>(size);
        h1 = crc_bytes_ref(h1, data, 0U, 8U);
        h2 = crc_bytes_ref(h2 ^ len, data, size - 8U, 8U);
    } else if (size < 32U) {
        const auto len = static_cast<std::uint32_t>(size);
        h1 = crc_bytes_ref(h1, data, 0U, 8U);
        h2 = crc_bytes_ref(h2, data, 8U, 8U);
        h3 = crc_bytes_ref(h3 ^ len, data, size - 16U, 8U);
        h4 = crc_bytes_ref(h4 ^ len, data, size - 8U, 8U);
    } else {
        // Only long inputs may subtract a complete block from size.
        std::size_t offset = 0U;
        while (offset <= size - 32U) {
            h1 = crc_bytes_ref(h1, data, offset, 8U);
            h2 = crc_bytes_ref(h2, data, offset + 8U, 8U);
            h3 = crc_bytes_ref(h3, data, offset + 16U, 8U);
            h4 = crc_bytes_ref(h4, data, offset + 24U, 8U);
            offset += 32U;
        }
        if (offset != size) {
            const std::size_t tail = size - 32U;
            h1 = crc_bytes_ref(h1, data, tail, 8U);
            h2 = crc_bytes_ref(h2, data, tail + 8U, 8U);
            h3 = crc_bytes_ref(h3, data, tail + 16U, 8U);
            h4 = crc_bytes_ref(h4, data, tail + 24U, 8U);
        }
    }

    // 折叠
    std::uint32_t low = h1 ^ std::rotl(h3, 17);
    std::uint32_t high = h2 ^ std::rotl(h4, 19);
    return (std::uint64_t(high) << 32) | (std::uint64_t(low));
}
}  // namespace format_hash_reference
template <class CharT, std::size_t N>
[[nodiscard]] constexpr std::uint64_t hash_literal_stored(const CharT (&text)[N]) noexcept {
    static_assert(std::is_same_v<CharT, char> || std::is_same_v<CharT, char8_t>);
    static_assert(N > 0U);

    const auto raw = format_hash_reference::hash_raw_ref(text, N - 1U);

    return raw == 0 ? 1U : raw;
}
}  // namespace qlog::detail