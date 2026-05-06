#include "qlog/buffer/log_buffer.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

namespace qlog
{

// ============================================================================
// log_tls_buffer_info 析构
// ============================================================================
log_tls_buffer_info::~log_tls_buffer_info()
{
    cur_hp_buffer_ = nullptr;
    owner_buffer_ = nullptr;
}

// ============================================================================
// 全局 ID 生成器（用于 TLS map key）
// ============================================================================
static std::atomic<uint64_t> s_id_counter{0};

// ============================================================================
// 构造 / 析构
// ============================================================================
log_buffer::log_buffer(
    uint32_t lp_capacity_bytes, uint32_t hp_capacity_per_thread_bytes, uint64_t hp_threshold
)
    : id_(s_id_counter.fetch_add(1, std::memory_order_relaxed))
    , lp_buffer_(lp_capacity_bytes)
    , hp_capacity_per_thread_(hp_capacity_per_thread_bytes)
    , hp_threshold_(hp_threshold)
    , destruction_mark_(std::make_shared<destruction_mark>()) // ← 新增
{
}

log_buffer::~log_buffer()
{
    // BqLog: scoped_spin_lock lock(destruction_mark_->lock_); is_destructed_ = true;
    {
        scoped_spin_lock lock(destruction_mark_->lock_);
        destruction_mark_->is_destructed_ = true;
    }
    // 释放 hp_pool_
    hp_pool_lock_.lock();
    hp_buffer_entry* node = hp_head_;
    while (node)
    {
        auto* nx = node->next;
        delete node;
        node = nx;
    }
    hp_head_ = nullptr;
    hp_pool_lock_.unlock();
}

struct tls_info_registry
{
    static constexpr int kCap = 16;

    struct slot
    {
        log_buffer* buf;
        log_tls_buffer_info* info;
    };
    slot slots[kCap]{};
    int count = 0;
    mutable int last_idx = -1;

    log_tls_buffer_info* find(const log_buffer* buf) const noexcept
    {
        if (last_idx >= 0 && slots[last_idx].buf == buf)
            return slots[last_idx].info;
        for (int i = 0; i < count; ++i)
            if (slots[i].buf == buf)
            {
                last_idx = i;
                return slots[i].info;
            }
        return nullptr;
    }

    void add(log_buffer* buf, log_tls_buffer_info* info) noexcept
    {
        if (count < kCap)
            slots[count++] = {buf, info};
        last_idx = count - 1;
    }

