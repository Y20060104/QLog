#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#include "qlog/async_logger.hpp"
int main(int argc, char** argv) {
    using namespace std::chrono_literals;
    using Clock = std::chrono::steady_clock;
    if (argc != 2) return 2;
    int fd = ::open(argv[1], O_CREAT | O_EXCL | O_RDONLY, 0600);
    if (fd < 0) return 2;
    qlog::LoggerConfig config;
    config.name = "visibility";
    qlog::AppenderConfig a;
    a.name = "file";
    a.type = qlog::AppenderType::TextFile;
    a.file.path = argv[1];
    config.appenders = {a};
    qlog::AsyncLogger logger(config);
    std::vector<double> latency;
    latency.reserve(60);
    off_t previous = 0;
    for (unsigned i = 0; i < 65; ++i) {
        const auto begin = Clock::now();
        if (!logger.try_log(0, qlog::LogLevel::info, "visible={}", i).accepted()) return 3;
        for (;;) {
            struct stat st {};
            if (::fstat(fd, &st) != 0) return 3;
            if (st.st_size > previous) {
                previous = st.st_size;
                break;
            }
            if (Clock::now() - begin > 5s) return 4;
            std::this_thread::sleep_for(100us);
        }
        if (i >= 5)
            latency.push_back(
                std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
        std::this_thread::sleep_for(1ms);
    }
    const auto& result = logger.shutdown();
    ::close(fd);
    if (result.backend_failed || !result.errors.empty()) return 5;
    std::sort(latency.begin(), latency.end());
    std::cout << "{\"samples\":60,\"poll_interval_us\":100,\"flush_interval_us\":100000,\"p50_ms\":"
              << latency[29] << ",\"p99_ms\":" << latency[58] << ",\"max_ms\":" << latency.back()
              << "}\n";
}
