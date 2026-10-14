#pragma once

#include <cassert>
#include <limits>
#include <type_traits>

#include "qlog/platform/atomic/atomic.hpp"  //CR:atomic.cpp是不是应该收录再atomic文件夹下atomic/atomic.cpp？ 如果是 请一并修改include等
#include "qlog/platform/thread_ops.hpp"
#include "qlog/utility/type_tools.hpp"
#include "qlog/utility/utility_types.hpp"

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

class alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) spin_lock_rw_crazy {
   public:
    typedef qlog::utility::condition_type_t<sizeof(void*) == 4, std::int32_t, std::int64_t>
        counter_type;

    static constexpr counter_type write_lock_mark_value =
        (std::numeric_limits<counter_type>::min)();

    qlog::platform::atomic<counter_type> counter_;
    char padding_[qlog::utility::QLOG_CACHE_LINE_SIZE - sizeof(counter_)];

   public:
    spin_lock_rw_crazy() : counter_(0) {
        (void)padding_;
    }

    spin_lock_rw_crazy(const spin_lock_rw_crazy&) = delete;
    spin_lock_rw_crazy(spin_lock_rw_crazy&&) noexcept = delete;
    spin_lock_rw_crazy& operator=(const spin_lock_rw_crazy&) = delete;
    spin_lock_rw_crazy& operator=(spin_lock_rw_crazy&&) noexcept = delete;

    inline void read_lock() {
        while (true) {
            counter_type previous_counter = counter_.fetch_add_acq_rel(1);

            if (previous_counter >= 0) {
                break;
            }

            counter_.fetch_sub_relaxed(1);

            while (true) {
                qlog::platform::cpu_relax();
                counter_type current_counter = counter_.load_acquire();

                if (current_counter >= 0) {
                    break;
                }
            }
        }
    }

    inline bool try_read_lock() {
        counter_type previous_counter = counter_.fetch_add_acq_rel(1);

        if (previous_counter >= 0) {
            return true;
        }

        counter_.fetch_sub_relaxed(1);
        return false;
    }

    inline void read_unlock() {
        counter_type previous_counter = counter_.fetch_sub_release(1);
        (void)previous_counter;
    }

    inline void write_lock() {
        while (true) {
            counter_type expected_counter = 0;

            if (counter_.compare_exchange_strong(expected_counter, write_lock_mark_value,
                                                 qlog::platform::memory_order::acq_rel,
                                                 qlog::platform::memory_order::acquire)) {
                break;
            }

            qlog::platform::cpu_relax();
        }
    }

    inline bool try_write_lock() {
        counter_type expected_counter = 0;

        if (counter_.compare_exchange_strong(expected_counter, write_lock_mark_value,
                                             qlog::platform::memory_order::acq_rel,
                                             qlog::platform::memory_order::acquire)) {
            return true;
        }

        return false;
    }

    inline void write_unlock() {
        while (true) {
            counter_type expected_counter = write_lock_mark_value;

            if (counter_.compare_exchange_strong(expected_counter, 0,
                                                 qlog::platform::memory_order::acq_rel,
                                                 qlog::platform::memory_order::acquire)) {
                break;
            }
        }
    }
};

class scoped_spin_lock_read_crazy {
   private:
    spin_lock_rw_crazy& lock_;

   public:
    scoped_spin_lock_read_crazy() = delete;

    explicit scoped_spin_lock_read_crazy(spin_lock_rw_crazy& lock) : lock_(lock) {
        lock_.read_lock();
    }

    ~scoped_spin_lock_read_crazy() {
        lock_.read_unlock();
    }
};

class scoped_try_spin_lock_read_crazy {
   private:
    spin_lock_rw_crazy& lock_;
    bool locked_;

   public:
    scoped_try_spin_lock_read_crazy() = delete;

    explicit scoped_try_spin_lock_read_crazy(spin_lock_rw_crazy& lock) : lock_(lock) {
        locked_ = lock_.try_read_lock();
    }

    bool owns_lock() const {
        return locked_;
    }

    ~scoped_try_spin_lock_read_crazy() {
        if (locked_) {
            lock_.read_unlock();
        }
    }
};

class scoped_spin_lock_write_crazy {
   private:
    spin_lock_rw_crazy& lock_;

   public:
    scoped_spin_lock_write_crazy() = delete;

    explicit scoped_spin_lock_write_crazy(spin_lock_rw_crazy& lock) : lock_(lock) {
        lock_.write_lock();
    }

    ~scoped_spin_lock_write_crazy() {
        lock_.write_unlock();
    }
};

class scoped_try_spin_lock_write_crazy {
   private:
    spin_lock_rw_crazy& lock_;
    bool locked_;

   public:
    scoped_try_spin_lock_write_crazy() = delete;

    explicit scoped_try_spin_lock_write_crazy(spin_lock_rw_crazy& lock) : lock_(lock) {
        locked_ = lock_.try_write_lock();
    }

    bool owns_lock() const {
        return locked_;
    }

    ~scoped_try_spin_lock_write_crazy() {
        if (locked_) {
            lock_.write_unlock();
        }
    }
};

}  // namespace qlog::platform