#include "qlog/layout/layout.hpp"

#include <array>
#include <cassert>
#include <cctype>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>

#include "qlog/log_level.hpp"
#include "qlog/record/argument_tag.hpp"
#include "qlog/utility/utf_conversion.hpp"
#include "qlog/utility/utility.hpp"

namespace qlog::layout {
namespace {
constexpr char kLevelText[6][3] = {
    {'[', 'V', ']'}, {'[', 'D', ']'}, {'[', 'I', ']'},
    {'[', 'W', ']'}, {'[', 'E', ']'}, {'[', 'F', ']'},
};

constexpr auto kDigits3 = [] {
    std::array<char, 3000> result{};

    for (std::size_t value = 0; value < 1000; ++value) {
        result[value * 3] = static_cast<char>('0' + value / 100);
        result[value * 3 + 1] = static_cast<char>('0' + (value / 10) % 10);
        result[value * 3 + 2] = static_cast<char>('0' + value % 10);
    }

    return result;
}();
}  // namespace

Layout::Layout() = default;

void Layout::FormatInfo::reset() {
    used = false;
    upper = true;

    fill = ' ';
    align = '<';
    sign = '-';
    prefix = ' ';

    offset = 0;
    width = 0;
    precision = std::numeric_limits<std::uint32_t>::max();

    type = ' ';
}

void Layout::tidy_memory() {
    if (format_content_.capacity() > 1024U) {
        std::vector<char> small;
        small.reserve(1024U);
        format_content_.swap(small);
        // CR: swap是什么 交换内存数据吗？
    }

    format_content_cursor_ = 0;
}

void Layout::reverse(std::uint32_t begin_cursor, std::uint32_t end_cursor) {
    while (begin_cursor < end_cursor) {
        const char saved = format_content_[begin_cursor];
        format_content_[begin_cursor] = format_content_[end_cursor];
        format_content_[end_cursor] = saved;

        ++begin_cursor;
        --end_cursor;
    }
}

template <typename Char>
Layout::FormatInfo Layout::c20_format(const Char* style, std::int32_t len) {
    FormatInfo info;
    info.used = true;

    assert(style != nullptr);
    assert(len > 0);

    if (style[0] != static_cast<Char>(':')) {
        return info;
    }  // CR：为什么要return？ 如果读取到开头是：不是正好说明是{之后的吗？

    std::int32_t index = 0;
    bool reading_precision = false;
    bool align_seen = false;
    bool fill_seen = false;

    char digits[3] = {'0', '\0', '\0'};

    while (true) {
        ++index;

        if (index >= len || index > 10) {
            info.offset = 0;
            info.width = 0;
            break;
        }

        const char c = static_cast<char>(style[index]);

        if (c == '{') {
            info.offset = 0;
            info.width = 0;
            break;
        }

        if (c == '}') {
            info.offset = static_cast<std::uint32_t>(index);
            break;
        }

        switch (c) {
            case '#':
                info.prefix = c;
                break;
            case '>':
            case '<':
            case '^':
                if (!align_seen) {
                    info.align = c;
                    align_seen = true;
                } else {
                    info.fill = c;
                    if (info.align == '^' || info.align == '<') {
                        info.fill = ' ';
                    }
                }
                break;

            case '+':
            case '-':
                info.sign = c;
                break;

            case 'b':
            case 'B':
            case 'e':
            case 'E':
            case 'f':
            case 'F':
            case 'x':
            case 'X':
            case 'o':
            case 'd':
                if (info.type == ' ') {
                    info.type = c;

                    if (info.type == 'E') {
                        info.type = 'e';
                        info.upper = true;
                    } else if (info.type == 'X') {
                        info.type = 'x';
                        info.upper = true;
                    } else if (info.type == 'B') {
                        info.type = 'b';
                        info.upper = true;
                    } else {
                        info.upper = false;
                    }
                }
                // CR:befx 有什么意义？
                if ((c == 'x' || c == 'X') && info.align != '>') {
                    info.fill = ' ';
                }
                break;

            case '.':
                reading_precision = true;
                info.width = static_cast<std::uint32_t>(std::atoi(digits));

                digits[0] = '0';
                digits[1] = '\0';
                break;

            default:
                if (!fill_seen && (c < '1' || c > '9')) {
                    info.fill = c;

                    if (info.align == '^' || info.align == '<') {
                        info.fill = ' ';
                    }
                } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                    fill_seen = true;

                    if (digits[0] == '0') {
                        digits[0] = c;
                    } else if (digits[1] == '\0') {
                        digits[1] = c;
                    }
                }
                break;
        }
    }
    if (reading_precision) {
        info.precision = static_cast<std::uint32_t>(std::atoi(digits));
    } else {
        info.width = static_cast<std::uint32_t>(std::atoi(digits));
    }

    if (info.type == ' ') {
        info.type = 'd';
    }

    return info;
}

void Layout::expand_format_content_buff_size(std::uint32_t new_size) {
    if (format_info_.offset != 0) {
        new_size += format_info_.width + format_info_.precision + 2U;
    }

    if (new_size <= format_content_.size()) {
        return;
    }

    format_content_.resize(qlog::utility::round_pow_of_two(new_size));
}

std::uint32_t Layout::insert_str_utf8(const char* str, const std::uint32_t len) {
    if (len == 0) {
        return 0;
    }

    expand_format_content_buff_size(format_content_cursor_ + len);

    std::memcpy(format_content_.data() + format_content_cursor_, str, len);
    format_content_cursor_ += len;

    assert(format_content_cursor_ <= format_content_.size());
    return len;
}

std::uint32_t Layout::insert_str_utf16(const char* str, const std::uint32_t len) {
    const std::uint32_t max_bytes_len = len * 3U / 2U + 1U;
    // CR:为什么要 *3/2 +1来计算？
    expand_format_content_buff_size(format_content_cursor_ + max_bytes_len);
    // CR:什么是按实际utf8长度推进？传入的长度len是对于utf8而言吗？
    // CR:普通 BMP 字符是utf16吗？为什么不是2个utf8？
    const std::uint32_t write_size = qlog::utility::utf16_to_utf8_sw(
        str, len, format_content_.data() + format_content_cursor_, max_bytes_len);

    format_content_cursor_ += write_size;

    assert(format_content_cursor_ <= format_content_.size());
    return write_size;
}

void Layout::insert_bool(bool value) {
    if (value) {
        insert_str_utf8("TRUE", 4U);
    } else {
        insert_str_utf8("FALSE", 5U);
    }
}

