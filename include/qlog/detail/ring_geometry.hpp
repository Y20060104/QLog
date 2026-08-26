#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace qlog::detail {

inline constexpr std::size_t kFrameAlignment = 8;

struct FrameHeader {
    std::uint32_t frame_bytes{};
    std::uint32_t payload_bytes{};
};
static_assert(sizeof(FrameHeader) == 8);
static_assert(std::is_trivially_copyable_v<FrameHeader>);

struct FrameLayout {
    std::size_t header_offset{};
    std::size_t payload_offset{};
    std::size_t frame_bytes{};
    std::size_t tail_waste_bytes{};
    bool wrapped{};
};

[[nodiscard]] constexpr bool is_power_of_two(std::size_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] constexpr std::size_t align_up_8(std::size_t value) noexcept {
    return (value + (kFrameAlignment - 1)) & ~(kFrameAlignment - 1);
}

[[nodiscard]] constexpr FrameLayout compute_frame_layout(std::uint64_t logical_position,
                                                         std::size_t payload_bytes,
                                                         std::size_t capacity) noexcept {
    FrameLayout layout;
    std::size_t align_payload = align_up_8(payload_bytes);
    std::size_t physical_position = logical_position & (capacity - 1);
    std::size_t tail_len = capacity - physical_position;
    std::size_t normal_bytes = sizeof(FrameHeader) + align_payload;

    if (normal_bytes <= tail_len) {
        layout.frame_bytes = normal_bytes;
        layout.header_offset = physical_position;
        layout.payload_offset = physical_position + sizeof(FrameHeader);
        layout.tail_waste_bytes = 0;
        layout.wrapped = false;
    } else if (normal_bytes > tail_len) {
        layout.frame_bytes = tail_len + align_payload;
        layout.header_offset = physical_position;
        layout.payload_offset = 0;
        layout.tail_waste_bytes = tail_len - sizeof(FrameHeader);
        layout.wrapped = true;
    }
    return layout;
}

}  // namespace qlog::detail