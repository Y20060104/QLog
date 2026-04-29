#include "qlog/serialization/serializer.h"

#include <cstring>
#include <string_view>

namespace qlog::serialization
{
// 核心写辅助宏：tag + memcpy
// 所有固定大小类型遵循同一模式：
//   buf[0] = tag;
//   memcpy(buf + 1, &val, sizeof(val));
//   return 1 + sizeof(val);
//
// 使用 memcpy 而非赋值：
//   1) 避免对齐 UB（buf 不保证对齐到 sizeof(T)）
//   2) 编译器会将小尺寸 memcpy 内联为单条 MOV 指令

#define QLOG_DEFINE_WRITE(TYPE, TAG)                        \
    size_t serializer::write(uint8_t* buf, TYPE v) noexcept \
    {                                                       \
        buf[0] = static_cast<uint8_t>(param_type::TAG);     \
        std::memcpy(buf + 1, &v, sizeof(TYPE));             \
        return 1 + sizeof(TYPE);                            \
    }

QLOG_DEFINE_WRITE(int8_t, type_int8)
QLOG_DEFINE_WRITE(uint8_t, type_uint8)
QLOG_DEFINE_WRITE(int16_t, type_int16)
QLOG_DEFINE_WRITE(uint16_t, type_uint16)
QLOG_DEFINE_WRITE(int32_t, type_int32)
QLOG_DEFINE_WRITE(uint32_t, type_uint32)
QLOG_DEFINE_WRITE(int64_t, type_int64)
QLOG_DEFINE_WRITE(uint64_t, type_uint64)
QLOG_DEFINE_WRITE(float, type_float)
QLOG_DEFINE_WRITE(double, type_double)

#undef QLOG_DEFINE_WRITE
#undef QLOG_DEFINE_WRITE

size_t serializer::write(uint8_t* buf, bool v) noexcept
{
    buf[0] = static_cast<uint8_t>(param_type::type_bool);
    buf[1] = v ? uint8_t{1} : uint8_t{0};
    return 2;
}

size_t serializer::write(uint8_t* buf, const void* ptr) noexcept
{
    buf[0] = static_cast<uint8_t>(param_type::type_pointer);
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    std::memcpy(buf + 1, &addr, sizeof(uintptr_t));
    return 1 + sizeof(uintptr_t);
}

// 字符串序列化
// 格式: [type_tag(1B)][length_le(4B)][bytes...]
size_t serializer::write(uint8_t* buf, std::string_view sv) noexcept
{
    buf[0] = static_cast<uint8_t>(param_type::type_string_utf8);

    // 截断超长字符串
    const uint32_t len =
        sv.size() > MAX_STRING_BYTES ? MAX_STRING_BYTES : static_cast<uint32_t>(sv.size());

    // 长度小端序写入
    std::memcpy(buf + 1, &len, sizeof(uint32_t));
    // 内容复制
    std::memcpy(buf + 1 + sizeof(uint32_t), sv.data(), len);
    return STRING_HEADER_SIZE + len;
}

size_t serializer::write(uint8_t* buf, const char* str) noexcept
{
    if (str == nullptr)
    {
        // null指针降级为空字符串
        return write(buf, std::string_view{});
    }
    return write(buf, std::string_view{str});
}

size_t serializer::write_header(uint8_t* buf, const entry_header& hdr) noexcept
{
    std::memcpy(buf, &hdr, sizeof(entry_header));
    return sizeof(entry_header);
}

// 反序列化
// 跳过type_tag字节 memcpy数据
size_t serializer::encoded_size(const char* str) noexcept
{
    if (str == nullptr)
        return STRING_HEADER_SIZE; // 空字符串
    const size_t n = std::strlen(str);
    const size_t content = n > MAX_STRING_BYTES ? MAX_STRING_BYTES : n;
    return STRING_HEADER_SIZE + content;
}

int32_t serializer::read_int32(const uint8_t* buf) noexcept
{
    // buf[0]为tag 从buf+1读数据
    int32_t v{};
    std::memcpy(&v, buf + 1, sizeof(int32_t));
    return v;
}

int64_t serializer::read_int64(const uint8_t* buf) noexcept
{
    int64_t v{};
    std::memcpy(&v, buf + 1, sizeof(int64_t));
    return v;
}

float serializer::read_float(const uint8_t* buf) noexcept
{
    float v{};
    std::memcpy(&v, buf + 1, sizeof(float));
    return v;
}

double serializer::read_double(const uint8_t* buf) noexcept
{
    double v{};
    std::memcpy(&v, buf + 1, sizeof(double));
    return v;
}

bool serializer::read_bool(const uint8_t* buf) noexcept
{
    return buf[1] != 0;
}

// 通用模板：tag(1B) + memcpy
#define QLOG_DEFINE_READ(FUNC, TYPE)                   \
    TYPE serializer::FUNC(const uint8_t* buf) noexcept \
    {                                                  \
        TYPE v{};                                      \
        std::memcpy(&v, buf + 1, sizeof(TYPE));        \
        return v;                                      \
    }

QLOG_DEFINE_READ(read_uint32, uint32_t)
QLOG_DEFINE_READ(read_uint64, uint64_t)
QLOG_DEFINE_READ(read_int8, int8_t)
QLOG_DEFINE_READ(read_uint8, uint8_t)
QLOG_DEFINE_READ(read_int16, int16_t)
QLOG_DEFINE_READ(read_uint16, uint16_t)

#undef QLOG_DEFINE_READ

uintptr_t serializer::read_pointer(const uint8_t* buf) noexcept
{
    uintptr_t v{};
    std::memcpy(&v, buf + 1, sizeof(uintptr_t));
    return v;
}

param_type serializer::read_type_tag(const uint8_t* buf) noexcept
{
    return static_cast<param_type>(buf[0]);
}

std::pair<std::string_view, size_t> serializer::read_string(const uint8_t* buf) noexcept
{
    uint32_t len{};
    std::memcpy(&len, buf + 1, sizeof(uint32_t));
    const char* str = reinterpret_cast<const char*>(buf + 1 + sizeof(uint32_t));
    return {std::string_view{str, len}, STRING_HEADER_SIZE + len};
}

size_t serializer::skip_param(const uint8_t* buf) noexcept
{
    const auto tag = static_cast<param_type>(buf[0]);
    switch (tag)
    {
    case param_type::type_null:
        return 1;
    case param_type::type_bool:
        return 1 + 1;
    case param_type::type_int8:
    case param_type::type_uint8:
        return 1 + 1;
    case param_type::type_int16:
    case param_type::type_uint16:
        return 1 + 2;
    case param_type::type_int32:
    case param_type::type_uint32:
    case param_type::type_float:
        return 1 + 4;
    case param_type::type_int64:
    case param_type::type_uint64:
    case param_type::type_double:
        return 1 + 8;
    case param_type::type_pointer:
        return 1 + sizeof(uintptr_t);
    case param_type::type_string_utf8:
    {
        // [tag(1)][len(4)][bytes]
        uint32_t len{};
        std::memcpy(&len, buf + 1, sizeof(uint32_t));
        return STRING_HEADER_SIZE + len;
    }
    default:
        // 未知 tag：无法安全跳过，返回 1 避免死循环
        // 调用方应检查 tag 合法性
        return 1;
    }
}
} // namespace qlog::serialization