void Layout::insert_char(char value) {
    expand_format_content_buff_size(format_content_cursor_ + 1U);

    format_content_[format_content_cursor_] = value;
    ++format_content_cursor_;
}

// CR: BQLog 也有expand format conetent buff size的函数吗？他的底层容器是什么 resize会不会开销很大？

void Layout::insert_char16(char16_t value) {
    insert_str_utf16(reinterpret_cast<const char*>(&value),
                     static_cast<std::uint32_t>(sizeof(value)));
    // CR:static_cast 和reinterpret_cast有什么区别？
}

void Layout::insert_char32(char32_t value) {
    if (value <= 0xFFFFU) {
        const char16_t tmp = static_cast<char16_t>(value);

        insert_str_utf16(reinterpret_cast<const char*>(&tmp),
                         static_cast<std::uint32_t>(sizeof(tmp)));
    } else {
        value -= 0x10000U;  // CR:为什么要减去10000
        char16_t tmp[2];
        tmp[0] = static_cast<char16_t>((value >> 10) + 0xD800U);  // CR: 0xd8 1101 1000
        tmp[1] = static_cast<char16_t>(
            (value & 0x3FFU) + 0xDC00U);  // CR：取前10位 +0xdc00 1101 1100 0000 0000是做什么？

        insert_str_utf16(reinterpret_cast<const char*>(tmp),
                         static_cast<std::uint32_t>(sizeof(tmp)));
    }
}

void Layout::fill_and_alignment(std::uint32_t write_begin_pos) {
    const std::uint32_t dis = format_content_cursor_ - write_begin_pos;

    if (dis < format_info_.width) {
        const std::uint32_t fill_count = format_info_.width - dis;

        if (format_info_.align == '>') {
            std::uint32_t ignore = 0;
            std::uint32_t ignore_index = 0;

            for (std::uint32_t i = 1; i <= dis; ++i) {
                const std::uint32_t opt_index = write_begin_pos + format_info_.width - i;
                const std::uint32_t move_index = write_begin_pos + dis - i;

                if ((format_content_[move_index] == '+' || format_content_[move_index] == '-') &&
                    format_info_.fill != ' ') {
                    format_content_[opt_index] = format_info_.fill;
                    ignore = 1;
                    continue;
                }

                if (format_info_.fill == '0' && format_info_.prefix == '#' &&
                    (format_info_.type == 'b' || format_info_.type == 'x')) {
                    if (format_content_[move_index] == 'b' || format_content_[move_index] == 'B' ||
                        format_content_[move_index] == 'x' || format_content_[move_index] == 'X') {
                        format_content_[opt_index] = format_info_.fill;
                        ignore_index = move_index - 1U;
                        ignore = 2;
                        continue;
                    }
                }

                if (ignore == 2 &&
                    ignore_index ==
                        move_index) {  // ignore_index==move_index是什么情况?
                                       // 上一个if分支ignore等于2 不是
                                       // ignore_index=move_index-1U;；上上个if分支 ignore等于1啊
                    format_content_[opt_index] = format_info_.fill;
                    continue;
                }

                format_content_[opt_index] =
                    format_content_[move_index];  // 这是从最后一个数字 开始向最右侧复制吗？
            }
            for (std::uint32_t i = ignore; i < fill_count; ++i) {
                format_content_[write_begin_pos + i] =
                    format_info_.fill;  // wirte 是参考原有拼写不参考,这是v2.5.0版本的错误吗？
            }
        } else if (format_info_.align == '<') {
            for (std::uint32_t i = 0; i < fill_count; ++i) {
                format_content_[format_content_cursor_ + i] = format_info_.fill;
            }
        } else if (format_info_.align == '^') {
            const std::uint32_t end = fill_count / 2U;     // 这是单侧对齐填充数？
            const std::uint32_t front = fill_count - end;  // 这是居中对齐的左侧数目吗？
            for (std::uint32_t i = 1; i <= dis; ++i) {
                const std::uint32_t opt_index = write_begin_pos + format_info_.width - i - end;
                const std::uint32_t move_index = write_begin_pos + dis - i;

                format_content_[opt_index] = format_content_[move_index];
            }

            for (std::uint32_t i = 0; i < front; ++i) {
                format_content_[write_begin_pos + i] = format_info_.fill;
            }

            for (std::uint32_t i = 0; i < end; ++i) {
                format_content_[front + write_begin_pos + dis + i] = format_info_.fill;
            }
        }

        format_content_cursor_ += fill_count;

        format_info_.used = false;
        format_info_.upper = false;
        format_info_.fill = '\0';
        format_info_.align = '\0';
        format_info_.sign = '\0';
        format_info_.prefix = '\0';
        format_info_.offset = 0;
        format_info_.width = 0;
        format_info_.precision = 0;
        format_info_.type = '\0';
    }
}

void Layout::fill_e_style(std::uint32_t e_count, std::uint32_t begin_cursor) {
    std::uint32_t bitcount = 0;
    auto temp = e_count;

    do {
        temp /= 10U;
        ++bitcount;
    } while (temp != 0);

    expand_format_content_buff_size(format_content_cursor_ + bitcount + 3U);

    const std::int32_t jump =
        static_cast<std::int32_t>(format_content_cursor_) -
        (static_cast<std::int32_t>(begin_cursor + format_info_.width - bitcount) - 2);

    if (jump > 0) {
        format_content_cursor_ -= static_cast<std::uint32_t>(jump);

        if (format_content_cursor_ == begin_cursor) {
            ++format_content_cursor_;
        }
    }

    if (format_info_.upper) {
        format_content_[format_content_cursor_++] = 'E';
    } else {
        format_content_[format_content_cursor_++] = 'e';
    }

    format_content_[format_content_cursor_++] = '+';

    begin_cursor = format_content_cursor_;
    temp = e_count;

    do {
        const std::int32_t digit =
            static_cast<std::int32_t>(e_count % 10U);  // CR:BQLOG也是这样？ % 开销会很大吧

        format_content_[format_content_cursor_++] = static_cast<char>('0' + digit);

        e_count /= 10U;
    } while (e_count != 0);

    if (temp <= 9U) {
        format_content_[format_content_cursor_++] = '0';
    }
    reverse(begin_cursor, format_content_cursor_ - 1U);
}

