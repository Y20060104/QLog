#include "qlog/platform/thread.hpp"

#include <cinttypes>

#include "qlog/platform/thread_ops.hpp"
#include "qlog/utility/diagnostics.hpp"

namespace qlog::platform {

bool thread::cancel() {
    enum_thread_status running_status = enum_thread_status::running;

    if (!status_.compare_exchange_strong(running_status, enum_thread_status::pendding_cancel)) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::warning,
            "you're trying to cancel a thread which is not in running "
            "status, status enum:%" PRId32,
            static_cast<std::int32_t>(status_.load()));
        return false;
    }

    return true;
}

bool thread::is_cancelled() {
    return status_.load(memory_order::acquire) == enum_thread_status::pendding_cancel;
}

void thread::yield() {
    qlog::platform::yield();
}

void thread::cpu_relax() {
    qlog::platform::cpu_relax();
}

void thread::sleep(std::uint64_t millsec) {
    qlog::platform::sleep(millsec);
}

std::string thread::get_current_thread_name() {
    return qlog::platform::get_current_thread_name();
}

thread::thread_id thread::get_current_thread_id() {
    return qlog::platform::get_current_thread_id();
}

}  // namespace qlog::platform