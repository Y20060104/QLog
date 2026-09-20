#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <iostream>
#include <string_view>
#include <vector>

#include "qlog/async_logger.hpp"
#include "qlog/detail/text_formatter.hpp"

using Clock = std::chrono::steady_clock;
template <class F>
double measure(F&& f) {
    for (unsigned i = 0; i < 1000; ++i) f(i);
    std::array<double, 9> samples{};
    for (auto& sample : samples) {
        const auto begin = Clock::now();
        for (unsigned i = 0; i < 500000; ++i) f(i);
        sample = std::chrono::duration<double, std::nano>(Clock::now() - begin).count() / 500000.0;
    }
    std::sort(samples.begin(), samples.end());
    return samples[4];
}
int main() {
    using namespace qlog::detail;
    constexpr std::string_view format = "QBENCH p={} n={} value={:.3f} text={}";
    constexpr std::string_view text = "abcdefghijklmnop";
    std::array<DecodedArg, 4> args{};
    args[0].tag = ArgumentTag::UInt32;
    args[0].value.bits = 1;
    args[1].tag = ArgumentTag::UInt32;
    args[2].tag = ArgumentTag::F64;
    args[2].value.bits = std::bit_cast<std::uint64_t>(3.14159);
    args[3].tag = ArgumentTag::Utf8String;
    args[3].value.bytes = reinterpret_cast<const std::byte*>(text.data());
    args[3].byte_count = text.size();
    std::array<std::byte, 256> output{};
    std::uint64_t checksum = 0;
    const double body_ns = measure([&](unsigned i) {
        args[1].value.bits = i;
        const auto result =
            render_message_utf8(reinterpret_cast<const std::byte*>(format.data()), format.size(),
                                args.data(), args.size(), output.data(), output.size());
        if (result.failure) std::abort();
        checksum += result.size + std::to_integer<unsigned>(output[0]);
    });
    qlog::LoggerConfig config;
    qlog::AppenderConfig appender;
    appender.name = "filtered";
    appender.filter.levels = 0;
    config.appenders = {appender};
    qlog::AsyncLogger logger(config);
    const double filtered_ns = measure([&](unsigned i) {
        const auto result = logger.try_log(0, qlog::LogLevel::info, "filtered={}", i);
        if (result.status() != qlog::LogStatus::filtered) std::abort();
        asm volatile("" : : "g"(&result) : "memory");
    });
    (void)logger.shutdown();
    std::vector<long long> clock_samples;
    clock_samples.reserve(10001);
    for (unsigned i = 0; i < 10001; ++i) {
        const auto begin = Clock::now();
        const auto end = Clock::now();
        clock_samples.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
    }
    std::sort(clock_samples.begin(), clock_samples.end());
    std::cout
        << "{\"method\":\"median of 9 batch means, 500000 calls per batch\",\"body_ns_per_record\":"
        << body_ns << ",\"filtered_ns_per_call\":" << filtered_ns
        << ",\"clock_pair_p50_ns\":" << clock_samples[5000]
        << ",\"clock_pair_p99_ns\":" << clock_samples[9900] << ",\"checksum\":" << checksum
        << "}\n";
}
