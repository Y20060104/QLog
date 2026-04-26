#include "qlog/buffer/log_buffer.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <mutex>
#include <shared_mutex>

namespace qlog
{

log_buffer::log_buffer(
    uint32_t lp_capacity_bytes,
    uint32_t hp_capacity_per_thread_bytes,
    uint64_t hp_threshould
)
    : lp_buffer_(lp_capacity_bytes) // <--- 直接在这里调用 mpsc_ring_buffer 的带参构造
    , hp_capacity_per_thread_(hp_capacity_per_thread_bytes)
    , hp_threshold_(hp_threshould)
{
    // lp_buffer_ 已经在上面的初始化列表中完成了初始化，不需要调用 init() 了
}

log_buffer::~log_buffer()
{
    // 释放 hp_pool_ 中的动态内存
    std::unique_lock<spin_lock_rw> wlock(hp_pool_lock_);
    for (auto* entry : hp_pool_)
    {
        delete entry;
    }
    hp_pool_.clear();
}
// TLS 变量定义
thread_local log_tls_buffer_info* log_buffer::tls_current_info_ = nullptr;

void* log_buffer::alloc_write_chunk(uint32_t size, uint64_t current_time_ms_)
{
    // 获取当前线程TLS状态
    log_tls_buffer_info& tls = get_tls_buffer_info();

    // 频率检测
    bool is_high_freq = (tls.cur_hp_buffer_ != nullptr);

    if (current_time_ms_ >= tls.last_update_epoch_ms_ + HP_CALL_FREQUENCY_CHECK_INTERVAL_MS)
    {
        // 超过 HP_CALL_FREQUENCY_CHECK_INTERVAL_MS 后重置计数,计数超过 threshold 时标记为高频
        if (tls.update_times_ < hp_threshold_)
        {
            // 本窗口低频
            is_high_freq = false;
            if (tls.cur_hp_buffer_ != nullptr)
            {
                // 释放HP buffer 标记inactive 不能delete 消费者可能还在读
                tls.cur_hp_buffer_ = nullptr;
            }
        }
        // 重置窗口
        tls.last_update_epoch_ms_ = current_time_ms_;
        tls.update_times_ = 0;
    }

    // 增加计数，如果超过阈值则升级为高频
    if (++tls.update_times_ >= hp_threshold_)
    {
        is_high_freq = true;
        tls.last_update_epoch_ms_ = current_time_ms_;
        tls.update_times_ = 0;
    }

    // 路由到HP或者LP
    if (is_high_freq)
    {
        // 高频路径
        // 确保有HP buffer
        if (tls.cur_hp_buffer_ == nullptr)
        {
            tls.cur_hp_buffer_ = get_or_create_hp_buffer(tls);
        }

        void* ptr = tls.cur_hp_buffer_->alloc_write_chunk(size);
        if (ptr != nullptr)
        {
            return ptr;
        }

        // HP buffer满:不阻塞，降级为LP
        // Bqlog auto_expand时重新申请块 这里简化直接降级
        tls.cur_hp_buffer_ = nullptr;
        is_high_freq = false;
    }

    // LP路径
    // LP 路径在真实数据前需要预留 sizeof(context_head) = 16 字节
    // context_head 由 log_buffer 内部填充，对上层调用方透明
    uint32_t total_alloc = size + static_cast<uint32_t>(sizeof(context_head));
    write_handle wh = lp_buffer_.alloc_write_chunk(total_alloc);
    if (!wh.success)
    {
        // 空间不足 根据策略丢弃或者返回nullptr
        lp_buffer_.commit_write_chunk(wh); // 释放占位
        return nullptr;
    }

    // 填充 context_head
    auto* ctx = reinterpret_cast<context_head*>(wh.data);
    ctx->version_ = 0;
    ctx->is_thread_finished_ = false;
    ctx->is_external_ref_ = false;
    ctx->seq_ = tls.wt_data_.current_write_seq_++; // 原子性由单线程写保证
    ctx->set_tls_info_ptr(&tls);
    // wh 缓存到tls
    tls.pending_lp_wh_ = wh;
    return wh.data + sizeof(context_head); // 返回 context_head 之后的地址（用户可写入区域）
}

void log_buffer::commit_write_chunk(void* data_ptr)
{
    if (data_ptr == nullptr)
    {
        return;
    }

    log_tls_buffer_info& tls = get_tls_buffer_info();

    if (tls.cur_hp_buffer_ != nullptr)
    {
        // HP路径
        tls.cur_hp_buffer_->commit_write_chunk();
    }
    else
    {
        // LP路径
        lp_buffer_.commit_write_chunk(tls.pending_lp_wh_);
    }
}

log_tls_buffer_info& log_buffer::get_tls_buffer_info()
{
    // 快速路径：已初始化
    if (tls_current_info_ != nullptr && tls_current_info_->owner_buffer_ == this)
    {
        return *tls_current_info_;
    }
    // 慢路径 为当前进程创建TLS状态
    auto* info = new log_tls_buffer_info();
    info->owner_buffer_ = this;
    // 注册线程退出回调（当线程销毁时通知 log_buffer）
    // 方式 1：pthread_key_t + destructor（Linux/macOS/Windows）
    // 方式 2：利用 thread_local 对象析构
    // QLog M3 推荐方式 2：
    struct tls_guard
    {
        log_tls_buffer_info* info;
        ~tls_guard()
        {
            if (info && info->owner_buffer_)
            {
                info->owner_buffer_->on_thread_exit(info);
            }
        }
    };

    static thread_local tls_guard guard{info};

    tls_current_info_ = info;
    return *info;
}

void log_buffer::on_thread_exit(log_tls_buffer_info* info)
{
    // 通知消费者：此线程不再写入
    // 对标 BqLog log_tls_info::~log_tls_info()
    // 在 LP buffer 中写一条 is_thread_finished=true 的 entry
    // 消费者读到后调用 delete info（延迟释放，保证消费者访问安全）
    info->is_thread_finished_ = true;
    // 标记 HP buffer 为inactive
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
    constexpr uint32_t k_finish_payload_size = 0;
    const uint32_t total_alloc =
        k_finish_payload_size + static_cast<uint32_t>(sizeof(context_head));

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
    }
}

