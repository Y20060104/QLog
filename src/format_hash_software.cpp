
#include <array>
#include <cstddef>
#include <cstdint>

#include "format_hash_core.hpp"

namespace qlog::detail {}  // namespace qlog::detail

namespace qlog::detail::hash_impl {

constexpr std::array<std::uint32_t, 256> make_crc_table() noexcept {
    std::array<std::uint32_t, 256> result{};
    for (std::size_t i = 0U; i < result.size(); ++i) {
        std::uint32_t state = static_cast<std::uint32_t>(i);
        for (std::size_t bit = 0U; bit < 8U; ++bit) {
            bool need_xor = (state & 1U) != 0;
            state >>= 1U;
            if (need_xor) {
                state ^= 0x82F63B78U;
            }
        }
        result[i] = state;
    }
    return result;
}
constexpr auto kCrcTable = make_crc_table();
static_assert(kCrcTable[1] == 0xF26B8303U);

struct SoftwareOps {
    static std::uint32_t u8(std::uint32_t state, std::uint8_t value) noexcept {
        return (state >> 8) ^ kCrcTable[(state ^ value) & 0xFF];
    }

    static std::uint32_t u16(std::uint32_t state, std::uint16_t value) noexcept {
        for (std::size_t i = 0; i < sizeof(value); ++i) {
            auto byte = static_cast<std::uint8_t>(value >> (8U * i));
            state = u8(state, byte);
        }
        return state;
    }
    static std::uint32_t u32(std::uint32_t state, std::uint32_t value) noexcept {
        for (std::size_t i = 0; i < sizeof(value); ++i) {
            auto byte = static_cast<std::uint8_t>(value >> (8U * i));
            state = u8(state, byte);
        }
        return state;
    }
    static std::uint32_t u64(std::uint32_t state, std::uint64_t value) noexcept {
        for (std::size_t i = 0; i < sizeof(value); ++i) {
            auto byte = static_cast<std::uint8_t>(value >> (8U * i));
            state = u8(state, byte);
        }
        return state;
    }
};

std::uint64_t software_hash_raw(const std::byte* source, std::size_t size) noexcept {
    return hash_core<false, SoftwareOps>(source, nullptr, size);
}

std::uint64_t software_copy_raw(const std::byte* source, std::byte* destination,
                                std::size_t size) noexcept {
    return hash_core<true, SoftwareOps>(source, destination, size);
}
}  // namespace qlog::detail::hash_impl