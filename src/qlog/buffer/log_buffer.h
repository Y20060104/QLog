#pragma once

#include "qlog/buffer/log_buffer_defs.h"
#include "qlog/buffer/mpsc_ring_buffer.h"
#include "qlog/buffer/spsc_ring_buffer.h"
#include "qlog/primitives/spin_lock.h" // ← 只用 spin_lock（移除 spin_lock_rw）
// 移除：#include <vector>  #include <mutex>  #include <shared_mutex>

#include <atomic>
#include <cstdint>

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
        uint64_t hp_threshold = DEFAULT_HP_THRESHOLD
    );
    ~log_buffer();

    log_buffer(const log_buffer&) = delete;
    log_buffer& operator=(const log_buffer&) = delete;

    [[nodiscard]] void* alloc_write_chunk(uint32_t size, uint64_t current_time_ms);
    void commit_write_chunk(void* data_ptr);

    [[nodiscard]] const void* read_chunk(uint32_t& out_size);
    void commit_read_chunk(const void* data_ptr);

    void flush();
    void on_thread_exit(log_tls_buffer_info* info);

private:
    // ── HP buffer 侵入式链表节点 ────────────────────────────────────────────
    // 对标 BqLog block_node_head（链表节点 + siso_ring_buffer 复合体）
    // 使用 alignas(64) 隔离各节点，防止不同线程的 buffer 之间 false sharing
    struct alignas(64) hp_buffer_entry
    {
        spsc_ring_buffer buffer;
        hp_buffer_entry* next = nullptr; // 侵入式单链表指针
        log_tls_buffer_info* tls_info = nullptr;
        bool is_active = true; // false = 线程已退出，等待 drain
    };

    enum class active_read_src : uint8_t
    {
        none,
        hp,
        lp
    };

    // ── 生产者内部接口 ──────────────────────────────────────────────────────
    spsc_ring_buffer* get_or_create_hp_buffer(log_tls_buffer_info& tls_info);
    log_tls_buffer_info& get_tls_buffer_info();

    // ── 消费者内部接口 ──────────────────────────────────────────────────────
    const void* rt_read_from_lp(uint32_t& out_size);

    // ── 成员变量 ────────────────────────────────────────────────────────────
    uint64_t id_; // 实例唯一 ID（TLS map key）
    mpsc_ring_buffer lp_buffer_;
    uint32_t hp_capacity_per_thread_;
    uint64_t hp_threshold_;

    // HP pool：侵入式单链表 + 单把 spin_lock
    // 对标 BqLog group_list head_ + lock（简化版：不分 group，单列表）
    // 生产者注册（冷路径）+ 消费者遍历（热路径，单消费者无竞争）
    // 用 spin_lock 而非 spin_lock_rw：
    //   - 写（注册新 HP buffer）极低频，一个线程一生只注册一次
    //   - 读（消费者遍历）是单线程，与自己不竞争
    //   - 两者不同时发生的概率极高，spin_lock 足够
    spin_lock hp_pool_lock_;
    hp_buffer_entry* hp_head_ = nullptr; // 链表头（最新注册的在头部）

    // 消费者状态（单消费者，无并发访问）
    struct rt_state_t
    {
        hp_buffer_entry* hp_current_read_ = nullptr; // 当前轮询到的 HP 节点
        hp_buffer_entry* pending_hp_entry_ = nullptr;
        const void* pending_hp_ptr_ = nullptr;
        const void* pending_lp_ptr_ = nullptr;
        read_handle pending_lp_rh_{};
        active_read_src last_read_src_ = active_read_src::none;
    } rt_state_;
};

} // namespace qlog