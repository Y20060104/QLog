#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <new>

#include "manual_worker_fixture.hpp"
#include "qlog/async_logger.hpp"
namespace {
std::size_t allocations = 0;
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
    auto& provider = qlog::test::install_manual_worker(false);
    char path[] = "/tmp/qlog-backend-allocation-XXXXXX";
    const int fd = ::mkstemp(path);
    if (fd < 0) return 1;
    ::close(fd);
    qlog::LoggerConfig c;
    c.name = "allocation";
    qlog::AppenderConfig a;
    a.name = "file";
    a.type = qlog::AppenderType::TextFile;
    a.file.path = path;
    c.appenders = {a};
    bool okay = true;
    {
        qlog::AsyncLogger logger(c);
        okay = logger.try_log(0, qlog::LogLevel::info, "warm={}", 1).accepted();
        provider.latest->step();
        const auto before = allocations;
        for (unsigned i = 0; i < 4096; ++i) {
            okay = logger.try_log(0, qlog::LogLevel::info, "n={}", i).accepted() && okay;
            provider.latest->step();
        }
        okay = allocations == before && okay;
        // Cold preparation allocates. The worker apply and management move do not.
        const auto request = logger.request_reset_appenders(c.appenders.data(), 1);
        okay = request.status == qlog::SubmitStatus::submitted && okay;
        const auto prepared = allocations;
        for (unsigned i = 0; i < 10; ++i) provider.latest->step();
        const auto result = logger.poll_management(request.request_id);
        okay = result.status == qlog::PollStatus::completed && allocations == prepared && okay;
        const auto stop = allocations;
        const auto& end = logger.shutdown(qlog::FlushMode::durable);
        okay = !end.backend_failed && end.errors.empty() && allocations == stop && okay;
    }
    ::unlink(path);
    std::puts(okay ? "PASS: steady producer+Session+line+output, prepared reset and shutdown: zero "
                     "C++ allocations"
                   : "FAIL: allocation or operation error");
    return okay ? 0 : 1;
}
