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

    // 分配对齐内存（M0 依赖）
    void* ptr = aligned_alloc(64, capacity);
    if (ptr == nullptr)
    {
        return false; // 分配失败
    }

    // 初始化成员变量
    buffer_ = static_cast<uint8_t*>(ptr); // 转换为 uint8_t*
    capacity_bytes_ = capacity;
    block_count_ = capacity >> k_block_size_log2; // 使用位移计算块数

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

    uint32_t size_required = size + sizeof(block_header);
    uint32_t blocks_needed = (size_required + (k_block_size - 1)) >> k_block_size_log2;
    if (blocks_needed == 0 || blocks_needed > block_count_)
    {
        return nullptr;
    }

    // 检查空间（缓存路径）
    uint32_t left_space = wt_read_cursor_cached_ + block_count_ - wt_write_cursor_cached_;
    if (left_space < blocks_needed)
    {
        // 缓存可能失效，更新缓存
        wt_read_cursor_cached_ = read_cursor_.load_acquire();
        left_space = wt_read_cursor_cached_ + block_count_ - wt_write_cursor_cached_;

        if (left_space < blocks_needed)
        {
            return nullptr; // 真的没空间
        }
    }

    // 写入 header
    uint32_t write_index = wt_write_cursor_cached_ % static_cast<uint32_t>(block_count_);
    uint8_t* header_ptr = buffer_ + (write_index << k_block_size_log2); // 环回到 ring 内地址
    auto* header = reinterpret_cast<block_header*>(header_ptr);
    header->block_count = blocks_needed;
    header->data_size = size;

    void* payload_ptr = header_ptr + sizeof(block_header);

    return payload_ptr;
}

void spsc_ring_buffer::commit_write_chunk()
{
    if (buffer_ == nullptr || block_count_ == 0)
    {
        return;
    }

    uint32_t write_index = wt_write_cursor_cached_ % static_cast<uint32_t>(block_count_);
    uint8_t* header_ptr = buffer_ + (write_index << k_block_size_log2);
    const auto* header = reinterpret_cast<const block_header*>(header_ptr);
    uint32_t blocks_allocated = header->block_count;

    // 更新本地缓存
    wt_write_cursor_cached_ += blocks_allocated;

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

    uint32_t read_index = rt_read_cursor_cached_ % static_cast<uint32_t>(block_count_);
    uint8_t* header_ptr = buffer_ + (read_index << k_block_size_log2);
    return header_ptr + sizeof(block_header);
}

void spsc_ring_buffer::commit_read_chunk()
{
    if (buffer_ == nullptr || block_count_ == 0)
    {
        return;
    }

    uint32_t read_index = rt_read_cursor_cached_ % static_cast<uint32_t>(block_count_);
    uint8_t* header_ptr = buffer_ + (read_index << k_block_size_log2);
    const auto* header = reinterpret_cast<const block_header*>(header_ptr);
    uint32_t blocks_read = header->block_count;

    // 更新本地缓存
    rt_read_cursor_cached_ += blocks_read;

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
    uint32_t read_index = rt_read_cursor_cached_ % static_cast<uint32_t>(block_count_);
    const uint8_t* header_ptr = buffer_ + (read_index << k_block_size_log2);
    return reinterpret_cast<const block_header*>(header_ptr)->data_size;
}
// ⚠️ 调用时机约束：此方法只能在 read_chunk() 返回非 nullptr 后、commit_read_chunk() 之前调用。
//  如果 read_chunk() 返回 nullptr，调用结果未定义。

} // namespace qlog