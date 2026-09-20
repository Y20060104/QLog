// Native defaults, common business workload. No syscall wrappers or sink replacement.
#include <sys/resource.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <latch>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>
#ifdef NATIVE_BQLOG
#include "bq_log/bq_log.h"
#else
#include "qlog/async_logger.hpp"
#endif

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
std::uint64_t ns(Clock::duration d) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
}
double sec(Clock::duration d) {
    return std::chrono::duration<double>(d).count();
}
struct Histogram {
    std::array<std::uint64_t, 4096> bins{};
    std::uint64_t count = 0, max = 0;
    void add(std::uint64_t n) {
        unsigned index;
        if (n < 64)
            index = static_cast<unsigned>(n);
        else {
            const auto e = std::bit_width(n) - 1;
            index = static_cast<unsigned>((e - 5) * 64 + ((n - (1ULL << e)) >> (e - 6)));
        }
        ++bins[index];
        ++count;
        max = std::max(max, n);
    }
    static std::uint64_t upper(unsigned index) {
        if (index < 64) return index;
        const unsigned e = index / 64 + 5;
        return (1ULL << e) + ((index % 64 + 1ULL) << (e - 6)) - 1;
    }
    std::uint64_t quantile(double p) const {
        const auto rank = static_cast<std::uint64_t>(double(count) * p + 0.999999);
        std::uint64_t seen = 0;
        for (unsigned i = 0; i < bins.size(); ++i)
            if ((seen += bins[i]) >= rank && count) return upper(i);
        return 0;
    }
    void merge(const Histogram& h) {
        for (unsigned i = 0; i < bins.size(); ++i) bins[i] += h.bins[i];
        count += h.count;
        max = std::max(max, h.max);
    }
    void json() const {
        std::cout << "{\"samples\":" << count << ",\"p50_ns_upper\":" << quantile(.5)
                  << ",\"p95_ns_upper\":" << quantile(.95) << ",\"p99_ns_upper\":" << quantile(.99)
                  << ",\"p999_ns_upper\":" << quantile(.999) << ",\"max_ns\":" << max
                  << ",\"bins\":[";
        bool first = true;
        for (unsigned i = 0; i < bins.size(); ++i)
            if (bins[i]) {
                if (!first) std::cout << ',';
                first = false;
                std::cout << '[' << i << ',' << bins[i] << ']';
            }
        std::cout << "]}";
    }
};
struct Digest {
    std::uint64_t count = 0, ordered = 1469598103934665603ULL, sum = 0;
    void add(std::uint64_t n) {
        ordered = (ordered ^ n) * 1099511628211ULL;
        auto x = n + 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        sum += x ^ (x >> 31);
        ++count;
    }
    bool operator==(const Digest&) const = default;
};
struct alignas(64) Stats {
    Digest expected[2];
    std::uint64_t attempted = 0, rejected = 0, errors = 0;
    Histogram accepted_latency, rejected_latency, scheduled_latency;
    std::atomic<std::uint64_t> published_accepted{0}, published_attempted{0};
};
class Adapter {
#ifdef NATIVE_BQLOG
    std::optional<bq::log> logger_;
#else
    std::unique_ptr<qlog::AsyncLogger> logger_;
#endif
   public:
    Adapter(const std::filesystem::path& root, unsigned targets, bool independent) {
#ifdef NATIVE_BQLOG
        std::string config;
        if (independent) config = "log.thread_mode=independent\n";
        for (unsigned t = 0; t < targets; ++t) {
            const auto folder = root / ("target" + std::to_string(t));
            std::filesystem::create_directory(folder);
            const auto k = "appenders_config.file" + std::to_string(t) + ".";
            config += k + "type=text_file\n" + k + "levels=[info]\n" + k +
                      "file_name=" + (folder / "output").string() + "\n";
        }
        std::ofstream(root / "config.txt") << config;
        logger_.emplace(bq::log::create_log("production", config.c_str()));
        if (!logger_->is_valid()) throw std::runtime_error("BQLog init failed");
#else
        qlog::LoggerConfig c;
        c.name = "production";
        if (independent) c.backend.thread_mode = qlog::ThreadMode::independent;
        for (unsigned t = 0; t < targets; ++t) {
            const auto folder = root / ("target" + std::to_string(t));
            std::filesystem::create_directory(folder);
            qlog::AppenderConfig a;
            a.name = "file" + std::to_string(t);
            a.type = qlog::AppenderType::TextFile;
            a.file.path = (folder / "output.log").string();
            c.appenders.push_back(a);
        }
        std::ofstream(root / "config.txt")
            << "QLog defaults; name=production; category=default; File targets=" << targets
            << "; independent=" << independent << '\n';
        logger_ = std::make_unique<qlog::AsyncLogger>(c);
#endif
    }
    template <std::size_t N, class... A>
    int emit(const char (&format)[N], const A&... args) {
#ifdef NATIVE_BQLOG
        return logger_->info(format, args...) ? 1 : 0;
#else
        const auto result = logger_->try_log(0, qlog::LogLevel::info, format, args...);
        if (result.accepted()) return 1;
        return result.status() == qlog::LogStatus::full ? 0 : -1;
#endif
    }
    void drain() {
#ifdef NATIVE_BQLOG
        logger_->force_flush();
#else
        const auto submit = logger_->request_drain(qlog::FlushMode::buffered);
        if (submit.status != qlog::SubmitStatus::submitted)
            throw std::runtime_error("drain submit failed");
        const auto limit = Clock::now() + 60s;
        for (;;) {
            auto p = logger_->poll_management(submit.request_id);
            if (p.status == qlog::PollStatus::completed) {
                if (!p.completion || p.completion->status != qlog::CompletionStatus::completed)
                    throw std::runtime_error("drain failed");
                return;
            }
            if (Clock::now() > limit) throw std::runtime_error("drain timeout");
            std::this_thread::sleep_for(50us);
        }
#endif
    }
};
std::vector<std::filesystem::path> files(const std::filesystem::path& root, unsigned target) {
    std::vector<std::filesystem::path> result;
    for (const auto& e :
         std::filesystem::recursive_directory_iterator(root / ("target" + std::to_string(target))))
        if (e.is_regular_file()) result.push_back(e.path());
    std::sort(result.begin(), result.end());
    return result;
}
std::uint64_t bytes(const std::filesystem::path& root, unsigned targets) {
    std::uint64_t n = 0;
    for (unsigned t = 0; t < targets; ++t)
        for (const auto& p : files(root, t)) n += std::filesystem::file_size(p);
    return n;
}
std::uint64_t parse(std::string_view& text, std::string_view prefix) {
    if (!text.starts_with(prefix)) throw std::runtime_error("invalid field");
    text.remove_prefix(prefix.size());
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{}) throw std::runtime_error("invalid number");
    text.remove_prefix(static_cast<std::size_t>(result.ptr - text.data()));
    return value;
}
double cpu(const rusage& u) {
    return double(u.ru_utime.tv_sec + u.ru_stime.tv_sec) +
           double(u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6;
}
}  // namespace
int main(int argc, char** argv) try {
    if (argc != 9)
        throw std::runtime_error(
            "dir mode producers targets payload warmup_seconds measure_seconds total_offered_rate");
    const std::filesystem::path root = argv[1];
    const bool independent = std::string_view(argv[2]) == "independent";
    const unsigned producers = static_cast<unsigned>(std::stoul(argv[3])),
                   targets = static_cast<unsigned>(std::stoul(argv[4]));
    const std::string workload = argv[5];
    const unsigned warm_seconds = static_cast<unsigned>(std::stoul(argv[6])),
                   duration = static_cast<unsigned>(std::stoul(argv[7]));
    if (!producers) throw std::runtime_error("zero producers");
    const std::uint64_t total_rate = std::stoull(argv[8]), rate = total_rate / producers;
    if (!producers || producers > 64 || !targets || targets > 4 || !duration ||
        (total_rate && (!rate || total_rate % producers)))
        throw std::runtime_error("invalid dimensions");
    if (!std::filesystem::create_directory(root))
        throw std::runtime_error("output must be a new directory");
    const std::string payload = workload == "large" ? std::string(512, 'x') : "abcdefghijklmnop";
    const auto cold = Clock::now();
    Adapter adapter(root, targets, independent);
    const auto cold_ns = ns(Clock::now() - cold);
    auto stats = std::make_unique<Stats[]>(producers);
    std::latch warm_done(producers), go(1);
    Clock::time_point begin;
    std::vector<std::thread> threads;
    for (unsigned p = 0; p < producers; ++p)
        threads.emplace_back([&, p] {
            auto& state = stats[p];
            for (unsigned phase = 0; phase < 2; ++phase) {
                if (phase) {
                    warm_done.count_down();
                    go.wait();
                }
                const auto start = phase ? begin : Clock::now();
                const auto deadline = start + std::chrono::seconds(phase ? duration : warm_seconds);
                std::uint64_t n = 0;
                while (true) {
                    if ((n % 256U) == 0 && Clock::now() >= deadline) break;
                    if (phase && rate && n >= rate * duration) break;
                    auto planned = start;
                    if (phase && rate) {
                        planned = start +
                                  std::chrono::nanoseconds((n / 32U) * 32U * 1000000000ULL / rate);
                        if (n % 32U == 0) std::this_thread::sleep_until(planned);
                    }
                    const bool sample = phase && n % 1021U == 0;
                    const auto t = sample ? Clock::now() : Clock::time_point{};
                    int accepted;
                    if (workload == "small")
                        accepted = adapter.emit("PROD w={} p={} n={} code=200", phase, p, n);
                    else if (workload == "mixed")
                        accepted = adapter.emit("PROD w={} p={} n={} value={:.3f} text={}", phase,
                                                p, n, 3.14159, std::string_view(payload));
                    else
                        accepted = adapter.emit("PROD w={} p={} n={} body={}", phase, p, n,
                                                std::string_view(payload));
                    const auto end = sample ? Clock::now() : Clock::time_point{};
                    if (accepted > 0) state.expected[phase].add(n);
                    if (phase) {
                        ++state.attempted;
                        if (!accepted) ++state.rejected;
                        if (accepted < 0) ++state.errors;
                        if (sample) {
                            (accepted > 0 ? state.accepted_latency : state.rejected_latency)
                                .add(ns(end - t));
                            if (rate)
                                state.scheduled_latency.add(end > planned ? ns(end - planned) : 0);
                        }
                        if (n % 256U == 0) {
                            state.published_accepted.store(state.expected[1].count,
                                                           std::memory_order_relaxed);
                            state.published_attempted.store(state.attempted,
                                                            std::memory_order_relaxed);
                        }
                    }
                    ++n;
                }
            }
            state.published_accepted.store(state.expected[1].count, std::memory_order_relaxed);
            state.published_attempted.store(state.attempted, std::memory_order_relaxed);
        });
    warm_done.wait();
    adapter.drain();
    const auto warm_bytes = bytes(root, targets);
    struct Window {
        double seconds;
        std::uint64_t accepted, attempted, file_bytes;
    };
    std::vector<Window> windows;
    windows.reserve(duration);
    rusage before{}, after{};
    ::getrusage(RUSAGE_SELF, &before);
    begin = Clock::now();
    go.count_down();
    for (unsigned second = 1; second <= duration; ++second) {
        std::this_thread::sleep_until(begin + std::chrono::seconds(second));
        std::uint64_t accepted = 0, attempted = 0;
        for (unsigned p = 0; p < producers; ++p) {
            accepted += stats[p].published_accepted.load(std::memory_order_relaxed);
            attempted += stats[p].published_attempted.load(std::memory_order_relaxed);
        }
        windows.push_back(
            {sec(Clock::now() - begin), accepted, attempted, bytes(root, targets) - warm_bytes});
    }
    for (auto& t : threads) t.join();
    const auto stopped = Clock::now();
    adapter.drain();
    const auto drained = Clock::now();
    ::getrusage(RUSAGE_SELF, &after);
    const auto output_bytes = bytes(root, targets) - warm_bytes;
    std::uint64_t accepted = 0, attempted = 0, rejected = 0, errors = 0;
    Histogram accepted_hist, rejected_hist, scheduled_hist;
    for (unsigned p = 0; p < producers; ++p) {
        const auto& s = stats[p];
        accepted += s.expected[1].count;
        attempted += s.attempted;
        rejected += s.rejected;
        errors += s.errors;
        accepted_hist.merge(s.accepted_latency);
        rejected_hist.merge(s.rejected_latency);
        scheduled_hist.merge(s.scheduled_latency);
    }
    if (errors || accepted + rejected != attempted) throw std::runtime_error("admission mismatch");
    const std::string suffix = workload == "small"   ? " code=200"
                               : workload == "mixed" ? " value=3.141 text=" + payload
                                                     : " body=" + payload;
    for (unsigned target = 0; target < targets; ++target) {
        std::vector<std::array<Digest, 2>> seen(producers);
        std::vector<std::array<std::uint64_t, 2>> last(producers);
        for (const auto& file : files(root, target)) {
            std::ifstream input(file, std::ios::binary);
            std::string line;
            while (std::getline(input, line)) {
                const auto offset = line.find("PROD w=");
                if (offset == std::string::npos) throw std::runtime_error("invalid body marker");
                std::string_view text(line.data() + offset, line.size() - offset);
                const auto phase = parse(text, "PROD w="), p = parse(text, " p="),
                           n = parse(text, " n=");
                if (phase > 1 || p >= producers || text != suffix)
                    throw std::runtime_error("invalid body");
                auto& actual = seen[p][phase];
                if (actual.count && n <= last[p][phase])
                    throw std::runtime_error("duplicate/order error");
                actual.add(n);
                last[p][phase] = n;
            }
            if (!input.eof()) throw std::runtime_error("file read failure");
        }
        for (unsigned p = 0; p < producers; ++p)
            for (unsigned phase = 0; phase < 2; ++phase)
                if (!(seen[p][phase] == stats[p].expected[phase]))
                    throw std::runtime_error("accepted delivery count/digest mismatch");
    }
#ifdef NATIVE_BQLOG
    constexpr const char* library = "bqlog";
#else
    constexpr const char* library = "qlog";
#endif
    const auto offered = rate ? rate * duration * producers : attempted;
    std::cout << "{\"library\":\"" << library << "\",\"mode\":\"" << argv[2] << "\",\"workload\":\""
              << workload << "\",\"producers\":" << producers << ",\"targets\":" << targets
              << ",\"warmup_seconds\":" << warm_seconds << ",\"measure_seconds\":" << duration
              << ",\"offered_rate\":" << total_rate << ",\"offered\":" << offered
              << ",\"attempted\":" << attempted << ",\"accepted\":" << accepted
              << ",\"rejected\":" << rejected << ",\"not_attempted_by_deadline\":"
              << (offered > attempted ? offered - attempted : 0) << ",\"errors\":" << errors
              << ",\"producer_seconds\":" << sec(stopped - begin)
              << ",\"end_to_end_seconds\":" << sec(drained - begin)
              << ",\"goodput\":" << double(accepted) / sec(drained - begin)
              << ",\"output_bytes\":" << output_bytes
              << ",\"cold_start_us\":" << double(cold_ns) / 1000
              << ",\"drain_ms\":" << sec(drained - stopped) * 1000
              << ",\"cpu_seconds\":" << cpu(after) - cpu(before)
              << ",\"baseline_max_rss_kib\":" << before.ru_maxrss
              << ",\"max_rss_kib\":" << after.ru_maxrss
              << ",\"harness_stats_bytes\":" << sizeof(Stats) * producers
              << ",\"voluntary_context_switches\":" << after.ru_nvcsw - before.ru_nvcsw
              << ",\"involuntary_context_switches\":" << after.ru_nivcsw - before.ru_nivcsw
              << ",\"accepted_latency\":";
    accepted_hist.json();
    std::cout << ",\"rejected_latency\":";
    rejected_hist.json();
    std::cout << ",\"scheduled_latency\":";
    scheduled_hist.json();
    std::cout << ",\"windows\":[";
    for (unsigned i = 0; i < windows.size(); ++i) {
        if (i) std::cout << ',';
        const auto& w = windows[i];
        std::cout << "{\"seconds\":" << w.seconds << ",\"accepted\":" << w.accepted
                  << ",\"attempted\":" << w.attempted << ",\"file_bytes\":" << w.file_bytes << '}';
    }
    std::cout << "],\"producer_digests\":[";
    for (unsigned p = 0; p < producers; ++p) {
        if (p) std::cout << ',';
        std::cout << "[";
        for (unsigned phase = 0; phase < 2; ++phase) {
            if (phase) std::cout << ',';
            const auto& d = stats[p].expected[phase];
            std::cout << "{\"count\":" << d.count << ",\"ordered\":\"" << d.ordered
                      << "\",\"sum\":\"" << d.sum << "\"}";
        }
        std::cout << ']';
    }
    std::cout << "],\"verified\":true,\"verification\":\"all lines parsed, exact body and monotone "
                 "IDs, accepted count plus two 64-bit fingerprints per producer/phase/target\"}\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
