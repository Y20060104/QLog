#include "qlog/buffer/log_buffer.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <thread>

namespace qlog
{

log_buffer::log_buffer(
    uint32_t lp_capacity_bytes, uint32_t hp_capacity_per_thread_bytes, uint64_t hp_threshould
)
    : lp_buffer_(lp_capacity_bytes)
    , hp_capacity_per_thread_(hp_capacity_per_thread_bytes)
    , hp_threshold_(hp_threshould)
{
}

log_buffer::~log_buffer()
{
    std::unique_lock<spin_lock_rw> wlock(hp_pool_lock_);
    for (auto* entry : hp_pool_)
        delete entry;
    hp_pool_.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// log_tls_buffer_info 析构
//
// 调用时机：消费者在 rt_read_from_lp() 中读到 is_thread_finished 后
//           执行 delete tls_info，触发此析构。
// 此时：对应生产者线程已退出，其所有 LP/HP 写入均已被消费。
// ─────────────────────────────────────────────────────────────────────────────
log_tls_buffer_info::~log_tls_buffer_info()
{
    cur_hp_buffer_ = nullptr;
    owner_buffer_ = nullptr;
}

thread_local log_tls_buffer_info* log_buffer::tls_current_info_ = nullptr;

void* log_buffer::alloc_write_chunk(uint32_t size, uint64_t current_time_ms_)
{
    log_tls_buffer_info& tls = get_tls_buffer_info();

    bool is_high_freq = (tls.cur_hp_buffer_ != nullptr);

    if (current_time_ms_ >= tls.last_update_epoch_ms_ + HP_CALL_FREQUENCY_CHECK_INTERVAL_MS)
    {
        if (tls.update_times_ < hp_threshold_)
        {
            is_high_freq = false;
            tls.cur_hp_buffer_ = nullptr;
        }
        tls.last_update_epoch_ms_ = current_time_ms_;
        tls.update_times_ = 0;
    }

    if (++tls.update_times_ >= hp_threshold_)
    {
        is_high_freq = true;
        tls.last_update_epoch_ms_ = current_time_ms_;
        tls.update_times_ = 0;
    }

    if (is_high_freq)
    {
        if (tls.cur_hp_buffer_ == nullptr)
            tls.cur_hp_buffer_ = get_or_create_hp_buffer(tls);

        if (tls.cur_hp_buffer_ != nullptr)
        {
            void* ptr = tls.cur_hp_buffer_->alloc_write_chunk(size);
            if (ptr != nullptr)
                return ptr;
            tls.cur_hp_buffer_ = nullptr;
        }
        else
        {
            tls.last_update_epoch_ms_ = current_time_ms_;
            tls.update_times_ = 0;
        }
    }

    // LP 路径
    const uint32_t total_alloc = size + static_cast<uint32_t>(sizeof(context_head));
    write_handle wh = lp_buffer_.alloc_write_chunk(total_alloc);
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

void log_buffer::commit_write_chunk(void* data_ptr)
{
    if (data_ptr == nullptr)
        return;

    log_tls_buffer_info& tls = get_tls_buffer_info();

    if (tls.cur_hp_buffer_ != nullptr)
        tls.cur_hp_buffer_->commit_write_chunk();
    else
        lp_buffer_.commit_write_chunk(tls.pending_lp_wh_);
}

log_tls_buffer_info& log_buffer::get_tls_buffer_info()
{
    if (tls_current_info_ != nullptr && tls_current_info_->owner_buffer_ == this)
        return *tls_current_info_;

    auto* info = new log_tls_buffer_info();
    info->owner_buffer_ = this;
    info->cur_hp_buffer_ = nullptr;

    struct tls_guard
    {
        log_tls_buffer_info* info;
        ~tls_guard()
        {
            if (info != nullptr && info->owner_buffer_ != nullptr && !info->is_thread_finished_)
            {
                info->owner_buffer_->on_thread_exit(info);
            }
        }
    };
    static thread_local tls_guard guard{info};

    tls_current_info_ = info;
    return *info;
}

// ─────────────────────────────────────────────────────────────────────────────
// on_thread_exit
//
// 向 LP buffer 写入一条 is_thread_finished=true 的标记 entry（payload=0，
// 仅含 context_head）。消费者读到后负责 delete tls_info。
//
// 修复说明（移除了 available_write_blocks() <= 2 的防御卫语句）：
//
//   原始代码的问题链：
//     1. alloc 的 post-check 使用 >= block_count_，将"恰好写满"也判定为溢出
//     2. 于是用 available <= 2 的卫语句提前放弃，掩盖了真正的 off-by-one
//     3. 结果 finish marker 丢失，消费者无法 delete tls_info，内存泄漏
//
//   修复后：
//     alloc 的 pre/post check 统一改为 >（严格大于），== block_count 合法
//     on_thread_exit 使用重试循环等待空间，而不是提前放弃
//     重试上限 kMaxRetries 防止 LP buffer 永久满时死循环（M3 降级处理）
//
//   对标 BqLog log_tls_info::~log_tls_info()：
//     BqLog 同样在线程退出时向 LP buffer 写 finish 标记；
//     alloc 失败时 BqLog 有 block_when_full 模式会等待，
//     QLog M3 简化为有限重试后放弃（可接受的 M3 限制）。
// ─────────────────────────────────────────────────────────────────────────────
void log_buffer::on_thread_exit(log_tls_buffer_info* info)
{
    info->is_thread_finished_ = true;

    // 清理 HP buffer 引用
    if (info->cur_hp_buffer_ != nullptr)
    {
        std::shared_lock<spin_lock_rw> rlock(hp_pool_lock_);
        for (hp_buffer_entry* entry : hp_pool_)
        {
            if (entry->tls_info == info)
            {
                entry->is_active = false;
                break;
            }
        }
        info->cur_hp_buffer_ = nullptr;
    }

    const uint32_t total_alloc = static_cast<uint32_t>(sizeof(context_head)); // payload = 0

    // 有限重试：等待消费者释放空间（M3 简化版，无 condition_variable）
    // 对标 BqLog block_when_full 模式的降级处理
    constexpr int kMaxRetries = 1024;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt)
    {
        write_handle wh = lp_buffer_.alloc_write_chunk(total_alloc);
        if (wh.success)
        {
            auto* ctx = reinterpret_cast<context_head*>(wh.data);
            ctx->version_ = 0;
            ctx->is_thread_finished_ = true;
            ctx->is_external_ref_ = false;
            ctx->seq_ = info->wt_data_.current_write_seq_++;
            ctx->set_tls_info_ptr(info);

            lp_buffer_.commit_write_chunk(wh);
            return; // 成功写入，退出
        }
        // 空间不足：yield 后重试，等待消费者推进 read_cursor
        std::this_thread::yield();
    }
    // 超过重试上限：finish marker 写入失败（M3 已知限制）
    // 后果：tls_info 不会被消费者 delete，造成内存泄漏。
    // M8 log_manager 会统一管理线程生命期，届时修复。
}

const void* log_buffer::read_chunk(uint32_t& out_size)
{
    // 优先读 HP pool（对标 BqLog HP 优先遍历策略）
    {
        std::shared_lock<spin_lock_rw> rlock(hp_pool_lock_);
        const size_t n = hp_pool_.size();
        for (size_t i = 0; i < n; ++i)
        {
            const size_t idx = (rt_state_.hp_pool_read_index_ + i) % n;
            hp_buffer_entry* entry = hp_pool_[idx];
            const void* ptr = entry->buffer.read_chunk();
            if (ptr != nullptr)
            {
                rt_state_.hp_pool_read_index_ = idx;
                rt_state_.pending_hp_ptr_ = ptr;
                rt_state_.pending_hp_entry_ = entry;
                rt_state_.last_read_src_ = active_read_src::hp;
                out_size = entry->buffer.last_read_data_size();
                return ptr;
            }
        }
    }
    return rt_read_from_lp(out_size);
}

const void* log_buffer::rt_read_from_lp(uint32_t& out_size)
{
    // BqLog 对标：while(true) 循环（非递归），对应 rt_read_from_lp_buffer()
    while (true)
    {
        read_handle rh = lp_buffer_.read_chunk();
        if (!rh.success)
            return nullptr;

        const auto* ctx = reinterpret_cast<const context_head*>(rh.data);
        auto* tls_info = reinterpret_cast<log_tls_buffer_info*>(ctx->get_tls_ptr());

        if (tls_info == nullptr)
        {
            lp_buffer_.commit_read_chunk(rh);
            continue;
        }

        const uint32_t expected_seq = tls_info->rt_data_.current_read_seq_;

        if (ctx->seq_ == expected_seq)
        {
            tls_info->rt_data_.current_read_seq_++;

            if (ctx->is_thread_finished_)
            {
                // finish 标记：消费后 delete tls_info（延迟释放），对调用方透明
                lp_buffer_.commit_read_chunk(rh);
                delete tls_info;
                continue;
            }

            // 正常数据：缓存完整 read_handle 供 commit_read_chunk 使用
            rt_state_.pending_lp_rh_ = rh;
            rt_state_.pending_lp_rh_.success = true;
            rt_state_.pending_lp_ptr_ = rh.data + sizeof(context_head);
            rt_state_.last_read_src_ = active_read_src::lp;

            out_size = rh.data_size - static_cast<uint32_t>(sizeof(context_head));
            return rt_state_.pending_lp_ptr_;
        }
        else if (ctx->seq_ > expected_seq)
        {
            // seq_pending：更早的 entry 尚未到达，本轮暂停（不消费）
            return nullptr;
        }
        else
        {
            // seq_invalid：过期数据，消费并跳过
            lp_buffer_.commit_read_chunk(rh);
            continue;
        }
    }
}

bool log_buffer::rt_verify_lp_context(const context_head& ctx)
{
    auto* tls_info = reinterpret_cast<log_tls_buffer_info*>(ctx.get_tls_ptr());
    if (tls_info == nullptr)
        return false;
    return ctx.seq_ == tls_info->rt_data_.current_read_seq_;
}

void log_buffer::commit_read_chunk(const void* data_ptr)
{
    if (data_ptr == nullptr)
        return;

    switch (rt_state_.last_read_src_)
    {
    case active_read_src::hp:
        if (rt_state_.pending_hp_entry_ != nullptr)
        {
            rt_state_.pending_hp_entry_->buffer.commit_read_chunk();
            rt_state_.pending_hp_ptr_ = nullptr;
            rt_state_.pending_hp_entry_ = nullptr;
        }
        break;

    case active_read_src::lp:
        if (rt_state_.pending_lp_rh_.success)
        {
            lp_buffer_.commit_read_chunk(rt_state_.pending_lp_rh_);
        }
        rt_state_.pending_lp_ptr_ = nullptr;
        rt_state_.pending_lp_rh_.success = false;
        break;

    default:
        break;
    }

    rt_state_.last_read_src_ = active_read_src::none;
}

spsc_ring_buffer* log_buffer::get_or_create_hp_buffer(log_tls_buffer_info& tls_info)
{
    auto* entry = new hp_buffer_entry();
    entry->tls_info = &tls_info;

    if (!entry->buffer.init(hp_capacity_per_thread_))
    {
        delete entry;
        return nullptr;
    }

    std::unique_lock<spin_lock_rw> wlock(hp_pool_lock_);
    hp_pool_.push_back(entry);
    return &entry->buffer;
}

void log_buffer::flush()
{
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

} // namespace qlog