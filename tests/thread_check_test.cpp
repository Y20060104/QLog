#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

#include "qlog/buffer/spsc_ring_buffer.hpp"
#include "qlog/global/log_vars.hpp"
#include "qlog/platform/aligned_memory.hpp"
#include "qlog/utility/utility_types.hpp"

namespace {
std::size_t checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
struct ScopeExit {};
}  // namespace

int main() {
    try {
#if defined(QLOG_DEBUG)
        constexpr bool initially_enabled = true;
#else
        constexpr bool initially_enabled = false;
#endif
        auto& globals = qlog::log_global_vars::get();
        check(globals.is_thread_check_enabled() == initially_enabled, "initial global policy");
        const auto memory_size = qlog::buffer::SpscRingBuffer::calculate_min_size_of_memory(128);
        std::unique_ptr<void, decltype(&qlog::platform::aligned_free)> memory(
            qlog::platform::aligned_alloc(qlog::utility::QLOG_CACHE_LINE_SIZE, memory_size),
            &qlog::platform::aligned_free);
        check(memory != nullptr, "128-byte aligned SISO fixture");
        qlog::buffer::SpscRingBuffer ring(memory.get(), memory_size, false);
        check(ring.is_thread_check_enable() == initially_enabled, "SISO initial policy");
        {
            qlog::scoped_thread_check_disable outer;
            check(!globals.is_thread_check_enabled(), "outer disables policy");
            check(!ring.is_thread_check_enable(), "SISO observes global policy");
            {
                qlog::scoped_thread_check_disable inner;
                check(!globals.is_thread_check_enabled(), "nested scope disables policy");
            }
            check(!globals.is_thread_check_enabled(), "inner exit preserves outer disable");
            bool observed = false;
            std::thread reader([&] {
                auto& other_globals = qlog::log_global_vars::get();
                observed = &other_globals == &globals && !other_globals.is_thread_check_enabled() &&
                           !ring.is_thread_check_enable();
            });
            reader.join();
            check(observed, "counter is process-wide, not thread-local");
        }
        check(globals.is_thread_check_enabled() == initially_enabled, "last scope restores policy");
        check(ring.is_thread_check_enable() == initially_enabled, "SISO policy restored");
        try {
            qlog::scoped_thread_check_disable guard;
            check(!globals.is_thread_check_enabled(), "exception scope disabled");
            throw ScopeExit{};
        } catch (const ScopeExit&) {
        }
        check(globals.is_thread_check_enabled() == initially_enabled,
              "exception unwind restores policy");
        ring.set_thread_check_enable(false);
        check(!ring.is_thread_check_enable(), "instance switch disables policy");
        ring.set_thread_check_enable(true);
        check(ring.is_thread_check_enable() == initially_enabled, "instance switch restored");
        std::cout << "PASS thread_check_policy checks=" << checks << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
