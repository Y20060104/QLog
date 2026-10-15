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
#include <cstdlib>
#include <cstring>
#include <type_traits>

#include "qlog/platform/thread.hpp"
#include "qlog/utility/diagnostics.hpp"

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

struct thread_platform_def {
    pthread_t thread_handle;
};

struct thread_platform_processor {
    static void* thread_process(void* data) {
        thread* thread_ptr = static_cast<thread*>(data);

        thread_ptr->thread_id_ = thread::get_current_thread_id();
        thread_ptr->internal_run();

        return nullptr;
    }
};

thread::thread(thread_attr attr) : attr_(attr), status_(enum_thread_status::init) {
    thread_id_ = 0;

    platform_data_ = static_cast<thread_platform_def*>(malloc(sizeof(thread_platform_def)));

    new (platform_data_, qlog::utility::enum_new_dummy::dummy) thread_platform_def();
}

void thread::internal_run() {
    while (status_.load() != enum_thread_status::running &&
           status_.load() != enum_thread_status::pendding_cancel &&
           status_.load() != enum_thread_status::detached) {
        cpu_relax();
    }

    apply_thread_name();
    run();

    auto expected_status = enum_thread_status::running;

    if (!status_.compare_exchange_strong(expected_status, enum_thread_status::finished,
                                         memory_order::seq_cst, memory_order::seq_cst)) {
        expected_status = enum_thread_status::pendding_cancel;

        status_.compare_exchange_strong(expected_status, enum_thread_status::finished,
                                        memory_order::seq_cst, memory_order::seq_cst);
    }

    on_finished();

    thread_id_ = 0;

    qlog::utility::util::log_device_console(qlog::LogLevel::info, "thread cancel success");

    status_.store_seq_cst(enum_thread_status::released);
}

template <typename>
std::int32_t accept_setname_param_ver1(std::int32_t (*)(pthread_t, const char*, void*));
template <typename>
char accept_setname_param_ver1(...);

template <typename>
std::int32_t accept_setname_param_ver2(std::int32_t (*)(pthread_t, const char*));
template <typename>
char accept_setname_param_ver2(...);

template <typename>
std::int32_t accept_setname_param_ver3(void (*)(pthread_t, const char*));
template <typename>
char accept_setname_param_ver3(...);

template <typename>
std::int32_t accept_setname_param_ver4(std::int32_t (*)(const char*));
template <typename>
char accept_setname_param_ver4(...);
// 1) pthread_setname_np(pthread_t, const char*, void*)
template <typename U, typename = void>
struct set_thread_name_func_sfinae1 : std::false_type {};
#if defined(QLOG_HAVE_PTHREAD_NP)
template <typename U>
struct set_thread_name_func_sfinae1<
    U, std::void_t<std::enable_if_t<
           std::is_same<decltype(accept_setname_param_ver1<U>(&::pthread_setname_np)),
                        std::int32_t>::value,
           std::int32_t>>> : std::true_type {};
#endif
// 2) pthread_setname_np(pthread_t, const char*)
template <typename U, typename = void>
struct set_thread_name_func_sfinae2 : std::false_type {};
#if defined(QLOG_HAVE_PTHREAD_NP)
template <typename U>
struct set_thread_name_func_sfinae2<
    U, std::void_t<std::enable_if_t<
           std::is_same<decltype(accept_setname_param_ver2<U>(&::pthread_setname_np)),
                        std::int32_t>::value,
           std::int32_t>>> : std::true_type {};
#endif
// 3) pthread_set_name_np(pthread_t, const char*)
template <typename U, typename = void>
struct set_thread_name_func_sfinae3 : std::false_type {};
#if defined(QLOG_HAVE_PTHREAD_NP_UNIX)
template <typename U>
struct set_thread_name_func_sfinae3<
    U, std::void_t<std::enable_if_t<
           std::is_same<decltype(accept_setname_param_ver3<U>(&::pthread_set_name_np)),
                        std::int32_t>::value,
           std::int32_t>>> : std::true_type {};
#endif
// 4) pthread_setname_np(const char*)
template <typename U, typename = void>
struct set_thread_name_func_sfinae4 : std::false_type {};
#if defined(QLOG_HAVE_PTHREAD_NP)
template <typename U>
struct set_thread_name_func_sfinae4<
    U, std::void_t<std::enable_if_t<
           std::is_same<decltype(accept_setname_param_ver4<U>(&::pthread_setname_np)),
                        std::int32_t>::value,
           std::int32_t>>> : std::true_type {};
