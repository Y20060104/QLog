#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace qlog::detail {

struct alignas(8) RecordHeader {
    std::uint64_t time_value;
    std::uint64_t format_hash;
    std::uint32_t format_bytes;
    std::uint32_t args_bytes;
    std::uint32_t category_id;
    std::uint16_t arg_count;
    std::uint8_t level;
    std::uint8_t flags;
};

static_assert(std::endian::native == std::endian::little);
static_assert(std::is_standard_layout_v<RecordHeader>);
static_assert(std::is_trivially_copyable_v<RecordHeader>);

static_assert(sizeof(RecordHeader) == 32U);
static_assert(alignof(RecordHeader) == 8U);

static_assert(offsetof(RecordHeader, time_value) == 0U);
static_assert(offsetof(RecordHeader, format_hash) == 8U);
static_assert(offsetof(RecordHeader, format_bytes) == 16U);
static_assert(offsetof(RecordHeader, args_bytes) == 20U);
static_assert(offsetof(RecordHeader, category_id) == 24U);
static_assert(offsetof(RecordHeader, arg_count) == 28U);
static_assert(offsetof(RecordHeader, level) == 30U);
static_assert(offsetof(RecordHeader, flags) == 31U);

}  // namespace qlog::detail
