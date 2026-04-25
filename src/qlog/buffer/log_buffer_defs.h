#pragma once

#include "qlog/primitives/aligned_alloc.h"
#include "qlog/primitives/atomic.h"

#include <cstddef>
#include <cstdint>

namespace qlog
{

#pragma pack(push, 1)
struct alignas(8) context_head
{
    uint16_t version_; // M3阶段始终为0 无mmap
    bool is_thread_finished_;
    bool is_external_ref_; // M3阶段始终为false 无oversize路径
    uint32_t seq_;         // 统一线程单调递增

// 跨平台指针存储
#if defined(__LP64__) || defined(_WIN64)
    struct alignas(8)
    {
        void* ptr;
    } tls_info_;
#else
    struct alignas(8)
    {
        void* ptr;
        uint32_t _pad;
    } tls_info_;
#endif

    void* get_tls_ptr() const
    {
        return tls_info_.ptr;
    }
    void set_tls_info_ptr(void* p)
    {
        tls_info_.ptr = p;
    }
};
#pragma pack(pop)

static_assert(sizeof(context_head) == 16, "[QLog] context_head MUST be 16 bytes (BqLog alignment)");
static_assert(alignof(context_head) == 8, "[QLog] context_head MUST be aligned to 8 bytes");
static_assert(sizeof(context_head) % 8 == 0, "[QLog] context_head size MUST be a multiple of 8");

class spsc_ring_buffer; // 前向声明
class log_buffer;

struct alignas(64) log_tls_buffer_info
{
    uint64_t last_update_epoch_ms_ = 0;
    uint64_t update_times = 0;
    spsc_ring_buffer* cur_hp_buffer_ =
        nullptr; // HP 路径：当前线程使用的 spsc_ring_buffer（nullptr 表示用 LP 路径）
    log_buffer* owner_buffer_ = nullptr; // 反向引用，析构时需要找到 log_buffer 做清理
    bool is_thread_finishedd_ = false; // 线程退出标记 & 生命周期保护（用于安全析构）

    // LP 路径 pending write handle 在 alloc 与 commit 之间缓存，避免重建
    bool has_pending_lp_write_ = false;
    uint32_t pending_lp_cursor_ = 0;
    uint32_t pending_lp_blocks_ = 0;

    char padding0_
        [64 - sizeof(uint64_t) * 2 - sizeof(spsc_ring_buffer*) - sizeof(log_buffer*) -
         sizeof(bool) * 2 - sizeof(bool) * 2 - sizeof(uint32_t) * 2];

    // 写线程频繁递增 current_write_seq_，独占 cache line 防止 false sharing
    alignas(64) struct
    {
        uint32_t current_write_seq_ = 0;
        char padding1_[64 - sizeof(uint32_t)];
    } wt_data_;

    // 消费者频繁读取/递增 current_read_seq_，独占 cache line
    alignas(64) struct
    {
        uint32_t current_read_seq_ = 0;
        char padding2_[64 - sizeof(uint32_t)];
    } rt_data_;

    ~log_tls_buffer_info();
};
static_assert(
    sizeof(log_tls_buffer_info) % 64 == 0,
    "[QLog] log_tls_buffer_info must be cache-line sized (multiple of 64)"
);
static_assert(
    offsetof(log_tls_buffer_info, wt_data_) % 64 == 0,
    "[QLog] wt_data_ must be on its own cache line"
);
static_assert(
    offsetof(log_tls_buffer_info, rt_data_) % 64 == 0,
    "[QLog] rt_data_ must be on its own cache line"
);

} // namespace qlog