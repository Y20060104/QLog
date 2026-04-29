#pragma once

#include "qlog/serialization/entry_format.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace qlog::serialization
{

class serializer
{
public:
    static size_t write(uint8_t* buf, int8_t v) noexcept;
    static size_t write(uint8_t* buf, uint8_t v) noexcept;
    static size_t write(uint8_t* buf, int16_t v) noexcept;
    static size_t write(uint8_t* buf, uint16_t v) noexcept;
    static size_t write(uint8_t* buf, int32_t v) noexcept;
    static size_t write(uint8_t* buf, uint32_t v) noexcept;
    static size_t write(uint8_t* buf, int64_t v) noexcept;
    static size_t write(uint8_t* buf, uint64_t v) noexcept;
    static size_t write(uint8_t* buf, float v) noexcept;
    static size_t write(uint8_t* buf, double v) noexcept;
    static size_t write(uint8_t* buf, bool v) noexcept;
    static size_t write(uint8_t* buf, const void* ptr) noexcept; // pointer

    // 字符串序列化
    // 写 [type_tag(1B)][length(4B)][bytes...]
    // 截断超过 MAX_STRING_TYPES 的字符串，保证不溢出
    static size_t write(uint8_t* buf, std::string_view sv) noexcept;
    static size_t write(uint8_t* buf, const char* str) noexcept;

    // entry_header写入
    static size_t write_header(uint8_t* buf, const entry_header& hdr) noexcept;

    // 参数大小预计算(编译期，用于alloc_write_chunk的size)
    // 基础数值类型 1tag+sizeof
    template<typename T> static constexpr size_t encoded_size(T) noexcept
    {
        return param_encoded_size<std::decay_t<T>>::value;
    }

    // 字符串运行时计算 1 tag+4length +实际bytes
    static size_t encoded_size(std::string_view sv) noexcept
    {
        const size_t content = sv.size() > MAX_STRING_BYTES ? MAX_STRING_BYTES : sv.size();
        return STRING_HEADER_SIZE + content;
    }
    static size_t encoded_size(const char* str) noexcept;

    // 反序列化（测试/decoder）
    static int8_t read_int8(const uint8_t* buf) noexcept;
    static uint8_t read_uint8(const uint8_t* buf) noexcept;
    static int16_t read_int16(const uint8_t* buf) noexcept;
    static uint16_t read_uint16(const uint8_t* buf) noexcept;
    static int32_t read_int32(const uint8_t* buf) noexcept;
    static uint32_t read_uint32(const uint8_t* buf) noexcept;
    static int64_t read_int64(const uint8_t* buf) noexcept;
    static uint64_t read_uint64(const uint8_t* buf) noexcept;
    static float read_float(const uint8_t* buf) noexcept;
    static double read_double(const uint8_t* buf) noexcept;
    static bool read_bool(const uint8_t* buf) noexcept;
    static uintptr_t read_pointer(const uint8_t* buf) noexcept;
    static std::pair<std::string_view, size_t> read_string(const uint8_t* buf) noexcept;
    static param_type read_type_tag(const uint8_t* buf) noexcept;
    static size_t skip_param(const uint8_t* buf) noexcept;
};
} // namespace qlog::serialization