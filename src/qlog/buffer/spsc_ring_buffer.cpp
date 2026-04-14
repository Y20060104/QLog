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
    write_cursor_cached_ = 0;
    read_cursor_cached_ = 0;
    read_write_cursor_cached_ = 0;

    return true;
}

void* spsc_ring_buffer::alloc_write_chunk(size_t size)
{
    uint32_t size_required = size + sizeof(block_header);
    uint32_t blocks_needed = (size_required + (k_block_size - 1)) >> k_block_size_log2;

    uint32_t current_write_cursor_ = write_cursor_cached_;
    uint32_t current_read_cursor_ = read_cursor_cached_;

    // 检查空间（缓存路径）
    uint32_t left_space = current_read_cursor_ + block_count_ - current_write_cursor_;
    if (left_space < blocks_needed)
    {
        // 缓存可能失效，更新缓存
        current_read_cursor_ = read_cursor_.load_acquire();
        read_cursor_cached_ = current_read_cursor_;
        left_space = current_read_cursor_ + block_count_ - current_write_cursor_;

        if (left_space < blocks_needed)
        {
            return nullptr; // 真的没空间
        }
    }

    // 写入 header
    uint8_t* header_ptr =
        buffer_ + (current_write_cursor_ << k_block_size_log2); // 左移计算字节地址
    auto* header = reinterpret_cast<block_header*>(header_ptr);
    header->block_count = blocks_needed;
    header->data_size = size;

    void* payload_ptr = header_ptr + sizeof(block_header);

    return payload_ptr;
}

void spsc_ring_buffer::commit_write_chunk()
{
    uint8_t* header_ptr = buffer_ + (write_cursor_cached_ << k_block_size_log2);
    const auto* header = reinterpret_cast<const block_header*>(header_ptr);
    uint32_t blocks_allocated = header->block_count;

    // 更新本地缓存
    write_cursor_cached_ += blocks_allocated;

    // 原子发布（release 语义）
    write_cursor_.store_release(write_cursor_cached_);
}

// 读端实现
const void* spsc_ring_buffer::read_chunk()
{
    if (read_write_cursor_cached_ == read_cursor_cached_)
    {
        // 没有新数据，尝试更新缓存
        read_write_cursor_cached_ = write_cursor_.load_acquire();
    }
    if (read_write_cursor_cached_ == read_cursor_cached_)
    {
        // 的确没有新数据
        return nullptr;
    }

    uint8_t* header_ptr = buffer_ + (read_cursor_cached_ << k_block_size_log2);
    return header_ptr + sizeof(block_header);
}

void spsc_ring_buffer::commit_read_chunk()
{
    uint8_t* header_ptr = buffer_ + (read_cursor_cached_ << k_block_size_log2);
    const auto* header = reinterpret_cast<const block_header*>(header_ptr);
    uint32_t blocks_read = header->block_count;

    // 更新本地缓存
    read_cursor_cached_ += blocks_read;

    // 原子发布（release 语义）
    read_cursor_.store_release(read_cursor_cached_);
}

void spsc_ring_buffer::reset()
{
    write_cursor_.store_relaxed(0);
    read_cursor_.store_relaxed(0);
    write_cursor_cached_ = 0;
    read_cursor_cached_ = 0;
    read_write_cursor_cached_ = 0;
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

} // namespace qlog