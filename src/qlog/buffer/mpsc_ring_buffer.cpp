#include "qlog/buffer/mpsc_ring_buffer.h"

#include "qlog/primitives/util.h"

#include <atomic>
#include <cstring>

namespace qlog
{

// 内部辅助：跨线程访问 status 字段的原子包装

static inline std::atomic_ref<block_status> atomic_status(block_status& s)
{
    return std::atomic_ref<block_status>(s);
}

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
    // 单线程初始化路径：relaxed 即可
    for (uint32_t i = 0; i < block_count_; ++i)
    {
        atomic_status(blocks_[i].chunk_head.status)
            .store(block_status::unused, std::memory_order_relaxed);
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

    if (used >= block_count_)
        return 0;

    return block_count_ - used;
}

// 移除 alloc 结尾处多余的 status=unused 写入

write_handle mpsc_ring_buffer::alloc_write_chunk(uint32_t size)
{
    write_handle handle;

    if (blocks_ == nullptr || block_count_ == 0)
        return handle;

    const uint32_t header_size =
        static_cast<uint32_t>(reinterpret_cast<size_t>(&(((block::chunk_head_def*)0)->data)));
    const uint32_t total_size = size + header_size;
    const uint32_t need_block_count = (total_size + CACHE_LINE_SIZE - 1u) >> CACHE_LINE_SIZE_LOG2;

    // 超容量 or 超半缓冲区（wrap-around 无法放下）直接拒绝
    if (need_block_count == 0 || need_block_count > block_count_ ||
        (need_block_count << 1u) > block_count_)
    {
        return handle;
    }

    // TLS 缓存 read_cursor（对标 BqLog miso_tls_buffer_info）
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

        current_write = cursors_.write_cursor.load_acquire();
        const uint32_t new_write = current_write + need_block_count;

        if ((new_write - read_cursor_cache) > block_count_)
        {
            read_cursor_cache = cursors_.read_cursor.load_acquire();
            if ((new_write - read_cursor_cache) > block_count_)
                return handle; // 真正不足，直接返回（不继续 fetch_add）
        }

        current_write = cursors_.write_cursor.fetch_add_relaxed(need_block_count);
        const uint32_t next_write = current_write + need_block_count;

        if ((next_write - read_cursor_cache) > block_count_)
        {
            while ((next_write - (read_cursor_cache = cursors_.read_cursor.load_acquire())) >
                   block_count_)
            {
                uint32_t expected = next_write;
                if (cursors_.write_cursor.compare_exchange_strong(
                        expected,
                        current_write,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed
                    ))
                {
                    return handle; // CAS 回滚成功，本次失败
                }
                // CAS 失败：其他线程已推进 write_cursor，继续刷新 read_cursor
            }
        }

        const uint32_t start_idx = current_write & block_count_mask_;
        const uint32_t end_idx = next_write & block_count_mask_;

        if (end_idx == 0u || start_idx < end_idx)
            break; // 连续区间，分配成功

        // wrap-around：标记 INVALID 占位
        // 消费者 read_chunk 用 acquire 读取 status，必须有 release 配对
        block* wrap_block = &blocks_[start_idx];
        wrap_block->chunk_head.set_block_num(block_count_ - start_idx);
        wrap_block->chunk_head.data_size = 0;
        atomic_status(wrap_block->chunk_head.status)
            .store(block_status::invalid, std::memory_order_release);
        // 继续外层 while，下一轮从 end_idx 开始
    }

    block* new_block = &blocks_[current_write & block_count_mask_];
    new_block->chunk_head.set_block_num(need_block_count);
    new_block->chunk_head.data_size = size;

    handle.success = true;
    handle.cursor = current_write;
    handle.data = new_block->chunk_head.data;
    handle.block_count = need_block_count;
    return handle;
}

// 移除 atomic_thread_fence，改用 atomic_ref.store(used, release)
void mpsc_ring_buffer::commit_write_chunk(const write_handle& handle)
{
    if (!handle.success)
        return;

    block* target_block = &blocks_[handle.cursor & block_count_mask_];

    // 对标 BqLog：BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT(...).store_release(used)
    atomic_status(target_block->chunk_head.status)
        .store(block_status::used, std::memory_order_release);
}

read_handle mpsc_ring_buffer::read_chunk()
{
    read_handle handle;

    if (blocks_ == nullptr || block_count_ == 0)
        return handle;

    uint32_t current_read_cursor = cursors_.read_cursor.load_relaxed();
    uint32_t scanned_blocks = 0;

    while (scanned_blocks < block_count_)
    {
        block* current_block = &blocks_[current_read_cursor & block_count_mask_];

        // 修复：atomic_ref.load(acquire) 对标 BqLog .load_acquire()
        const block_status status =
            atomic_status(current_block->chunk_head.status).load(std::memory_order_acquire);

        switch (status)
        {
        case block_status::invalid:
        {
            // INVALID：wrap-around 占位块，acquire load 已同步 block_num 写入
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
            // acquire load 已建立 happens-before：
            //   producer 写 block_num/data_size/data 均可安全读取
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

    // relaxed：由后续 read_cursor.store_release 建立对 producer 的可见性
    atomic_status(current_block->chunk_head.status)
        .store(block_status::unused, std::memory_order_relaxed);
    current_block->chunk_head.set_block_num(0);
    current_block->chunk_head.data_size = 0;

    const uint32_t new_read_cursor = handle.cursor + handle.block_count;
    // release store：producer 的 load_acquire(read_cursor) 后可见上面的 unused 写
    cursors_.read_cursor.store_release(new_read_cursor);
}

} // namespace qlog