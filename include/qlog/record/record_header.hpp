#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace qlog::record {

struct alignas(8) RecordHeader {
    std::uint64_t timestamp_epoch;
    std::uint32_t ext_info_offset;
    std::uint32_t category_idx;
    std::uint64_t log_thread_id;
    std::uint64_t format_hash;
    std::uint8_t log_format_str_type;
    std::uint8_t level;
    std::uint16_t padding;
    std::uint32_t log_format_data_len;
    static constexpr std::uint32_t get_head_size_without_format_str() {
        return offsetof(RecordHeader, log_format_str_type);
    }
};

static_assert(std::is_standard_layout_v<RecordHeader>);
static_assert(std::is_trivially_copyable_v<RecordHeader>);
static_assert(RecordHeader::get_head_size_without_format_str() == 32);
static_assert(sizeof(RecordHeader) == 40);
static_assert(alignof(RecordHeader) == 8);
static_assert(offsetof(RecordHeader, timestamp_epoch) == 0);
static_assert(offsetof(RecordHeader, ext_info_offset) == 8);
static_assert(offsetof(RecordHeader, category_idx) == 12);
static_assert(offsetof(RecordHeader, log_thread_id) == 16);
static_assert(offsetof(RecordHeader, format_hash) == 24);
static_assert(offsetof(RecordHeader, log_format_str_type) == 32);
static_assert(offsetof(RecordHeader, level) == 33);
static_assert(offsetof(RecordHeader, padding) == 34);
static_assert(offsetof(RecordHeader, log_format_data_len) == 36);

struct RecordExtHeader {
    std::uint8_t thread_name_len_;
};
static_assert(sizeof(RecordExtHeader) == 1);
}  // namespace qlog::record
