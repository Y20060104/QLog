#include "qlog/platform/time_ops.hpp"

#include <windows.h>

namespace qlog::platform {

std::uint64_t high_performance_epoch_ms() {
    FILETIME ft;
    LARGE_INTEGER li;

    GetSystemTimeAsFileTime(&ft);

    li.LowPart = static_cast<decltype(li.LowPart)>(ft.dwLowDateTime);
    li.HighPart = static_cast<decltype(li.HighPart)>(ft.dwHighDateTime);

    std::uint64_t ret = static_cast<std::uint64_t>(li.QuadPart);

    const std::uint64_t UNIX_TIME_START = 0x019DB1DED53E8000;
    const std::uint64_t TICKS_PER_SECOND = 10000;

    ret -= UNIX_TIME_START;
    ret /= TICKS_PER_SECOND;

    return ret;
}

}  // namespace qlog::platform