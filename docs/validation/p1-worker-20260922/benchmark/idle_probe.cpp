
#include <atomic>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/resource.h>
static std::atomic<clockid_t> p1_worker_clock{0};
extern "C" int __real_pthread_sigmask(int, const sigset_t*, sigset_t*);
extern "C" int __wrap_pthread_sigmask(int how, const sigset_t* set, sigset_t* old) {
    const int result = __real_pthread_sigmask(how, set, old);
    if (!result && set && how == SIG_BLOCK) {
        clockid_t clock{};
        if (!pthread_getcpuclockid(pthread_self(), &clock))
            p1_worker_clock.store(clock, std::memory_order_release);
    }
    return result;
}
static double p1_worker_cpu() {
    const auto clock = p1_worker_clock.load(std::memory_order_acquire);
    timespec t{};
    if (!clock || clock_gettime(clock, &t)) return -1.0;
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec)*1e-9;
}
// Audit-only probe. Does not alter QLog. Run each case as a fresh process.
// It creates one TLS Context per short-lived producer thread, waits for real
// output, samples process CPU while the main thread sleeps, drains explicitly,
// samples again, then validates shutdown and output line counts.
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "qlog/async_logger.hpp"

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

void perf_control(const char* command) {
    const char* path=std::getenv("AUDIT_PERF_CTL");
    if (!path) return;
    static int ctl=::open(path,O_WRONLY);
    static int ack=::open(std::getenv("AUDIT_PERF_ACK"),O_RDONLY);
    if (ctl<0 || ack<0) throw std::runtime_error("perf FIFO open");
    const std::string value=std::string(command)+'\n';
    if (::write(ctl,value.data(),value.size())!=static_cast<ssize_t>(value.size()))
        throw std::runtime_error("perf FIFO write");
    char c;
    do { if (::read(ack,&c,1)!=1) throw std::runtime_error("perf ack"); } while(c!='\n');
}

double process_cpu_seconds() {
    timespec stamp{};
    if (::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &stamp) != 0)
        throw std::runtime_error("CLOCK_PROCESS_CPUTIME_ID failed");
    return static_cast<double>(stamp.tv_sec) + static_cast<double>(stamp.tv_nsec) * 1e-9;
}

std::size_t count_lines(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::size_t lines = 0;
    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() != 0)
        lines += static_cast<std::size_t>(std::count(buffer, buffer + file.gcount(), '\n'));
    return lines;
}

void sample(const std::string& name, const char* phase, double seconds) {
    const bool profile=std::string_view(phase)=="after_explicit_drain";
    if (profile) perf_control("enable");
    const auto begin = Clock::now();
    const auto cpu_begin = process_cpu_seconds();
    const auto worker_begin = p1_worker_cpu();
    rusage usage_before{}, usage_after{};
    getrusage(RUSAGE_SELF, &usage_before);
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    const auto cpu = process_cpu_seconds() - cpu_begin;
    const auto worker_cpu = p1_worker_cpu() - worker_begin;
    getrusage(RUSAGE_SELF, &usage_after);
    const auto wall = std::chrono::duration<double>(Clock::now() - begin).count();
    if (profile) perf_control("disable");
    std::cout << std::fixed << std::setprecision(6) << "sample," << name << ',' << phase
              << ",wall_s=" << wall << ",process_cpu_s=" << cpu
              << ",worker_cpu_s=" << worker_cpu << ",voluntary_cs=" << usage_after.ru_nvcsw-usage_before.ru_nvcsw << ",involuntary_cs=" << usage_after.ru_nivcsw-usage_before.ru_nivcsw << ",cpu_cores=" << cpu / wall << '\n' << std::flush;
}

void drain(qlog::AsyncLogger& logger) {
    const auto submitted = logger.request_drain(qlog::FlushMode::buffered);
    if (submitted.status != qlog::SubmitStatus::submitted)
        throw std::runtime_error("drain submission failed");
    const auto deadline = Clock::now() + 10s;
    for (;;) {
        auto result = logger.poll_management(submitted.request_id);
        if (result.status == qlog::PollStatus::completed) {
            if (!result.completion ||
                result.completion->status != qlog::CompletionStatus::completed ||
                !result.completion->errors.empty())
                throw std::runtime_error("drain completed with an error");
            return;
        }
        if (result.status != qlog::PollStatus::pending || Clock::now() >= deadline)
            throw std::runtime_error("drain did not complete within 10 seconds");
        std::this_thread::sleep_for(100us);
    }
}

