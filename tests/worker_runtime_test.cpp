#include <gtest/gtest.h>
#include <pthread.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

#include "qlog/async_logger.hpp"

namespace {
std::atomic<int> fail_mask{0}, fault_fd{-1}, fault_errno{0};
}
extern "C" ssize_t __real_write(int, const void*, size_t);
extern "C" ssize_t __wrap_write(int fd, const void* data, size_t size) {
    if (fd == fault_fd.load(std::memory_order_relaxed)) {
        const int error = fault_errno.load(std::memory_order_relaxed);
        if (error) {
            errno = error;
            return -1;
        }
    }
    return __real_write(fd, data, size);
}
extern "C" int __real_pthread_sigmask(int, const sigset_t*, sigset_t*);
extern "C" int __wrap_pthread_sigmask(int how, const sigset_t* set, sigset_t* old) {
    if (set && fail_mask.exchange(0)) return EPERM;
    return __real_pthread_sigmask(how, set, old);
}
namespace {
using namespace qlog;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
class WorkerRuntimeTest : public ::testing::Test {
   protected:
    std::string directory;
    void SetUp() override {
        char name[] = "/tmp/qlog-worker-XXXXXX";
        const auto* path = ::mkdtemp(name);
        ASSERT_NE(path, nullptr);
        directory = path;
    }
    void TearDown() override {
        std::filesystem::remove_all(directory);
    }
    LoggerConfig config(std::string name = "main", ThreadMode mode = ThreadMode::async) {
        LoggerConfig c;
        c.name = name;
        c.backend.thread_mode = mode;
        c.ring.capacity_bytes = 1024U * 1024U;
        AppenderConfig a;
        a.name = "file";
        a.type = AppenderType::TextFile;
        a.file.path = directory + "/" + name + ".log";
        a.text.flush_interval_us = 1000;
        c.appenders = {a};
        return c;
    }
    std::string read(std::string name = "main") {
        std::ifstream f(directory + "/" + name + ".log", std::ios::binary);
        return {std::istreambuf_iterator<char>(f), {}};
    }
    PollResult finish(AsyncLogger& logger, SubmitResult result) {
        EXPECT_EQ(result.status, SubmitStatus::submitted);
        const auto deadline = Clock::now() + 5s;
        PollResult p;
        do {
            p = logger.poll_management(result.request_id);
            if (p.status != PollStatus::pending) break;
            std::this_thread::sleep_for(100us);
        } while (Clock::now() < deadline);
        EXPECT_EQ(p.status, PollStatus::completed);
        return p;
    }
    static std::size_t lines(const std::string& s) {
        return static_cast<std::size_t>(std::count(s.begin(), s.end(), '\n'));
    }
};
TEST_F(WorkerRuntimeTest, SharedAndIndependentProduceRealFiles) {
    for (auto mode : {ThreadMode::async, ThreadMode::independent}) {
        const std::string name = mode == ThreadMode::async ? "shared" : "independent";
        AsyncLogger logger(config(name, mode));
        ASSERT_TRUE(logger.try_log(0, LogLevel::info, "value={} hex={:#x}", 42, 42).accepted());
        auto p = finish(logger, logger.request_drain(FlushMode::durable));
        ASSERT_TRUE(p.completion);
        EXPECT_EQ(p.completion->status, CompletionStatus::completed);
        EXPECT_FALSE(logger.shutdown().backend_failed);
        EXPECT_NE(read(name).find("value=42 hex=0x2a"), std::string::npos);
    }
}
TEST_F(WorkerRuntimeTest, LowTrafficMakesProgressWithoutPressureNotification) {
    AsyncLogger logger(config());
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "low traffic").accepted());
    const auto deadline = Clock::now() + 2s;
    while (read().empty() && Clock::now() < deadline) std::this_thread::sleep_for(1ms);
    EXPECT_NE(read().find("low traffic"), std::string::npos);
}
TEST_F(WorkerRuntimeTest, MultiProducerExactCountAndPerProducerOrder) {
    for (auto mode : {ThreadMode::async, ThreadMode::independent}) {
        const std::string name = mode == ThreadMode::async ? "multi-shared" : "multi-independent";
        AsyncLogger logger(config(name, mode));
        std::atomic<unsigned> rejected{0};
        std::vector<std::thread> threads;
        for (unsigned p = 0; p < 8; ++p)
            threads.emplace_back([&, p] {
                for (unsigned i = 0; i < 2000; ++i)
                    if (!logger.try_log(0, LogLevel::info, "P{}:{};", p, i).accepted()) ++rejected;
            });
        for (auto& t : threads) t.join();
        const auto& result = logger.shutdown(FlushMode::durable);
        EXPECT_FALSE(result.backend_failed);
        EXPECT_FALSE(result.drain_incomplete);
        EXPECT_TRUE(result.errors.empty());
        EXPECT_EQ(rejected.load(), 0U);
        const auto data = read(name);
        EXPECT_EQ(lines(data), 16000U);
        for (unsigned p = 0; p < 8; ++p) {
            std::size_t pos = 0;
            for (unsigned i = 0; i < 2000; ++i) {
                const auto token = "P" + std::to_string(p) + ":" + std::to_string(i) + ";";
                auto next = data.find(token, pos);
                ASSERT_NE(next, std::string::npos);
                pos = next + token.size();
            }
        }
    }
}
TEST_F(WorkerRuntimeTest, SharedDetachLeavesOtherLoggerAlive) {
    AsyncLogger survivor(config("survivor"));
    {
        AsyncLogger first(config("first"));
        ASSERT_TRUE(first.try_log(0, LogLevel::info, "first").accepted());
        EXPECT_FALSE(first.shutdown().backend_failed);
    }
    ASSERT_TRUE(survivor.try_log(0, LogLevel::info, "after detach").accepted());
    EXPECT_FALSE(survivor.shutdown().backend_failed);
    EXPECT_NE(read("survivor").find("after detach"), std::string::npos);
}
TEST_F(WorkerRuntimeTest, RepeatedWakeAndManagementReceipt) {
    AsyncLogger logger(config());
    for (unsigned i = 0; i < 150; ++i) {
        ASSERT_TRUE(logger.try_log(0, LogLevel::info, "round={}", i).accepted());
        auto p = finish(logger, logger.request_drain(FlushMode::buffered));
        ASSERT_TRUE(p.completion);
        EXPECT_EQ(p.completion->status, CompletionStatus::completed);
    }
    EXPECT_EQ(lines(read()), 150U);
}
TEST_F(WorkerRuntimeTest, ResetWhileProducersRunPreservesEveryAcceptedLine) {
    auto c = config();
    AsyncLogger logger(c);
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> accepted{0};
    std::thread producer([&] {
        std::uint64_t i = 0;
        while (!stop.load()) {
            if (logger.try_log(0, LogLevel::info, "sequence={}", i++).accepted()) ++accepted;
        }
    });
    for (int i = 0; i < 30; ++i) {
        c.appenders[0].text.time_zone.offset_minutes = static_cast<std::int16_t>(i % 2 ? 480 : 0);
        auto p =
            finish(logger, logger.request_reset_appenders(c.appenders.data(), c.appenders.size()));
        EXPECT_TRUE(p.completion && p.completion->status == CompletionStatus::applied);
    }
    stop.store(true);
    producer.join();
    EXPECT_TRUE(logger.shutdown().errors.empty());
    EXPECT_EQ(lines(read()), accepted.load());
}
TEST_F(WorkerRuntimeTest, PendingResetShutdownDrainsIntoNewTarget) {
    auto c = config();
    AsyncLogger logger(c);
    for (unsigned i = 0; i < 100; ++i)
        ASSERT_TRUE(logger.try_log(0, LogLevel::info, "entry={}", i).accepted());
    c.appenders[0].file.path = directory + "/new.log";
    ASSERT_EQ(logger.request_reset_appenders(c.appenders.data(), 1).status,
              SubmitStatus::submitted);
    const auto& s = logger.shutdown(FlushMode::durable);
    EXPECT_FALSE(s.backend_failed);
    EXPECT_TRUE(s.errors.empty());
    EXPECT_EQ(lines(read()) + lines(read("new")), 100U);
    EXPECT_EQ(&s, &logger.shutdown());
}
TEST_F(WorkerRuntimeTest, ConcurrentConstructionAndTeardown) {
    std::atomic<unsigned> failures{0};
    std::vector<std::thread> threads;
    for (unsigned p = 0; p < 6; ++p)
        threads.emplace_back([&, p] {
            for (unsigned n = 0; n < 25; ++n) {
                try {
                    AsyncLogger logger(config("c" + std::to_string(p) + "-" + std::to_string(n),
                                              n % 2 ? ThreadMode::async : ThreadMode::independent));
                    if (!logger.try_log(0, LogLevel::info, "created").accepted()) ++failures;
                    if (logger.shutdown().backend_failed) ++failures;
                } catch (...) {
                    ++failures;
                }
            }
        });
    for (auto& t : threads) t.join();
    EXPECT_EQ(failures.load(), 0U);
}
TEST_F(WorkerRuntimeTest, BusyLoggerDoesNotStarveSparseLogger) {
    AsyncLogger busy(config("busy")), sparse(config("sparse"));
    std::atomic<bool> stop{false};
    std::thread producer([&] {
        while (!stop.load()) (void)busy.try_log(0, LogLevel::info, "busy={}", 42);
    });
    ASSERT_TRUE(sparse.try_log(0, LogLevel::info, "sparse").accepted());
    auto p = finish(sparse, sparse.request_drain(FlushMode::buffered));
    stop.store(true);
    producer.join();
    EXPECT_TRUE(p.completion && p.completion->status == CompletionStatus::completed);
    EXPECT_NE(read("sparse").find("sparse"), std::string::npos);
}
TEST_F(WorkerRuntimeTest, BrokenPipeDoesNotKillProcessOrChangeCallerMask) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    const int saved = ::dup(STDOUT_FILENO);
    ASSERT_GE(saved, 0);
    sigset_t before, after;
    ASSERT_EQ(::pthread_sigmask(SIG_SETMASK, nullptr, &before), 0);
    ::close(fds[0]);
    ASSERT_EQ(::dup2(fds[1], STDOUT_FILENO), STDOUT_FILENO);
    ::close(fds[1]);
    LoggerConfig c;
    c.backend.thread_mode = ThreadMode::independent;
    bool accepted = false, failed = false, has_error = false;
    {
        AsyncLogger logger(c);
        accepted = logger.try_log(0, LogLevel::info, "broken pipe").accepted();
        const auto& result = logger.shutdown();
        failed = result.backend_failed;
        has_error = !result.errors.empty() || result.retired_io.event_count != 0;
    }
    ::dup2(saved, STDOUT_FILENO);
    ::close(saved);
    ASSERT_EQ(::pthread_sigmask(SIG_SETMASK, nullptr, &after), 0);
    EXPECT_EQ(::sigismember(&before, SIGPIPE), ::sigismember(&after, SIGPIPE));
    EXPECT_TRUE(accepted);
    EXPECT_FALSE(failed);
    EXPECT_TRUE(has_error);
}
TEST_F(WorkerRuntimeTest, StartupMaskFailureRollsBackAndNextConstructionWorks) {
    fail_mask.store(1);
    EXPECT_THROW(AsyncLogger logger(config("fail", ThreadMode::independent)), std::system_error);
    EXPECT_EQ(fail_mask.load(), 0);
    AsyncLogger logger(config("okay", ThreadMode::independent));
    EXPECT_FALSE(logger.shutdown().backend_failed);
}