std::uint32_t Layout::insert_integral_unsigned(std::uint64_t value, std::uint32_t base) {
    const std::uint32_t width = format_content_cursor_;

    assert(base <= 32U && "base is a number belongs to [ 2 , 32 ]");

    if (format_info_.type == 'b') {
        base = 2U;
    } else if (format_info_.type == 'x') {
        base = 16U;
    } else if (format_info_.type == 'o') {
        base = 8U;
    }

    if (base >= 16U) {
        expand_format_content_buff_size(format_content_cursor_ + 16U);
    } else if (base >= 10U) {
        expand_format_content_buff_size(format_content_cursor_ + 20U);  // CR:为什么要加20U？
    } else {
        expand_format_content_buff_size(format_content_cursor_ + 64U);
    }

    if (value > 0) {
        if (format_info_.sign == '+') {
            format_content_[format_content_cursor_++] = '+';
        }
    }

    if (format_info_.prefix == '#') {
        if (format_info_.type == 'b') {
            format_content_[format_content_cursor_++] = '0';

            if (format_info_.upper) {
                format_content_[format_content_cursor_++] = 'B';
            } else {
                format_content_[format_content_cursor_++] = 'b';
            }
        } else if (format_info_.type == 'x') {
            format_content_[format_content_cursor_++] = '0';

            if (format_info_.upper) {
                format_content_[format_content_cursor_++] = 'X';
            } else {
                format_content_[format_content_cursor_++] = 'x';
            }
        }
    }

    const std::uint32_t begin_cursor = format_content_cursor_;
    std::uint32_t e_count = 0;

    if (base == 10U && format_info_.type != 'e' && value >= 1000000000ULL) {
        const char* const digits3 = kDigits3.data();

        char tmp[20];
        char* p = tmp + sizeof(tmp);

        while (value >= 1000U) {
            const std::uint32_t r = static_cast<std::uint32_t>(value % 1000U);

            value /= 1000U;
            p -= 3;
            std::memcpy(p, digits3 + r * 3U, 3);
        }

        const char* d = digits3 + static_cast<std::uint32_t>(value) * 3U;
        // CR: *3是找到 对应的三位数的开头？

        const std::int32_t start = value >= 100U ? 0 : (value >= 10U ? 1 : 2);

        for (std::int32_t k = 2; k >= start; --k) {
            *--p = d[k];
        }

        const std::uint32_t digit_count = static_cast<std::uint32_t>(tmp + sizeof(tmp) - p);

        std::memcpy(format_content_.data() + format_content_cursor_, p, digit_count);

        format_content_cursor_ += digit_count;
        return format_content_cursor_ - width;
    }

    do {
        const std::int32_t digit = static_cast<std::int32_t>(value % base);

        if (digit < 0xA) {
            format_content_[format_content_cursor_] = static_cast<char>('0' + digit);
        } else {
            if (format_info_.upper) {
                format_content_[format_content_cursor_] = static_cast<char>('A' + digit - 0xA);
            } else {
                format_content_[format_content_cursor_] = static_cast<char>('a' + digit - 0xA);
            }
        }
        value /= base;
        ++format_content_cursor_;

        if (value > base) {
            ++e_count;
        }
    } while (value != 0);

    // CR：上面这一段是科学计数法？

    if (format_info_.type == 'e') {
        format_content_[format_content_cursor_] = format_content_[format_content_cursor_ - 1U];

        format_content_[format_content_cursor_ - 1U] = '.';
        ++format_content_cursor_;

        reverse(begin_cursor, format_content_cursor_ - 1);
        fill_e_style(e_count, begin_cursor);
    } else {
        reverse(begin_cursor, format_content_cursor_ - 1U);
    }
    return format_content_cursor_ - width;  // 返回的是什么？ 写入长度吗？
}
std::uint32_t Layout::insert_integral_signed(std::int64_t value, std::uint32_t base) {
    const std::uint32_t width = format_content_cursor_;

    assert(base <= 32U && "base is a number belongs to [2, 32]");

    if (format_info_.type == 'b') {
        base = 2U;
    } else if (format_info_.type == 'x') {
        base = 16U;
    } else if (format_info_.type == 'o') {
        base = 8U;
    }

    if (base >= 16U) {
        expand_format_content_buff_size(format_content_cursor_ + 16U);
    } else if (base >= 10U) {
        expand_format_content_buff_size(format_content_cursor_ + 20U);
    } else {
        expand_format_content_buff_size(format_content_cursor_ + 64U);
    }

    if (value < 0) {
        format_content_[format_content_cursor_++] = '-';
    } else {
        if (format_info_.sign == '+') {
            format_content_[format_content_cursor_++] = '+';
        }
    }

    if (format_info_.prefix == '#') {
        if (format_info_.type == 'b') {
            format_content_[format_content_cursor_++] = '0';

            if (format_info_.upper) {
                format_content_[format_content_cursor_++] = 'B';
            } else {
                format_content_[format_content_cursor_++] = 'b';
            }
        } else if (format_info_.type == 'x') {
            format_content_[format_content_cursor_++] = '0';

            if (format_info_.upper) {
                format_content_[format_content_cursor_++] = 'X';
            } else {
                format_content_[format_content_cursor_++] = 'x';
            }
        }
    }

    const std::uint32_t begin_cursor = format_content_cursor_;
    std::uint32_t e_count = 0;
    if (base == 10U && format_info_.type != 'e' &&
        (value >= 1000000000LL || value <= -1000000000LL)) {
        std::uint64_t mag = value < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(value)
                                      : static_cast<std::uint64_t>(value);

        const char* const digit3 = kDigits3.data();

        char tmp[20];
        char* p = tmp + sizeof(tmp);

        while (mag >= 1000U) {
            const std::uint32_t r = static_cast<std::uint32_t>(mag % 1000U);

            mag /= 1000U;
            p -= 3;

            std::memcpy(p, digit3 + r * 3U, 3);
        }

        const char* d = digit3 + static_cast<std::uint32_t>(mag) * 3U;

        const std::int32_t start = mag >= 100U ? 0 : (mag >= 10U ? 1 : 2);

        for (std::int32_t k = 2; k >= start; --k) {
            *--p = d[k];
        }

        const std::uint32_t digit_count = static_cast<std::uint32_t>(tmp + sizeof(tmp) - p);

        std::memcpy(format_content_.data() + format_content_cursor_, p, digit_count);

        format_content_cursor_ += digit_count;
        return format_content_cursor_ - width;
    }

    do {
        const char digit = static_cast<char>(
            std::abs(static_cast<std::int32_t>(value % static_cast<std::int32_t>(base))));

        if (digit < 0xA) {
            format_content_[format_content_cursor_] = static_cast<char>('0' + digit);
        } else {
            if (format_info_.upper) {
                format_content_[format_content_cursor_] = static_cast<char>('A' + digit - 0xA);
            } else {
                format_content_[format_content_cursor_] = static_cast<char>('a' + digit - 0xA);
            }
        }

        value /= base;

        if (value > base) {
            ++e_count;
        }

        ++format_content_cursor_;
    } while (value != 0);

    if (format_info_.type == 'e') {
        format_content_[format_content_cursor_] = format_content_[format_content_cursor_ - 1U];

        format_content_[format_content_cursor_ - 1U] = '.';
        ++format_content_cursor_;

        reverse(begin_cursor, format_content_cursor_ - 1U);
        fill_e_style(e_count, begin_cursor);
    } else {
        reverse(begin_cursor, format_content_cursor_ - 1U);
    }
    return format_content_cursor_ - width;
}

