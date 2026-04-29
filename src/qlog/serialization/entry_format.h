#pragma once

#include <cstddef>
#include <cstdint>

namespace qlog::serialization
{
// log_level定义
enum class log_level : uint8_t
{
    verbose = 0,
    debug = 1,
    info = 2,
    warning = 3,
    error = 4,
    fatal = 5,
    count = 6,
    none = 0xFF
};

// 参数类型标签
enum class param_type : uint8_t
{
    type_null = 0,
    type_string_utf8 = 1,  // [tag(1)][len(4)][bytes]
    type_string_utf16 = 2, // 预留，M5 实现
    type_string_utf32 = 3, // 预留，M5 实现
    type_bool = 4,         // [tag(1)][uint8: 0/1]
    type_pointer = 5,      // [tag(1)][uintptr_t]
    type_float = 6,        // [tag(1)][4B IEEE754]
    type_double = 7,       // [tag(1)][8B IEEE754]
    type_int8 = 8,
    type_uint8 = 9,
    type_int16 = 10,
    type_uint16 = 11,
    type_int32 = 12,
    type_uint32 = 13,
    type_int64 = 14,
    type_uint64 = 15,
};

// Entry Header
#pragma pack(push, 4)
struct alignas(8) entry_header
{
    uint64_t timestamp_ms; // 8B epoch 毫秒
    uint64_t thread_id;    // 8B 线程ID
    uint32_t fmt_hash;     // 4B crc32c_hash_32
    uint16_t category_idx; // 2B 分类索引
    uint8_t level;         // 1B log_level
    uint8_t reserved;      // 1B 对齐填充
};
#pragma pack(pop)

static_assert(sizeof(entry_header) == 24, "[QLog] entry_header must be 24 bytes");
static_assert(alignof(entry_header) == 8, "[QLog] entry_header must be 8-byte aligned");
static_assert(offsetof(entry_header, fmt_hash) == 16, "fmt_hash offset must be 16");

template<typename T> struct param_encoded_size;
template<> struct param_encoded_size<int8_t>
{
    static constexpr size_t value = 1 + 1;
};
template<> struct param_encoded_size<uint8_t>
{
    static constexpr size_t value = 1 + 1;
};
template<> struct param_encoded_size<int16_t>
{
    static constexpr size_t value = 1 + 2;
};
template<> struct param_encoded_size<uint16_t>
{
    static constexpr size_t value = 1 + 2;
};
template<> struct param_encoded_size<int32_t>
{
    static constexpr size_t value = 1 + 4;
};
template<> struct param_encoded_size<uint32_t>
{
    static constexpr size_t value = 1 + 4;
};
template<> struct param_encoded_size<int64_t>
{
    static constexpr size_t value = 1 + 8;
};
template<> struct param_encoded_size<uint64_t>
{
    static constexpr size_t value = 1 + 8;
};
template<> struct param_encoded_size<float>
{
    static constexpr size_t value = 1 + 4;
};
template<> struct param_encoded_size<double>
{
    static constexpr size_t value = 1 + 8;
};
template<> struct param_encoded_size<bool>
{
    static constexpr size_t value = 1 + 1;
};

// 字符串 1B tag+ 4B length +N字节内容
inline constexpr size_t STRING_HEADER_SIZE = 1 + sizeof(uint32_t);
// 最大字符串长度 (防止entry分配失败)
inline constexpr uint32_t MAX_STRING_BYTES = 4096;
// 最大单条entry 字节数
inline constexpr uint32_t MAX_ENTRY_SIZE = 32 * 1024;

// 平台工具函数（热路径辅助）
uint64_t get_current_thread_id() noexcept;
uint64_t get_current_timestamp_ms() noexcept;
entry_header make_entry_header(uint32_t fmt_hash, uint16_t category_idx, log_level level) noexcept;

} // namespace qlog::serialization