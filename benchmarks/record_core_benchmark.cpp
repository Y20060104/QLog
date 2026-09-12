#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <qlog/detail/format_hash_reference.hpp>
#include <qlog/detail/record_decoder.hpp>
#include <qlog/detail/record_encoder.hpp>
#include <string>
#include <vector>

#include "format_hash_test_access.hpp"
namespace {
using namespace qlog::detail;
bool latency_mode = false;
const RecordValidationPolicy policy{{~0ULL, ~0ULL, ~0ULL, ~0ULL}, true};
template <class T>
inline void consume(const T& value) {
    asm volatile("" : : "g"(&value) : "memory");
}
template <class Fn>
void bench(const char* name, const char* backend, std::size_t bytes, Fn fn) {
    const std::size_t iterations = latency_mode ? 128U : 20000U;
    for (int i = 0; i < 100; ++i) fn();
    std::vector<double> samples(latency_mode ? 101U : 9U);
    for (auto& sample : samples) {
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) fn();
        const auto end = std::chrono::steady_clock::now();
        sample = std::chrono::duration<double, std::nano>(end - start).count() /
                 static_cast<double>(iterations);
    }
    std::sort(samples.begin(), samples.end());
    const auto median = samples[samples.size() / 2];
    std::vector<double> deviations(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i)
        deviations[i] = samples[i] > median ? samples[i] - median : median - samples[i];
    std::sort(deviations.begin(), deviations.end());
    std::printf("%s,%s,%zu,%.3f,%.3f,%.3f,%.3f\n", name, backend, bytes, median,
                deviations[deviations.size() / 2], static_cast<double>(bytes) * 1e9 / median,
                samples[(samples.size() - 1) * 99 / 100]);
}
template <class... Args>
void record_case(const char* name, const FormatInput format, Args&&... args) {
    const auto dispatch = FormatHashDispatch::automatic();
    const auto measured = measure_record(format, 1024U * 1024U, args...);
    if (!measured.succeeded()) std::abort();
    const auto bytes = measured.prepared()->payload_size();
    std::vector<std::byte> out(bytes);
    std::array<DecodedArg, 32> workspace{};
    const auto e = encode_v1(out.data(), bytes, *measured.prepared(), {}, policy, dispatch);
    if (!e.succeeded()) std::abort();
    const auto d = decode_v1(out.data(), bytes, workspace.data(), 32, policy);
    if (!d.succeeded()) std::abort();
    const std::string prefix = name;
    bench((prefix + "/measure").c_str(), "automatic", bytes, [&] {
        const auto m = measure_record(format, 1024U * 1024U, args...);
        consume(m);
    });
    bench((prefix + "/measure_encode").c_str(), "automatic", bytes, [&] {
        const auto m = measure_record(format, 1024U * 1024U, args...);
        const auto result = encode_v1(out.data(), bytes, *m.prepared(), {}, policy, dispatch);
        consume(result);
        consume(out);
    });
    bench((prefix + "/decode").c_str(), "none", bytes, [&] {
        const auto result = decode_v1(out.data(), bytes, workspace.data(), 32, policy);
        consume(result);
        consume(workspace);
    });
}
template <std::size_t... I>
void many_cstr(const char* text, std::index_sequence<I...>) {
    record_case("32_cstr", {nullptr, 0, 0}, ((void)I, qlog::cstr(text))...);
}
}  // namespace
int main(int argc, char**) {
    latency_mode = argc > 1;
    std::puts("case,backend,bytes,median_ns,mad_ns,bytes_per_second,p99_batch_amortized_ns");
    std::vector<std::byte> input(8192 + 32, std::byte{0x5A}), out(8192 + 32);
    const std::array<std::size_t, 18> lengths{0,  1,  3,  4,   7,   8,    15,   16,   31,
                                              32, 33, 64, 128, 256, 1024, 8191, 8192, 8193};
    const auto hash_cases = [&](const FormatHashDispatch& dispatch, const char* backend) {
        for (const auto n : lengths)
            for (const std::size_t offset : {0U, 1U}) {
                const std::string suffix = offset ? "unaligned_hot" : "aligned_hot";
                bench(("hash/" + suffix).c_str(), backend, n, [&] {
                    const auto h = dispatch.hash_raw_unchecked(input.data() + offset, n);
                    consume(h);
                });
                bench(("copy_hash/" + suffix).c_str(), backend, n, [&] {
                    const auto h =
                        dispatch.copy_raw_unchecked(input.data() + offset, out.data() + offset, n);
                    consume(h);
                    consume(out);
                });
            }
    };
    hash_cases(FormatHashTestAccess::software(), "software");
    if (auto hw = FormatHashTestAccess::hardware()) hash_cases(*hw, "hardware");
    constexpr char format[] = "{} {}";
    const FormatInput runtime{reinterpret_cast<const std::byte*>(format), 5, 0};
    const FormatInput literal{runtime.data, 5, hash_literal_stored(format)};
    record_case("empty", {nullptr, 0, 0});
    record_case("two_int_runtime", runtime, 42, 99);
    record_case("two_int_literal", literal, 42, 99);
    record_case("mixed", runtime, true, 42, 3.14, "player", qlog::ptr(input.data()));
    record_case("short_string", runtime, std::string_view("hello"));
    const std::string long_text(8192, 'x');
    record_case("long_string", runtime, std::string_view(long_text));
    record_case("one_cstr", runtime, qlog::cstr(long_text.c_str()));
    record_case("two_cstr", runtime, qlog::cstr("first"), qlog::cstr("last"));
    many_cstr("value", std::make_index_sequence<32>{});
}
