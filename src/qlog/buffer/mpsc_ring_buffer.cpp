#include "mpsc_ring_buffer.h"

#include "qlog/primitives/util.h"

#include <cstring>

namespace qlog
{

mpsc_ring_buffer::mpsc_ring_buffer(uint32_t capacity_bytes)
    : block_count_(0)
    , block_count_mask_(0)
    , blocks_(nullptr)
    , buffer_ptr_(nullptr)
{
    if (capacity_bytes == 0)
        return;

    uint32_t aligned_capacity = (capacity_bytes + CACHE_LINE_SIZE - 1) >> CACHE_LINE_SIZE_LOG2;

    block_count_ = util::roundup_pow_of_two(aligned_capacity);
    block_count_mask_ = block_count_ - 1;

    uint32_t total_size = block_count_ * CACHE_LINE_SIZE;
    buffer_ptr_ = static_cast<uint8_t*>(aligned_alloc(CACHE_LINE_SIZE, total_size));

    if (!buffer_ptr_)
        return;

    blocks_ = reinterpret_cast<block*>(buffer_ptr_);

    cursors_.write_cursor.store_relaxed(0);
    cursors_.read_cursor.store_relaxed(0);

    reset();
}

mpsc_ring_buffer::~mpsc_ring_buffer()
{
    if (buffer_ptr_)
    {
        aligned_free(buffer_ptr_);
        buffer_ptr_ = nullptr;
    }
    blocks_ = nullptr;
}

void mpsc_ring_buffer::reset()
{
    for (uint32_t i = 0; i < block_count_; ++i)
    {
        blocks_[i].chunk_head.status = block_status::unused;
        blocks_[i].chunk_head.set_block_num(0);
        blocks_[i].chunk_head.data_size = 0;
    }
    cursors_.write_cursor.store_relaxed(0);
    cursors_.read_cursor.store_relaxed(0);
}

uint32_t mpsc_ring_buffer::available_write_blocks() const
{
    if (block_count_ == 0)
        return 0;

    const uint32_t write_cursor = cursors_.write_cursor.load_acquire();
    const uint32_t read_cursor = cursors_.read_cursor.load_acquire();
    const uint32_t used = write_cursor - read_cursor;

    // used == block_count_ 是合法的"恰好写满"状态，可用 = 0
    // used >  block_count_ 不应出现（alloc 逻辑保证），防御返回 0
    if (used >= block_count_)
        return 0;

    return block_count_ - used;
}

// ─────────────────────────────────────────────────────────────────────────────
// alloc_write_chunk
//
// BqLog 对标：miso_ring_buffer.cpp::alloc_write_chunk()
//
// 关键设计差异说明（与 BqLog 的刻意偏差）：
//
//   BqLog 在 pre/post check 中混用 > 和 >=，原因：
//     pre-check  使用 >  → == block_count 时允许继续尝试 fetch_add
//     post-check 使用 >= → 若 == block_count 则判定溢出并 CAS 回滚
//   这在 BqLog 的大缓冲区（64KB+）实践中几乎不触及 == block_count 边界，
//   因此两者行为等价。
//
//   QLog M3 在 Test 11 使用 16KB 小缓冲区并将其写满，会精确触发
//   "next_write - read == block_count"场景：消费者归还 N 块后，
//   生产者分配 N 块，恰好写满 → 此为合法分配，不应触发 CAS 回滚。
//
//   修复：pre/post check 统一使用 >（严格大于），语义：
//     > block_count → 真正溢出（会覆盖未读数据）→ 拒绝
//     == block_count → 恰好写满（安全）         → 允许
//
//   同步修复：pre-check 检测到真正不足时直接 return，不继续执行 fetch_add。
//   CAS 回滚成功后直接 return failure，不在内层重试（外层 while 负责重试）。
// ─────────────────────────────────────────────────────────────────────────────
write_handle mpsc_ring_buffer::alloc_write_chunk(uint32_t size)
{
    write_handle handle;

    if (blocks_ == nullptr || block_count_ == 0)
        return handle;

    const uint32_t header_size = offsetof(block::chunk_head_def, data);
    const uint32_t total_size = size + header_size;
    const uint32_t need_block_count = (total_size + CACHE_LINE_SIZE - 1u) >> CACHE_LINE_SIZE_LOG2;

    // 单条请求超容量，或超过半个缓冲区（wrap-around 后数据无法连续存放）
    // 对标 BqLog err_alloc_size_invalid，同时排除 need > block_count/2 的死锁情形
    if (need_block_count == 0 || need_block_count > block_count_ ||
        (need_block_count << 1u) > block_count_)
    {
        return handle;
    }

    // TLS 缓存 read_cursor，减少跨核 atomic load（对标 BqLog miso_tls_buffer_info）
    static thread_local tls_buffer_info tls_info;
    if (tls_info.is_new_created)
    {
        tls_info.is_new_created = false;
        tls_info.read_cursor_cache = cursors_.read_cursor.load_acquire();
    }
    uint32_t& read_cursor_cache = tls_info.read_cursor_cache;

    uint32_t current_write;

    while (true)
    {
        // ── 预检查（BqLog 对标：pre-check with > ）──────────────────────────
        // 读取当前 write_cursor 估算分配后占用量。
        // 使用 >（严格大于）：== block_count 表示恰好写满，仍允许分配。
        // 若超出则刷新 TLS 缓存后再判断；若仍超出则空间真正不足，直接 return。
        // 注意：此处 return 是正确的，不应继续执行 fetch_add —— BqLog 同策略。
        current_write = cursors_.write_cursor.load_acquire();
        const uint32_t new_write = current_write + need_block_count;

        if ((new_write - read_cursor_cache) > block_count_)
        {
            read_cursor_cache = cursors_.read_cursor.load_acquire();
            if ((new_write - read_cursor_cache) > block_count_)
            {
                // 真正空间不足，直接返回
                // （BqLog 在此处有 err_not_enough_space return，QLog 同）
                return handle;
            }
        }

        // ── fetch_add：乐观占位（MPSC 热路径核心）──────────────────────────
        // 多线程并发时 fetch_add 是原子的，各线程得到不同的 current_write。
        // 对标 BqLog：fetch_add_relaxed（commit 时的 release store 保证可见性）
        current_write = cursors_.write_cursor.fetch_add_relaxed(need_block_count);
        const uint32_t next_write = current_write + need_block_count;

        // ── 后检查：验证 fetch_add 后是否真的有空间 ─────────────────────────
        // 多线程并发 fetch_add 可能导致总分配量超出容量，需要 CAS 回滚。
        // 同样使用 >（严格大于），与预检查保持一致，避免 off-by-one。
        if ((next_write - read_cursor_cache) > block_count_)
        {
            while ((next_write - (read_cursor_cache = cursors_.read_cursor.load_acquire())) >
                   block_count_)
            {
                // 仍超出：尝试 CAS 回滚
                // 对标 BqLog compare_exchange_strong(expected=next_write, desired=current_write)
                uint32_t expected = next_write;
                if (cursors_.write_cursor.compare_exchange_strong(
                        expected,
                        current_write,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed
                    ))
                {
                    // CAS 成功：write_cursor 已回滚至分配前状态，本次失败。
                    // 直接 return（对标 BqLog：回滚后即 return err_not_enough_space，
                    // 不在此处重试；若调用方需重试，由调用方控制循环）。
                    return handle;
                }
                // CAS 失败：其他线程已推进（或回滚了）write_cursor，
                // 继续内层 while 刷新 read_cursor_cache 再检查。
            }
            // 退出内层 while：read_cursor 已推进，空间重新可用，分配有效。
        }

        // ── 连续性检查（wrap-around 处理）───────────────────────────────────
        // 若本次分配跨越缓冲区末尾，数据无法在物理上连续存放。
        // 对标 BqLog err_data_not_contiguous：标记 INVALID 后重新外层循环。
        const uint32_t start_idx = current_write & block_count_mask_;
        const uint32_t end_idx = next_write & block_count_mask_;

        if (end_idx == 0u || start_idx < end_idx)
        {
            // 分配连续（或恰好到达缓冲末尾），退出外层循环
            break;
        }

        // wrap-around：本次分配的空间标记为 INVALID 占位
        // 消费者 read_chunk() 会识别 INVALID 并跳过，推进游标；
        // 后续 write_cursor 从 end_idx=0 处开始新的连续分配。
        block* wrap_block = &blocks_[start_idx];
        wrap_block->chunk_head.set_block_num(block_count_ - start_idx);
        wrap_block->chunk_head.data_size = 0;
        wrap_block->chunk_head.status = block_status::invalid;
        // 继续外层 while，下一次 fetch_add 从 end_idx(=0 附近) 开始
    }

    // ── 分配成功，填写 block header ─────────────────────────────────────────
    block* new_block = &blocks_[current_write & block_count_mask_];
    new_block->chunk_head.set_block_num(need_block_count);
    new_block->chunk_head.status = block_status::unused; // commit 时改为 used
    new_block->chunk_head.data_size = size;

    handle.success = true;
    handle.cursor = current_write;
    handle.data = new_block->chunk_head.data;
    handle.block_count = need_block_count;
    return handle;
}

void mpsc_ring_buffer::commit_write_chunk(const write_handle& handle)
{
    if (!handle.success)
        return;

    block* target_block = &blocks_[handle.cursor & block_count_mask_];
    // release fence 确保 data 写入对消费者可见（对标 BqLog store_release(status=used)）
    std::atomic_thread_fence(std::memory_order_release);
    target_block->chunk_head.status = block_status::used;
}

read_handle mpsc_ring_buffer::read_chunk()
{
    read_handle handle;

    if (blocks_ == nullptr || block_count_ == 0)
        return handle;

    const uint32_t start_read_cursor = cursors_.read_cursor.load_relaxed();
    uint32_t current_read_cursor = start_read_cursor;
    uint32_t scanned_blocks = 0;

    while (scanned_blocks < block_count_)
    {
        // 游标取模：游标是无限递增的逻辑游标，通过 & mask 映射到物理索引
        block* current_block = &blocks_[current_read_cursor & block_count_mask_];
        block_status status = current_block->chunk_head.status;

        switch (status)
        {
        case block_status::invalid:
        {
            // wrap-around 占位块，跳过
            uint32_t block_num = current_block->chunk_head.get_block_num();
            if (block_num == 0)
                block_num = 1;
            current_read_cursor += block_num;
            scanned_blocks += block_num;
            break;
        }
        case block_status::unused:
            // 尚未提交，缓冲区到此为止
            return handle;

        case block_status::used:
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            uint32_t block_num = current_block->chunk_head.get_block_num();
            if (block_num == 0)
                return handle;

            handle.success = true;
            handle.cursor = current_read_cursor;
            handle.data = current_block->chunk_head.data;
            handle.data_size = current_block->chunk_head.data_size;
            handle.block_count = block_num;
            return handle;
        }
        default:
            current_read_cursor += 1;
            scanned_blocks += 1;
            break;
        }
    }

    return handle;
}

void mpsc_ring_buffer::commit_read_chunk(const read_handle& handle)
{
    if (!handle.success)
        return;

    block* current_block = &blocks_[handle.cursor & block_count_mask_];
    current_block->chunk_head.status = block_status::unused;
    current_block->chunk_head.set_block_num(0);
    current_block->chunk_head.data_size = 0;

    const uint32_t new_read_cursor = handle.cursor + handle.block_count;
    cursors_.read_cursor.store_release(new_read_cursor);
}

} // namespace qlog