void Layout::insert_pointer(const void* ptr) {  // CR:这个是直接写入指针的地址内的数据吗？
    if (ptr != nullptr) {
        const std::uint32_t len = static_cast<std::uint32_t>(sizeof("0x") - 1U);  // 为什么0x-1U？

        expand_format_content_buff_size(format_content_cursor_ + len);

        std::memcpy(format_content_.data() + format_content_cursor_, "0x", len);

        format_content_cursor_ += len;

        insert_integral_unsigned(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(ptr)),
                                 16U);

    } else {
        const std::uint32_t len = static_cast<std::uint32_t>(sizeof("null") - 1U);

        expand_format_content_buff_size(format_content_cursor_ + len);

        std::memcpy(format_content_.data() + format_content_cursor_, "null", len);

        format_content_cursor_ += len;
    }
}

void Layout::insert_decimal(float value) {
    if (format_info_.type == 'e') {
        format_info_.type = 'Z';
    }
    /* if (format_info_.type == 'e') {
            format_info_.type = 'Z';
        }
            这里 如果传入的type要求使用科学计数法 就先处理整数部分 最后变为e处理 啥？*/
    std::uint32_t int_width = 0;

    std::int32_t precision = static_cast<std::int32_t>(
        format_info_.precision != 0xFFFFFFFFU ? format_info_.precision : 7U);

    const std::uint32_t begin_cursor = format_content_cursor_;

    if (value >= 0) {
        const std::uint64_t i_part = static_cast<std::uint64_t>(value);

        int_width = insert_integral_unsigned(i_part, 10U);
        value -= static_cast<float>(i_part);
    } else {
        const std::int64_t i_part = static_cast<std::int64_t>(value);

        int_width = insert_integral_signed(i_part, 10U);
        value -= static_cast<float>(i_part);
    }

    if (format_info_.width > 0 &&
        format_info_.width < static_cast<std::uint32_t>(precision) + 1U + int_width) {
        precision = static_cast<std::int32_t>(format_info_.width - int_width - 1U);
    }

    value = std::fabs(value);

    expand_format_content_buff_size(format_content_cursor_ + static_cast<std::uint32_t>(precision) +
                                    5U);  // CR:为什么+5u？

    std::uint32_t point_index = 0;

    if (precision > 0 || (format_info_.type == 'Z' && int_width > 1U)) {
        point_index = format_content_cursor_;
        format_content_[format_content_cursor_++] = '.';
    }

    while (precision > 0) {
        value *= 10;

        const std::int32_t digit = static_cast<std::int32_t>(value);

        value -= static_cast<float>(digit);
        --precision;

        format_content_[format_content_cursor_++] = static_cast<char>('0' + digit);
    }

    if (format_info_.type == 'Z') {  // CR:Z是什么type？为什么上面要加点 这里还要改type
        format_info_.type = 'e';

        std::uint32_t moves = int_width - 1U;

        if (point_index != 0) {
            while (moves > 0) {
                const char temp = format_content_[point_index];

                format_content_[point_index] = format_content_[point_index - 1U];

                format_content_[point_index - 1U] = temp;

                --point_index;
                --moves;
            }
        }

        moves = int_width - 1U;

        if (format_info_.precision > 0 &&
            format_info_.precision < format_content_cursor_ - begin_cursor) {
            format_content_cursor_ = point_index + format_info_.precision + 1U;
        }  // CR:+1U是因为小数点.吗？

        fill_e_style(moves, begin_cursor);
    }
}

void Layout::insert_decimal(double value) {
    if (format_info_.type == 'e') {
        format_info_.type = 'Z';
    }

    std::uint32_t int_width = 0;

    std::int32_t precision = static_cast<std::int32_t>(
        format_info_.precision != 0xFFFFFFFFU ? format_info_.precision : 15U);

    const std::uint32_t begin_cursor = format_content_cursor_;

    if (value >= 0) {
        const std::uint64_t i_part = static_cast<std::uint64_t>(value);

        int_width = insert_integral_unsigned(i_part, 10U);
        value -= static_cast<double>(i_part);
    } else {
        const std::int64_t i_part = static_cast<std::int64_t>(value);

        int_width = insert_integral_signed(i_part, 10U);
        value -= static_cast<double>(i_part);
    }

    if (format_info_.width > 0 &&
        format_info_.width < static_cast<std::uint32_t>(precision) + 1U + int_width) {
        precision = static_cast<std::int32_t>(format_info_.width) -
                    static_cast<std::int32_t>(int_width) - 1;

        if (precision < 0) {
            precision = 0;
        }
    }

    value = std::fabs(value);

    expand_format_content_buff_size(format_content_cursor_ + static_cast<std::uint32_t>(precision) +
                                    5U);

    std::uint32_t point_index = 0;

    if (precision > 0 || (format_info_.type == 'Z' && int_width > 1U)) {
        point_index = format_content_cursor_;
        format_content_[format_content_cursor_++] = '.';
    }

    auto temp_precision = precision;

    while (temp_precision > 0) {
        value *= 10;

        const std::int32_t digit = static_cast<std::int32_t>(value);

        value -= static_cast<double>(digit);
        --temp_precision;

        format_content_[format_content_cursor_++] = static_cast<char>('0' + digit);
    }

    if (format_info_.type == 'Z') {
        format_info_.type = 'e';

        std::uint32_t moves = int_width - 1U;

        if (point_index != 0) {
            while (moves > 0) {
                const char temp = format_content_[point_index];

                format_content_[point_index] = format_content_[point_index - 1U];

                format_content_[point_index - 1U] = temp;
                --point_index;
                --moves;
            }
        }

        moves = int_width - 1U;

        if (format_info_.precision > 0 &&
            format_info_.precision < format_content_cursor_ - begin_cursor) {
            format_content_cursor_ = point_index + format_info_.precision + 1U;
        }

        fill_e_style(moves, begin_cursor);
    }

    // CR：不是说改成z可以防止进入指数分支吗 开头直接改为 z
    // ，到这里不久是e了嘛？而且上面这些都是针对Z的分支啊
}

