// Matched buffered endpoint variant of async_logger_benchmark.cpp.
#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <latch>
#include <numeric>
#include <string_view>
#include <thread>

#include "qlog/async_logger.hpp"

namespace {
std::atomic<std::uint64_t> writes{0}, syncs{0};
}
extern "C" ssize_t __real_write(int, const void*, size_t);
extern "C" ssize_t __wrap_write(int fd, const void* p, size_t n) {
    writes.fetch_add(1, std::memory_order_relaxed);
    return __real_write(fd, p, n);
}
extern "C" int __real_fdatasync(int);
extern "C" int __wrap_fdatasync(int fd) {
    syncs.fetch_add(1, std::memory_order_relaxed);
    return __real_fdatasync(fd);
}
namespace {
using namespace qlog;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
double seconds(Clock::duration d) {
    return std::chrono::duration<double>(d).count();
}
double cpu(const rusage& u) {
    return double(u.ru_utime.tv_sec + u.ru_stime.tv_sec) +
           double(u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6;
}
void drain(AsyncLogger& logger) {
    auto s = logger.request_drain(FlushMode::buffered);
    if (s.status != SubmitStatus::submitted) throw std::runtime_error("drain submit");
    const auto deadline = Clock::now() + 30s;
    for (;;) {
        auto p = logger.poll_management(s.request_id);
        if (p.status == PollStatus::completed) {
            if (!p.completion || p.completion->status != CompletionStatus::completed)
                throw std::runtime_error("drain failed");
            return;
        }
        if (Clock::now() > deadline) throw std::runtime_error("drain timeout");
        std::this_thread::sleep_for(50us);
    }
}
struct Sample {
    std::vector<std::uint64_t> accepted_ns, full_ns;
    std::vector<unsigned char> accepted;
    std::uint64_t count{}, full{}, other{};
};
void quantiles(const char* key, std::vector<std::uint64_t> values) {
    std::sort(values.begin(), values.end());
    auto q = [&](double p) {
        return values.empty()
                   ? 0
                   : values[std::min(values.size() - 1,
                                     static_cast<std::size_t>(p * double(values.size() - 1)))];
    };
    std::cout << '"' << key << "\":{\"samples\":" << values.size() << ",\"p50_ns\":" << q(.5)
              << ",\"p95_ns\":" << q(.95) << ",\"p99_ns\":" << q(.99) << ",\"p999_ns\":" << q(.999)
              << '}';
}
}  // namespace
int main(int argc, char** argv) try {
    // directory, mode, producers, attempts/producer, targets, durable, per-producer rate
    if (argc != 8)
        throw std::runtime_error(
            "usage: dir shared|independent producers attempts targets durable(0|1) "
            "rate(0=unpaced)");
    const std::filesystem::path root = argv[1];
    const bool independent = std::string_view(argv[2]) == "independent";
    const unsigned producers = static_cast<unsigned>(std::stoul(argv[3]));
    const unsigned attempts = static_cast<unsigned>(std::stoul(argv[4]));
    const unsigned targets = static_cast<unsigned>(std::stoul(argv[5]));
    const bool durable = std::stoul(argv[6]) != 0;
    if (durable) throw std::runtime_error("comparison uses buffered drain only");
    const unsigned rate = static_cast<unsigned>(std::stoul(argv[7]));
    if (!producers || producers > 64 || !attempts || !targets || targets > 8)
        throw std::runtime_error("bad dimensions");
    if (!std::filesystem::create_directory(root))
        throw std::runtime_error("output directory must be new");
    LoggerConfig config;
    config.name = "bench";
    config.backend.thread_mode = independent ? ThreadMode::independent : ThreadMode::async;
    config.ring.capacity_bytes = 1024U * 1024U;
    for (unsigned t = 0; t < targets; ++t) {
        AppenderConfig a;
        a.name = "file" + std::to_string(t);
        a.type = AppenderType::TextFile;
        a.file.path = (root / (a.name + ".log")).string();
        config.appenders.push_back(a);
    }
    const auto cold_start = Clock::now();
    AsyncLogger logger(config);
    const double construction_us = seconds(Clock::now() - cold_start) * 1e6;
    std::vector<Sample> samples(producers);
    for (auto& s : samples) {
        s.accepted.resize(attempts);
        s.accepted_ns.reserve(attempts / 64 + 1);
        s.full_ns.reserve(attempts / 64 + 1);
    }
    std::latch warmed(producers), start(1);
    std::vector<std::thread> threads;
    for (unsigned p = 0; p < producers; ++p)
        threads.emplace_back([&, p] {
            for (unsigned i = 0; i < 1000; ++i) {
                while (!logger.try_log(0, LogLevel::info, "QWARM p={} n={}", p, i).accepted())
                    std::this_thread::yield();
            }
            warmed.count_down();
            start.wait();
            auto& s = samples[p];
            const auto begin = Clock::now();
            for (unsigned i = 0; i < attempts; ++i) {
                if (rate && i % 32U == 0U)
                    std::this_thread::sleep_until(
                        begin + std::chrono::nanoseconds(std::uint64_t(i) * 1000000000ULL / rate));
                const bool sample = i % 67U == 0U;
                const auto t = sample ? Clock::now() : Clock::time_point{};
                const auto result =
                    logger.try_log(0, LogLevel::info, "QBENCH p={} n={} value={:.3f} text={}", p, i,
                                   3.14159, std::string_view("abcdefghijklmnop"));
                const auto elapsed =
                    sample
                        ? static_cast<std::uint64_t>(
                              std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t)
                                  .count())
                        : 0;
                if (result.accepted()) {
                    s.accepted[i] = 1;
                    ++s.count;
                    if (sample) s.accepted_ns.push_back(elapsed);
                } else if (result.status() == LogStatus::full) {
                    ++s.full;
                    if (sample) s.full_ns.push_back(elapsed);
                } else
                    ++s.other;
            }
        });
    warmed.wait();
    drain(logger);
    writes.store(0);
    syncs.store(0);
    rusage before{}, after{};
    ::getrusage(RUSAGE_SELF, &before);
    const auto begin = Clock::now();
    start.count_down();
    for (auto& thread : threads) thread.join();
    const auto producers_done = Clock::now();
    drain(logger);  // comparison endpoint: buffered drain, not shutdown
    const auto finished = Clock::now();
    ::getrusage(RUSAGE_SELF, &after);
    const auto write_count = writes.load(), sync_count = syncs.load();
    std::uint64_t accepted = 0, full = 0, other = 0, bytes = 0;
    std::vector<std::uint64_t> accepted_ns, full_ns;
    for (const auto& s : samples) {
        accepted += s.count;
        full += s.full;
        other += s.other;
        accepted_ns.insert(accepted_ns.end(), s.accepted_ns.begin(), s.accepted_ns.end());
        full_ns.insert(full_ns.end(), s.full_ns.begin(), s.full_ns.end());
    }
    if (other || accepted + full != std::uint64_t(producers) * attempts)
        throw std::runtime_error("admission conservation");
    for (unsigned t = 0; t < targets; ++t) {
        const auto file = root / ("file" + std::to_string(t) + ".log");
        bytes += std::filesystem::file_size(file);
        std::ifstream in(file);
        std::string line;
        std::uint64_t seen = 0, warm = 0;
        std::vector<std::vector<unsigned char>> delivered(producers,
                                                          std::vector<unsigned char>(attempts));
        std::vector<int> last(producers, -1);
        while (std::getline(in, line)) {
            auto pos = line.find("QBENCH p=");
            if (pos == std::string::npos) {
                if (line.find("QWARM p=") == std::string::npos)
                    throw std::runtime_error("malformed line");
                ++warm;
                continue;
            }
            unsigned p = 0, n = 0;
            if (std::sscanf(line.c_str() + pos, "QBENCH p=%u n=%u", &p, &n) != 2 ||
                p >= producers || n >= attempts || !samples[p].accepted[n] || delivered[p][n] ||
                static_cast<int>(n) <= last[p] ||
                line.find("value=3.141 text=abcdefghijklmnop") == std::string::npos)
                throw std::runtime_error("delivery/order/format mismatch");
            delivered[p][n] = 1;
            last[p] = static_cast<int>(n);
            ++seen;
        }
        if (seen != accepted || warm != 1000ULL * producers) throw std::runtime_error("lost lines");
    }
    std::cout << "{\"mode\":\"" << (independent ? "independent" : "shared")
              << "\",\"producers\":" << producers << ",\"attempts_per_producer\":" << attempts
              << ",\"targets\":" << targets << ",\"durable\":" << (durable ? "true" : "false")
              << ",\"rate_per_producer\":" << rate
              << ",\"ring_bytes_per_producer\":1048576,\"batch_bytes_per_target\":262144"
              << ",\"accepted\":" << accepted << ",\"full\":" << full << ",\"other\":" << other
              << ",\"producer_seconds\":" << seconds(producers_done - begin)
              << ",\"end_to_end_seconds\":" << seconds(finished - begin)
              << ",\"accepted_per_second\":" << double(accepted) / seconds(finished - begin)
              << ",\"attempts_per_second\":"
              << double(accepted + full) / seconds(producers_done - begin)
              << ",\"construction_us\":" << construction_us
              << ",\"final_flush_ms\":" << seconds(finished - producers_done) * 1e3
              << ",\"cpu_seconds\":" << cpu(after) - cpu(before)
              << ",\"max_rss_kib\":" << after.ru_maxrss << ",\"write_calls\":" << write_count
              << ",\"fdatasync_calls\":" << sync_count
              << ",\"file_bytes_including_warmup\":" << bytes << ",\"verified\":true,";
    quantiles("accepted_latency", std::move(accepted_ns));
    std::cout << ',';
    quantiles("full_latency", std::move(full_ns));
    std::cout << ",\"accepted_by_producer\":[";
    for (unsigned p = 0; p < producers; ++p) {
        if (p) std::cout << ',';
        std::cout << samples[p].count;
    }
    std::cout << "]}\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
