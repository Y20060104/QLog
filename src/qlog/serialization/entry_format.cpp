#include "qlog/serialization/entry_format.h"

#ifdef __WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <chrono>

namespace qlog::serialization
{
uint64_t get_current_thread_id() noexcept
{
#ifdef _WIN32
    return static_cast<uint64_t>(GetCurrentThreadId());
#else
    // pthread_t 是整数类型（unsigned long on Linux, opaque on macOS）
    // reinterpret_cast 保证跨平台不触发 UB
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pthread_self()));
#endif
}

uint64_t get_current_timestamp_ms() noexcept
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch()
    )
                                     .count());
}

entry_header make_entry_header(uint32_t fmt_hash, uint16_t category_idx, log_level level) noexcept
{
    entry_header hdr{};
    hdr.timestamp_ms = get_current_timestamp_ms();
    hdr.thread_id = get_current_thread_id();
    hdr.fmt_hash = fmt_hash;
    hdr.category_idx = category_idx;
    hdr.level = static_cast<uint8_t>(level);
    hdr.reserved = 0;
    return hdr;
}
} // namespace qlog::serialization