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

    // 分配对齐内存
    uint32_t total_size = block_count_ * CACHE_LINE_SIZE;
    buffer_ptr_ = static_cast<uint8_t*>(aligned_alloc(CACHE_LINE_SIZE, total_size));

    if (!buffer_ptr_)
    {
        return;
    }

    blocks_ = reinterpret_cast<block*>(buffer_ptr_);

    // 初始化cursor
    cursors_.write_cursor.store_relaxed(0);
    cursors_.read_cursor.store_relaxed(0);

    // 初始化所有块
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
    {
        return 0;
    }

    const uint32_t write_cursor = cursors_.write_cursor.load_acquire();
    const uint32_t read_cursor = cursors_.read_cursor.load_acquire();
    const uint32_t used = write_cursor - read_cursor;
    if (used >= block_count_)
    {
        return 0;
    }
    return static_cast<uint32_t>(block_count_ - used);
}

write_handle mpsc_ring_buffer::alloc_write_chunk(uint32_t size)
{
    write_handle handle;

    if (blocks_ == nullptr || block_count_ == 0)
    {
        return handle;
    }

    const uint32_t header_size = offsetof(block::chunk_head_def, data);
    uint32_t total_size = size + header_size;
    uint32_t need_block_count = (total_size + CACHE_LINE_SIZE - 1) >> CACHE_LINE_SIZE_LOG2;

    if (need_block_count > block_count_ || need_block_count == 0)
    {
        return handle;
    }

    // TLS缓存初始化
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
        uint32_t new_write = current_write + need_block_count;

        if ((new_write - read_cursor_cache) >= block_count_)
        {
            read_cursor_cache = cursors_.read_cursor.load_acquire();
            if((new_write-read_cursor_cache)>block_count_)
            {
                return handle;
            }

        }

        current_write = cursors_.write_cursor.fetch_add_relaxed(need_block_count);
        uint32_t next_write = current_write + need_block_count;

        while ((next_write - read_cursor_cache) >= block_count_)
        {
            read_cursor_cache = cursors_.read_cursor.load_acquire();
            if ((next_write - read_cursor_cache) > block_count_)
            {
                // 缓冲真的满了，尝试回滚
                uint32_t expected = next_write;
                if (cursors_.write_cursor.compare_exchange_strong(
                        expected,
                        current_write,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed
                    ))
                {
                    return handle;
                }
                // CAS 失败，说明有其他线程修改了 write_cursor，跳出内层 while
            }
        }

    

        uint32_t start_idx = current_write & block_count_mask_;
        uint32_t end_idx = next_write & block_count_mask_;

        if (start_idx < end_idx || end_idx == 0)
        {
            break; // 内存连续，成功分配
        }
       
            // 内存不连续（wrap-around），标记为 invalid 块后重新尝试
            block* wrap_block = &blocks_[start_idx];
            wrap_block->chunk_head.set_block_num(block_count_ - start_idx);
            wrap_block->chunk_head.data_size = 0;
            wrap_block->chunk_head.status = block_status::invalid;
        
    }
    block* new_block = &blocks_[current_write & block_count_mask_];
    new_block->chunk_head.set_block_num(need_block_count);
    new_block->chunk_head.status = block_status::unused;
    new_block->chunk_head.data_size = size;

    handle.success = true;
    handle.cursor = current_write;
    handle.data = new_block->chunk_head.data; // 设置数据指针
    handle.block_count = need_block_count;
    return handle;
}

void mpsc_ring_buffer::commit_write_chunk(const write_handle& handle)
{
    if (!handle.success)
    {
        return;
    }

    block* target_block = &blocks_[handle.cursor & block_count_mask_];
    std::atomic_thread_fence(std::memory_order_release);
    target_block->chunk_head.status = block_status::used;
}

read_handle mpsc_ring_buffer::read_chunk()
{
    read_handle handle;

    if (blocks_ == nullptr || block_count_ == 0)
    {
        return handle;
    }

    const uint32_t start_read_cursor = cursors_.read_cursor.load_relaxed();
    uint32_t current_read_cursor = start_read_cursor;
    uint32_t scanned_blocks = 0;

    while (scanned_blocks < block_count_)
    {
        block* current_block = &blocks_[current_read_cursor & block_count_mask_];
        block_status status = current_block->chunk_head.status;

        switch (status)
        {
        case block_status::invalid:
        {
            //  跳过 wrap-around 的占位块
            uint32_t block_num = current_block->chunk_head.get_block_num();
            if (block_num == 0)
            {
                block_num = 1;
            }
            current_read_cursor += block_num;
            scanned_blocks += block_num;
            break;
        }
        case block_status::unused:
            //  遇到 unused 直接退出，缓冲空
            return handle;
        case block_status::used:
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            uint32_t block_num = current_block->chunk_head.get_block_num();
            if (block_num == 0)
            {
                return handle;
            }
            //  找到有效数据，返回
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
    {
        return;
    }

    block* current_block = &blocks_[handle.cursor & block_count_mask_];
    current_block->chunk_head.status = block_status::unused;
    current_block->chunk_head.set_block_num(0);
    current_block->chunk_head.data_size = 0;

    uint32_t new_read_cursor = handle.cursor + handle.block_count;
    cursors_.read_cursor.store_release(new_read_cursor);
}
} // namespace qlog