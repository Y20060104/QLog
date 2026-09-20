#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>

#include "qlog/async_logger.hpp"
namespace {
std::atomic<std::size_t> allocations{0};
void* allocate(std::size_t n) {
    ++allocations;
    if (auto p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* allocate_aligned(std::size_t n, std::align_val_t a) {
    ++allocations;
    void* p{};
    if (::posix_memalign(&p, static_cast<std::size_t>(a), n ? n : 1) == 0) return p;
    throw std::bad_alloc();
}
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
    return allocate_aligned(n, a);
}
void* operator new[](std::size_t n, std::align_val_t a) {
    return allocate_aligned(n, a);
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
        return allocate_aligned(n, a);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try {
        return allocate_aligned(n, a);
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
int main() {
    char path[] = "/tmp/qlog-worker-allocation-XXXXXX";
    int fd = ::mkstemp(path);
    if (fd < 0) return 1;
    ::close(fd);
    bool okay = true;
    for (auto mode : {qlog::ThreadMode::async, qlog::ThreadMode::independent}) {
        qlog::LoggerConfig c;
        c.name = "allocation";
        c.backend.thread_mode = mode;
        c.ring.capacity_bytes = 1024U * 1024U;
        qlog::AppenderConfig a;
        a.name = "file";
        a.type = qlog::AppenderType::TextFile;
        a.file.path = path;
        c.appenders = {a};
        qlog::AsyncLogger logger(c);
        okay = logger.try_log(0, qlog::LogLevel::info, "warm={}", 1).accepted() && okay;
        auto warm = logger.request_drain(qlog::FlushMode::buffered);
        while (logger.poll_management(warm.request_id).status == qlog::PollStatus::pending)
            std::this_thread::yield();
        const auto before = allocations.load();
        for (unsigned i = 0; i < 4096; ++i)
            okay = logger.try_log(0, qlog::LogLevel::info, "steady={}", i).accepted() && okay;
        const auto& end = logger.shutdown(qlog::FlushMode::durable);
        okay = !end.backend_failed && end.errors.empty() && allocations.load() == before && okay;
    }
    ::unlink(path);
    std::puts(
        okay ? "PASS: real shared/independent worker pipeline and shutdown: zero C++ allocations"
             : "FAIL: allocation/operation");
    return okay ? 0 : 1;
}
