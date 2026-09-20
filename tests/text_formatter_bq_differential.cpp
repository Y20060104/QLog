// Optional validation against an external BQLog checkout; not a QLog dependency.
#include <array>
#include <bit>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "bq_common/bq_common.h"
#include "bq_log/log/layout.h"
#include "qlog/detail/text_formatter.hpp"

namespace {
using namespace qlog::detail;
DecodedArg scalar(ArgumentTag tag, std::uint64_t bits) {
    DecodedArg result{};
    result.tag = tag;
    result.value.bits = bits;
    return result;
}
void append_arg(std::vector<std::uint8_t>& wire, const DecodedArg& arg) {
    using T = bq::log_arg_type_enum;
    constexpr T tags[] = {T::unsupported_type, T::bool_type,    T::char_type,        T::int8_type,
                          T::uint8_type,       T::int16_type,   T::uint16_type,      T::int32_type,
                          T::uint32_type,      T::int64_type,   T::uint64_type,      T::float_type,
                          T::double_type,      T::pointer_type, T::string_utf8_type, T::null_type};
    constexpr unsigned widths[] = {0, 1, 1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 8, 0, 0};
    const auto tag = static_cast<unsigned>(arg.tag);
    const auto begin = wire.size();
    if (arg.tag == ArgumentTag::Utf8String) {
        wire.resize(begin + 8U + bq::align_4(arg.byte_count));
        wire[begin] = static_cast<std::uint8_t>(tags[tag]);
        std::memcpy(wire.data() + begin + 4U, &arg.byte_count, 4U);
        if (arg.byte_count) std::memcpy(wire.data() + begin + 8U, arg.value.bytes, arg.byte_count);
    } else {
        const auto width = widths[tag];
        const auto offset = width < 4U ? 2U : 4U;
        wire.resize(begin + (width < 4U ? 4U : 4U + width));
        wire[begin] = static_cast<std::uint8_t>(tags[tag]);
        if (width) std::memcpy(wire.data() + begin + offset, &arg.value.bits, width);
    }
}

unsigned checked = 0;
unsigned excluded = 0;
unsigned mismatches = 0;
void compare(const std::string& format, const std::vector<DecodedArg>& args) {
    std::array<std::byte, 65536> out{};
    const auto result =
        render_message_utf8(reinterpret_cast<const std::byte*>(format.data()), format.size(),
                            args.data(), args.size(), out.data(), out.size());
    // QLog's explicitly rejected unsafe states must never reach BQLog.
    if (result.failure) {
        ++excluded;
        return;
    }
    std::vector<std::uint8_t> wire(sizeof(bq::_log_entry_head_def) + bq::align_4(format.size()));
    std::memcpy(wire.data() + sizeof(bq::_log_entry_head_def), format.data(), format.size());
    for (const auto& arg : args) append_arg(wire, arg);
    const auto ext_offset = wire.size();
    wire.resize(wire.size() + sizeof(bq::_log_entry_ext_head_def));
    bq::_log_entry_head_def head{};
    head.log_format_str_type = static_cast<std::uint8_t>(bq::log_arg_type_enum::string_utf8_type);
    head.log_format_data_len = static_cast<std::uint32_t>(format.size());
    head.ext_info_offset = static_cast<std::uint32_t>(ext_offset);
    std::memcpy(wire.data(), &head, sizeof(head));
    const bq::log_entry_handle entry(wire.data(), static_cast<std::uint32_t>(wire.size()));
    static bq::layout reference;
    static bool warmed = false;
    if (!warmed) {
        // The body-only BQ test wrapper omits the normal log prefix/buffer
        // setup. Reserve enough through its public raw-copy path before wide
        // padding; fill_and_alignment itself does not grow the BQ buffer.
        std::vector<std::uint8_t> warm(sizeof(bq::_log_entry_head_def) + 4096U + 1U);
        bq::_log_entry_head_def warm_head{};
        warm_head.log_format_str_type =
            static_cast<std::uint8_t>(bq::log_arg_type_enum::string_utf8_type);
        warm_head.log_format_data_len = 4096U;
        warm_head.ext_info_offset = static_cast<std::uint32_t>(warm.size() - 1U);
        std::memcpy(warm.data(), &warm_head, sizeof(warm_head));
        const bq::log_entry_handle warm_entry(warm.data(), static_cast<std::uint32_t>(warm.size()));
        reference.test_python_style_format_content_simd(warm_entry);
        warmed = true;
    }
    reference.test_python_style_format_content_simd(entry);
    const std::string expected(reference.get_formated_str() ? reference.get_formated_str() : "",
                               reference.get_formated_str_len());
    const std::string actual(reinterpret_cast<const char*>(out.data()), result.size);
    ++checked;
    if (actual != expected) {
        if (mismatches++ < 20U) {
            std::cerr << "Mismatch format=" << format
                      << " tag=" << (args.empty() ? 0U : static_cast<unsigned>(args[0].tag))
                      << " bits="
                      << (args.empty() || args[0].tag == ArgumentTag::Utf8String
                              ? 0U
                              : args[0].value.bits)
                      << "\nBQ: [" << expected << "]\nQ:  [" << actual << "]\n";
        }
    }
}
}  // namespace