namespace {
template <typename T>
T read_argument_value(const std::uint8_t* src) {
    static_assert(std::is_trivially_copyable_v<T>);

    T value;
    std::memcpy(&value, src, sizeof(value));
    return value;
}

std::uint32_t find_brace_and_copy_sw(const char* src, std::uint32_t len, char* dst,
                                     bool& found_brace) {
    found_brace = false;

    std::uint32_t copied = 0;
    while (copied < len) {
        const char c = src[copied];

        if (c == '{' || c == '}') {
            found_brace = true;
            break;
        }

        dst[copied] = c;
        ++copied;
    }
    // CR:found_brace有什么用？
    return copied;
}
}  // namespace

void Layout::python_style_format_content_utf8(const qlog::record::LogEntryHandle& log_entry) {
    const std::uint8_t* args_data_ptr = log_entry.get_log_args_data();
    const std::uint32_t args_data_len = log_entry.get_log_args_data_size();

    const char* format_data_ptr = log_entry.get_format_string_data();
    const std::uint32_t format_data_len = log_entry.get_log_head().log_format_data_len;

    if (args_data_len == 0) {
        insert_str_utf8(format_data_ptr, format_data_len);
        return;
    }

    expand_format_content_buff_size(format_content_cursor_ + format_data_len);

    std::uint32_t i = 0;
    std::uint32_t args_data_cursor = 0;

    while (i < format_data_len) {
        bool found_brace = false;

        const std::uint32_t remaining = format_data_len - i;
        const std::uint32_t copied =
            find_brace_and_copy_sw(format_data_ptr + i, remaining,
                                   format_content_.data() + format_content_cursor_, found_brace);

        i += copied;
        format_content_cursor_ += copied;

        if (found_brace) {
            const char c = format_data_ptr[i];
            ++i;
            if (i < format_data_len) {
                const char next_c = format_data_ptr[i];
                if (c == '}' && next_c == '}') {
                    expand_format_content_buff_size(format_content_cursor_ + 1U);
                    format_content_[format_content_cursor_++] = '}';

                    ++i;
                    continue;
                }
            }
            if (c == '}') {
                expand_format_content_buff_size(format_content_cursor_ + 1U);

                format_content_[format_content_cursor_++] = '}';
                continue;
            }

            if (format_info_.used == true) {
                format_info_.reset();
            }

            if (args_data_cursor < args_data_len) {
                std::int32_t format_spec_len = 0;
                bool format_spec_closed = false;

                for (std::uint32_t k = i; k < format_data_len && k < i + 20U; ++k) {
                    const char c_scan = format_data_ptr[k];

                    if (c_scan == '}') {
                        format_spec_closed = true;
                        break;
                    }

                    if (c_scan == '{') {
                        format_spec_closed = false;
                        break;
                    }

                    ++format_spec_len;
                }

                if (format_spec_closed) {
                    format_info_ = c20_format(format_data_ptr + i, format_spec_len + 1);

                    if (format_info_.offset != 0) {
                        i += format_info_.offset + 1U;
                    } else {
                        i += static_cast<std::uint32_t>(format_spec_len + 1);
                    }

                    const std::uint32_t write_begin_pos = format_content_cursor_;

                    const std::uint8_t type_info_i = args_data_ptr[args_data_cursor];
                    // CR：这个typeinfoi对应的是什么 为什么在args data cursor的起始点
                    const qlog::record::ArgumentTag type_info =
                        static_cast<qlog::record::ArgumentTag>(type_info_i);

                    const std::uint8_t* arg = args_data_ptr + args_data_cursor;

                    switch (type_info) {
                        case qlog::record::ArgumentTag::unsupported_type:
                            // 预留输出内部警告
                            break;
                        case qlog::record::ArgumentTag::null_type:
                            assert(sizeof(void*) >= 4);

                            insert_str_utf8("null", 4U);
                            args_data_cursor += 4;
                            break;

                        case qlog::record::ArgumentTag::pointer_type: {
                            assert(sizeof(void*) >= 4);

                            const std::uint64_t pointer_value =
                                read_argument_value<std::uint64_t>(arg + 4);
                            // CR：这是直接读取8字节大小的内容嘛？ 如果指针4字节呢？
                            const void* arg_data_ptr = reinterpret_cast<const void*>(
                                static_cast<std::uintptr_t>(pointer_value));

                            insert_pointer(arg_data_ptr);
                            args_data_cursor += 12U;  // CR:为什么就是12U了
                            break;
                        }

                        case qlog::record::ArgumentTag::bool_type:
                            insert_bool(read_argument_value<bool>(arg + 2));

                            args_data_cursor += 4U;
                            break;

                        case qlog::record::ArgumentTag::char_type:
                            insert_char(read_argument_value<char>(arg + 2));

                            args_data_cursor += 4U;
                            break;

                        case qlog::record::ArgumentTag::char16_type:
                            insert_char16(read_argument_value<char16_t>(arg + 2));

                            args_data_cursor += 4U;
                            break;

                        case qlog::record::ArgumentTag::char32_type:
                            insert_char32(read_argument_value<char32_t>(arg + 4));

                            args_data_cursor += 8U;
                            break;

                        case qlog::record::ArgumentTag::int8_type:
                            insert_integral_signed(read_argument_value<std::int8_t>(arg + 2));

                            args_data_cursor += 4U;
                            break;

                        case qlog::record::ArgumentTag::uint8_type:
                            insert_integral_unsigned(read_argument_value<std::uint8_t>(arg + 2));

                            args_data_cursor += 4U;
                            break;

                        case qlog::record::ArgumentTag::int16_type:
                            insert_integral_signed(read_argument_value<std::int16_t>(arg + 2));

                            args_data_cursor += 4U;
                            break;

                        case qlog::record::ArgumentTag::uint16_type:
                            insert_integral_unsigned(read_argument_value<std::uint16_t>(arg + 2));

                            args_data_cursor += 4U;
                            break;

                        case qlog::record::ArgumentTag::int32_type:
                            insert_integral_signed(read_argument_value<std::int32_t>(arg + 4));

                            args_data_cursor += 8U;
                            break;

                        case qlog::record::ArgumentTag::uint32_type:
                            insert_integral_unsigned(read_argument_value<std::uint32_t>(arg + 4));

                            args_data_cursor += 8U;
                            break;

                        case qlog::record::ArgumentTag::int64_type:
                            insert_integral_signed(read_argument_value<std::int64_t>(arg + 4));
                            // CR: 不应该读取8字节的int64嘛？
                            args_data_cursor += 12U;
                            break;

                        case qlog::record::ArgumentTag::uint64_type:
                            insert_integral_unsigned(read_argument_value<std::uint64_t>(arg + 4));

                            args_data_cursor += 12U;
                            break;

                        case qlog::record::ArgumentTag::float_type:
                            insert_decimal(read_argument_value<float>(arg + 4));

                            args_data_cursor += 8U;
                            break;

                        case qlog::record::ArgumentTag::double_type:
                            insert_decimal(read_argument_value<double>(arg + 4));

                            args_data_cursor += 12U;
                            break;

                        case qlog::record::ArgumentTag::string_utf8_type: {
                            const std::uint32_t str_len =
                                read_argument_value<std::uint32_t>(arg + 4);

                            const char* str =
                                reinterpret_cast<const char*>(arg + 8);  // 为什么要+8？

                            insert_str_utf8(str, str_len);

                            args_data_cursor += 8U + ((str_len + 3U) & ~std::uint32_t{3});

                            break;
                        }

                        case qlog::record::ArgumentTag::string_utf16_type: {
                            const std::uint32_t str_len =
                                read_argument_value<std::uint32_t>(arg + 4);

                            const char* str = reinterpret_cast<const char*>(arg + 8);

                            insert_str_utf16(str, str_len);

                            args_data_cursor += 8U + ((str_len + 3U) & ~std::uint32_t{3U});
                            break;
                        }

                        default:
                            break;
                    }

                    assert(args_data_cursor <= args_data_len);

                    fill_and_alignment(write_begin_pos);  // CR:这是在干什么 刚才不是都写入了嘛

                    const std::uint32_t safe_buff_size =
                        format_content_cursor_ + (format_data_len - i);

                    expand_format_content_buff_size(safe_buff_size);
                } else {
                    expand_format_content_buff_size(format_content_cursor_ + 1U);

                    format_content_[format_content_cursor_++] = '{';
                }
            } else {
                expand_format_content_buff_size(format_content_cursor_ + 1U);

                format_content_[format_content_cursor_++] =
                    '{';  // cR:为什么要这么做？这个是自动添加{嘛？
            }
        }
    }
}