TEST_F(WorkerRuntimeTest, ContinuousRegistrationPastOneDiscoveryChunk) {
    auto c = config();
    c.ring.capacity_bytes = 65536;
    AsyncLogger logger(c);
    std::atomic<unsigned> accepted{0};
    for (unsigned i = 0; i < 100; ++i) {
        std::thread producer([&, i] {
            if (logger.try_log(0, LogLevel::info, "transient={}", i).accepted()) ++accepted;
        });
        producer.join();
    }
    EXPECT_FALSE(logger.shutdown().drain_incomplete);
    EXPECT_EQ(accepted.load(), 100U);
    EXPECT_EQ(lines(read()), 100U);
}
TEST_F(WorkerRuntimeTest, DiskFullTargetRecoversWhileHealthyTargetKeepsWriting) {
    auto c = config();
    c.appenders[0].file.retry_interval_us = 1000;
    auto second = c.appenders[0];
    second.name = "healthy";
    second.file.path = directory + "/healthy.log";
    c.appenders.push_back(second);
    AsyncLogger logger(c);
    int descriptor = -1;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
        std::error_code ec;
        if (std::filesystem::read_symlink(entry.path(), ec) == c.appenders[0].file.path && !ec)
            descriptor = std::stoi(entry.path().filename().string());
    }
    ASSERT_GE(descriptor, 0);
    fault_errno.store(ENOSPC);
    fault_fd.store(descriptor);
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "before recovery").accepted());
    auto first = finish(logger, logger.request_drain(FlushMode::buffered));
    ASSERT_TRUE(first.completion);
    EXPECT_EQ(first.completion->status, CompletionStatus::completed_with_io_error);
    EXPECT_NE(read("healthy").find("before recovery"), std::string::npos);
    fault_fd.store(-1);
    fault_errno.store(0);
    const auto deadline = Clock::now() + 2s;
    while (read().empty() && Clock::now() < deadline) std::this_thread::sleep_for(1ms);
    EXPECT_NE(read().find("before recovery"), std::string::npos);
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "after recovery").accepted());
    const auto& result = logger.shutdown();
    EXPECT_FALSE(result.backend_failed);
    EXPECT_EQ(result.retired_io.lost_bytes, 0U);
    EXPECT_GT(result.retired_io.event_count, 0U);
    EXPECT_NE(read().find("after recovery"), std::string::npos);
    EXPECT_NE(read("healthy").find("after recovery"), std::string::npos);
}
}  // namespace
