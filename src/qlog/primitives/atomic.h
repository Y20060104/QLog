#pragma once
#include <atomic>

#include "cpu_relax.h"

namespace qlog
{
template<typename T> class atomic
{
private:
    std::atomic<T> value_;

public:
    atomic(T value = T())
        : value_(value)
    {
    }

    atomic(const atomic&) = delete;
    atomic& operator=(const atomic&) = delete;

    atomic(atomic&&) = delete;
    atomic& operator=(atomic&&) = delete;

    // 定义基础操作，
    T load(std::memory_order order) const
    {
        return value_.load(order);
    }
    void store(T value, std::memory_order order)
    {
        return value_.store(value, order);
    }

    // 一些常用的便捷操作
    T load_acquire() const
    {
        return value_.load(std::memory_order_acquire);
    }

    void store_release(T value
    ) // 这里不加const 是const 承诺不修改非静态成员的状态，这个函数本来就要修改状态 C+++忘完了哈哈哈
    {
        value_.store(value, std::memory_order_release);
    }

    T load_relaxed() const
    {
        return value_.load(std::memory_order_relaxed);
    }

    void store_relaxed(T value)
    {
        value_.store(value, std::memory_order_relaxed);
    }

    // CAS接口 用于fetch_add 的回滚算法
    bool compare_exchange_weak(
        T& expected, T desired, std::memory_order success, std::memory_order failure
    )
    {
        return value_.compare_exchange_weak(expected, desired, success, failure);
    }

    bool compare_exchange_strong(
        T& expected, T desired, std::memory_order success, std::memory_order failure
    )
    {
        return value_.compare_exchange_strong(expected, desired, success, failure);
    }

    T fetch_add(T number, std::memory_order order)
    {
        return value_.fetch_add(number, order);
    }

    T fetch_add_relaxed(T number)
    {
        return value_.fetch_add(number, std::memory_order_relaxed);
    }

    T fetch_sub(T number, std::memory_order order)
    {
        return value_.fetch_sub(number, order);
    }

    T fetch_and(T mask, std::memory_order order)
    {
        return value_.fetch_and(mask, order);
    }

    T fetch_or(T mask, std::memory_order order)
    {
        return value_.fetch_or(mask, order);
    }

    T fetch_xor(T val, std::memory_order order)
    {
        return value_.fetch_xor(val, order);
    }
    T exchange(T desired, std::memory_order order)
    {
        return value_.exchange(desired, order);
    }
};
} // namespace qlog