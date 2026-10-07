#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "qlog/layout/layout.hpp"
#include "qlog/record/argument_serialization.hpp"
#include "qlog/record/argument_tag.hpp"

namespace {
using qlog::layout::Layout;
using qlog::layout::TimeZone;
using qlog::record::ArgumentTag;
using qlog::record::RecordHeader;
using Result = Layout::enum_layout_result;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void equal(const std::string& actual, const std::string& expected, const char* name) {
    if (actual != expected) {
        std::cerr << name << " expected(" << expected.size() << "): " << expected << " actual("
                  << actual.size() << "): " << actual << '\n';
        throw std::runtime_error(name);
    }
}
std::size_t align4(std::size_t n) {
    return (n + 3U) & ~std::size_t{3};
}
std::string bytes(std::u16string_view value) {
    return {reinterpret_cast<const char*>(value.data()), value.size() * sizeof(char16_t)};
}

// Independent wire fixtures: do not use the production serializer to calculate offsets.
struct Args {
    std::vector<std::uint8_t> data;
    template <class T>
    void pod(ArgumentTag tag, T value) {
        const std::size_t offset = sizeof(T) <= 2 ? 2U : 4U;
        const std::size_t start = data.size();
        data.resize(start + align4(offset + sizeof(T)), 0);
        data[start] = static_cast<std::uint8_t>(tag);
        std::memcpy(data.data() + start + offset, &value, sizeof(value));
    }
    void null() {
        const auto start = data.size();
        data.resize(start + 4U, 0);
        data[start] = static_cast<std::uint8_t>(ArgumentTag::null_type);
    }
    void string(ArgumentTag tag, std::string_view value) {
        const auto start = data.size();
        data.resize(start + 8U + align4(value.size()), 0);
        data[start] = static_cast<std::uint8_t>(tag);
        const auto len = static_cast<std::uint32_t>(value.size());
        std::memcpy(data.data() + start + 4U, &len, sizeof(len));
        if (!value.empty()) std::memcpy(data.data() + start + 8U, value.data(), value.size());
    }
};
struct StorageDelete {
    void operator()(void* p) const {
        ::operator delete(p, std::align_val_t{alignof(RecordHeader)});
    }
};
struct Record {
    std::unique_ptr<void, StorageDelete> storage;
    std::uint32_t size;
    Record(std::string_view format, ArgumentTag tag, const Args& args = {}, std::uint8_t level = 2,
           std::uint32_t category = 0, std::uint64_t tid = 42, std::string_view name = "worker",
           std::uint64_t epoch = 1123)
        : storage(nullptr), size(0) {
        check(name.size() <= 100, "fixture thread name bound");
        const auto ext = sizeof(RecordHeader) + align4(format.size()) + args.data.size();
        size = static_cast<std::uint32_t>(ext + 1U + name.size());
        storage.reset(::operator new(size, std::align_val_t{alignof(RecordHeader)}));
        auto* raw = static_cast<std::uint8_t*>(storage.get());
        std::memset(raw, 0, size);
        auto* head = std::construct_at(reinterpret_cast<RecordHeader*>(raw));
        head->timestamp_epoch = epoch;
        head->ext_info_offset = static_cast<std::uint32_t>(ext);
        head->category_idx = category;
        head->log_thread_id = tid;
        head->log_format_str_type = static_cast<std::uint8_t>(tag);
        head->level = level;
        head->log_format_data_len = static_cast<std::uint32_t>(format.size());
        if (!format.empty()) std::memcpy(raw + sizeof(RecordHeader), format.data(), format.size());
        if (!args.data.empty())
            std::memcpy(raw + sizeof(RecordHeader) + align4(format.size()), args.data.data(),
                        args.data.size());
        raw[ext] = static_cast<std::uint8_t>(name.size());
        if (!name.empty()) std::memcpy(raw + ext + 1U, name.data(), name.size());
    }
    qlog::record::LogEntryHandle view() const {
        return {static_cast<const std::uint8_t*>(storage.get()), size};
    }
};
const std::string time_prefix = "UTC0 1970-01-01 00:00:01.123";
const std::string thread_prefix = "[tid-42 worker]\t";
const std::string prefix = time_prefix + thread_prefix + "[I]\t[core]\t";
std::string output(Layout& layout) {
    return {layout.get_formated_str(), layout.get_formated_str_len()};
}
std::string render(Layout& layout, TimeZone& zone, const Record& record,
                   const std::vector<std::string>& categories) {
    check(record.view().validate(), "fixture structural validation");
    check(layout.do_layout(record.view(), zone, &categories) == Result::finished,
          "layout finished");
    check(layout.get_formated_str()[layout.get_formated_str_len()] == '\0', "trailing NUL");
    return output(layout);
}
void body(std::string_view format, const Args& args, std::string_view expected, bool u16 = false) {
    Layout layout;
    TimeZone zone(false, 0, 0, 0, "UTC0");
    const std::vector<std::string> categories{"core"};
    Record record(format, u16 ? ArgumentTag::string_utf16_type : ArgumentTag::string_utf8_type,
                  args);
    equal(render(layout, zone, record, categories), prefix + std::string(expected), "body");
}
// Integration path: production measurement and serialization feed the same public Layout API.
template <class... Ts>
Args serialized(const Ts&... values) {
    const auto sizes = qlog::record::detail::make_size_seq<true>(values...);
    const auto total = sizes.get_total();
    std::vector<std::uint8_t> guarded(total + 32U, 0xA5);
    qlog::record::detail::fill_arguments(guarded.data() + 16U, sizes, values...);
    for (std::size_t i = 0; i < 16U; ++i) {
        check(guarded[i] == 0xA5 && guarded[16U + total + i] == 0xA5,
              "serializer exceeded measured size");
    }
    Args result;
    result.data.assign(guarded.begin() + 16, guarded.end() - 16);
    return result;
}
void serialized_mixed_cases() {
    const auto a = serialized(
        std::int8_t{-7}, std::uint8_t{250}, std::int16_t{-32000}, std::uint16_t{65000},
        std::int32_t{-123456}, std::uint32_t{4000000000U}, std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::uint64_t>::max(), true, false, 'Q', u'\u4E2D', U'\U0001F600',
        nullptr, static_cast<void*>(nullptr), 1.25F, -2.5);
    const std::string expected =
        "-7|250|-32000|65000|-123456|4000000000|-9223372036854775808|18446744073709551615|TRUE|"
        "FALSE|Q|\xE4\xB8\xAD|\xF0\x9F\x98\x80|null|null|1.25|-2.50";
    body("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{:.2f}|{:.2f}", a, expected);
    body(bytes(u"{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{:.2f}|{:.2f}"), a, expected, true);
    int object = 0;
    const auto pointer = serialized(&object);
    check(pointer.data.size() == 12, "serialized pointer slot size");
    std::uint64_t stored = 0;
    std::memcpy(&stored, pointer.data.data() + 4, sizeof(stored));
    check(stored == static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&object)),
          "serialized pointer value");
    Args manual;
    manual.pod(ArgumentTag::pointer_type, stored);
    check(pointer.data[0] == static_cast<std::uint8_t>(ArgumentTag::pointer_type), "pointer tag");
    Layout first, second;
    TimeZone zone(false, 0, 0, 0, "UTC0");
    const std::vector<std::string> categories{"core"};
    Record one("{}", ArgumentTag::string_utf8_type, pointer);
    Record two("{}", ArgumentTag::string_utf8_type, manual);
    equal(render(first, zone, one, categories), render(second, zone, two, categories),
          "pointer wire integration");
    body("no args }}", serialized(), "no args }}");
}
void serialized_string_cases() {
    const std::string embedded("a\0b", 3);
    const std::u16string wide = u"\u4E2D\U0001F600";
    const std::u32string wide32 = U"\u4E2D\U0001F600";
    const char* null_text = nullptr;
    const auto a = serialized(embedded, std::string_view("view"), wide, std::u16string_view(wide),
                              wide32, std::u32string_view(wide32), null_text);
    const std::string unicode = "\xE4\xB8\xAD\xF0\x9F\x98\x80";
    const std::string expected =
        embedded + "|view|" + unicode + "|" + unicode + "|" + unicode + "|" + unicode + "|null";
    body("{}|{}|{}|{}|{}|{}|{}", a, expected);
    body(bytes(u"{}|{}|{}|{}|{}|{}|{}"), a, expected, true);
    const auto utf32 = serialized(wide32);
    check(utf32.data[0] == static_cast<std::uint8_t>(ArgumentTag::string_utf16_type),
          "UTF32 strings stored as UTF16");
    std::uint32_t length = 0;
    std::memcpy(&length, utf32.data.data() + 4, sizeof(length));
    check(length == 6, "UTF32 string measured UTF16 byte length");
    for (std::size_t n = 0; n <= 65; ++n) {
        const std::string text(n, 'x');
        const std::u16string text16(n, u'\u4E2D');
        const std::string chinese = "\xE4\xB8\xAD";
        std::string converted;
        for (std::size_t i = 0; i < n; ++i) converted += chinese;
        const auto pack =
            serialized(text, std::uint64_t{1234567890123456789ULL}, text16, std::int8_t{-3});
        const auto want = text + "|1234567890123456789|" + converted + "|-3";
        body("{}|{}|{}|{}", pack, want);
        body(bytes(u"{}|{}|{}|{}"), pack, want, true);
    }
    const std::string large(10000, 'z');
    body("{}:{}", serialized(large, 9), large + ":9");
    body("{:^7}|{:04d}", serialized(std::string("AB"), 7), "   AB  |0007");
}
struct MemberChars {
    std::string text;
    std::size_t log_format_str_size() const {
        return text.size();
    }
    const char* log_format_str_chars() const {
        return text.data();
    }
};
struct MemberCallback {
    std::u16string text;
    mutable std::size_t calls = 0;
    std::size_t log_format_str_size() const {
        return text.size();
    }
    void log_custom_format(char16_t* dst, std::size_t count) const {
        check(count == text.size() * sizeof(char16_t), "member callback receives bytes");
        ++calls;
        std::memcpy(dst, text.data(), count);
    }
};
namespace adl_fixture {
struct Chars {
    std::u16string text;
};
std::size_t log_format_str_size(const Chars& v) {
    return v.text.size();
}
const char16_t* log_format_str_chars(const Chars& v) {
    return v.text.data();
}
struct Callback {
    std::string text;
    mutable std::size_t calls = 0;
};
std::size_t log_format_str_size(const Callback& v) {
    return v.text.size();
}
void log_custom_format(const Callback& v, char* dst, std::size_t count) {
    check(count == v.text.size(), "ADL callback receives bytes");
    ++v.calls;
    std::memcpy(dst, v.text.data(), count);
}
}  // namespace adl_fixture
void serialized_custom_cases() {
    const MemberChars member{std::string("m\0n", 3)};
    const MemberCallback callback{u"\u4E2D\U0001F600"};
    const adl_fixture::Chars adl{u"\u6587"};
    const adl_fixture::Callback adl_callback{"adl"};
    const auto args = serialized(member, callback, adl, adl_callback, std::uint32_t{99});
    check(callback.calls == 1 && adl_callback.calls == 1, "callbacks called once at fill");
    const auto expected = member.text + "|\xE4\xB8\xAD\xF0\x9F\x98\x80|\xE6\x96\x87|adl|99";
    body("{}|{}|{}|{}|{}", args, expected);
    body(bytes(u"{}|{}|{}|{}|{}"), args, expected, true);
    check(callback.calls == 1 && adl_callback.calls == 1,
          "Layout does not call custom formatter again");
    const MemberCallback empty{u""};
    body("{}:{}", serialized(empty, 1), ":1");
    check(empty.calls == 0, "zero byte custom callback skipped");
}
void prefix_cases() {
    Layout layout;
    TimeZone zone(false, 0, 0, 0, "UTC0");
    const std::vector<std::string> categories{"core", ""};
    const char* levels[] = {"[V]", "[D]", "[I]", "[W]", "[E]", "[F]"};
    for (std::uint8_t level = 0; level < 6; ++level) {
        Record r("hello", ArgumentTag::string_utf8_type, {}, level);
        equal(render(layout, zone, r, categories),
              time_prefix + thread_prefix + levels[level] + "\t[core]\thello", "level prefix");
    }
    Record empty("", ArgumentTag::string_utf8_type, {}, 2, 1);
    equal(render(layout, zone, empty, categories), time_prefix + thread_prefix + "[I]\t",
          "empty category/body");
    const std::vector<std::string> replacement{"other"};
    Record r("x", ArgumentTag::string_utf8_type);
    equal(render(layout, zone, r, replacement), time_prefix + thread_prefix + "[I]\t[other]\tx",
          "rebind categories");
    Record later("x", ArgumentTag::string_utf8_type, {}, 2, 0, 42, "worker", 2007);
    equal(render(layout, zone, later, categories),
          "UTC0 1970-01-01 00:00:02.007" + thread_prefix + "[I]\t[core]\tx", "time refresh");
}
void scanner_cases() {
    Args a;
    a.pod(ArgumentTag::int32_type, std::int32_t{7});
    body("{} }}", {}, "{} }}");
    body("x{}y}}z", a, "x7y}z");
    body("{} {}", a, "7 {}");
    body("tail{", a, "tail{");
    body("single}", a, "single}");
    body("{{}", a, "{7");
    body("{2}", a, "7");
    body("{abcdefghijklmnopqrst}", a, "{abcdefghijklmnopqrst}");
    body(bytes(u"{} }}"), {}, "{} }", true);
    body(bytes(u"before{}after"), a, "before7after", true);
    body(bytes(u"before{:04d}after"), a, "before0007after", true);
    body(std::string("a\0b", 3), {}, std::string("a\0b", 3));
}
void numeric_cases() {
    Args a;
    a.pod(ArgumentTag::int8_type, std::int8_t{-1});
    a.pod(ArgumentTag::uint8_type, std::uint8_t{255});
    a.pod(ArgumentTag::int16_type, std::int16_t{-32768});
    a.pod(ArgumentTag::uint16_type, std::uint16_t{65535});
    a.pod(ArgumentTag::int32_type, std::numeric_limits<std::int32_t>::min());
    a.pod(ArgumentTag::uint32_type, std::numeric_limits<std::uint32_t>::max());
    a.pod(ArgumentTag::int64_type, std::numeric_limits<std::int64_t>::min());
    a.pod(ArgumentTag::uint64_type, std::numeric_limits<std::uint64_t>::max());
    const std::string expected =
        "-1|255|-32768|65535|-2147483648|4294967295|-9223372036854775808|18446744073709551615";
    body("{}|{}|{}|{}|{}|{}|{}|{}", a, expected);
    body(bytes(u"{}|{}|{}|{}|{}|{}|{}|{}"), a, expected, true);
    Args f;
    f.pod(ArgumentTag::float_type, 1.25F);
    f.pod(ArgumentTag::double_type, -2.5);
    body("{:.2f}|{:.2f}", f, "1.25|-2.50");
    body(bytes(u"{:.2f}|{:.2f}"), f, "1.25|-2.50", true);
    Args zero;
    zero.pod(ArgumentTag::int32_type, std::int32_t{0});
    zero.pod(ArgumentTag::uint8_type, std::uint8_t{0});
    body("{:+d}|{:+d}", zero, "+0|0");
    body(bytes(u"{:+d}|{:+d}"), zero, "+0|0", true);
    Args hex;
    hex.pod(ArgumentTag::uint32_type, std::uint32_t{42});
    body("{:#08X}", hex, "0X00002A");
    Args text;
    text.string(ArgumentTag::string_utf8_type, "AB");
    body("{:^7}", text, "   AB  ");
}
void argument_cases() {
    Args a;
    a.null();
    a.pod(ArgumentTag::pointer_type, std::uint64_t{0x1234});
    a.pod(ArgumentTag::pointer_type, std::uint64_t{0});
    a.pod(ArgumentTag::bool_type, true);
    a.pod(ArgumentTag::bool_type, false);
    a.pod(ArgumentTag::char_type, 'A');
    a.pod(ArgumentTag::char16_type, u'\u4E2D');
    a.pod(ArgumentTag::char32_type, U'\U0001F600');
    a.string(ArgumentTag::string_utf8_type, std::string("a\0b", 3));
    a.string(ArgumentTag::string_utf16_type, bytes(u"\u6587\U0001F600"));
    const std::string expected =
        std::string("null|0x1234|null|TRUE|FALSE|A|\xE4\xB8\xAD|\xF0\x9F\x98\x80|a") + '\0' +
        "b|\xE6\x96\x87\xF0\x9F\x98\x80";
    body("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}", a, expected);
    body(bytes(u"{}|{}|{}|{}|{}|{}|{}|{}|{}|{}"), a, expected, true);
    body(bytes(u"A\u00E9\u4E2D\U0001F600Z"), {}, "A\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80Z", true);
    const char16_t malformed[] = {0xD800, u'X', 0xDC00, u'Y'};
    body(bytes(std::u16string_view(malformed, 4)), {}, "XY", true);
}
void failure_cases() {
    Layout layout;
    TimeZone zone(false, 0, 0, 0, "UTC0");
    const std::vector<std::string> categories{"core"};
    Record level("body", ArgumentTag::string_utf8_type, {}, 6);
    check(layout.do_layout(level.view(), zone, &categories) == Result::parse_error, "bad level");
    equal(output(layout), time_prefix + thread_prefix, "partial prefix bad level");
    Record category("body", ArgumentTag::string_utf8_type, {}, 2, 9);
    check(layout.do_layout(category.view(), zone, &categories) == Result::parse_error,
          "bad category");
    equal(output(layout), time_prefix + thread_prefix + "[I]\t", "partial prefix bad category");
    Record good("ok", ArgumentTag::string_utf8_type);
    equal(render(layout, zone, good, categories), prefix + "ok", "reuse after failure");
}
void reuse_cases() {
    Layout layout;
    TimeZone zone(false, 0, 0, 0, "UTC0");
    const std::vector<std::string> categories{"core"};
    const std::string large(10000, 'x');
    Record big(large, ArgumentTag::string_utf8_type);
    equal(render(layout, zone, big, categories), prefix + large, "buffer growth");
    layout.tidy_memory();
    check(layout.get_formated_str_len() == 0, "tidy length");
    Record renamed("short", ArgumentTag::string_utf8_type, {}, 2, 0, 42, "renamed");
    equal(render(layout, zone, renamed, categories), prefix + "short",
          "thread cache survives tidy");
    Record other("x", ArgumentTag::string_utf8_type, {}, 2, 0, 43, "new");
    equal(render(layout, zone, other, categories), time_prefix + "[tid-43 new]\t[I]\t[core]\tx",
          "new thread");
    TimeZone offset(false, 1, 0, 3600000, "UTC+1");
    equal(render(layout, offset, renamed, categories),
          "UTC+1 1970-01-01 01:00:01.123" + thread_prefix + "[I]\t[core]\tshort",
          "rebind timezone");
}
}  // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "expected test group");
        const std::string_view group = argv[1];
        if (group == "serialization_mixed")
            serialized_mixed_cases();
        else if (group == "serialization_strings")
            serialized_string_cases();
        else if (group == "serialization_custom")
            serialized_custom_cases();
        else if (group == "prefix")
            prefix_cases();
        else if (group == "scanner")
            scanner_cases();
        else if (group == "numeric")
            numeric_cases();
        else if (group == "arguments")
            argument_cases();
        else if (group == "failure")
            failure_cases();
        else if (group == "reuse")
            reuse_cases();
        else
            throw std::runtime_error("unknown group");
        std::cout << "PASS " << group << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
