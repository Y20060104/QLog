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
    // 定义基础操作，load和store必须强制同步，确保拿到或者存入的数据是最新的
    T load(std::memory_order order = std::memory_order_seq_cst) const
    {
        return value_.load(order);
    }
    void store(T value, std::memory_order order = std::memory_order_seq_cst)
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

    // 预留CAS接口 用于fetch_add 的回滚算法
};
} // namespace qlog