#pragma once

#include "qlog/buffer/log_buffer_defs.h"
#include "qlog/buffer/mpsc_ring_buffer.h"
#include "qlog/buffer/spsc_ring_buffer.h"
#include "qlog/primitives/spin_lock_rw.h"

#include <cstdint>
#include <vector>

namespace qlog
{
class alignas(64) log_buffer
{
public:
    static constexpr uint64_t HP_CALL_FREQUENCY_CHECK_INTERVAL_MS = 1000;
    static constexpr uint64_t DEFAULT_HP_THRESHOLD = 1000;
    explicit log_buffer(
        uint32_t lp_capacity_bytes,
        uint32_t hp_capacity_per_thread_bytes,
        uint64_t hp_threshould = DEFAULT_HP_THRESHOLD
    );

    ~log_buffer();

    // 禁用拷贝
    log_buffer(const log_buffer&) = delete;
    log_buffer& operator=(const log_buffer&) = delete;

    // 生产者接口(多线程安全)
    [[nodiscard]] void* alloc_write_chunk(uint32_t size, uint64_t current_time_ms);
    void commit_write_chunk(void* data_ptr);

    // 消费者接口（单线程）
    [[nodiscard]] const void* read_chunk(uint32_t& out_size);
    void commit_read_chunk(const void* data_ptr);

    // 等待所有已提交数据可被消费（用于 flush/force_flush）
    void flush();

private:
    // 内部类型

    // HP buffer 池：每个线程有独立的 spsc_ring_buffer
    // key = TLS 指针（线程唯一标识），value = spsc 实例
    // 访问时需持有 hp_pool_lock_
    struct hp_buffer_entry
    {
        spsc_ring_buffer buffer;
        bool is_active = true; // 线程退出后设为 false，等待消费完毕再释放
        log_tls_buffer_info* tls_info = nullptr;
    };

    // 当前正在读的 entry 类型（HP 还是 LP），供 return_read_chunk 用
    enum class active_read_src : uint8_t
    {
        none,
        hp,
        lp
    };

private:
    // 生产者
    // 获取（或创建）当前线程的 HP buffer
    spsc_ring_buffer* get_or_create_hp_buffer(log_tls_buffer_info& tls_info);
    // 获取（或创建）当前线程的 TLS 状态
    log_tls_buffer_info& get_tls_buffer_info();
    void on_thread_exit(log_tls_buffer_info* info);

    // 消费者
    // 从 LP buffer 读取并验证 context
    const void* rt_read_from_lp(uint32_t& out_size);

    // 从 HP buffer 池读取
    const void* rt_read_from_hp(uint32_t& out_size);

    // 验证 LP entry 的 context（seq 校验）
    bool rt_verify_lp_context(const context_head& ctx);

    // 成员变量
    // LP buffer（低频线程共享）
    mpsc_ring_buffer lp_buffer_;

    // HP buffer 容量（每线程独立 spsc 的大小）
    uint32_t hp_capacity_per_thread_;

    // 频率检测阈值（每秒写入次数超过此值 → HP）
    uint64_t hp_threshold_;

    // HP_buffer池 读多写少 需要spin_lock_rw
    spin_lock_rw hp_pool_lock_;
    // 使用 std::vector 或侵入式链表（此处为简单版）
    // 生产：线程注册时 push，线程退出后标记 inactive
    // 消费：轮询所有 active + inactive（直到 empty）entry
    // 注意：此处允许 heap 分配，因为线程注册是冷路径
    std::vector<hp_buffer_entry*> hp_pool_;
    // TLS key：每个线程的独立状态（析构时自动清理）
    // 使用 pthread_key 或 thread_local（选一种，保持一致）
    static thread_local log_tls_buffer_info* tls_current_info_;
    // 消费者状态（单消费者，无竞争）
    struct rt_state_t
    {
        size_t hp_pool_read_index_ = 0;        // 当前正在读的 HP buffer 下标（轮询）
        const void* pending_lp_ptr_ = nullptr; // 等待 return 的 LP entry
        const void* pending_hp_ptr_ = nullptr; // 等待 return 的 HP entry
        hp_buffer_entry* pending_hp_entry_ = nullptr;
        read_handle pending_lp_rh_; // 用于commit_read_chunk还原cursor
        active_read_src last_read_src_ = active_read_src::none;
    } rt_state_;
};

} // namespace qlog