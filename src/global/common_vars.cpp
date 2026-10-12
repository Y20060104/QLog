#include "qlog/global/common_vars.hpp"

#include <cstddef>
#include <cstdint>

#include "qlog/utility/diagnostics.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if (defined(__i386__) || defined(__x86_64__)) && !defined(_MSC_VER)
#include <cpuid.h>
#endif

#if defined(__linux__) && (defined(__arm__) || defined(__aarch64__))
#include <sys/auxv.h>
#endif

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace qlog {
static common_global_vars* common_global_var_default_initer_ = &common_global_vars::get();

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
static bool check_avx2_support() {
    bool result = false;
    std::int32_t regs[4];
    std::int32_t id;

#if defined(_MSC_VER)
    __cpuid(regs, 0);
    id = regs[0];
#else
    id = static_cast<std::int32_t>(__get_cpuid_max(0, reinterpret_cast<std::uint32_t*>(regs)));
#endif

    if (id >= 7) {
#if defined(_MSC_VER)
        __cpuidex(regs, 7, 0);
#else
        __cpuid_count(7, 0, regs[0], regs[1], regs[2], regs[3]);
#endif
        // EBX bit 5 is AVX2.
        result = (regs[1] & (1 << 5)) != 0;
    }

#ifdef QLOG_UNIT_TEST
    qlog::utility::util::set_log_device_console_min_level(qlog::LogLevel::info);
#endif

#if !defined(NDEBUG) || defined(QLOG_UNIT_TEST)
    qlog::utility::util::log_device_console(qlog::LogLevel::info, "Hardware AVX2 support:%s",
                                            result ? "true" : "false");
#endif

#ifdef QLOG_UNIT_TEST
    qlog::utility::util::set_log_device_console_min_level(qlog::LogLevel::warning);
#endif

    return result;
}
#endif

static bool check_crc32_support() {
    bool result = false;

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    std::int32_t regs[4];

#if defined(_MSC_VER)
    __cpuid(regs, 1);
#else
    __cpuid(1, regs[0], regs[1], regs[2], regs[3]);
#endif

    // ECX bit 20 is SSE4.2.
    result = (regs[2] & (1 << 20)) != 0;

#elif (defined(__arm__) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)) && \
    defined(__ARM_FEATURE_CRC32)

#if defined(_WIN32)
    result = IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE) != 0;

#elif defined(__APPLE__)
    std::int32_t val = 0;
    std::size_t len = sizeof(val);

    if (sysctlbyname("hw.optional.armv8_crc32", &val, &len, nullptr, 0) == 0) {
        result = (val != 0);
    }

#elif defined(__linux__) || defined(__ANDROID__)

#ifndef AT_HWCAP
#define AT_HWCAP 16
#endif

#ifndef AT_HWCAP2
#define AT_HWCAP2 26
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    auto aux = getauxval(AT_HWCAP);
    // ARM64: HWCAP_CRC32 is bit 7.
    result = (aux & (1 << 7)) != 0;
#else
    auto aux = getauxval(AT_HWCAP2);
    // ARM32: HWCAP2_CRC32 is bit 4.
    result = (aux & (1 << 4)) != 0;
#endif

#endif
#endif

#ifdef QLOG_UNIT_TEST
    qlog::utility::util::set_log_device_console_min_level(qlog::LogLevel::info);
#endif

#if !defined(NDEBUG) || defined(QLOG_UNIT_TEST)
    qlog::utility::util::log_device_console(qlog::LogLevel::info, "Hardware CRC32 support:%s",
                                            result ? "true" : "false");
#endif

#ifdef QLOG_UNIT_TEST
    qlog::utility::util::set_log_device_console_min_level(qlog::LogLevel::warning);
#endif

    return result;
}
// CPUID 的 leaf 表示查询类别。 AVX2 先查询 leaf 0，确定是否支持 leaf 7，再读取 leaf 7、subleaf 0 的
// EBX 第 5 位。CRC 指令能力在 x86 上读取 leaf 1 的 ECX 第 20 位，即 SSE4.2 没看懂这段话，讲解一下
// 这个 AVX2 函数按参考只检查 CPU 能力位能力位是什么？如何经检查的

// 获取页大小 → x86 下检查 AVX2 → 检查 CRC → Windows 下取得进程句柄。partial_destruct() 为空与当前
// BQLog 一致，不需要自行加入 delete file_manager_inst_。结合代码讲解

common_global_vars::common_global_vars() {
#if defined(_WIN32)
    SYSTEM_INFO sysInfo;
    ::GetSystemInfo(&sysInfo);
    page_size_ = static_cast<std::size_t>(sysInfo.dwPageSize);
#else
    page_size_ = static_cast<std::size_t>(::getpagesize());
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    avx2_support_ = check_avx2_support();
#endif

    crc32_supported_ = check_crc32_support();

#if defined(_WIN32)
    stack_trace_process_ = ::GetCurrentProcess();
#endif
}

void common_global_vars::partial_destruct() {}

}  // namespace qlog
