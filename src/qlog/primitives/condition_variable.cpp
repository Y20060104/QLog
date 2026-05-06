// src/qlog/primitives/condition_variable.cpp
// 替换全部 TODO 空实现

#include "condition_variable.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>

#include <cerrno>
#endif

namespace qlog::platform
{

mutex::mutex()
{
#ifdef _WIN32
    auto* cs = new CRITICAL_SECTION;
    InitializeCriticalSection(cs);
    platform_data_ = cs;
#else
    auto* m = new pthread_mutex_t;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
    pthread_mutex_init(m, &attr);
    pthread_mutexattr_destroy(&attr);
    platform_data_ = m;
#endif
}

mutex::~mutex()
{
#ifdef _WIN32
    DeleteCriticalSection(static_cast<CRITICAL_SECTION*>(platform_data_));
    delete static_cast<CRITICAL_SECTION*>(platform_data_);
#else
    pthread_mutex_destroy(static_cast<pthread_mutex_t*>(platform_data_));
    delete static_cast<pthread_mutex_t*>(platform_data_);
#endif
    platform_data_ = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// mutex — BqLog platform/thread/bq_mutex.h/cpp 对齐
// platform_data_ 存储 pthread_mutex_t* 或 CRITICAL_SECTION*
// ─────────────────────────────────────────────────────────────────────────────

void mutex::lock()
{
#ifdef _WIN32
    EnterCriticalSection(static_cast<CRITICAL_SECTION*>(platform_data_));
#else
    pthread_mutex_lock(static_cast<pthread_mutex_t*>(platform_data_));
#endif
}

void mutex::unlock()
{
#ifdef _WIN32
    LeaveCriticalSection(static_cast<CRITICAL_SECTION*>(platform_data_));
#else
    pthread_mutex_unlock(static_cast<pthread_mutex_t*>(platform_data_));
#endif
}

bool mutex::try_lock()
{
#ifdef _WIN32
    return TryEnterCriticalSection(static_cast<CRITICAL_SECTION*>(platform_data_)) != 0;
#else
    return pthread_mutex_trylock(static_cast<pthread_mutex_t*>(platform_data_)) == 0;
#endif
}

// ─── scoped_lock ─────────────────────────────────────────────────────────────
scoped_lock::scoped_lock(mutex& m)
    : m_(m)
{
    m_.lock();
}
scoped_lock::~scoped_lock()
{
    m_.unlock();
}

// ─────────────────────────────────────────────────────────────────────────────
// condition_variable — BqLog platform/thread/bq_condition_variable.cpp 对齐
// 构造/析构分配和释放 pthread_cond_t；wait/wait_for 直接操作底层句柄
// ─────────────────────────────────────────────────────────────────────────────

condition_variable::condition_variable()
{
#ifdef _WIN32
    auto* cv = new CONDITION_VARIABLE;
    InitializeConditionVariable(cv);
    platform_data_ = cv;
#else
    // BqLog: pthread_cond_init with CLOCK_MONOTONIC 属性，避免系统时钟跳变
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
#if defined(__linux__)
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#endif
    auto* cv = new pthread_cond_t;
    pthread_cond_init(cv, &attr);
    pthread_condattr_destroy(&attr);
    platform_data_ = cv;
#endif
}

condition_variable::~condition_variable()
{
#ifdef _WIN32
    // Windows CONDITION_VARIABLE 无需显式销毁
    delete static_cast<CONDITION_VARIABLE*>(platform_data_);
#else
    pthread_cond_destroy(static_cast<pthread_cond_t*>(platform_data_));
    delete static_cast<pthread_cond_t*>(platform_data_);
#endif
    platform_data_ = nullptr;
}

void condition_variable::wait(scoped_lock& lock)
{
    // BqLog: scoped_lock 内部持有 mutex，condition_variable 是其 friend
    // 直接访问 m_.platform_data_ 取得底层句柄
#ifdef _WIN32
    SleepConditionVariableCS(
        static_cast<CONDITION_VARIABLE*>(platform_data_),
        static_cast<CRITICAL_SECTION*>(lock.m_.platform_data_),
        INFINITE
    );
#else
    pthread_cond_wait(
        static_cast<pthread_cond_t*>(platform_data_),
        static_cast<pthread_mutex_t*>(lock.m_.platform_data_)
    );
#endif
}

bool condition_variable::wait_for(scoped_lock& lock, int64_t ms)
{
#ifdef _WIN32
    BOOL ok = SleepConditionVariableCS(
        static_cast<CONDITION_VARIABLE*>(platform_data_),
        static_cast<CRITICAL_SECTION*>(lock.m_.platform_data_),
        static_cast<DWORD>(ms < 0 ? INFINITE : ms)
    );
    return ok != 0;
#else
    // 使用 CLOCK_MONOTONIC 避免系统时钟调整导致等待时间异常（BqLog 同策略）
    struct timespec ts;
#if defined(__linux__)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    // macOS / BSD: clock_gettime with CLOCK_REALTIME fallback
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    const int64_t ns_total = static_cast<int64_t>(ts.tv_nsec) + (ms % 1000) * 1'000'000LL;
    ts.tv_sec += ms / 1000 + ns_total / 1'000'000'000LL;
    ts.tv_nsec = static_cast<long>(ns_total % 1'000'000'000LL);
    if (ts.tv_nsec < 0)
    {
        ts.tv_sec--;
        ts.tv_nsec += 1'000'000'000L;
    }

    int ret = pthread_cond_timedwait(
        static_cast<pthread_cond_t*>(platform_data_),
        static_cast<pthread_mutex_t*>(lock.m_.platform_data_),
        &ts
    );
    return ret != ETIMEDOUT;
#endif
}

void condition_variable::notify_one()
{
#ifdef _WIN32
    WakeConditionVariable(static_cast<CONDITION_VARIABLE*>(platform_data_));
#else
    pthread_cond_signal(static_cast<pthread_cond_t*>(platform_data_));
#endif
}

void condition_variable::notify_all()
{
#ifdef _WIN32
    WakeAllConditionVariable(static_cast<CONDITION_VARIABLE*>(platform_data_));
#else
    pthread_cond_broadcast(static_cast<pthread_cond_t*>(platform_data_));
#endif
}

} // namespace qlog::platform