struct Entry {
    std::string path;
    unsigned contexts{};
    std::unique_ptr<qlog::AsyncLogger> logger;
};
}  // namespace

int main(int argc, char** argv) {
    try {
        const std::string name = argc >= 2 ? argv[1] : "pair23";
        const double seconds = argc >= 3 ? std::stod(argv[2]) : 3.0;
        if (!(seconds > 0.0 && seconds <= 30.0))
            throw std::runtime_error("sample duration must be in (0, 30] seconds");
        std::vector<unsigned> context_counts;
        if (name == "empty") context_counts = {0};
        else if (name == "single") context_counts = {1};
        else if (name == "single101") context_counts = {101};
        else if (name == "single204") context_counts = {204};
        else if (name == "pair102102") context_counts = {102, 102};
        else if (name == "pair23") context_counts = {2, 3};
        else if (name == "pair24") context_counts = {2, 4};
        else if (name == "pair101103") context_counts = {101, 103};
        else throw std::runtime_error("case: empty | single | single101 | pair23 | pair24 | pair101103");

        char pattern[] = "/tmp/qlog-idle-probe-XXXXXX";
        const auto* created = ::mkdtemp(pattern);
        if (!created) throw std::runtime_error("mkdtemp failed");
        const std::string directory = created;
        std::cout << "setup,case=" << name << ",pid=" << ::getpid()
                  << ",directory=" << directory << ",ring_bytes=65536,thread_mode=shared"
                  << ",sample_seconds=" << seconds << '\n' << std::flush;
        std::vector<Entry> entries;
        for (std::size_t i = 0; i < context_counts.size(); ++i) {
            qlog::LoggerConfig config;
            config.name = "idle-" + std::to_string(i);
            config.ring.capacity_bytes = 65536;
            config.ring.max_payload_bytes = 8192;
            // Keep the normal shared worker, default quota and periodic flush.
            qlog::AppenderConfig output;
            output.name = "file";
            output.type = qlog::AppenderType::TextFile;
            output.file.path = directory + "/logger-" + std::to_string(i) + ".log";
            config.appenders = {output};
            entries.push_back({output.file.path, context_counts[i],
                               std::make_unique<qlog::AsyncLogger>(config)});
        }
        for (std::size_t i = 0; i < entries.size(); ++i) {
            auto& entry = entries[i];
            for (unsigned context = 0; context < entry.contexts; ++context) {
                bool accepted = false;
                std::thread producer([&] {
                    accepted = entry.logger->try_log(0, qlog::LogLevel::info,
                                                      "idle context={}", context).accepted();
                });
                producer.join();
                if (!accepted) throw std::runtime_error("single context record rejected");
            }
            std::cout << "registered,logger=" << i << ",contexts=" << entry.contexts << '\n';
        }
        // Real file visibility proves the known records were consumed/flushed.
        // This setup polling is outside CPU samples and may affect sweep phase.
        const auto visible_deadline = Clock::now() + 10s;
        for (;;) {
            bool complete = true;
            for (const auto& entry : entries)
                complete = complete && count_lines(entry.path) == entry.contexts;
            if (complete) break;
            if (Clock::now() >= visible_deadline)
                throw std::runtime_error("expected natural output did not appear within 10 seconds");
            std::this_thread::sleep_for(1ms);
        }
        sample(name, "natural_output_visible", seconds);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto started = Clock::now();
            drain(*entries[i].logger);
            std::cout << "drain,logger=" << i << ",wall_s="
                      << std::chrono::duration<double>(Clock::now() - started).count() << '\n';
        }
        sample(name, "after_explicit_drain", seconds);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto started = Clock::now();
            const auto& result = entries[i].logger->shutdown(qlog::FlushMode::buffered);
            const auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
            const auto lines = count_lines(entries[i].path);
            const bool valid = !result.backend_failed && !result.drain_incomplete &&
                               result.errors.empty() && result.retired_io.lost_bytes == 0 &&
                               lines == entries[i].contexts;
            std::cout << "shutdown,logger=" << i << ",wall_s=" << elapsed
                      << ",lines=" << lines << ",expected=" << entries[i].contexts
                      << ",ok=" << valid << '\n' << std::flush;
            if (!valid) throw std::runtime_error("shutdown or output validation failed");
        }
        // All producers are joined and all Session detach handshakes completed.
        // Files are retained in the printed unique directory as audit evidence.
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "idle_probe failed: " << error.what() << '\n';
        return 1;
    }
}
