#include "spsc_ring_buffer.h"

namespace qlog
{

bool spsc_ring_buffer::init(size_t capacity)
{
    // 参数验证
    if ((capacity & (k_block_size - 1)) != 0)
    {
        return false; // 不是 8 的倍数
    }

    size_t raw_count = capacity >> k_block_size_log2;
    size_t actual_count = 1;

    while (actual_count < raw_count)
    {
        actual_count <<= 1;
    }

    // 分配对齐内存（M0 依赖）
    void* ptr = aligned_alloc(64, capacity);
    if (ptr == nullptr)
    {
        return false; // 分配失败
    }

    // 初始化成员变量
    buffer_ = static_cast<uint8_t*>(ptr); // 转换为 uint8_t*
    block_count_ = actual_count;          // 使用位移计算块数
    block_count_mask_ = actual_count - 1;
    capacity_bytes_ = actual_count << k_block_size_log2;
    // 初始化游标
    write_cursor_.store(0, std::memory_order_relaxed);
    read_cursor_.store(0, std::memory_order_relaxed);
    wt_write_cursor_cached_ = 0;
    wt_read_cursor_cached_ = 0;
    rt_read_cursor_cached_ = 0;
    rt_write_cursor_cached_ = 0;

    return true;
}

void* spsc_ring_buffer::alloc_write_chunk(size_t size)
{
    if (buffer_ == nullptr || block_count_ == 0)
    {
        return nullptr;
    }

    const uint32_t size_required = size + sizeof(block_header);
    const uint32_t data_only_blocks =
        (static_cast<uint32_t>(size) + k_block_size - 1) >> k_block_size_log2;
    const uint32_t blocks_needed = (size_required + (k_block_size - 1)) >> k_block_size_log2;
    if (blocks_needed == 0 || blocks_needed > block_count_)
    {
        return nullptr;
    }

    // 检查空间（缓存路径）
    const uint32_t write_index = wt_write_cursor_cached_ & static_cast<uint32_t>(block_count_mask_);
    const uint32_t left_to_tail = static_cast<uint32_t>(block_count_) - write_index;
    // actual_blocks：本次实际消耗的块数
    // is_split：header 在 tail 处，data 回绕到 buffer[0]

    uint32_t actual_blocks;
    bool is_split;
    if (left_to_tail < blocks_needed)
    {
        // tail 空间不足以放下完整 header+data：
        //   消耗 left_to_tail 个 padding 块 + data_only_blocks 个数据块
        //   data 写到 buffer 开头
        actual_blocks = left_to_tail + data_only_blocks;
        is_split = true;
    }
    else
    {
        actual_blocks = blocks_needed;
        is_split = false;
    }

    // 超过半个缓冲区则一次 alloc 永远无法放下
    if (actual_blocks > static_cast<uint32_t>(block_count_))
    {
        return nullptr;
    }

    // 空间检查（缓存路径，避免频繁原子读）
    uint32_t left_space =
        wt_read_cursor_cached_ + static_cast<uint32_t>(block_count_) - wt_write_cursor_cached_;
    if (left_space < actual_blocks)
    {
        wt_read_cursor_cached_ = read_cursor_.load_acquire();
        left_space =
            wt_read_cursor_cached_ + static_cast<uint32_t>(block_count_) - wt_write_cursor_cached_;
        if (left_space < actual_blocks)
            return nullptr;
    }

    // 写 header（始终在 write_index 处）
    uint8_t* header_ptr = buffer_ + (write_index << k_block_size_log2);
    auto* header = reinterpret_cast<block_header*>(header_ptr);
    header->block_count = actual_blocks;
    header->data_size = static_cast<uint32_t>(size);

    // split 时返回 buffer_ 起始地址（data 从头写），非 split 返回 header 后紧接地址
    return is_split ? buffer_ : (header_ptr + sizeof(block_header));
}

void spsc_ring_buffer::commit_write_chunk()
{
    if (buffer_ == nullptr || block_count_ == 0)
    {
        return;
    }

    const uint32_t write_index = wt_write_cursor_cached_ & static_cast<uint32_t>(block_count_mask_);
    const auto* header =
        reinterpret_cast<const block_header*>(buffer_ + (write_index << k_block_size_log2));

    // 更新本地缓存
    wt_write_cursor_cached_ += header->block_count;

    // 原子发布（release 语义）
    write_cursor_.store_release(wt_write_cursor_cached_);
}

// 读端实现
const void* spsc_ring_buffer::read_chunk()
{
    if (buffer_ == nullptr || block_count_ == 0)
    {
        return nullptr;
    }

    if (rt_write_cursor_cached_ == rt_read_cursor_cached_)
    {
        // 没有新数据，尝试更新缓存
        rt_write_cursor_cached_ = write_cursor_.load_acquire();
    }
    if (rt_write_cursor_cached_ == rt_read_cursor_cached_)
    {
        // 的确没有新数据
        return nullptr;
    }

    const uint32_t read_index = rt_read_cursor_cached_ & static_cast<uint32_t>(block_count_mask_);
    const uint8_t* header_ptr = buffer_ + (read_index << k_block_size_log2);
    const auto* header = reinterpret_cast<const block_header*>(header_ptr);

    const uint8_t* candidate = header_ptr + sizeof(block_header);
    if (candidate + header->data_size > buffer_ + capacity_bytes_)
    {
        return buffer_;
    }

    return candidate;
}

void spsc_ring_buffer::commit_read_chunk()
{
    if (buffer_ == nullptr || block_count_ == 0)
    {
        return;
    }

    const uint32_t read_index = rt_read_cursor_cached_ % static_cast<uint32_t>(block_count_);

    const auto* header =
        reinterpret_cast<const block_header*>(buffer_ + (read_index << k_block_size_log2));

    // 更新本地缓存
    rt_read_cursor_cached_ += header->block_count;

    // 原子发布（release 语义）
    read_cursor_.store_release(rt_read_cursor_cached_);
}

void spsc_ring_buffer::reset()
{
    write_cursor_.store_relaxed(0);
    read_cursor_.store_relaxed(0);
    wt_write_cursor_cached_ = 0;
    wt_read_cursor_cached_ = 0;
    rt_write_cursor_cached_ = 0;
    rt_read_cursor_cached_ = 0;
}

// 析构函数
spsc_ring_buffer::~spsc_ring_buffer()
{
    if (buffer_)
    {
        aligned_free(buffer_);
        buffer_ = nullptr;
    }
}

uint32_t spsc_ring_buffer::last_read_data_size() const
{
    if (!buffer_ || block_count_ == 0)
    {
        return 0;
    }
    // rt_read_cursor_cached_指向当前未消费的块的起始位置
    // 该块的block_header.data_size就是用户数据的大小
    const uint32_t read_index = rt_read_cursor_cached_ & static_cast<uint32_t>(block_count_mask_);

    return reinterpret_cast<const block_header*>(buffer_ + (read_index << k_block_size_log2))
        ->data_size;
    // ⚠️ 调用时机约束：此方法只能在 read_chunk() 返回非 nullptr 后、commit_read_chunk() 之前调用。
    //  如果 read_chunk() 返回 nullptr，调用结果未定义。
}
} // namespace qlog