    // ── BqLog ~log_tls_info() (doc26 L73-L119) 对齐 ──────────────────────
    ~tls_info_registry()
    {
        for (int i = 0; i < count; ++i)
        {
            auto* info = slots[i].info;
            auto* buf = slots[i].buf;
            if (!info || info->is_thread_finished_)
                continue;

            // shared_ptr 保证 destruction_mark 此时仍然存活（即使 log_buffer 已析构）
            auto mark = info->destruction_mark_;
            if (!mark)
            {
                delete info;
                continue;
            }

            // BqLog: scoped_spin_lock lock(protector->lock_)
            // 与 ~log_buffer() 互斥：确保 is_destructed_ 的判断和写 finish marker 是原子的
            scoped_spin_lock lock(mark->lock_);

            if (!mark->is_destructed_)
            {
                // log_buffer 仍存活，写 finish marker（BqLog doc26 L100-L117）
                buf->on_thread_exit(info);
            }
            else
            {
                // log_buffer 已析构：直接释放，不能访问 buf（BqLog doc26 L118-L120）
                delete info;
            }
        }
    }
};

thread_local tls_info_registry t_tls_registry;

log_tls_buffer_info& log_buffer::get_tls_buffer_info()
{
    if (auto* existing = t_tls_registry.find(this))
        return *existing;

    auto* info = new log_tls_buffer_info();
    info->owner_buffer_ = this;
    info->destruction_mark_ = destruction_mark_;
    t_tls_registry.add(this, info);
    return *info;
}
// ============================================================================
// alloc_write_chunk（HP/LP 路由，对标 BqLog log_buffer::alloc_write_chunk）
// ============================================================================
void* log_buffer::alloc_write_chunk(uint32_t size, uint64_t current_time_ms)
{
    log_tls_buffer_info& tls = get_tls_buffer_info();

    // ── 频率检测（对标 BqLog is_high_frequency 判定）──────────────────────
    bool is_high_freq = (tls.cur_hp_buffer_ != nullptr);

    if (current_time_ms >= tls.last_update_epoch_ms_ + HP_CALL_FREQUENCY_CHECK_INTERVAL_MS)
    {
        if (tls.update_times_ < hp_threshold_)
        {
            is_high_freq = false;
            tls.cur_hp_buffer_ = nullptr;
        }
        tls.last_update_epoch_ms_ = current_time_ms;
        tls.update_times_ = 0;
    }

    if (++tls.update_times_ >= hp_threshold_)
    {
        is_high_freq = true;
        tls.last_update_epoch_ms_ = current_time_ms;
        tls.update_times_ = 0;
    }

    // ── HP 路径 ────────────────────────────────────────────────────────────
    if (is_high_freq)
    {
        if (!tls.cur_hp_buffer_)
            tls.cur_hp_buffer_ = get_or_create_hp_buffer(tls);

        if (tls.cur_hp_buffer_)
        {
            void* ptr = tls.cur_hp_buffer_->alloc_write_chunk(size);
            if (ptr)
                return ptr;
            // HP buffer 满：清除引用，回落 LP
            tls.cur_hp_buffer_ = nullptr;
        }
        // 回落：重置频率计数，走 LP
        tls.last_update_epoch_ms_ = current_time_ms;
        tls.update_times_ = 0;
    }

    // ── LP 路径 ────────────────────────────────────────────────────────────
    const uint32_t total = size + static_cast<uint32_t>(sizeof(context_head));
    write_handle wh = lp_buffer_.alloc_write_chunk(total);
    if (!wh.success)
        return nullptr;

    auto* ctx = reinterpret_cast<context_head*>(wh.data);
    ctx->version_ = 0;
    ctx->is_thread_finished_ = false;
    ctx->is_external_ref_ = false;
    ctx->seq_ = tls.wt_data_.current_write_seq_++;
    ctx->set_tls_info_ptr(&tls);

    tls.pending_lp_wh_ = wh;
    return wh.data + sizeof(context_head);
}

// ============================================================================
// commit_write_chunk
// ============================================================================
void log_buffer::commit_write_chunk(void* data_ptr)
{
    if (!data_ptr)
        return;
    log_tls_buffer_info& tls = get_tls_buffer_info();
    if (tls.cur_hp_buffer_)
        tls.cur_hp_buffer_->commit_write_chunk();
    else
        lp_buffer_.commit_write_chunk(tls.pending_lp_wh_);
}

// ============================================================================
// on_thread_exit（对标 BqLog log_tls_info::~log_tls_info 中的 finish marker 写入）
// ============================================================================
void log_buffer::on_thread_exit(log_tls_buffer_info* info)
{
    info->is_thread_finished_ = true;

    // 清理 HP buffer 引用（标 inactive）
    if (info->cur_hp_buffer_)
    {
        hp_pool_lock_.lock();
        for (hp_buffer_entry* node = hp_head_; node; node = node->next)
        {
            if (node->tls_info == info)
            {
                node->is_active = false;
                break;
            }
        }
        hp_pool_lock_.unlock();
        info->cur_hp_buffer_ = nullptr;
    }

    // 向 LP buffer 写入 finish marker（payload = 0，仅 context_head）
    // 消费者读到后 delete tls_info
    // 对标 BqLog log_tls_info 析构中的 alloc+commit finish entry
    const uint32_t total = static_cast<uint32_t>(sizeof(context_head));
    constexpr int kMaxRetries = 2048;

    for (int attempt = 0; attempt < kMaxRetries; ++attempt)
    {
        write_handle wh = lp_buffer_.alloc_write_chunk(total);
        if (wh.success)
        {
            auto* ctx = reinterpret_cast<context_head*>(wh.data);
            ctx->version_ = 0;
            ctx->is_thread_finished_ = true;
            ctx->is_external_ref_ = false;
            ctx->seq_ = info->wt_data_.current_write_seq_++;
            ctx->set_tls_info_ptr(info);
            lp_buffer_.commit_write_chunk(wh);
            return;
        }
        std::this_thread::yield();
    }

    // 极端情况：无法写 finish marker（LP buffer 持续满）
    // 直接 delete，避免泄漏（代价：消费者可能在 seq_pending 状态下永久等待此线程）
    // 实际场景下此路径几乎不会触发（Worker 线程持续 drain）
    delete info;
}

// ============================================================================
// get_or_create_hp_buffer（侵入式链表头插）
// 对标 BqLog group_list::alloc_new_block 的 free list 弹出 + stage list 推入
// ============================================================================
spsc_ring_buffer* log_buffer::get_or_create_hp_buffer(log_tls_buffer_info& tls_info)
{
    // 创建新节点（冷路径：每线程仅一次）
    auto* entry = new hp_buffer_entry();
    if (!entry->buffer.init(hp_capacity_per_thread_))
    {
        delete entry;
        return nullptr;
    }
    entry->tls_info = &tls_info;

    // 头插法（对标 BqLog group_list::alloc_new_block 的 head_.node_ = new_node）
    // spin_lock 保护链表结构，生产者注册与消费者遍历互斥
    hp_pool_lock_.lock();
    entry->next = hp_head_;
    hp_head_ = entry;
    hp_pool_lock_.unlock();

    return &entry->buffer;
}

// ============================================================================
// read_chunk（HP 侵入式链表轮询 + LP 回落）
// 对标 BqLog log_buffer::read_chunk 的 HP/LP 双路遍历
// ============================================================================
const void* log_buffer::read_chunk(uint32_t& out_size)
{
    // ── HP 路径：遍历侵入式链表（对标 BqLog rt_try_traverse_to_next_block_in_group）
    // 从上次读到的节点继续（轮询，避免总从 head 开始导致旧节点饥饿）
    {
        hp_pool_lock_.lock();
        hp_buffer_entry* start = rt_state_.hp_current_read_ ? rt_state_.hp_current_read_ : hp_head_;
        hp_buffer_entry* node = start;

        while (node)
        {
            const void* ptr = node->buffer.read_chunk();
            if (ptr)
            {
                out_size = node->buffer.last_read_data_size();
                rt_state_.hp_current_read_ = node;
                rt_state_.pending_hp_entry_ = node;
                rt_state_.pending_hp_ptr_ = ptr;
                rt_state_.last_read_src_ = active_read_src::hp;
                hp_pool_lock_.unlock();
                return ptr;
            }
            node = node->next;
        }

        // 从 head 到 start 之前的节点（完成一轮 wrap-around）
        if (start != hp_head_)
        {
            node = hp_head_;
            while (node && node != start)
            {
                const void* ptr = node->buffer.read_chunk();
                if (ptr)
                {
                    out_size = node->buffer.last_read_data_size();
                    rt_state_.hp_current_read_ = node;
                    rt_state_.pending_hp_entry_ = node;
                    rt_state_.pending_hp_ptr_ = ptr;
                    rt_state_.last_read_src_ = active_read_src::hp;
                    hp_pool_lock_.unlock();
                    return ptr;
                }
                node = node->next;
            }
        }
        hp_pool_lock_.unlock();
    }

    return rt_read_from_lp(out_size);
}

// ============================================================================
// rt_read_from_lp（seq 验证，同原实现，无结构变化）
// ============================================================================
const void* log_buffer::rt_read_from_lp(uint32_t& out_size)
{
    while (true)
    {
        read_handle rh = lp_buffer_.read_chunk();
        if (!rh.success)
            return nullptr;

        const auto* ctx = reinterpret_cast<const context_head*>(rh.data);
        auto* tls_info = reinterpret_cast<log_tls_buffer_info*>(ctx->get_tls_ptr());
        if (!tls_info)
        {
            lp_buffer_.commit_read_chunk(rh);
            continue;
        }

        const uint32_t expected = tls_info->rt_data_.current_read_seq_;
        if (ctx->seq_ == expected)
        {
            tls_info->rt_data_.current_read_seq_++;
            if (ctx->is_thread_finished_)
            {
                lp_buffer_.commit_read_chunk(rh);
                delete tls_info;
                continue;
            }
            rt_state_.pending_lp_rh_ = rh;
            rt_state_.pending_lp_ptr_ = rh.data + sizeof(context_head);
            rt_state_.last_read_src_ = active_read_src::lp;
            out_size = rh.data_size - static_cast<uint32_t>(sizeof(context_head));
            return rt_state_.pending_lp_ptr_;
        }
        else if (ctx->seq_ > expected)
            return nullptr; // seq_pending：等待更早的 entry
        else
            lp_buffer_.commit_read_chunk(rh); // seq_invalid：跳过
    }
}

// ============================================================================
// commit_read_chunk
// ============================================================================
void log_buffer::commit_read_chunk(const void* data_ptr)
{
    if (!data_ptr)
        return;

    switch (rt_state_.last_read_src_)
    {
    case active_read_src::hp:
        if (rt_state_.pending_hp_entry_)
        {
            rt_state_.pending_hp_entry_->buffer.commit_read_chunk();
            rt_state_.pending_hp_ptr_ = nullptr;
            rt_state_.pending_hp_entry_ = nullptr;
        }
        break;

    case active_read_src::lp:
        if (rt_state_.pending_lp_rh_.success)
            lp_buffer_.commit_read_chunk(rt_state_.pending_lp_rh_);
        rt_state_.pending_lp_ptr_ = nullptr;
        rt_state_.pending_lp_rh_.success = false;
        break;

    default:
        break;
    }
    rt_state_.last_read_src_ = active_read_src::none;
}

// ============================================================================
// flush
// ============================================================================
void log_buffer::flush()
{
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

} // namespace qlog