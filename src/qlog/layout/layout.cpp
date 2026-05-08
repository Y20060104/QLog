#include "qlog/layout/layout.h"

#include "qlog/layout/utf_utils.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace qlog::layout
{

layout::layout()
{
    // 初始分配init_buffer_size 后续按需realloc *2
    buf_begin_ = static_cast<uint8_t*>(std::malloc(INIT_BUFFER_SIZE));
    if (buf_begin_)
    {
        buf_capacity_ = INIT_BUFFER_SIZE;
        buf_cur_ = buf_begin_;
        buf_end_ = buf_begin_ + buf_capacity_;
    }
}

layout::~layout()
{
    std::free(buf_begin_);
}
void layout::reset_buffer() noexcept
{
    buf_cur_ = buf_begin_;
}

bool layout::ensure_space(size_t n) noexcept
{
    const size_t used = static_cast<size_t>(buf_cur_ - buf_begin_);
    const size_t remain = buf_capacity_ - used;
    if (remain >= n)
    {
        return true;
    }

    size_t new_cap = buf_capacity_ << 1;
    while (new_cap < used + n)
    {
        new_cap <<= 1;
    }
    auto* new_buf = static_cast<uint8_t*>(std::realloc(buf_begin_, new_cap));
    if (!new_buf)
    {
        return false;
    }

    buf_begin_ = new_buf;
    buf_cur_ = new_buf + used;
    buf_end_ = new_buf + new_cap;
    buf_capacity_ = new_cap;
    return true;
}

inline void layout::push_char(char c) noexcept
{
    if (buf_cur_ >= buf_end_)
    {
        ensure_space(1);
    }
    *buf_cur_++ = static_cast<uint8_t>(c);
}

inline void layout::push_bytes(const void* src, size_t n) noexcept
{
    if (n == 0)
        return;
    ensure_space(n);
    std::memcpy(buf_cur_, src, n);
    buf_cur_ += n;
}

int layout::parse_format_spec(std::string_view spec, format_info& out) noexcept
{
    out = {};
    if (spec.empty())
    {
        return 0;
    }

    const char* p = spec.data();
    const char* p_end = p + spec.size();
    int consumed = 0;

    //   如果第 2 个字符是对齐符 '<', '>', '^', '=' → 第 1 个字符是 fill
    //   如果第 1 个字符是对齐符 → fill 为空格（默认）
    auto is_align_char = [](char c)
    {
        return c == '<' || c == '>' || c == '^' || c == '=';
    };

    if (p + 1 < p_end && is_align_char(p[1]))
    {
        out.fill_char = p[0];
        out.align = static_cast<format_info::align_type>(p[1]);
        p += 2;
    }
    else if (p < p_end && is_align_char(p[0]))
    {
        out.align = static_cast<format_info::align_type>(p[0]);
        p += 1;
    }

    // [sign]
    if (p < p_end && (*p == '+' || *p == '-' || *p == ' '))
    {
        out.sign = static_cast<format_info::sign_type>(*p);
        ++p;
    }

    // [#]替代形式
    if (p < p_end && *p == '#')
    {
        out.alt_form = true;
        ++p;
    }

    // [0]补0 在width之前
    if (p < p_end && *p == '0' && out.align == format_info::align_type::none)
    {
        out.zero_pad = true;
        out.fill_char = '0';
        out.align = format_info::align_type::pad_after_sign;
        ++p;
    }

    // [width]
    while (p < p_end && *p >= '0' && *p <= '9')
    {
        out.width = out.width * 10 + (*p - '0');
        ++p;
    }

    if (p < p_end && (*p == ',' || *p == '_'))
    {
        ++p;
    }
    // [.precision]
    if (p < p_end && *p == '.')
    {
        ++p;
        out.precision = 0;
        while (p < p_end && *p >= '0' && *p <= '9')
        {
            out.precision = out.precision * 10 + (*p - '0');
            ++p;
        }
    }

    //[type]
    if (p < p_end)
    {
        if (*p == 'o')
        {
            out.type = format_info::fmt_type::octal;
        }
        else
        {
            out.type = static_cast<format_info::fmt_type>(*p);
        }
        ++p;
    }

    return static_cast<int>(p - spec.data());
}

void layout::fill_and_alignment(
    const char* content, size_t content_len, const format_info& fi
) noexcept
{
    const int width = fi.width;
    const int pad_total = (width > 0 && static_cast<int>(content_len) < width)
                              ? (width - static_cast<int>(content_len))
                              : 0;

    if (pad_total == 0)
    {
        push_bytes(content, content_len);
        return;
    }

    const char fill = fi.fill_char;

    switch (fi.align)
    {
    case format_info::align_type::left:
    case format_info::align_type::none:
    {
        push_bytes(content, content_len);
        ensure_space(pad_total);
        std::memset(buf_cur_, fill, pad_total);
        buf_cur_ += pad_total;
        break;
    }
    case format_info::align_type::right:
    {
        ensure_space(pad_total);
        std::memset(buf_cur_, fill, pad_total);
        buf_cur_ += pad_total;
        push_bytes(content, content_len);
        break;
    }
    case format_info::align_type::center:
    {
        const int left_pad = pad_total / 2;
        const int right_pad = pad_total - left_pad;
        ensure_space(pad_total);
        std::memset(buf_cur_, fill, left_pad);
        buf_cur_ += left_pad;
        push_bytes(content, content_len);
        std::memset(buf_cur_, fill, right_pad);
        buf_cur_ += right_pad;
        break;
    }
    case format_info::align_type::pad_after_sign:
    {
        // 数字专用：符号和前缀应与数字保持相邻
        const char* cur = content;
        size_t len = content_len;

        if (len > 0 && (cur[0] == '+' || cur[0] == '-' || cur[0] == ' '))
        {
            push_char(cur[0]);
            ++cur;
            --len;
        }

        if (fi.alt_form && len > 0 && cur[0] == '0')
        {
            if (len > 1 && (cur[1] == 'x' || cur[1] == 'X' || cur[1] == 'b' || cur[1] == 'B'))
            {
                push_bytes(cur, 2);
                cur += 2;
                len -= 2;
            }
        }

        ensure_space(pad_total);
        std::memset(buf_cur_, fill, pad_total);
        buf_cur_ += pad_total;
        push_bytes(cur, len);
        break;
    }
    }
}

size_t layout::insert_integral_unsigned(uint64_t val, const format_info& fi) noexcept
{
    // 栈缓冲区：64位整数最大 64 个二进制位 + 前缀 2B + 符号 1B = 67B
    char tmp[72];
    char* const tmp_end = tmp + sizeof(tmp);
    char* p = tmp_end;

    const auto type = fi.type;
    // 确定进制
    if (type == format_info::fmt_type::binary)
    {
        do
        {
            *--p = '0' + static_cast<int>(val & 1);
            val >>= 1;
        } while (val != 0);
        if (fi.alt_form)
        {
            *--p = 'b';
            *--p = '0';
        }
    }
    else if (type == format_info::fmt_type::octal)
    {
        do
        {
            *--p = '0' + static_cast<int>(val & 7);
            val >>= 3;
        } while (val != 0);
    }
    else if (type == format_info::fmt_type::hex_lower || type == format_info::fmt_type::hex_upper)
    {
        const char* const digits =
            (type == format_info::fmt_type::hex_lower) ? "0123456789abcdef" : "0123456789ABCDEF";
        do
        {
            *--p = digits[val & 0xF];
            val >>= 4;
        } while (val != 0);
        if (fi.alt_form)
        {
            *--p = (type == format_info::fmt_type::hex_lower) ? 'x' : 'X';
            *--p = '0';
        }
    }
    else // decimal
    {
        do
        {
            *--p = static_cast<char>('0' + (val % 10));
            val /= 10;
        } while (val != 0);
    }
    // ── 符号处理（无符号整数只有 '+' 和 ' ' 两种非默认情况）──────────────
    if (fi.sign == format_info::sign_type::plus)
        *--p = '+';
    else if (fi.sign == format_info::sign_type::space)
        *--p = ' ';

    const char* const full_start = p; // 完整字符串起点
    const size_t full_len = static_cast<size_t>(tmp_end - p);
    const size_t before = static_cast<size_t>(buf_cur_ - buf_begin_);
    fill_and_alignment(full_start, full_len, fi);
    return static_cast<size_t>(buf_cur_ - buf_begin_) - before;
}

size_t layout::insert_integral_signed(int64_t val, const format_info& fi) noexcept
{
    char tmp[72];
    char* const tmp_end = tmp + sizeof(tmp);
    char* p = tmp_end;

    // 处理最小值 INT64_MIN（取负会溢出，特殊处理）
    const bool negative = (val < 0);
    uint64_t uval = negative ? (val == INT64_MIN ? static_cast<uint64_t>(INT64_MAX) + 1
                                                 : static_cast<uint64_t>(-val))
                             : static_cast<uint64_t>(val);

    // 在 tmp 中用 decimal 构建数字部分（不走 insert_integral_unsigned，避免 ensure_space）
    const auto type = fi.type;
    if (type == format_info::fmt_type::binary)
    {
        do
        {
            *--p = '0' + static_cast<int>(uval & 1);
            uval >>= 1;
        } while (uval);
        if (fi.alt_form)
        {
            *--p = 'b';
            *--p = '0';
        }
    }
    else if (type == format_info::fmt_type::octal)
    {
        do
        {
            *--p = '0' + static_cast<int>(uval & 7);
            uval >>= 3;
        } while (uval);
    }
    else if (type == format_info::fmt_type::hex_lower || type == format_info::fmt_type::hex_upper)
    {
        const char* const digits =
            (type == format_info::fmt_type::hex_lower) ? "0123456789abcdef" : "0123456789ABCDEF";
        do
        {
            *--p = digits[uval & 0xF];
            uval >>= 4;
        } while (uval);
        if (fi.alt_form)
        {
            *--p = (type == format_info::fmt_type::hex_lower) ? 'x' : 'X';
            *--p = '0';
        }
    }
    else
    {
        do
        {
            *--p = static_cast<char>('0' + (uval % 10));
            uval /= 10;
        } while (uval);
    }

    // 添加符号字节
    if (negative)
        *--p = '-';
    else if (fi.sign == format_info::sign_type::plus)
        *--p = '+';
    else if (fi.sign == format_info::sign_type::space)
        *--p = ' ';

    const char* const full_start = p;
    const size_t full_len = static_cast<size_t>(tmp_end - p);
    const size_t before = static_cast<size_t>(buf_cur_ - buf_begin_);
    fill_and_alignment(full_start, full_len, fi);
    return static_cast<size_t>(buf_cur_ - buf_begin_) - before;
}

size_t layout::insert_decimal(double val, const format_info& fi, bool is_float) noexcept
{
    // 构建 printf 格式串（在栈上，最多 16 字节足够）
    char fmt_buf[16];
    char* fp = fmt_buf;
    *fp++ = '%';

    if (fi.sign == format_info::sign_type::plus)
        *fp++ = '+';
    else if (fi.sign == format_info::sign_type::space)
        *fp++ = ' ';

    if (fi.alt_form)
        *fp++ = '#';

    // precision
    if (fi.precision >= 0)
    {
        *fp++ = '.';
        if (fi.precision >= 10)
            *fp++ = '0' + fi.precision / 10;
        *fp++ = '0' + fi.precision % 10;
    }

    // type specifier
    switch (fi.type)
    {
    case format_info::fmt_type::scientific:
        *fp++ = 'e';
        break;
    case format_info::fmt_type::scientific_upper:
        *fp++ = 'E';
        break;
    case format_info::fmt_type::general:
        *fp++ = 'g';
        break;
    case format_info::fmt_type::general_upper:
        *fp++ = 'G';
        break;
    case format_info::fmt_type::fixed_upper:
        *fp++ = 'F';
        break;
    default: /* fixed or none */
        *fp++ = 'f';
        break;
    }
    *fp = '\0';

    // 格式化到临时栈缓冲
    char tmp[64];
    const int n = std::snprintf(tmp, sizeof(tmp), fmt_buf, is_float ? val : val);
    if (n <= 0)
        return 0;

    const size_t before = static_cast<size_t>(buf_cur_ - buf_begin_);
    fill_and_alignment(tmp, static_cast<size_t>(n), fi);
    return static_cast<size_t>(buf_cur_ - buf_begin_) - before;
}

size_t layout::insert_str_utf8(std::string_view sv, const format_info& fi) noexcept
{
    // precision 在字符串语义中表示最大字符数（截断）
    std::string_view actual = sv;
    if (fi.precision >= 0 && static_cast<size_t>(fi.precision) < sv.size())
        actual = sv.substr(0, static_cast<size_t>(fi.precision));

    const size_t before = static_cast<size_t>(buf_cur_ - buf_begin_);

    // 字符串默认左对齐
    format_info fi_str = fi;
    if (fi_str.align == format_info::align_type::none)
        fi_str.align = format_info::align_type::left;

    fill_and_alignment(actual.data(), actual.size(), fi_str);
    return static_cast<size_t>(buf_cur_ - buf_begin_) - before;
}

size_t layout::insert_str_utf16(std::string_view raw_bytes, const format_info& fi) noexcept
{
    const size_t src_len = raw_bytes.size();
    if (src_len == 0)
        return insert_str_utf8({}, fi);

    // UTF-8 最坏上界：每 2 字节 UTF-16 → 最多 3 字节 UTF-8（加 1 避免零分配）
    const size_t utf8_max = (src_len / 2) * 3 + 4;

    // 小字符串走栈缓冲（避免 malloc）
    constexpr size_t STACK_BUF = 6144; // 4096 chars * 1.5 = 6144
    uint8_t stack_utf8[STACK_BUF];
    uint8_t* utf8_buf;
    bool heap_alloc = false;

    if (utf8_max <= STACK_BUF)
    {
        utf8_buf = stack_utf8;
    }
    else
    {
        utf8_buf = static_cast<uint8_t*>(std::malloc(utf8_max));
        if (!utf8_buf)
            return 0;
        heap_alloc = true;
    }

    const size_t utf8_len =
        utf::utf16le_to_utf8(reinterpret_cast<const uint8_t*>(raw_bytes.data()), src_len, utf8_buf);

    const std::string_view utf8_sv{reinterpret_cast<const char*>(utf8_buf), utf8_len};
    const size_t written = insert_str_utf8(utf8_sv, fi);

    if (heap_alloc)
        std::free(utf8_buf);
    return written;
}

// UTF-32 同理，但上界是 byte_len / 4 * 4 = byte_len（每码点最多 4B UTF-8）
size_t layout::insert_str_utf32(std::string_view raw_bytes, const format_info& fi) noexcept
{
    const size_t src_len = raw_bytes.size();
    if (src_len == 0)
        return insert_str_utf8({}, fi);

    // UTF-8 上界：UTF-32 char 数 * 4（每码点至多 4 字节）
    const size_t utf8_max = (src_len / 4) * 4 + 4;

    constexpr size_t STACK_BUF = 4096; // MAX_STRING_BYTES / 4 * 4
    uint8_t stack_utf8[STACK_BUF];
    uint8_t* utf8_buf;
    bool heap_alloc = false;

    if (utf8_max <= STACK_BUF)
    {
        utf8_buf = stack_utf8;
    }
    else
    {
        utf8_buf = static_cast<uint8_t*>(std::malloc(utf8_max));
        if (!utf8_buf)
            return 0;
        heap_alloc = true;
    }

    const size_t utf8_len =
        utf::utf32le_to_utf8(reinterpret_cast<const uint8_t*>(raw_bytes.data()), src_len, utf8_buf);

    const std::string_view utf8_sv{reinterpret_cast<const char*>(utf8_buf), utf8_len};
    const size_t written = insert_str_utf8(utf8_sv, fi);

    if (heap_alloc)
        std::free(utf8_buf);
    return written;
}

size_t layout::insert_bool(bool val, const format_info& fi) noexcept
{
    // type='d' 或无 → 数字形式 '1'/'0'
    // 其他 → "true"/"false"
    if (fi.type == format_info::fmt_type::decimal || fi.type == format_info::fmt_type::binary)
    {
        return insert_integral_unsigned(static_cast<uint64_t>(val), fi);
    }
    const std::string_view sv = val ? std::string_view{"TRUE"} : std::string_view{"FALSE"};
    return insert_str_utf8(sv, fi);
}

size_t layout::insert_pointer(uintptr_t ptr, const format_info& fi) noexcept
{
    if (ptr == 0)
    {
        const char* null_str = "null";
        const size_t before = static_cast<size_t>(buf_cur_ - buf_begin_);
        fill_and_alignment(null_str, 4, fi);
        return static_cast<size_t>(buf_cur_ - buf_begin_) - before;
    }

    char tmp[2 + sizeof(uintptr_t) * 2 + 1];
    const int n = std::snprintf(tmp, sizeof(tmp), "0x%" PRIxPTR, ptr);
    if (n <= 0)
        return 0;
    const size_t before = static_cast<size_t>(buf_cur_ - buf_begin_);
    fill_and_alignment(tmp, static_cast<size_t>(n), fi);
    return static_cast<size_t>(buf_cur_ - buf_begin_) - before;
}

// 输出: "2026-04-29 12:34:56.789"（固定 23 字节）
size_t layout::insert_timestamp(uint64_t timestamp_ms) noexcept
{
    const uint64_t ms_part = timestamp_ms % 1000;
    const time_t sec = static_cast<time_t>(timestamp_ms / 1000);

    struct tm tm_buf
    {
    };
#ifdef _WIN32
    localtime_s(&tm_buf, &sec);
#else
    localtime_r(&sec, &tm_buf);
#endif

    // "YYYY-MM-DD HH:MM:SS.mmm" = 23 字节
    ensure_space(24);
    const int n = std::snprintf(
        reinterpret_cast<char*>(buf_cur_),
        24,
        "%04d-%02d-%02d %02d:%02d:%02d.%03d",
        tm_buf.tm_year + 1900,
        tm_buf.tm_mon + 1,
        tm_buf.tm_mday,
        tm_buf.tm_hour,
        tm_buf.tm_min,
        tm_buf.tm_sec,
        static_cast<int>(ms_part)
    );
    if (n > 0)
        buf_cur_ += n;
    return n > 0 ? static_cast<size_t>(n) : 0;
}

size_t layout::insert_param(const uint8_t* param_ptr, const format_info& fi) noexcept
{
    using namespace qlog::serialization;
    const param_type tag = serializer::read_type_tag(param_ptr);

    switch (tag)
    {
    case param_type::type_int8:
        insert_integral_signed(serializer::read_int8(param_ptr), fi);
        return param_encoded_size<int8_t>::value;

    case param_type::type_uint8:
        insert_integral_unsigned(serializer::read_uint8(param_ptr), fi);
        return param_encoded_size<uint8_t>::value;

    case param_type::type_int16:
        insert_integral_signed(serializer::read_int16(param_ptr), fi);
        return param_encoded_size<int16_t>::value;

    case param_type::type_uint16:
        insert_integral_unsigned(serializer::read_uint16(param_ptr), fi);
        return param_encoded_size<uint16_t>::value;

    case param_type::type_int32:
        insert_integral_signed(serializer::read_int32(param_ptr), fi);
        return param_encoded_size<int32_t>::value;

    case param_type::type_uint32:
        insert_integral_unsigned(serializer::read_uint32(param_ptr), fi);
        return param_encoded_size<uint32_t>::value;

    case param_type::type_int64:
        insert_integral_signed(serializer::read_int64(param_ptr), fi);
        return param_encoded_size<int64_t>::value;

    case param_type::type_uint64:
        insert_integral_unsigned(serializer::read_uint64(param_ptr), fi);
        return param_encoded_size<uint64_t>::value;

    case param_type::type_float:
        insert_decimal(static_cast<double>(serializer::read_float(param_ptr)), fi, true);
        return param_encoded_size<float>::value;

    case param_type::type_double:
        insert_decimal(serializer::read_double(param_ptr), fi, false);
        return param_encoded_size<double>::value;

    case param_type::type_bool:
        insert_bool(serializer::read_bool(param_ptr), fi);
        return param_encoded_size<bool>::value;

    case param_type::type_pointer:
        insert_pointer(serializer::read_pointer(param_ptr), fi);
        return 1 + sizeof(uintptr_t);

    case param_type::type_string_utf8:
    {
        auto [sv, consumed] = serializer::read_string(param_ptr);
        insert_str_utf8(sv, fi);
        return consumed;
    }
    case param_type::type_string_utf16:
    {
        // read_raw_string 与 read_string 格式相同（tag+len+bytes），直接复用
        auto [raw, consumed] = serializer::read_raw_string(param_ptr);
        insert_str_utf16(raw, fi);
        return consumed;
    }

    case param_type::type_string_utf32:
    {
        auto [raw, consumed] = serializer::read_raw_string(param_ptr);
        insert_str_utf32(raw, fi);
        return consumed;
    }

    default:
        return serializer::skip_param(param_ptr); // 未知类型，跳过
    }
}

void layout::scan_params() noexcept
{
    using namespace qlog::serialization;
    param_count_ = 0;
    const uint8_t* p = params_begin_;

    while (p < params_end_ && param_count_ < MAX_PARAMS)
    {
        param_offsets_[param_count_++] = p;
        const size_t step = serializer::skip_param(p);
        if (step == 0)
            break; // 防止无限循环
        p += step;
    }
}

std::string_view layout::do_layout(
    const uint8_t* entry_data,
    uint32_t entry_size,
    std::string_view fmt_str,
    const char* const* categories,
    uint32_t category_count
) noexcept
{
    using namespace qlog::serialization;

    reset_buffer();

    if (entry_data == nullptr || entry_size < sizeof(entry_header))
        return {};

    const auto& hdr = *reinterpret_cast<const entry_header*>(entry_data);

    // ── 步骤 1: 输出前缀 "[时间][级别][分类] " ───────────────────────────
    // 时间戳
    push_char('[');
    insert_timestamp(hdr.timestamp_ms);
    push_char(']');

    // 级别
    push_char('[');
    switch (static_cast<log_level>(hdr.level))
    {
    case log_level::verbose:
        push_bytes("V", 1);
        break;
    case log_level::debug:
        push_bytes("D", 1);
        break;
    case log_level::info:
        push_bytes("I", 1);
        break;
    case log_level::warning:
        push_bytes("W", 1);
        break;
    case log_level::error:
        push_bytes("E", 1);
        break;
    case log_level::fatal:
        push_bytes("F", 1);
        break;
    default:
        push_char('?');
        break;
    }
    push_char(']');

    // 分类
    if (categories != nullptr && hdr.category_idx < category_count)
    {
        push_char('[');
        const char* cat = categories[hdr.category_idx];
        push_bytes(cat, std::strlen(cat));
        push_char(']');
    }
    push_char(' ');

    // ── 步骤 2: 设置参数区指针，预扫描参数 ───────────────────────────────
    params_begin_ = entry_data + sizeof(entry_header);
    params_end_ = entry_data + entry_size;
    scan_params();

    // ── 步骤 3: 格式字符串状态机 ─────────────────────────────────────────
    // 对标 BqLog python_style_format_content_utf8 的主循环
    if (fmt_str.empty())
    {
        push_char('\n');
        return result();
    }

    const char* p = fmt_str.data();
    const char* p_end = p + fmt_str.size();
    int auto_index = 0;

    while (p < p_end)
    {
        if (*p == '{')
        {
            // '{{' → 转义输出 '{'
            if (p + 1 < p_end && p[1] == '{')
            {
                push_char('{');
                p += 2;
                continue;
            }

            ++p; // 跳过 '{'

            // ── 解析参数索引 ──────────────────────────────────────────
            int index = 0;
            bool has_index = false;
            while (p < p_end && *p >= '0' && *p <= '9')
            {
                index = index * 10 + (*p - '0');
                has_index = true;
                ++p;
            }

            if (!has_index)
                index = auto_index;

            // ── 解析格式说明符 ────────────────────────────────────────
            format_info fi{};
            if (p < p_end && *p == ':')
            {
                ++p;
                // 找到 '}'
                const char* spec_start = p;
                while (p < p_end && *p != '}')
                    ++p;
                const std::string_view spec{spec_start, static_cast<size_t>(p - spec_start)};
                parse_format_spec(spec, fi);
            }

            // 跳过 '}'
            if (p < p_end && *p == '}')
                ++p;

            // ── 输出对应参数 ──────────────────────────────────────────
            if (index < param_count_)
            {
                insert_param(param_offsets_[index], fi);
            }
            ++auto_index;
        }
        else if (*p == '}' && p + 1 < p_end && p[1] == '}')
        {
            // '}}' → 转义输出 '}'
            push_char('}');
            p += 2;
        }
        else
        {
            push_char(*p);
            ++p;
        }
    }

    push_char('\n');
    return result();
}

std::string_view layout::result() const noexcept
{
    return {reinterpret_cast<const char*>(buf_begin_), static_cast<size_t>(buf_cur_ - buf_begin_)};
}

// 处理流程：
//   1. 解析 entry_header（时间、级别、分类）
//   2. scan_params 建立参数索引
//   3. 遍历格式字符串：
//      a. 普通字符 → 直接输出
//      b. '{{'   → 输出 '{'（转义）
//      c. '{{n:spec}}' → 解析 index 和 format_spec，调用 insert_param
//   4. 追加换行符 '\n'
void layout::reserve(size_t capacity)
{
    if (capacity <= buf_capacity_)
        return;
    auto* new_buf = static_cast<uint8_t*>(std::realloc(buf_begin_, capacity));
    if (!new_buf)
        return;
    const size_t used = static_cast<size_t>(buf_cur_ - buf_begin_);
    buf_begin_ = new_buf;
    buf_cur_ = new_buf + used;
    buf_end_ = new_buf + capacity;
    buf_capacity_ = capacity;
}
} // namespace qlog::layout