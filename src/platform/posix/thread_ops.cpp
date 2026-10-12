#include "qlog/platform/thread_ops.hpp"

#include <pthread.h>
#include <sched.h>
#include <time.h>

#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#if defined(__has_include)
#if __has_include(<sys/prctl.h>)
#include <sys/prctl.h>
#define QLOG_HAVE_DLFCN
#endif

#ifndef QLOG_HAVE_DLFCN
#if __has_include(<pthread_np.h>)
#include <pthread_np.h>
#define QLOG_HAVE_PTHREAD_NP_UNIX
#else
#define QLOG_HAVE_PTHREAD_NP
#endif
#endif
#endif

namespace qlog::platform {
namespace {
thread_local thread_id current_thread_id_ = 0;

// CR:bqlog也有很多 匿名空间嘛？ 是不是应该把这些单独做一个集合呢？

template <typename Handle>
thread_id convert_thread_id(Handle handle) {
    if constexpr (std::is_pointer_v<Handle>) {
        return static_cast<thread_id>(reinterpret_cast<std::uintptr_t>(handle));
    } else {
        return static_cast<thread_id>(handle);
    }
}
}  // namespace

thread_id get_current_thread_id() {
    if (current_thread_id_ == 0) {
        current_thread_id_ = convert_thread_id(::pthread_self());
    }

    return current_thread_id_;
}

void yield() {
    ::sched_yield();
}

void cpu_relax() {
#if defined(__arm__) || defined(__aarch64__)
    __asm__ __volatile__("yield");
#else
    __asm__ __volatile__("pause");
#endif
}

void sleep(std::uint64_t millsec) {
    timespec req;
    timespec rem;

    req.tv_sec = static_cast<time_t>(millsec / 1000U);
    req.tv_nsec = static_cast<long>((millsec % 1000U) * 1000000U);

    while (::nanosleep(&req, &rem) == -1)  // CR:这是在干什么
    {
        if (errno == EINTR) {  // CR:errno哪来的？
            req = rem;         // CR：这完善赋值
        } else {
            break;
        }
    }
}

constexpr std::size_t thread_name_max_len() {
#if defined(__linux__) || defined(__ANDROID__)
    return 16;
#elif defined(__APPLE__)
    return 64;
#elif defined(__FreeBSD__) || defined(__DragonFly__)
    return 64;
#else
    return 32;
#endif
}

template <typename>
std::int32_t accept_getname_param_ver1(std::int32_t (*)(pthread_t, char*, std::size_t));

template <typename>
char accept_getname_param_ver1(...);

template <typename>
std::int32_t accept_getname_param_ver2(void (*)(pthread_t, char*, std::size_t));

template <typename>
char accept_getname_param_ver2(...);

template <typename>
std::int32_t accept_prctl_param(std::int32_t (*)(std::int32_t, ...));

template <typename>
char accept_prctl_param(...);

template <typename U, typename = void>
struct get_thread_name_func_sfinae1 : std::false_type {};  // CR:sfinael是什么？

#if defined(QLOG_HAVE_PTHREAD_NP)
template <typename U>
struct get_thread_name_func_sfinae1<
    U, std::void_t<std::enable_if_t<
           std::is_same<decltype(accept_getname_param_ver1<U>(&::pthread_getname_np)),
                        std::int32_t>::value,
           std::int32_t>>> : std::true_type {};
#endif

template <typename U, typename = void>
struct get_thread_name_func_sfinae2 : std::false_type {};

#if defined(QLOG_HAVE_PTHREAD_NP_UNIX)
template <typename U>
struct get_thread_name_func_sfinae2<
    U, std::void_t<std::enable_if_t<
           std::is_same<decltype(accept_getname_param_ver2<U>(&::pthread_get_name_np)),
                        std::int32_t>::value,
           std::int32_t>>> : std::true_type {};
#endif

// CR:ver1/2 有什么区别 ？为什么要ifdefined来区分两种getthread name

template <typename U, typename = void>
struct get_thread_name_func_sfinae3 : std::false_type {};

#if defined(QLOG_HAVE_DLFCN)
template <typename U>
struct get_thread_name_func_sfinae3<
    U, std::void_t<std::enable_if_t<
           std::is_same<decltype(accept_prctl_param<U>(&::prctl)), std::int32_t>::value,
           std::int32_t>>> : std::true_type {};
#endif

#if defined(QLOG_HAVE_PTHREAD_NP)
template <typename U, std::enable_if_t<get_thread_name_func_sfinae1<U>::value, bool> = true>
std::string get_thread_name_impl(U thread_handle) {
    char thread_name_buf[thread_name_max_len()];

    std::int32_t get_name_result =
        pthread_getname_np(thread_handle, thread_name_buf, sizeof(thread_name_buf));

    if (get_name_result != 0) {
        return "";
    }

    return std::string(thread_name_buf);
}
#endif

#if defined(QLOG_HAVE_PTHREAD_NP_UNIX)
template <typename U, std::enable_if_t<!get_thread_name_func_sfinae1<U>::value &&
                                           get_thread_name_func_sfinae2<U>::value,
                                       bool> = true>
std::string get_thread_name_impl(U thread_handle) {
    char thread_name_buf[thread_name_max_len()] = {0};

    pthread_get_name_np(thread_handle, thread_name_buf, sizeof(thread_name_buf));

    return std::string(thread_name_buf);
}
#endif

#if defined(QLOG_HAVE_DLFCN)
template <typename U, std::enable_if_t<!get_thread_name_func_sfinae1<U>::value &&
                                           !get_thread_name_func_sfinae2<U>::value &&
                                           get_thread_name_func_sfinae3<U>::value,
                                       bool> = true>
std::string get_thread_name_impl(U thread_handle) {
    (void)thread_handle;

    assert(thread_handle == pthread_self() &&
           "prctl(PR_GET_NAME) can only get current thread name!");

    char thread_name_buf[thread_name_max_len()] = {0};

    std::int32_t get_name_result = prctl(PR_GET_NAME, thread_name_buf);
    if (get_name_result < 0) {
        return "";
    }

    return std::string(thread_name_buf);
}
#endif

template <typename U, std::enable_if_t<!get_thread_name_func_sfinae1<U>::value &&
                                           !get_thread_name_func_sfinae2<U>::value &&
                                           !get_thread_name_func_sfinae3<U>::value,
                                       bool> = true>
std::string get_thread_name_impl(U thread_handle) {
    char thread_name_buf[64] = {0};

    std::uint64_t tid = static_cast<std::uint64_t>(thread_handle);
    snprintf(thread_name_buf, sizeof(thread_name_buf), "pthread_%" PRIu64, tid);

    return thread_name_buf;
}

std::string get_current_thread_name() {
    return get_thread_name_impl<pthread_t>(pthread_self());
}
// CR:SFINAE是什么意思 用来做什么？

}  // namespace qlog::platform
