/*
 * Copyright (c) 2026 QLog Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "qlog/layout/layout.h"
#include "qlog/serialization/entry_format.h"
#include "qlog/serialization/serializer.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

using qlog::layout::layout;
using qlog::serialization::entry_header;
using qlog::serialization::log_level;
using qlog::serialization::serializer;

struct TestResult
{
    int passed = 0;
    int failed = 0;
};

static TestResult g_res;

#define CHECK(cond, msg)                                                 \
    do                                                                   \
    {                                                                    \
        if (!(cond))                                                     \
        {                                                                \
            std::cerr << "  FAIL [" << __LINE__ << "]: " << msg << "\n"; \
            g_res.failed++;                                              \
        }                                                                \
        else                                                             \
        {                                                                \
            std::cout << "  PASS: " << msg << "\n";                      \
            g_res.passed++;                                              \
        }                                                                \
    } while (0)

static bool ends_with(std::string_view s, std::string_view suffix)
{
    if (s.size() < suffix.size())
        return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static bool has_level_tag(std::string_view s, char level)
{
    const char tag[3] = {'[', level, ']'};
    return s.find(std::string_view{tag, sizeof(tag)}) != std::string_view::npos;
}

static std::string with_prefix(std::string_view inner)
{
    std::string out;
    out.reserve(1 + inner.size() + 1);
    out.push_back(' ');
    out.append(inner.data(), inner.size());
    out.push_back('\n');
    return out;
}

class EntryBuilder
{
public:
    explicit EntryBuilder(log_level level = log_level::info, uint16_t category_idx = 0)
    {
        entry_header hdr{};
        hdr.timestamp_ms = 0;
        hdr.thread_id = 1;
        hdr.fmt_hash = 0;
        hdr.category_idx = category_idx;
        hdr.level = static_cast<uint8_t>(level);
        hdr.reserved = 0;
        size_ = serializer::write_header(buf_.data(), hdr);
    }

    template<typename T> void write(T v)
    {
        size_ += serializer::write(buf_.data() + size_, v);
    }

    void write_utf16(std::u16string_view sv)
    {
        size_ += serializer::write_utf16(buf_.data() + size_, sv);
    }

    void write_utf32(std::u32string_view sv)
    {
        size_ += serializer::write_utf32(buf_.data() + size_, sv);
    }

    const uint8_t* data() const
    {
        return buf_.data();
    }

    uint32_t size() const
    {
        return static_cast<uint32_t>(size_);
    }

private:
    std::array<uint8_t, 1024> buf_{};
    size_t size_ = 0;
};

static std::string_view run_layout(layout& lay, const EntryBuilder& entry, std::string_view fmt)
{
    return lay.do_layout(entry.data(), entry.size(), fmt);
}

static void test_basic_integer_format()
{
    std::cout << "\n[Test 1] Basic integer format\n";
    layout lay;
    EntryBuilder entry(log_level::info);
    entry.write(int32_t{42});

    const std::string_view out = run_layout(lay, entry, "value={0}");
    CHECK(has_level_tag(out, 'I'), "level tag is [I]");
    CHECK(ends_with(out, with_prefix("value=42")), "format value=42");
}

static void test_float_precision()
{
    std::cout << "\n[Test 2] Float precision\n";
    layout lay;

    EntryBuilder entry1(log_level::info);
    entry1.write(double{3.14159});
    auto out = run_layout(lay, entry1, "|{0:.2f}|");
    CHECK(ends_with(out, with_prefix("|3.14|")), "fixed precision .2f");

    EntryBuilder entry2(log_level::info);
    entry2.write(double{12345.6});
    out = run_layout(lay, entry2, "|{0:10.4e}|");
    CHECK(ends_with(out, with_prefix("|1.2346e+04|")), "scientific width 10.4e");
}

static void test_string_alignment()
{
    std::cout << "\n[Test 3] String alignment\n";
    layout lay;
    EntryBuilder entry(log_level::info);
    entry.write("hello");

    auto out = run_layout(lay, entry, "|{0:<10}|");
    CHECK(ends_with(out, with_prefix("|hello     |")), "left align <10");

    out = run_layout(lay, entry, "|{0:>10}|");
    CHECK(ends_with(out, with_prefix("|     hello|")), "right align >10");

    out = run_layout(lay, entry, "|{0:^10}|");
    CHECK(ends_with(out, with_prefix("|  hello   |")), "center align ^10");
}

static void test_integer_bases()
{
    std::cout << "\n[Test 4] Integer bases\n";
    layout lay;

    EntryBuilder entry_hex(log_level::info);
    entry_hex.write(uint32_t{255});
    auto out = run_layout(lay, entry_hex, "|{0:#010x}|");
    CHECK(ends_with(out, with_prefix("|0x000000ff|")), "hex alt zero pad");

    EntryBuilder entry_bin(log_level::info);
    entry_bin.write(uint32_t{10});
    out = run_layout(lay, entry_bin, "|{0:b}|");
    CHECK(ends_with(out, with_prefix("|1010|")), "binary base");

    EntryBuilder entry_oct(log_level::info);
    entry_oct.write(uint32_t{8});
    out = run_layout(lay, entry_oct, "|{0:#o}|");
    CHECK(ends_with(out, with_prefix("|10|")), "octal alt form");
}

static void test_sign_control()
{
    std::cout << "\n[Test 5] Sign control\n";
    layout lay;

    EntryBuilder entry_pos(log_level::info);
    entry_pos.write(int32_t{42});
    auto out = run_layout(lay, entry_pos, "|{0:+d}|");
    CHECK(ends_with(out, with_prefix("|+42|")), "sign + for positive");

    EntryBuilder entry_space(log_level::info);
    entry_space.write(int32_t{42});
    out = run_layout(lay, entry_space, "|{0: d}|");
    CHECK(ends_with(out, with_prefix("| 42|")), "sign space for positive");

    EntryBuilder entry_neg(log_level::info);
    entry_neg.write(int32_t{-1});
    out = run_layout(lay, entry_neg, "|{0:d}|");
    CHECK(ends_with(out, with_prefix("|-1|")), "negative sign default");
}

static void test_zero_padding()
{
    std::cout << "\n[Test 6] Zero padding\n";
    layout lay;

    EntryBuilder entry(log_level::info);
    entry.write(int32_t{42});
    const auto out = run_layout(lay, entry, "|{0:08d}|");
    CHECK(ends_with(out, with_prefix("|00000042|")), "zero pad width 8");
}

static void test_param_reorder()
{
    std::cout << "\n[Test 7] Parameter reorder\n";
    layout lay;

    EntryBuilder entry(log_level::info);
    entry.write("A");
    entry.write("B");
    const auto out = run_layout(lay, entry, "{1} before {0}");
    CHECK(ends_with(out, with_prefix("B before A")), "reordered parameters");
}

static void test_escape_braces()
{
    std::cout << "\n[Test 8] Escaped braces\n";
    layout lay;

    EntryBuilder entry(log_level::info);
    const auto out = run_layout(lay, entry, "{{literal braces}}");
    CHECK(ends_with(out, with_prefix("{literal braces}")), "literal braces output");
}

static void test_buffer_reuse()
{
    std::cout << "\n[Test 9] Buffer reuse\n";
    layout lay;

    EntryBuilder entry(log_level::info);
    entry.write(int32_t{7});

    std::string_view out;
    for (int i = 0; i < 10000; ++i)
    {
        out = run_layout(lay, entry, "value={0}");
    }
    CHECK(ends_with(out, with_prefix("value=7")), "repeated do_layout 10000x");
}

static void test_bool_and_pointer()
{
    std::cout << "\n[Test 10] Bool and pointer\n";
    layout lay;

    EntryBuilder entry_bool(log_level::info);
    entry_bool.write(true);
    auto out = run_layout(lay, entry_bool, "{0}");
    CHECK(ends_with(out, with_prefix("TRUE")), "bool true as TRUE");

    EntryBuilder entry_ptr(log_level::info);
    entry_ptr.write(static_cast<const void*>(nullptr));
    out = run_layout(lay, entry_ptr, "{0}");
    CHECK(ends_with(out, with_prefix("null")), "null pointer as null");
}

static void test_layout_performance()
{
    std::cout << "\n[Test 11] Layout performance\n";
    layout lay;

    EntryBuilder entry(log_level::info);
    entry.write(int32_t{123});

    constexpr int kWarmup = 1000;
    constexpr int kIter = 200000;
    volatile size_t sink = 0;

    for (int i = 0; i < kWarmup; ++i)
    {
        const auto out = run_layout(lay, entry, "{0}");
        sink += out.size();
    }

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIter; ++i)
    {
        const auto out = run_layout(lay, entry, "{0}");
        sink += out.size();
    }
    const auto t1 = std::chrono::steady_clock::now();

    const double us_per_op =
        std::chrono::duration<double, std::micro>(t1 - t0).count() / static_cast<double>(kIter);
    std::cout << "  do_layout avg: " << us_per_op << " us/op\n";
    CHECK(us_per_op < 1.0, "do_layout < 1us");

    (void)sink;
}

static void test_utf16_basic()
{
    std::cout << "\n[Test UTF-16] Basic BMP\n";
    layout lay;

    EntryBuilder entry(log_level::info);
    std::u16string s = u"Hello";
    entry.write_utf16(s);

    const auto out = run_layout(lay, entry, "{0}");
    CHECK(ends_with(out, with_prefix("Hello")), "utf16 basic hello");
}

static void test_utf16_surrogate_pair()
{
    std::cout << "\n[Test UTF-16] Surrogate pair\n";
    layout lay;

    EntryBuilder entry(log_level::info);
    const char16_t emoji[] = {0xD83D, 0xDE00};
    entry.write_utf16(std::u16string_view{emoji, 2});

    const std::string inner(std::string("\xF0\x9F\x98\x80", 4));
    const auto out = run_layout(lay, entry, "{0}");
    CHECK(ends_with(out, with_prefix(inner)), "utf16 surrogate pair -> utf8");
}

static void test_utf32_basic()
{
    std::cout << "\n[Test UTF-32] Basic\n";
    layout lay;

    EntryBuilder entry(log_level::info);
    const char32_t jp[] = {0x65E5, 0x672C, 0x8A9E};
    entry.write_utf32(std::u32string_view{jp, 3});

    const std::string inner(std::string("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", 9));
    const auto out = run_layout(lay, entry, "{0}");
    CHECK(ends_with(out, with_prefix(inner)), "utf32 -> utf8");
}


int main()
{
    std::cout << "==============================\n";
    std::cout << " QLog Layout Tests (M4)\n";
    std::cout << "==============================\n";

    test_basic_integer_format();
    test_float_precision();
    test_string_alignment();
    test_integer_bases();
    test_sign_control();
    test_zero_padding();
    test_param_reorder();
    test_escape_braces();
    test_buffer_reuse();
    test_bool_and_pointer();
    test_layout_performance();
    test_utf16_basic();
    test_utf16_surrogate_pair();
    test_utf32_basic();

    std::cout << "\nPASSED: " << g_res.passed << "\n";
    std::cout << "FAILED: " << g_res.failed << "\n";
    return g_res.failed == 0 ? 0 : 1;
}