namespace {
std::uint32_t find_brace_and_convert_u16_sw(const char16_t* src, std::uint32_t len, char* dst,
                                            bool& found_brace, bool& non_ascii) {
    found_brace = false;
    non_ascii = false;

    std::uint32_t processed = 0;

    while (processed < len) {
        const char16_t c = src[processed];

        if (c == u'{' || c == u'}') {
            found_brace = true;
            break;
        }

        if (c >= 0x80U) {
            non_ascii = true;
            break;
        }

        dst[processed] = static_cast<char>(c);
        ++processed;
    }

    return processed;
}

}  // namespace

void Layout::python_style_format_content_utf16(const qlog::record::LogEntryHandle& log_entry) {
    const std::uint8_t* args_data_ptr = log_entry.get_log_args_data();
    const std::uint32_t args_data_len = log_entry.get_log_args_data_size();

    const char16_t* format_data_ptr =
        reinterpret_cast<const char16_t*>(log_entry.get_format_string_data());

    const std::uint32_t format_data_len = log_entry.get_log_head().log_format_data_len;

    std::uint32_t safe_buff_size =
        format_content_cursor_ +
        static_cast<std::uint32_t>((static_cast<std::size_t>(format_data_len) * 3U) >> 1U);

    expand_format_content_buff_size(safe_buff_size);

    const std::uint32_t wchar_len = format_data_len >> 1U;

    std::uint32_t i = 0;
    std::uint32_t args_data_cursor = 0;

    while (i < wchar_len) {
        bool found_brace = false;
        bool non_ascii = false;

        const std::uint32_t remaining = wchar_len - i;

        const std::uint32_t processed = find_brace_and_convert_u16_sw(
            format_data_ptr + i, remaining, format_content_.data() + format_content_cursor_,
            found_brace, non_ascii);

        i += processed;
        format_content_cursor_ += processed;

        if (non_ascii) {
            if (i < wchar_len) {
                const char16_t c = format_data_ptr[i];
                ++i;

                if (c < 0x80U) {
                    format_content_[format_content_cursor_++] = static_cast<char>(c);
                } else if (c < 0x800U) {
                    expand_format_content_buff_size(format_content_cursor_ + 2U);

                    format_content_[format_content_cursor_++] =
                        static_cast<char>(static_cast<unsigned char>(0xC0U | (c >> 6)));

                    format_content_[format_content_cursor_++] = static_cast<char>(
                        static_cast<unsigned char>(0x80U | (c & 0x3FU)));  // 为什么是0x3fu？

                } else if (c >= 0xD800U && c <= 0xDBFFU) {
                    if (i < wchar_len) {
                        const char16_t c2 = format_data_ptr[i];

                        if (c2 >= 0xDC00U && c2 <= 0xDFFFU) {
                            ++i;

                            const std::uint32_t codepoint =
                                0x10000U + (static_cast<std::uint32_t>(c) - 0xD800U) * 0x400U +
                                (static_cast<std::uint32_t>(c2) - 0xDC00U);

                            expand_format_content_buff_size(format_content_cursor_ + 4U);

                            format_content_[format_content_cursor_++] = static_cast<char>(
                                static_cast<unsigned char>(0xF0U | (codepoint >> 18)));

                            format_content_[format_content_cursor_++] = static_cast<char>(
                                static_cast<unsigned char>(0x80U | ((codepoint >> 12) & 0x3FU)));

                            format_content_[format_content_cursor_++] = static_cast<char>(
                                static_cast<unsigned char>(0x80U | ((codepoint >> 6) & 0x3FU)));

                            format_content_[format_content_cursor_++] = static_cast<char>(
                                static_cast<unsigned char>(0x80U | (codepoint & 0x3FU)));
                        }
                    }
                } else if (c >= 0xDC00U && c <= 0xDFFFU) {
                    // 忽略低代理
                } else {
                    expand_format_content_buff_size(format_content_cursor_ + 3U);

                    format_content_[format_content_cursor_++] =
                        static_cast<char>(static_cast<unsigned char>(0xE0U | (c >> 12)));

                    format_content_[format_content_cursor_++] =
                        static_cast<char>(static_cast<unsigned char>(0x80U | ((c >> 6) & 0x3FU)));

                    format_content_[format_content_cursor_++] =
                        static_cast<char>(static_cast<unsigned char>(0x80U | (c & 0x3FU)));
                }
            }
            continue;
        }

        if (found_brace) {
            const char16_t c = format_data_ptr[i];
            ++i;

            if (i < wchar_len) {
                const char16_t next_c = format_data_ptr[i];

                if (c == u'}' && next_c == u'}') {
                    expand_format_content_buff_size(format_content_cursor_ + 1U);

                    format_content_[format_content_cursor_++] = '}';

                    ++i;
                    continue;
                }
            }

            if (c == u'}') {
                expand_format_content_buff_size(format_content_cursor_ + 1U);

                format_content_[format_content_cursor_++] = '}';
                continue;
            }

            if (format_info_.used) {
                format_info_.reset();
            }

            if (args_data_cursor < args_data_len) {
                std::int32_t format_spec_len = 0;
                bool format_spec_closed = false;

                for (std::uint32_t k = i; k < wchar_len && k < i + 20U; ++k) {
                    const char16_t c_scan = format_data_ptr[k];

                    if (c_scan == u'}') {
                        format_spec_closed = true;
                        break;
                    }

                    if (c_scan == u'{') {
                        bool is_escaped = false;

                        if (k + 1U < wchar_len && format_data_ptr[k + 1U] == u'{') {
                            is_escaped = true;
                        }

                        if (!is_escaped) {
                            format_spec_closed = false;
                            break;
                        } else {
                            ++k;
                            ++format_spec_len;
                        }
                    }
                    ++format_spec_len;
                }

                if (format_spec_closed) {
                    format_info_ = c20_format(format_data_ptr + i, format_spec_len + 1);

                    if (format_info_.offset != 0) {
                        i += format_info_.offset + 1U;
                    } else {
                        i += static_cast<std::uint32_t>(format_spec_len + 1);
                    }

                    const std::uint32_t write_begin_pos = format_content_cursor_;

                    const std::uint8_t type_info_i = args_data_ptr[args_data_cursor];

                    const qlog::record::ArgumentTag type_info =
                        static_cast<qlog::record::ArgumentTag>(type_info_i);

                    const std::uint8_t* arg = args_data_ptr + args_data_cursor;

                    switch (type_info) {
                        case qlog::record::ArgumentTag::unsupported_type:

                            break;

                        case qlog::record::ArgumentTag::null_type:
                            assert(sizeof(void*) >= 4);

                            insert_str_utf8("null", 4U);
                            args_data_cursor += 4U;
                            break;

                        case qlog::record::ArgumentTag::pointer_type: {
                            assert(sizeof(void*) >= 4);

                            const std::uint64_t pointer_value =
                                read_argument_value<std::uint64_t>(arg + 4);

                            const void* ptr_data = reinterpret_cast<const void*>(
                                static_cast<std::uintptr_t>(pointer_value));

                            insert_pointer(ptr_data);

                            args_data_cursor += 12U;
                            break;
                        }

                        case qlog::record::ArgumentTag::bool_type:
                            insert_bool(read_argument_value<bool>(arg + 2));

                            args_data_cursor += 4U;
                            break;

                        case qlog::record::ArgumentTag::char_type:
                            insert_char(read_argument_value<char>(arg + 2));

                            args_data_cursor += 4U;
                            break;

                        case qlog::record::ArgumentTag::char16_type:
                            insert_char16(read_argument_value<char16_t>(arg + 2));

                            args_data_cursor += 4U;
                            break;

                        case qlog::record::ArgumentTag::char32_type:
                            insert_char32(read_argument_value<char32_t>(arg + 4));

                            args_data_cursor += 8U;
                            break;

                        case qlog::record::ArgumentTag::int8_type:
                            insert_integral_signed(read_argument_value<std::int8_t>(arg + 2));

                            args_data_cursor += 4U;
                            break;

                        case qlog::record::ArgumentTag::uint8_type:
                            insert_integral_unsigned(read_argument_value<std::uint8_t>(arg + 2));

                            args_data_cursor += 4U;
                            break;

                        case qlog::record::ArgumentTag::int16_type:
                            insert_integral_signed(read_argument_value<std::int16_t>(arg + 2));

                            args_data_cursor += 4U;
                            break;

                        case qlog::record::ArgumentTag::uint16_type:
                            insert_integral_unsigned(read_argument_value<std::uint16_t>(arg + 2));

                            args_data_cursor += 4U;
                            break;

                        case qlog::record::ArgumentTag::int32_type:
                            insert_integral_signed(read_argument_value<std::int32_t>(arg + 4));

                            args_data_cursor += 8U;
                            break;

                        case qlog::record::ArgumentTag::uint32_type:
                            insert_integral_unsigned(read_argument_value<std::uint32_t>(arg + 4));

                            args_data_cursor += 8U;
                            break;

                        case qlog::record::ArgumentTag::int64_type:
                            insert_integral_signed(read_argument_value<std::int64_t>(arg + 4));

                            args_data_cursor += 12U;
                            break;

                        case qlog::record::ArgumentTag::uint64_type:
                            insert_integral_unsigned(read_argument_value<std::uint64_t>(arg + 4));

                            args_data_cursor += 12U;
                            break;

                        case qlog::record::ArgumentTag::float_type:
                            insert_decimal(read_argument_value<float>(arg + 4));

                            args_data_cursor += 8U;
                            break;

                        case qlog::record::ArgumentTag::double_type:
                            insert_decimal(read_argument_value<double>(arg + 4));

                            args_data_cursor += 12U;
                            break;

                        case qlog::record::ArgumentTag::string_utf8_type: {
                            const std::uint32_t str_len =
                                read_argument_value<std::uint32_t>(arg + 4);

                            const char* str = reinterpret_cast<const char*>(arg + 8);

                            insert_str_utf8(str, str_len);

                            args_data_cursor += 8U + ((str_len + 3U) & ~std::uint32_t{3});

                            break;
                        }

                        case qlog::record::ArgumentTag::string_utf16_type: {
                            const std::uint32_t str_len =
                                read_argument_value<std::uint32_t>(arg + 4);

                            const char* str = reinterpret_cast<const char*>(arg + 8);

                            insert_str_utf16(str, str_len);

                            args_data_cursor += 8U + ((str_len + 3U) & ~std::uint32_t{3});

                            break;
                        }

                        default:
                            break;
                    }
                    assert(args_data_cursor <= args_data_len);

                    fill_and_alignment(write_begin_pos);

                    safe_buff_size = format_content_cursor_ +
                                     static_cast<std::uint32_t>((wchar_len - i) * 3U / 2U);

                    expand_format_content_buff_size(safe_buff_size);
                } else {
                    expand_format_content_buff_size(format_content_cursor_ + 1U);

                    format_content_[format_content_cursor_++] = '{';
                }
            } else {
                expand_format_content_buff_size(format_content_cursor_ + 1U);

                format_content_[format_content_cursor_++] = '{';
            }
        }
    }
}

