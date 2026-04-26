#include "atomic.h"
#include "cpu_relax.h"

namespace qlog
{

class spin_lock_rw
{
private:
    // 状态字段（32 位 atomic）
    // Bit [0]     : write_locked (1 = 写锁被持有)
    // Bit [1..30] : read_count   (读者数量)
    atomic<uint32_t> state_{0};

    static constexpr uint32_t WRITE_BIT = 1U;      // Bit 0
    static constexpr uint32_t READ_INCREMENT = 2U; // Bit 1+ per reader

public:
    spin_lock_rw() = default;
    ~spin_lock_rw() = default;

    // 禁用拷贝/移动
    spin_lock_rw(const spin_lock_rw&) = delete;
    spin_lock_rw& operator=(const spin_lock_rw&) = delete;
    spin_lock_rw(spin_lock_rw&&) = delete;
    spin_lock_rw& operator=(spin_lock_rw&&) = delete;

    void read_lock()
    {
        uint32_t state;
        while (true)
        {
            state = state_.load(std::memory_order_relaxed);

            // 检查是否被写锁持有
            if (state & WRITE_BIT)
            {
                // 写锁被持有，yield CPU
                cpu_relax();
                continue;
            }

            // 尝试递增读计数
            uint32_t next = state + READ_INCREMENT;
            if (state_.compare_exchange_weak(
                    state, next, std::memory_order_acquire, std::memory_order_relaxed
                ))
            {
                // CAS 成功，获得读锁
                break;
            }

            // CAS 失败，重试
            cpu_relax();
        }
    }

    void read_unlock()
    {
        state_.fetch_sub(READ_INCREMENT, std::memory_order_release);
    }

    void write_lock()
    {

        uint32_t state;
        while (true)
        {
            state = state_.load(std::memory_order_relaxed);

            // 尝试设置写锁位
            uint32_t next = state | WRITE_BIT;
            if (state_.compare_exchange_weak(
                    state, next, std::memory_order_acquire, std::memory_order_relaxed
                ))
            {
                // CAS 成功
                break;
            }

            // CAS 失败，重试
            cpu_relax();
        }

        while (true)
        {
            state = state_.load(std::memory_order_acquire);

            // 检查是否无读者（清除 WRITE_BIT 后检查）
            if ((state & ~WRITE_BIT) == 0)
            {
                // 没有读者
                break;
            }

            // 还有读者，自旋等待
            cpu_relax();
        }
    }

    void write_unlock()
    {
        state_.fetch_and(~WRITE_BIT, std::memory_order_release);
    }

    void lock_shared()
    {
        read_lock();
    }

    void unlock_shared()
    {
        read_unlock();
    }
    void lock()
    {
        write_lock();
    }
    void unlock()
    {
        write_unlock();
    }

    bool try_lock()
    {
        uint32_t state = state_.load_relaxed();
        if (state & WRITE_BIT)
            return false;
        uint32_t desired = state | WRITE_BIT;
        return state_.compare_exchange_strong(
            state, desired, std::memory_order_acquire, std::memory_order_relaxed
        );
    }
};

} // namespace qlog
