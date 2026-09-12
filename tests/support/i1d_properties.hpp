#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <qlog/detail/format_hash_reference.hpp>
#include <qlog/detail/record_decoder.hpp>
#include <qlog/detail/record_encoder.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "format_hash_test_access.hpp"

namespace i1d {
using namespace qlog::detail;
inline std::uint64_t case_seed = 0;
inline std::size_t case_index = 0, case_size = 0;
inline void require(bool condition) {
    if (!condition) {
        std::fprintf(stderr, "I1-D invariant failure seed=%llu case=%zu size=%zu\n",
                     static_cast<unsigned long long>(case_seed), case_index, case_size);
        std::abort();
    }
}
inline const RecordValidationPolicy policy{{~0ULL, ~0ULL, ~0ULL, ~0ULL}, true};
inline void decode(const std::uint8_t* input, std::size_t size) {
    // Exact allocation gives ASan a real boundary; offset exercises unaligned payloads.
    const std::size_t offset = size == 0 ? 0 : input[0] % 32U;
    std::vector<std::byte> storage(size + offset);
    if (size) std::memcpy(storage.data() + offset, input, size);
    const auto* p = size ? storage.data() + offset : nullptr;
    std::array<DecodedArg, 33> a{}, b{};
    a[32].value.bits = b[32].value.bits = 0xFEEDU;
    const auto x = decode_v1(p, size, a.data(), 32, policy);
    const auto y = decode_v1(p, size, b.data(), 32, policy);
    require(x.succeeded() == y.succeeded());
    require(a[32].value.bits == 0xFEEDU && b[32].value.bits == 0xFEEDU);
    if (!x.succeeded()) {
        require(x.record() == nullptr && x.failure() != nullptr);
        require(x.failure()->error == y.failure()->error);
        require(x.failure()->error_offset == y.failure()->error_offset);
        require(x.failure()->argument_index == y.failure()->argument_index);
        require(x.failure()->error_offset <= size);
        return;
    }
    const auto& h = x.record()->header();
    require(h.arg_count <= 32 && h.format_bytes <= 8192);
    require(32U + std::size_t(h.format_bytes) + h.args_bytes == size);
    require(x.record()->format_data() == p + 32);
    for (std::size_t i = 0; i < h.arg_count; ++i) {
        require(a[i].tag == b[i].tag && a[i].byte_count == b[i].byte_count);
        if (a[i].tag == ArgumentTag::Utf8String) {
            const auto address = reinterpret_cast<std::uintptr_t>(a[i].value.bytes);
            const auto begin = reinterpret_cast<std::uintptr_t>(p);
            require(address >= begin && address - begin <= size);
            require(a[i].byte_count <= size - (address - begin));
            require(a[i].value.bytes == b[i].value.bytes);
        } else {
            require(a[i].tag >= ArgumentTag::Bool && a[i].tag <= ArgumentTag::NullUtf8);
            require(a[i].byte_count == 0 && a[i].value.bits == b[i].value.bits);
        }
    }
}
inline void hash(const std::uint8_t* input, std::size_t size) {
    const auto* p = reinterpret_cast<const std::byte*>(input);
    const auto ref = format_hash_reference::hash_raw_ref(p, size);
    std::vector<std::byte> dest(size + 64, std::byte{0xA5});
    const auto check = [&](const FormatHashDispatch& d) {
        require(d.hash_raw_unchecked(p, size) == ref);
        require(d.copy_raw_unchecked(p, dest.data() + 31, size) == ref);
        if (size) require(std::memcmp(dest.data() + 31, p, size) == 0);
        for (std::size_t i = 0; i < 31; ++i) require(dest[i] == std::byte{0xA5});
        for (std::size_t i = 31 + size; i < dest.size(); ++i) require(dest[i] == std::byte{0xA5});
    };
    check(FormatHashTestAccess::software());
    check(FormatHashDispatch::automatic());
    if (auto hw = FormatHashTestAccess::hardware()) check(*hw);
}
inline void roundtrip(const std::uint8_t* input, std::size_t size) {
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < std::min(size, std::size_t{8}); ++i)
        bits |= std::uint64_t{input[i]} << (8U * i);
    const std::size_t fs = std::min(size, std::size_t{8192});
    const char* chars = size ? reinterpret_cast<const char*>(input) : "";
    const std::string_view value(chars, size);
    const bool flag = (bits & 1U) != 0;
    const auto n = static_cast<std::uint32_t>(bits);
    const double f = std::bit_cast<double>(bits);
    const auto m = measure_record({reinterpret_cast<const std::byte*>(chars), fs, 0}, 1024U * 1024U,
                                  flag, n, f, value);
    require(m.succeeded());
    require(m.prepared()->payload_size() == 32U + fs + 2U + 5U + 9U + 5U + size);
    std::vector<std::byte> out(m.prepared()->payload_size() + 32, std::byte{0xA5});
    const auto e = encode_v1(out.data() + 1, m.prepared()->payload_size(), *m.prepared(), {},
                             policy, FormatHashDispatch::automatic());
    require(e.succeeded() && e.bytes_written() == m.prepared()->payload_size());
    require(out[0] == std::byte{0xA5});
    for (std::size_t i = 1 + e.bytes_written(); i < out.size(); ++i)
        require(out[i] == std::byte{0xA5});
    std::array<DecodedArg, 32> a{};
    const auto d = decode_v1(out.data() + 1, e.bytes_written(), a.data(), 32, policy);
    require(d.succeeded());
    require(a[0].tag == ArgumentTag::Bool && a[0].value.bits == flag);
    require(a[1].tag == ArgumentTag::UInt32 && a[1].value.bits == n);
    require(a[2].tag == ArgumentTag::F64 && a[2].value.bits == bits);
    require(a[3].tag == ArgumentTag::Utf8String && a[3].byte_count == size);
    if (size) require(std::memcmp(a[3].value.bytes, input, size) == 0);
    const auto before = out;
    const auto fail = encode_v1(out.data() + 1, e.bytes_written() - 1, *m.prepared(), {}, policy,
                                FormatHashDispatch::automatic());
    require(!fail.succeeded() && fail.bytes_written() == 0 && out == before);
}
inline void cstr(const std::uint8_t* input, std::size_t size) {
    std::vector<char> text(size + 2, 'x');
    for (std::size_t i = 0; i < size; ++i) text[i] = static_cast<char>(input[i]);
    text[size] = '\0';
    std::size_t length = 0;
    while (length < size && text[length] != '\0') ++length;
    const auto m = measure_record({nullptr, 0, 0}, 1024U * 1024U, qlog::cstr(text.data()));
    require(m.succeeded() && m.prepared()->payload_size() == 37U + length);
    text[length] = 'z';  // A rescan would observe a longer string; storage remains valid.
    text[size + 1] = '\0';
    std::vector<std::byte> out(m.prepared()->payload_size());
    const auto e = encode_v1(out.data(), out.size(), *m.prepared(), {}, policy,
                             FormatHashDispatch::automatic());
    require(e.succeeded());
    std::array<DecodedArg, 32> a{};
    const auto d = decode_v1(out.data(), out.size(), a.data(), 32, policy);
    require(d.succeeded() && a[0].byte_count == length);
    if (length) require(std::memcmp(a[0].value.bytes, text.data(), length) == 0);
}
}  // namespace i1d
