#pragma once

#include <pthread.h>
#include <sched.h>
#include <sys/utsname.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace qlog::bench {

inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#else
    std::this_thread::yield();
#endif
}

inline void compiler_memory_barrier(const void* address) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : : "r"(address) : "memory");
#else
    (void)address;
#endif
}

[[nodiscard]] inline std::vector<int> allowed_cpus() {
    cpu_set_t set;
    CPU_ZERO(&set);

    if (sched_getaffinity(0, sizeof(set), &set) != 0) {
        return {};
    }

    std::vector<int> cpus;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &set) != 0) {
            cpus.push_back(cpu);
        }
    }
    return cpus;
}

[[nodiscard]] inline bool pin_current_thread(int cpu, std::string& error) {
    if (cpu < 0) {
        error = "CPU affinity was not resolved";
        return false;
    }

    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);

    const int result = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    if (result != 0) {
        error = "pthread_setaffinity_np(cpu=" + std::to_string(cpu) + ") failed with error " +
                std::to_string(result);
        return false;
    }
    return true;
}

[[nodiscard]] inline std::string trim_copy(std::string value) {
    const std::size_t first = value.find_first_not_of(" 	");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" 	");
    return value.substr(first, last - first + 1U);
}

[[nodiscard]] inline std::string cpu_model_name() {
    std::ifstream input("/proc/cpuinfo");
    std::string line;
    while (std::getline(input, line)) {
        constexpr std::string_view kKey{"model name"};
        if (line.compare(0U, kKey.size(), kKey) != 0) {
            continue;
        }
        const std::size_t separator = line.find(':');
        if (separator != std::string::npos) {
            return trim_copy(line.substr(separator + 1U));
        }
    }
    return "unknown";
}

[[nodiscard]] inline std::string kernel_description() {
    utsname info{};
    if (uname(&info) != 0) {
        return "unknown";
    }
    return std::string(info.sysname) + " " + info.release + " " + info.machine;
}

}  // namespace qlog::bench
