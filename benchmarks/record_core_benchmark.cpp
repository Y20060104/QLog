#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <qlog/detail/format_hash_reference.hpp>
#include <qlog/detail/record_decoder.hpp>
#include <qlog/detail/record_encoder.hpp>
#include <string>
#include <string_view>
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
void bench(const char* name, const char* backend, std::size_t bytes, Fn fn,
           std::size_t format_bytes = 0, std::size_t args_bytes = 0, std::size_t working_set = 0) {
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
    for (std::size_t i = 0; i < samples.size(); ++i) deviations[i] = std::abs(samples[i] - median);
    std::sort(deviations.begin(), deviations.end());
    const auto rank = (99U * samples.size() + 99U) / 100U;
    const auto p99 = latency_mode ? samples[rank - 1U] : std::numeric_limits<double>::quiet_NaN();
    std::printf("%s,%s,%zu,%zu,%zu,%zu,%zu,%zu,%.6f,%.6f,%.3f,%.6f\n", name, backend, bytes,
                format_bytes, args_bytes, working_set, iterations, samples.size(), median,
                deviations[deviations.size() / 2], static_cast<double>(bytes) * 1e9 / median, p99);
}
template <class... Args>
void record_case(const char* name, const FormatInput format, Args&&... args) {
    const auto dispatch = FormatHashDispatch::automatic();
    const char* backend = dispatch.backend() == HashBackend::x86_crc32c ? "hardware" : "software";
    const auto measured = measure_record(format, 1024U * 1024U, args...);
    if (!measured.succeeded()) std::abort();
    const auto bytes = measured.prepared()->payload_size();
    const auto arg_bytes = measured.prepared()->args_size();
    std::vector<std::byte> out(bytes);
    std::array<DecodedArg, 32> workspace{};
    const auto e = encode_v1(out.data(), bytes, *measured.prepared(), {}, policy, dispatch);
    if (!e.succeeded()) std::abort();
    const auto d = decode_v1(out.data(), bytes, workspace.data(), 32, policy);
    if (!d.succeeded()) std::abort();
    const auto exact = measure_record(format, bytes, args...);
    const auto rejected = measure_record(format, bytes - 1U, args...);
    if (!exact.succeeded() || rejected.succeeded()) std::abort();
    const std::string prefix = name;
    bench((prefix + "/measure").c_str(), backend, bytes,
          [&] {
              const auto m = measure_record(format, 1024U * 1024U, args...);
              consume(m);
          },
          format.size, arg_bytes);
    bench((prefix + "/measure_exact_quota").c_str(), backend, bytes,
          [&] {
              const auto m = measure_record(format, bytes, args...);
              consume(m);
          },
          format.size, arg_bytes);
    bench((prefix + "/measure_quota_reject").c_str(), backend, bytes,
          [&] {
              const auto m = measure_record(format, bytes - 1U, args...);
              consume(m);
          },
          format.size, arg_bytes);
    bench((prefix + "/measure_encode").c_str(), backend, bytes,
          [&] {
              const auto m = measure_record(format, 1024U * 1024U, args...);
              const auto result = encode_v1(out.data(), bytes, *m.prepared(), {}, policy, dispatch);
              consume(result);
              consume(out);
          },
          format.size, arg_bytes);
    bench((prefix + "/encode").c_str(), backend, bytes,
          [&] {
              const auto result =
                  encode_v1(out.data(), bytes, *measured.prepared(), {}, policy, dispatch);
              consume(result);
              consume(out);
          },
          format.size, arg_bytes);
    bench((prefix + "/decode").c_str(), "none", bytes,
          [&] {
              const auto result = decode_v1(out.data(), bytes, workspace.data(), 32, policy);
              consume(result);
              consume(workspace);
          },
          format.size, arg_bytes);
}
template <std::size_t... I>
void many_cstr(const char* name, const char* text, std::index_sequence<I...>) {
    record_case(name, {nullptr, 0, 0}, ((void)I, qlog::cstr(text))...);
}
template <std::size_t... I>
void many_int(std::index_sequence<I...>) {
    record_case("32_int", {nullptr, 0, 0}, static_cast<std::uint32_t>(I)...);
}
std::byte* aligned_base(std::vector<std::byte>& data) {
    const auto address = reinterpret_cast<std::uintptr_t>(data.data());
    return data.data() + ((32U - (address & 31U)) & 31U);
}
}  // namespace
int main(int argc, char**) {
    latency_mode = argc > 1;
    std::puts(
        "case,backend,bytes,format_bytes,args_bytes,working_set_bytes,iterations,samples,median_ns,"
        "mad_ns,bytes_per_second,p99_batch_amortized_ns");
    constexpr std::size_t stride = 8320U, slots = 8192U;
    std::vector<std::byte> input(stride * slots + 64U, std::byte{0x5A}), out(stride * slots + 64U);
    auto* source = aligned_base(input);
    auto* destination = aligned_base(out);
    const std::array<std::size_t, 18> lengths{0,  1,  3,  4,   7,   8,    15,   16,   31,
                                              32, 33, 64, 128, 256, 1024, 8191, 8192, 8193};
    const auto hash_cases = [&](const FormatHashDispatch& dispatch, const char* backend) {
        for (const auto n : lengths)
            for (const std::size_t offset : {0U, 1U})
                for (const bool rotating : {false, true}) {
                    const std::string suffix = std::string(offset ? "unaligned_" : "aligned_") +
                                               (rotating ? "rotating" : "hot");
                    std::size_t index = 0;
                    bench(("hash/" + suffix).c_str(), backend, n,
                          [&] {
                              const auto pos = rotating ? index * stride : 0U;
                              index = (index + 1U) & (slots - 1U);
                              const auto h = dispatch.hash_raw_unchecked(source + pos + offset, n);
                              consume(h);
                          },
                          n, 0, rotating ? stride * slots : stride);
                    bench(("copy_hash/" + suffix).c_str(), backend, n,
                          [&] {
                              const auto pos = rotating ? index * stride : 0U;
                              index = (index + 1U) & (slots - 1U);
                              const auto h = dispatch.copy_raw_unchecked(
                                  source + pos + offset, destination + pos + offset, n);
                              consume(h);
                              consume(out);
                          },
                          n, 0, rotating ? 2U * stride * slots : 2U * stride);
                }
    };
    hash_cases(FormatHashTestAccess::software(), "software");
    if (auto hw = FormatHashTestAccess::hardware()) hash_cases(*hw, "hardware");
    constexpr char format[] = "{} {}";
    constexpr auto stored = hash_literal_stored(format);
    const FormatInput runtime{reinterpret_cast<const std::byte*>(format), 5, 0};
    const FormatInput literal{runtime.data, 5, stored};
    record_case("empty", {nullptr, 0, 0});
    record_case("two_int_runtime", runtime, 42, 99);
    record_case("two_int_literal", literal, 42, 99);
    record_case("mixed", runtime, true, 42, 3.14, "player", qlog::ptr(source));
    record_case("short_string", runtime, std::string_view("hello"));
    const std::string long_text(8192, 'x');
    record_case("long_string", runtime, std::string_view(long_text));
    for (const bool long_case : {false, true}) {
        const auto* text = long_case ? long_text.c_str() : "value";
        const std::string p = long_case ? "long_" : "short_";
        record_case((p + "one_cstr_first").c_str(), runtime, qlog::cstr(text), 42, 3.14);
        record_case((p + "one_cstr_middle").c_str(), runtime, 42, qlog::cstr(text), 3.14);
        record_case((p + "one_cstr_last").c_str(), runtime, 42, 3.14, qlog::cstr(text));
        record_case((p + "two_cstr_first").c_str(), runtime, qlog::cstr(text), qlog::cstr(text),
                    42);
        record_case((p + "two_cstr_middle").c_str(), runtime, 42, qlog::cstr(text),
                    qlog::cstr(text), 3.14);
        record_case((p + "two_cstr_last").c_str(), runtime, 42, qlog::cstr(text), qlog::cstr(text));
        many_cstr((p + "32_cstr").c_str(), text, std::make_index_sequence<32>{});
    }
    many_int(std::make_index_sequence<32>{});
}