int main() {
    using namespace qlog::detail;
    std::vector<DecodedArg> values;
    for (unsigned tag = 1U; tag <= 13U; ++tag) {
        const auto type = static_cast<ArgumentTag>(tag);
        if (type == ArgumentTag::F32 || type == ArgumentTag::F64) continue;
        for (const auto bits : {0ULL, 1ULL, 42ULL, 0x8000000000000000ULL, ~0ULL}) {
            if (type == ArgumentTag::Bool && bits > 1U) continue;
            values.push_back(scalar(type, bits));
        }
    }
    for (double value : {-12345.875, -12.5, -0.5, -0.0, 0.0, 0.125, 1.5, 123.875, 0x1p40}) {
        values.push_back(scalar(ArgumentTag::F64, std::bit_cast<std::uint64_t>(value)));
        values.push_back(
            scalar(ArgumentTag::F32, std::bit_cast<std::uint32_t>(static_cast<float>(value))));
    }
    const std::array<std::string, 6> strings{"",    "abc",  "a-b",
                                             "a+x", "0xAB", std::string("a\0b", 3)};
    for (const auto& text : strings) {
        DecodedArg arg{};
        arg.tag = ArgumentTag::Utf8String;
        arg.byte_count = static_cast<std::uint32_t>(text.size());
        arg.value.bytes = reinterpret_cast<const std::byte*>(text.data());
        values.push_back(arg);
    }
    values.push_back(scalar(ArgumentTag::NullUtf8, 0));
    const std::vector<std::string> formats{"{}",
                                           "{name}",
                                           "{0}",
                                           "{:}",
                                           "{:d}",
                                           "{:x}",
                                           "{:X}",
                                           "{:b}",
                                           "{:B}",
                                           "{:o}",
                                           "{:#x}",
                                           "{:#X}",
                                           "{:#o}",
                                           "{:+}",
                                           "{:05}",
                                           "{:#08x}",
                                           "{:*>9}",
                                           "{:^9}",
                                           "{:<9}",
                                           "{:>>9}",
                                           "{:<>9}",
                                           "{:O9}",
                                           "{:D9}",
                                           "{:123}",
                                           "{:12dddddddddddd}",
                                           "{:.0}",
                                           "{:.2}",
                                           "{:.7f}",
                                           "{:.15F}",
                                           "{:.99f}",
                                           "{:8e}",
                                           "{:8E}",
                                           "{:8.2e}",
                                           "{:20.7e}",
                                           "prefix {:8.2E} suffix",
                                           "{{}}",
                                           "{} }} {} tail",
                                           "{abcdefghijklmnopqrs}",
                                           "{abcdefghijklmnopqrst}",
                                           "abc{bad{}}",
                                           "abc",
                                           "{{{{}}}}",
                                           "pre{}post"};
    for (const auto& format : formats) {
        compare(format, {});
        for (const auto& value : values) compare(format, {value});
    }
    std::mt19937_64 random(190919U);
    const std::string alphabet = "<>^+-#.0123456789xXbBoOdDfF";
    for (unsigned i = 0; i < 20000U; ++i) {
        std::string format = "pre {:";
        const auto length = random() % 16U;
        for (std::size_t j = 0; j < length; ++j) format += alphabet[random() % alphabet.size()];
        format += "} post }} {}";
        compare(format, {values[random() % values.size()], scalar(ArgumentTag::UInt64, random())});
    }
    std::cout << "checked=" << checked << " excluded_unsafe=" << excluded
              << " mismatches=" << mismatches << '\n';
    return mismatches == 0U ? 0 : 1;
}
