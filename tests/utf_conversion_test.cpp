#include "qlog/utility/utf_conversion.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void converts(std::u16string_view source, std::string_view expected) {
    constexpr char sentinel = 'Z';
    std::vector<char> output(expected.size() + 2U, sentinel);
    char* const dst = output.data() + 1U;
    const auto written =
        qlog::utility::utf16_to_utf8_sw(source.data(), static_cast<std::uint32_t>(source.size()),
                                        dst, static_cast<std::uint32_t>(expected.size()));

    check(written == expected.size(), "UTF-8 byte count");
    check(std::string_view(dst, written) == expected, "UTF-8 content");
    check(output.front() == sentinel, "write before output");
    check(output.back() == sentinel, "write past output or append terminator");
}
}  // namespace

int main() {
    try {
        check(qlog::utility::utf16_to_utf8_sw(nullptr, 0, nullptr, 0) == 0, "empty null input");
        converts(u"", "");
        converts(u"ABC", "ABC");
        converts(std::u16string_view(u"A\0B", 3), std::string_view("A\0B", 3));
        converts(u"{}A", "{}A");
        converts(u"\u00A2\u20AC", "\xC2\xA2\xE2\x82\xAC");
        converts(u"A\u4E2DB\U0001F600C",
                 "A\xE4\xB8\xAD"
                 "B\xF0\x9F\x98\x80"
                 "C");
        converts(u"\U00010000\U0010FFFF", "\xF0\x90\x80\x80\xF4\x8F\xBF\xBF");
        converts(std::u16string{char16_t{0xDC00}, u'A'}, "A");
        converts(std::u16string{u'A', char16_t{0xD800}}, "A");
        converts(std::u16string{char16_t{0xD800}, u'A'}, "A");
        converts(std::u16string{char16_t{0xD800}, char16_t{0xD800}, char16_t{0xDC00}},
                 "\xF0\x90\x80\x80");
        converts(std::u16string{char16_t{0xD800}, char16_t{0xDC00}, char16_t{0xDC00}},
                 "\xF0\x90\x80\x80");
        converts(std::u16string{char16_t{0xD800}}, "");
        converts(std::u16string{char16_t{0xDC00}}, "");
        const std::u16string long_ascii(4096, u'A');
        const std::string expected_ascii(4096, 'A');
        converts(long_ascii, expected_ascii);
        std::cout << "UTF-16 software conversion: 16 cases passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