const void* log_buffer::read_chunk(uint32_t& out_size)
{
    // 优先 读HP
    {
        // 用锁遍历HP pool
        // BqLog: group_list 遍历；QLog M3: 简单 vector 遍历
        // 问题：我没有再spinlock下实现这个锁没有实现 C++ 标准要求的 lock_shared() 和
        // unlock_shared() 方法，检查BqLog实现方法
        std::shared_lock<spin_lock_rw> rlock(hp_pool_lock_);
        const size_t n = hp_pool_.size();
        for (size_t i = 0; i < n; ++i)
        {
            size_t idx =
                (rt_state_.hp_pool_read_index_ + i) % n; // 这个取模运算开销不小，后续考虑优化
            hp_buffer_entry* entry = hp_pool_[idx];
            const void* ptr = entry->buffer.read_chunk();
            if (ptr != nullptr)
            {
                // 读到数据，记录当前 entry 供 return_read_chunk 使用
                rt_state_.hp_pool_read_index_ = idx;
                rt_state_.pending_hp_ptr_ = ptr;
                rt_state_.pending_hp_entry_ = entry;
                rt_state_.last_read_src_ = active_read_src::hp;
                // 大小需要从 spsc block_header 中取
                // 这里调用 spsc 的辅助方法获取 data_size
                out_size = entry->buffer.last_read_data_size();
                return ptr;
            }
        }
    }
    return rt_read_from_lp(out_size);
}