#endif
// 5) prctl(PR_SET_NAME)
template <typename U, typename = void>
struct set_thread_name_func_sfinae5 : std::false_type {};
#if defined(QLOG_HAVE_DLFCN)
template <typename U>
struct set_thread_name_func_sfinae5<
    U, std::void_t<std::enable_if_t<
           std::is_same<decltype(accept_prctl_param<U>(&::prctl)), std::int32_t>::value,
           std::int32_t>>> : std::true_type {};
#endif
#if defined(QLOG_HAVE_PTHREAD_NP)
template <typename U, std::enable_if_t<set_thread_name_func_sfinae1<U>::value, bool> = true>
bool set_thread_name_impl(U thread_handle, const std::string& thread_name) {
    using func_type = std::int32_t (*)(pthread_t, const char*, void*);
    auto func_ptr = reinterpret_cast<func_type>(reinterpret_cast<void*>(&::pthread_setname_np));
    std::int32_t set_name_result =
        func_ptr(thread_handle, "%s", const_cast<char*>(thread_name.c_str()));
    return set_name_result == 0;
}
#endif
#if defined(QLOG_HAVE_PTHREAD_NP)
template <typename U, std::enable_if_t<!set_thread_name_func_sfinae1<U>::value &&
                                           set_thread_name_func_sfinae2<U>::value,
                                       bool> = true>
bool set_thread_name_impl(U thread_handle, const std::string& thread_name) {
    using func_type = std::int32_t (*)(pthread_t, const char*);
    auto func_ptr = reinterpret_cast<func_type>(reinterpret_cast<void*>(&::pthread_setname_np));
    std::int32_t set_name_result = func_ptr(thread_handle, thread_name.c_str());
    return set_name_result == 0;
}
#endif
#if defined(QLOG_HAVE_PTHREAD_NP_UNIX)
template <typename U, std::enable_if_t<!set_thread_name_func_sfinae1<U>::value &&
                                           !set_thread_name_func_sfinae2<U>::value &&
                                           set_thread_name_func_sfinae3<U>::value,
                                       bool> = true>
bool set_thread_name_impl(U thread_handle, const std::string& thread_name) {
    pthread_set_name_np(thread_handle, thread_name.c_str());
    return true;
}
#endif
#if defined(QLOG_HAVE_PTHREAD_NP)
template <typename U,
          std::enable_if_t<
              !set_thread_name_func_sfinae1<U>::value && !set_thread_name_func_sfinae2<U>::value &&
                  !set_thread_name_func_sfinae3<U>::value && set_thread_name_func_sfinae4<U>::value,
              bool> = true>
bool set_thread_name_impl(U thread_handle, const std::string& thread_name) {
    (void)thread_handle;
    assert(thread_handle == pthread_self() &&
           "pthread_setname_np can only set current thread name!");
    using func_type = std::int32_t (*)(const char*);
    func_type func_ptr =
        reinterpret_cast<func_type>(reinterpret_cast<void*>(&::pthread_setname_np));
    std::int32_t set_name_result = func_ptr(thread_name.c_str());
    return set_name_result >= 0;
}
#endif
#if defined(QLOG_HAVE_DLFCN)
template <typename U,
          std::enable_if_t<
              !set_thread_name_func_sfinae1<U>::value && !set_thread_name_func_sfinae2<U>::value &&
                  !set_thread_name_func_sfinae3<U>::value &&
                  !set_thread_name_func_sfinae4<U>::value && set_thread_name_func_sfinae5<U>::value,
              bool> = true>
bool set_thread_name_impl(U thread_handle, const std::string& thread_name) {
    (void)thread_handle;
    assert(thread_handle == pthread_self() &&
           "prctl(PR_SET_NAME) can only set current thread name!");
    std::int32_t set_name_result = prctl(PR_SET_NAME, thread_name.c_str());
    return set_name_result >= 0;
}
#endif
template <typename U, std::enable_if_t<!set_thread_name_func_sfinae1<U>::value &&
                                           !set_thread_name_func_sfinae2<U>::value &&
                                           !set_thread_name_func_sfinae3<U>::value &&
                                           !set_thread_name_func_sfinae4<U>::value &&
                                           !set_thread_name_func_sfinae5<U>::value,
                                       bool> = true>
bool set_thread_name_impl(U thread_handle, const std::string& thread_name) {
    (void)thread_handle;
    (void)thread_name;
    return false;
}

