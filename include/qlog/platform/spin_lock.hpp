#pragma once

#include <cassert>
#include <type_traits>

#include "qlog/platform/atomic.hpp"  //CR:atomic.cpp是不是应该收录再atomic文件夹下？ 如果是 请一并修改include等
#include "qlog/platform/thread_ops.hpp"

namespace qlog::platform {

class spin_lock_zero_init {
   protected:
    qlog::platform::atomic_trivially_constructible<bool> value_;
#if !defined(NDEBUG) || defined(QLOG_UNIT_TEST)
    qlog::platform::atomic_trivially_constructible<qlog::platform::thread_id> thread_id_;
#endif

   protected:
#if !defined(NDEBUG) || defined(QLOG_UNIT_TEST)
    qlog::platform::atomic_trivially_constructible<qlog::platform::thread_id>& thread_id() {
        return thread_id_;
    }
#endif

   public:
    inline void lock() {
        while (true) {
            if (!value_.exchange(true, qlog::platform::memory_order::acquire)) {
#if !defined(NDEBUG) || defined(QLOG_UNIT_TEST)
                thread_id().store(qlog::platform::get_current_thread_id(),
                                  qlog::platform::memory_order::seq_cst);
#endif
                break;
            }

#if !defined(NDEBUG) || defined(QLOG_UNIT_TEST)
            assert(qlog::platform::get_current_thread_id() !=
                       thread_id().load(qlog::platform::memory_order::seq_cst) &&
                   "spin_lock is not reentrant");  // CR:什么时候会触发 断言
#endif

            while (value_.load(qlog::platform::memory_order::
                                   relaxed)) {  // CR：exchange成功后，while的判断条件是load成功
                                                // 就cpurelax
                                                // 如果exchange成功后load会失败吗？
                qlog::platform::cpu_relax();
            }
        }
    }

    inline bool try_lock() {
        if (!value_.exchange(true, qlog::platform::memory_order::acquire)) {
#if !defined(NDEBUG) || defined(QLOG_UNIT_TEST)
            thread_id().store(qlog::platform::get_current_thread_id(),
                              qlog::platform::memory_order::seq_cst);
#endif

            return true;
        }
#if !defined(NDEBUG) || defined(QLOG_UNIT_TEST)
        assert(qlog::platform::get_current_thread_id() !=
                   thread_id().load(qlog::platform::memory_order::seq_cst) &&
               "spin_lock is not reentrant");
#endif

        return false;
    }

    inline void unlock() {
#if !defined(NDEBUG) || defined(QLOG_UNIT_TEST)
        thread_id().store(0, qlog::platform::memory_order::seq_cst);
#endif
        value_.store(false, qlog::platform::memory_order::release);
    }
};

static_assert(std::is_trivially_constructible<spin_lock_zero_init>::value,
              "spin_lock_zero_init must be trivially constructible");
static_assert(std::is_trivially_destructible<spin_lock_zero_init>::value,
              "spin_lock_zero_init must be trivially destructible");

class spin_lock : public spin_lock_zero_init {
   public:
    spin_lock() {
        value_.store_seq_cst(false);
#if !defined(NDEBUG) || defined(QLOG_UNIT_TEST)
        thread_id_.store_seq_cst(0);
#endif
    }

    spin_lock(const spin_lock&) = delete;
    spin_lock(spin_lock&&) noexcept = delete;
    spin_lock& operator=(const spin_lock&) = delete;
};

class scoped_spin_lock {
   private:
    spin_lock_zero_init& lock_;

   public:
    scoped_spin_lock() = delete;

    explicit scoped_spin_lock(spin_lock_zero_init& lock) : lock_(lock) {
        lock_.lock();
    }

    ~scoped_spin_lock() {
        lock_.unlock();
    }
};

class scoped_try_spin_lock {
   private:
    spin_lock_zero_init& lock_;
    bool locked_;

   public:
    scoped_try_spin_lock() = delete;

    explicit scoped_try_spin_lock(spin_lock_zero_init& lock) : lock_(lock) {
        locked_ = lock_.try_lock();
    }

    bool owns_lock() const {
        return locked_;
    }

    ~scoped_try_spin_lock() {
        if (locked_) {
            lock_.unlock();
        }
    }
};

}  // namespace qlog::platform