const void* log_buffer::rt_read_from_lp(uint32_t& out_size)
{
    while (true)
    {
        read_handle rh = lp_buffer_.read_chunk();
        if (!rh.success)
        {
            // LP buffer为空 本轮无数据
            return nullptr;
        }

        // 解析context_head
        const auto* ctx = reinterpret_cast<const context_head*>(rh.data);

        // 获取TLS状态
        auto* tls_info = reinterpret_cast<log_tls_buffer_info*>(ctx->get_tls_ptr());
        if (tls_info == nullptr)
        {
            // 无效context 跳过
            lp_buffer_.commit_read_chunk(rh);
            continue;
        }
        const uint32_t expected_seq = tls_info->rt_data_.current_read_seq_;

        // seq校验
        if (ctx->seq_ == expected_seq)
        {
            tls_info->rt_data_.current_read_seq_++;
            if (ctx->is_thread_finished_)
            {
                lp_buffer_.commit_read_chunk(rh
                );               // 1. 消费这条 finish 标记（推进 mpsc read_cursor）
                delete tls_info; // 2. 延迟释放 TLS（消费者负责，生产者线程已退出）
                continue; // 3. 继续循环，尝试读下一条（finish 标记无用户数据）
            }
            // 正常数据
            // 缓存完整read_handle
            rt_state_.pending_lp_rh_ = rh;
            rt_state_.pending_lp_ptr_ = rh.data + sizeof(context_head);
            rt_state_.last_read_src_ = active_read_src::lp;
            // 返回用户数据 跳过context_head 前缀
            out_size = rh.data_size - static_cast<uint32_t>(sizeof(context_head));
            return rt_state_.pending_lp_ptr_;
        }
        else if (ctx->seq_ > expected_seq)
        {
            // 更早的entry还没有到
            return nullptr;
        }
        else
        {
            // 过期了
            lp_buffer_.commit_read_chunk(rh);
            continue;
        }
    }
}

bool log_buffer::rt_verify_lp_context(const context_head& ctx)
{
    auto* tls_info = reinterpret_cast<log_tls_buffer_info*>(ctx.get_tls_ptr());
    if (tls_info == nullptr)
    {
        return false;
    }
    uint32_t expected = tls_info->rt_data_.current_read_seq_;

    if (ctx.seq_ == expected)
    {
        return true;
    }
    if (ctx.seq_ > expected)
    {
        return false;
    }
    // ctx.seq_ < expected：来自已退出线程的过期数据（M3 不含 mmap 恢复，直接跳过）
    return false;
}

void log_buffer::commit_read_chunk(const void* data_ptr)
{
    if (data_ptr == nullptr)
    {
        return;
    }

    switch (rt_state_.last_read_src_)
    {
    case active_read_src::hp:
    {
        // HP 路径直接调用 spsc commit_read_chunk
        rt_state_.pending_hp_entry_->buffer.commit_read_chunk();
        rt_state_.pending_hp_ptr_ = nullptr;
        rt_state_.pending_hp_entry_ = nullptr;
        break;
    }
    case active_read_src::lp:
    {
        lp_buffer_.commit_read_chunk(rt_state_.pending_lp_rh_);
        rt_state_.pending_lp_ptr_ = nullptr;
        rt_state_.pending_lp_rh_.success = false;
        break;
    }
    default:
        break;
    }
    rt_state_.last_read_src_ = active_read_src::none;
}

spsc_ring_buffer* log_buffer::get_or_create_hp_buffer(log_tls_buffer_info& tls_info)
{
    auto* entry = new hp_buffer_entry();
    entry->tls_info = &tls_info;

    bool ok = entry->buffer.init(hp_capacity_per_thread_);
    if (!ok)
    {
        delete entry;
        return nullptr;
    }
    {
        // 加写锁，将新 entry 添加到 pool
        // BqLog: 对应 group_list::alloc_new_block()
        // 我原本实现没有write_guard啊 你这样设计需要修改我的原实现吧
        std::unique_lock<spin_lock_rw> wlock(hp_pool_lock_);
        hp_pool_.push_back(entry);
    }
    return &entry->buffer;
}
} // namespace qlog