void thread::set_thread_name(const std::string& thread_name) {
    thread_name_ = thread_name;
    if (thread_name_.size() >= 15) {
        std::string truncated_name = thread_name_.substr(0, 15);
        qlog::utility::util::log_device_console(
            qlog::LogLevel::warning,
            "thread name \"%s\" exceed max length limit of android, the length of thread name can "
            "not be larger than 15. so it will be truncated to \"%s\"",
            thread_name_.c_str(), truncated_name.c_str());
        thread_name_ = truncated_name;
    }
    auto current_status = status_.load();
    if (current_status == enum_thread_status::running) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::warning,
            "trying to set thread name \"%s\" when thread have already been running, thread id "
            ":%" PRIu64 ", thread status:%" PRId32,
            thread_name.c_str(), static_cast<std::uint64_t>(thread_id_),
            (std::int32_t)current_status);
    }
}

void thread::start() {
    auto current_status = status_.load();
    if (current_status == enum_thread_status::running) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::warning,
            "trying to start a thread \"%s\" which is still running, thread id :%" PRIu64
            ", thread status:%" PRId32,
            thread_name_.c_str(), static_cast<std::uint64_t>(thread_id_),
            (std::int32_t)current_status);
        return;
    }
    if (current_status == enum_thread_status::detached) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::fatal,
            "trying to start a thread \"%s\" which is detached, thread id :%" PRIu64
            ", thread status:%" PRId32,
            thread_name_.c_str(), static_cast<std::uint64_t>(thread_id_),
            (std::int32_t)current_status);
        assert(false && "trying to start a detached thread");
        return;
    }
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, attr_.max_stack_size);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    auto create_result = pthread_create(&platform_data_->thread_handle, &attr,
                                        &thread_platform_processor::thread_process, (void*)this);
    pthread_attr_destroy(&attr);
    if (create_result == EAGAIN) {
        qlog::utility::util::log_device_console(qlog::LogLevel::fatal,
                                                "create thread \"%s\" failed, error code:EAGAIN",
                                                thread_name_.c_str());
    } else if (create_result == EINVAL) {
        qlog::utility::util::log_device_console(qlog::LogLevel::fatal,
                                                "create thread \"%s\" failed, error code:EINVAL",
                                                thread_name_.c_str());
    } else if (create_result == EPERM) {
        qlog::utility::util::log_device_console(qlog::LogLevel::fatal,
                                                "create thread \"%s\" failed, error code:EPERM",
                                                thread_name_.c_str());
    } else if (create_result != 0) {
        qlog::utility::util::log_device_console(qlog::LogLevel::fatal,
                                                "create thread \"%s\" failed, error code:%" PRId32,
                                                thread_name_.c_str(), create_result);
    }
    if (create_result != 0) {
        return;
    }
    thread_id_ = (thread_id)(platform_data_->thread_handle);

    // If the previous status is init or released, we can set it to running.
    // internal_run() is spinning and waiting for this status change.
    auto expected_status = enum_thread_status::init;
    if (!status_.compare_exchange_strong(expected_status, enum_thread_status::running)) {
        expected_status = enum_thread_status::released;
        status_.compare_exchange_strong(expected_status, enum_thread_status::running);
    }
}

void thread::join() {
    auto join_result = pthread_join(platform_data_->thread_handle, nullptr);
    if (ESRCH == join_result) {
        // maybe thread is not created yet.
        return;
    } else if (EINVAL == join_result) {
        // maybe thread is detached or already joined.
        return;
    } else if (0 != join_result) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error,
            "join thread \"%s\" failed, thread_id:%" PRIu64 ", error code:%" PRId32,
            thread_name_.c_str(), static_cast<std::uint64_t>(thread_id_), join_result);
    }
}

void thread::detach() {
    auto current_status = status_.load();
    if (current_status != enum_thread_status::running) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::warning,
            "trying to detach a thread \"%s\" which is not running, thread id :%" PRIu64
            ", thread status:%" PRId32,
            thread_name_.c_str(), static_cast<std::uint64_t>(thread_id_),
            (std::int32_t)current_status);
        return;
    }
    auto detach_result = pthread_detach(platform_data_->thread_handle);
    if (detach_result != 0) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error,
            "detach thread \"%s\" failed, thread_id:%" PRIu64 ", error code:%" PRId32,
            thread_name_.c_str(), static_cast<std::uint64_t>(thread_id_), detach_result);
    }
    status_.store_seq_cst(enum_thread_status::detached);
}

thread::~thread() {
    // auto current_status = status_.load();
    // This is not robust when program is exited by error
    // assert(current_status != enum_thread_status::running && "thread instance is destructed when
    // thread is still running");
    platform_data_->~thread_platform_def();
    free(platform_data_);
    platform_data_ = nullptr;
}

void thread::apply_thread_name() {
    set_thread_name_impl<pthread_t>(pthread_self(), thread_name_);
}

}  // namespace qlog::platform