void Layout::python_style_format_content(const qlog::record::LogEntryHandle& log_entry) {
    if (log_entry.get_log_head().log_format_str_type ==
        static_cast<std::uint8_t>(qlog::record::ArgumentTag::string_utf8_type)) {
        python_style_format_content_utf8(log_entry);
    } else {
        python_style_format_content_utf16(log_entry);
    }

    expand_format_content_buff_size(format_content_cursor_ + 1U);

    format_content_[format_content_cursor_] = '\0';

    assert(format_content_cursor_ < format_content_.size());
}

Layout::enum_layout_result Layout::insert_time(const qlog::record::LogEntryHandle& log_entry) {
    expand_format_content_buff_size(format_content_cursor_ + TimeZone::MAX_TIME_SIZE_LEN + 3U);

    const std::uint64_t epoch_ms = log_entry.get_log_head().timestamp_epoch;

    time_zone_ptr_->refresh_time_string_cache(epoch_ms);

    std::memcpy(format_content_.data() + format_content_cursor_,
                time_zone_ptr_->get_time_string_cache(),
                time_zone_ptr_->get_time_string_cache_len());

    format_content_cursor_ +=
        static_cast<std::uint32_t>(time_zone_ptr_->get_time_string_cache_len());

    const std::int32_t m_sec = static_cast<std::int32_t>(epoch_ms % 1000U);

    const char* ms_src = kDigits3.data() + m_sec * 3;

    char* ms_dest = format_content_.data() + format_content_cursor_;

    ms_dest[0] = ms_src[0];
    ms_dest[1] = ms_src[1];
    ms_dest[2] = ms_src[2];

    format_content_cursor_ += 3U;

    return enum_layout_result::finished;
}

