#include <fcntl.h>
#include <gtest/gtest.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <new>
#include <string>

#include "qlog/detail/appender_factory.hpp"

namespace {
thread_local bool count_allocations = false;
thread_local std::size_t allocations = 0;
void* allocate(std::size_t size) {
    if (count_allocations) ++allocations;
    if (auto p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void* aligned_allocate(std::size_t size, std::size_t alignment) {
    if (count_allocations) ++allocations;
    void* p = nullptr;
    if (::posix_memalign(&p, alignment, size == 0 ? 1 : size) == 0) return p;
    throw std::bad_alloc();
}
std::string watched_prefix;
int watched_fd = -1;
int next_write_error = 0, next_sync_error = 0, next_stat_error = 0, next_close_error = 0;
int recovery_opens = 0, sync_calls = 0, close_calls = 0;
}  // namespace
void* operator new(std::size_t n) {
    return allocate(n);
}
void* operator new[](std::size_t n) {
    return allocate(n);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
void* operator new(std::size_t n, std::align_val_t a) {
    return aligned_allocate(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a) {
    return aligned_allocate(n, static_cast<std::size_t>(a));
}
void operator delete(void* p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}

void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try {
        return allocate(n);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try {
        return allocate(n);
    } catch (...) {
        return nullptr;
    }
}
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try {
        return aligned_allocate(n, static_cast<std::size_t>(a));
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try {
        return aligned_allocate(n, static_cast<std::size_t>(a));
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void* p, const std::nothrow_t&) noexcept {
    std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
    std::free(p);
}
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    std::free(p);
}

extern "C" {
int __real_open(const char*, int, ...);
ssize_t __real_write(int, const void*, size_t);
int __real_fdatasync(int);
int __real_fstat(int, struct stat*);
int __real_close(int);
int __wrap_open(const char* path, int flags, ...) {
    mode_t mode = 0;
    if ((flags & O_CREAT) != 0) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }
    const bool watched = !watched_prefix.empty() &&
                         std::strncmp(path, watched_prefix.data(), watched_prefix.size()) == 0;
    if (watched && std::strstr(path, ".recovery.") != nullptr) ++recovery_opens;
    const int fd = __real_open(path, flags, mode);
    if (watched && fd >= 0) watched_fd = fd;
    return fd;
}
ssize_t __wrap_write(int fd, const void* data, size_t size) {
    if (fd == watched_fd && next_write_error != 0) {
        errno = std::exchange(next_write_error, 0);
        return -1;
    }
    return __real_write(fd, data, size);
}
int __wrap_fdatasync(int fd) {
    if (fd == watched_fd) {
        ++sync_calls;
        if (next_sync_error != 0) {
            errno = std::exchange(next_sync_error, 0);
            return -1;
        }
    }
    return __real_fdatasync(fd);
}
int __wrap_fstat(int fd, struct stat* st) {
    if (fd == watched_fd && next_stat_error != 0) {
        errno = std::exchange(next_stat_error, 0);
        return -1;
    }
    return __real_fstat(fd, st);
}
int __wrap_close(int fd) {
    if (fd == watched_fd) {
        ++close_calls;
        watched_fd = -1;
        const int injected = std::exchange(next_close_error, 0);
        const int result = __real_close(fd);
        if (injected != 0) {
            errno = injected;
            return -1;
        }
        return result;
    }
    return __real_close(fd);
}
}

namespace {
using namespace qlog;
using namespace qlog::detail;
const std::byte* bytes(const char* p) {
    return reinterpret_cast<const std::byte*>(p);
}
std::string read_all(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
class PosixAppender : public ::testing::Test {
   protected:
    std::string dir;
    AppenderConfig c;
    ConsoleOutputGate gate;
    void SetUp() override {
        char pattern[] = "/tmp/qlog-appender-XXXXXX";
        const auto p = ::mkdtemp(pattern);
        ASSERT_NE(p, nullptr);
        dir = p;
        c.name = "real";
        c.type = AppenderType::TextFile;
        c.file.path = dir + "/output.log";
        c.filter.category_enabled = {1};
        watched_prefix = dir + "/";
        watched_fd = -1;
        recovery_opens = sync_calls = close_calls = 0;
        next_write_error = next_sync_error = next_stat_error = next_close_error = 0;
    }
    void TearDown() override {
        count_allocations = false;
        watched_prefix.clear();
        watched_fd = -1;
        std::filesystem::remove_all(dir);  // only our unique mkdtemp directory
    }
};
TEST_F(PosixAppender, AppendDurableAndRaiiClose) {
    {
        std::ofstream f(c.file.path);
        f << "before";
    }
    auto a = make_appender(c, gate);
    auto report = prepare_io_report(&c, 1);
    ASSERT_TRUE(a->accept_line(bytes("x\0y"), 3));
    EXPECT_EQ(a->flush(FlushMode::durable, report, 0).status, FlushStatus::completed);
    EXPECT_EQ(sync_calls, 1);
    EXPECT_FALSE(report.has_errors());
    EXPECT_EQ(read_all(c.file.path), std::string("beforex\0y", 9));
    const int fd = watched_fd;
    a.reset();
    EXPECT_EQ(::fcntl(fd, F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(close_calls, 1);
}
TEST_F(PosixAppender, RejectsDirectoryFifoAndMissingParent) {
    c.file.path = dir;
    EXPECT_THROW(make_appender(c, gate), AppenderOpenError);
    c.file.path = dir + "/fifo";
    ASSERT_EQ(::mkfifo(c.file.path.c_str(), 0600), 0);
    EXPECT_THROW(make_appender(c, gate), AppenderOpenError);
    c.file.path = dir + "/missing/output";
    EXPECT_THROW(make_appender(c, gate), AppenderOpenError);
    EXPECT_FALSE(std::filesystem::exists(dir + "/missing"));
}
TEST_F(PosixAppender, ColdFstatFailureDoesNotLeakDescriptor) {
    next_stat_error = EIO;
    EXPECT_THROW(make_appender(c, gate), AppenderOpenError);
    EXPECT_EQ(close_calls, 1);
    EXPECT_EQ(watched_fd, -1);
}
TEST_F(PosixAppender, RecoveryUsesExclusiveNamesAndSixteenAttemptLimit) {
    for (int i = 0; i < 17; ++i) {
        std::ofstream f(c.file.path + ".recovery." + std::to_string(i));
        f << "keep";
    }
    auto a = make_appender(c, gate);
    auto report = prepare_io_report(&c, 1);
    ASSERT_TRUE(a->accept_line(bytes("lost"), 4));
    next_write_error = EIO;
    (void)a->flush(FlushMode::buffered, report, 0);
    report.clear();
    a->service(a->next_service_time(), report, 0);
    EXPECT_EQ(recovery_opens, 16);
    EXPECT_EQ(a->state(), OutputState::reopen_pending);
    auto errors = report.take_errors();
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0].output_path, c.file.path + ".recovery.0");
    report = prepare_io_report(&c, 1);
    a->service(a->next_service_time(), report, 0);
    EXPECT_EQ(recovery_opens, 18);
    EXPECT_TRUE(a->ready_for_record());
    // This operation still reports the collision even though the next open succeeded.
    EXPECT_TRUE(report.has_errors());
    report.clear();
    ASSERT_TRUE(a->accept_line(bytes("next"), 4));
    a->final_close(FlushMode::durable, report, 0);
    EXPECT_FALSE(report.has_errors());
    EXPECT_EQ(read_all(c.file.path + ".recovery.17"), "next");
    EXPECT_EQ(read_all(c.file.path + ".recovery.0"), "keep");
    RetiredIoSummary h;
    a->merge_history_into(h);
    EXPECT_EQ(h.lost_bytes, 4U);
    EXPECT_EQ(h.event_count, 18U);
}
TEST_F(PosixAppender, RecoveryValidationAndCleanupFailuresHaveSeparateStages) {
    auto a = make_appender(c, gate);
    auto report = prepare_io_report(&c, 1);
    ASSERT_TRUE(a->accept_line(bytes("x"), 1));
    next_write_error = EIO;
    (void)a->flush(FlushMode::buffered, report, 0);
    report.clear();
    next_stat_error = EIO;
    next_close_error = EINTR;
    a->service(a->next_service_time(), report, 0);
    EXPECT_EQ(watched_fd, -1);
    EXPECT_EQ(a->state(), OutputState::reopen_pending);
    auto errors = report.take_errors();
    ASSERT_EQ(errors.size(), 2U);
    EXPECT_EQ(errors[0].stage, IoStage::open);
    EXPECT_EQ(errors[1].stage, IoStage::close);
    report = prepare_io_report(&c, 1);
    a->service(a->next_service_time(), report, 0);
    ASSERT_TRUE(a->accept_line(bytes("ok"), 2));
    a->final_close(FlushMode::buffered, report, 0);
    EXPECT_EQ(read_all(c.file.path + ".recovery.1"), "ok");
}
TEST_F(PosixAppender, NoCppAllocationInPreparedOutputFailureRecoveryAndExport) {
    auto a = make_appender(c, gate);
    auto report = prepare_io_report(&c, 1);
    RetiredIoSummary history;
    std::vector<IoFailure> errors;
    allocations = 0;
    count_allocations = true;
    const bool accepted = a->accept_line(bytes("abc"), 3);
    next_write_error = ENOSPC;
    const auto failed = a->flush(FlushMode::buffered, report, 0);
    report.clear();
    next_write_error = EIO;
    (void)a->flush(FlushMode::buffered, report, 0);
    report.clear();
    a->service(a->next_service_time(), report, 0);
    const bool next_accepted = a->accept_line(bytes("next"), 4);
    next_sync_error = EINTR;
    (void)a->flush(FlushMode::durable, report, 0);
    a->final_close(FlushMode::durable, report, 0);
    a->merge_history_into(history);
    errors = report.take_errors();
    count_allocations = false;
    EXPECT_EQ(allocations, 0U);
    EXPECT_TRUE(accepted);
    EXPECT_TRUE(next_accepted);
    EXPECT_EQ(failed.status, FlushStatus::failed);
    EXPECT_EQ(history.lost_bytes, 3U);
    EXPECT_EQ(history.event_count, 3U);
    EXPECT_FALSE(errors.empty());
}
TEST_F(PosixAppender, ConsolePreservesNulAndDoesNotOwnStdout) {
    int pipefd[2];
    ASSERT_EQ(::pipe(pipefd), 0);
    const pid_t pid = ::fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        ::close(pipefd[0]);
        if (::dup2(pipefd[1], STDOUT_FILENO) < 0) ::_exit(10);
        ::close(pipefd[1]);
        c.type = AppenderType::Console;
        auto a = make_appender(c, gate);
        auto report = prepare_io_report(&c, 1);
        if (!a->accept_line(bytes("a\0b"), 3)) ::_exit(11);
        a->final_close(FlushMode::durable, report, 0);
        if (report.has_errors() || ::fcntl(STDOUT_FILENO, F_GETFD) < 0) ::_exit(12);
        ::_exit(0);
    }
    ::close(pipefd[1]);
    char buffer[8]{};
    const auto n = ::read(pipefd[0], buffer, sizeof(buffer));
    ::close(pipefd[0]);
    int status{};
    ASSERT_EQ(::waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
    ASSERT_EQ(n, 3);
    EXPECT_EQ(std::memcmp(buffer, "a\0b", 3), 0);
}
TEST_F(PosixAppender, BrokenConsolePipeReportsEpipeWithWorkerSignalMask) {
    const pid_t pid = ::fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        int fds[2];
        if (::pipe(fds) != 0) ::_exit(10);
        ::close(fds[0]);
        if (::dup2(fds[1], STDOUT_FILENO) < 0) ::_exit(11);
        ::close(fds[1]);
        sigset_t mask;
        ::sigemptyset(&mask);
        ::sigaddset(&mask, SIGPIPE);
        if (::pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0) ::_exit(12);
        c.type = AppenderType::Console;
        auto a = make_appender(c, gate);
        auto report = prepare_io_report(&c, 1);
        if (!a->accept_line(bytes("x"), 1)) ::_exit(13);
        const auto result = a->flush(FlushMode::buffered, report, 0);
        auto errors = report.take_errors();
        ::_exit(result.status == FlushStatus::failed && errors.size() == 1 &&
                        errors[0].system_error == EPIPE
                    ? 0
                    : 14);
    }
    int status{};
    ASSERT_EQ(::waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}
}  // namespace
