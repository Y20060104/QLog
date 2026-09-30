#include "qlog/layout/layout.hpp"

#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

#include "qlog/log_level.hpp"
#include "qlog/record/argument_tag.hpp"
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

void Layout::expand_format_content_buff_size(std::uint32_t required_size) {}

}  // namespace qlog::layout