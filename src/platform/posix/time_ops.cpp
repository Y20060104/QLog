#include "qlog/platform/time_ops.hpp"

#include <time.h>

namespace qlog::platform {

std::uint64_t high_performance_epoch_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME_COARSE, &ts);

    std::uint64_t epoch_milliseconds = static_cast<std::uint64_t>(ts.tv_sec) * 1000 +
                                       static_cast<std::uint64_t>(ts.tv_nsec) / 1000000;

    return epoch_milliseconds;
}

}  // namespace qlog::platform