Layout::enum_layout_result Layout::insert_thread_info(
    const qlog::record::LogEntryHandle& log_entry) {
    const auto& ext_info = log_entry.get_ext_head();

    auto iter = thread_names_cache_.find(log_entry.get_log_head().log_thread_id);

    if (iter == thread_names_cache_.end()) {
        char tmp[256];

        std::uint32_t cursor = static_cast<std::uint32_t>(
            std::snprintf(tmp, sizeof(tmp), "[tid-%" PRIu64 " ",
                          log_entry.get_log_head()
                              .log_thread_id));  // snprintf返回的是什么 %有什么用？占位嘛？占的谁？

        std::memcpy(tmp + cursor,
                    reinterpret_cast<const std::uint8_t*>(&ext_info) +
                        sizeof(qlog::record::RecordExtHeader),
                    ext_info.thread_name_len_);

        cursor += ext_info.thread_name_len_;

        tmp[cursor++] = ']';
        tmp[cursor++] = '\t';

        assert(cursor < 256U);

        tmp[cursor] = '\0';

        std::string thread_name = tmp;

        iter = thread_names_cache_
                   .emplace(log_entry.get_log_head().log_thread_id, std::move(thread_name))
                   .first;
    }

    if (!iter->second.empty()) {
        expand_format_content_buff_size(format_content_cursor_ +
                                        static_cast<std::uint32_t>(iter->second.size()));

        std::memcpy(format_content_.data() + format_content_cursor_, iter->second.c_str(),
                    iter->second.size());

        format_content_cursor_ += static_cast<std::uint32_t>(iter->second.size());
    }

    return enum_layout_result::finished;
}

Layout::enum_layout_result Layout::layout_prefix(const qlog::record::LogEntryHandle& log_entry) {
    const auto& head = log_entry.get_log_head();

    auto result = insert_time(log_entry);

    if (result != enum_layout_result::finished) {
        return result;
    }

    result = insert_thread_info(log_entry);

    if (result != enum_layout_result::finished) {
        return result;
    }

    const auto level = log_entry.get_level();

    if (level < qlog::LogLevel::verbose || level > qlog::LogLevel::fatal) {
        return enum_layout_result::parse_error;
    }

    const auto& level_str = kLevelText[static_cast<std::int32_t>(level)];

    insert_str_utf8(level_str, static_cast<std::uint32_t>(sizeof(level_str)));

    insert_char('\t');

    if (head.category_idx >= categories_name_array_ptr_->size()) {
        return enum_layout_result::parse_error;
    }

    const std::string* categories_str_ptr_ = &(*categories_name_array_ptr_)[head.category_idx];
    // CR:讲解一下这个 strptr

    if (!categories_str_ptr_->empty()) {
        insert_char('[');

        insert_str_utf8(categories_str_ptr_->c_str(),
                        static_cast<std::uint32_t>(categories_str_ptr_->size()));

        insert_str_utf8("]\t", 2U);
    }

    return enum_layout_result::finished;
}

Layout::enum_layout_result Layout::do_layout(
    const qlog::record::LogEntryHandle& log_entry, TimeZone& input_time_zone,
    const std::vector<std::string>* categories_name_array_ptr) {
    time_zone_ptr_ = &input_time_zone;
    categories_name_array_ptr_ = categories_name_array_ptr;

    format_content_cursor_ = 0;

    expand_format_content_buff_size(1024U);

    const auto result = layout_prefix(log_entry);

    if (result != enum_layout_result::finished) {
        return result;
    }

    python_style_format_content(log_entry);

    return enum_layout_result::finished;
}

}  // namespace qlog::layout