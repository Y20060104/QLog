#pragma once
#include <atomic>

#include "cpu_relax.h"
namespace qlog
{
class spin_lock
{
private:
    std::atomic<bool> locked_ = false;

public:
    inline void lock()
    {
        while (true)
        {
            if (!locked_.exchange(true, std::memory_order_acquire))
            {
                break; // 成功抢到锁
            }
            while (locked_.load(std::memory_order_relaxed))
            {
                cpu_relax(); // 让cpu休息 优化性能
            }
        }
    }

    inline void unlock()
    {
        locked_.store(false, std::memory_order_release);
    }
};
class scoped_spin_lock
{
public:
    explicit scoped_spin_lock(spin_lock& lock)
        : lock_(lock)
    {
        lock_.lock();
    }
    ~scoped_spin_lock()
    {
        lock_.unlock();
    }

    scoped_spin_lock(const scoped_spin_lock&) = delete;
    scoped_spin_lock& operator=(const scoped_spin_lock&) = delete;

private:
    spin_lock& lock_;
};
} // namespace qlog