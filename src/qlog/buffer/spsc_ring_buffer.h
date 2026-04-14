#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// M0 依赖 封装的atomic
#include "qlog/primitives/aligned_alloc.h"
#include "qlog/primitives/atomic.h"

namespace qlog
{
// 块配置常量
constexpr size_t k_block_size = 8;
constexpr size_t k_block_size_log2 = 3;             // log2(8)
constexpr size_t k_default_buffer_size = 64 * 1024; // 默认 64KB
constexpr size_t k_cache_line_size = 64;

// 块头部：记录日志条目的元数据
// 每个写入的日志条目前 8 字节是这个 header
struct block_header
{
    // 这条日志占多少个块
    uint32_t block_count;
    // 日志数据长度（不包含header）
    uint32_t data_size;
};

static_assert(sizeof(block_header) == 8, "block_header must be 8 bytes for alignment");

class spsc_ring_buffer
{
private:
    alignas(k_cache_line_size) atomic<uint32_t> write_cursor_; // 原子写游标

    uint32_t write_cursor_cached_; // 缓存的写游标
    uint32_t read_cursor_cached_;  // 写线程缓存的读游标

    // padding 确保write 占一整行cacheline
    uint8_t write_padding_[k_cache_line_size - 3 * sizeof(uint32_t)];

    alignas(k_cache_line_size) atomic<uint32_t> read_cursor_;

    uint32_t read_write_cursor_cached_; // 读线程缓存的写游标

    uint8_t read_padding_[k_cache_line_size - 2 * sizeof(uint32_t)];

    // 共享元数据
    // 缓冲区管理
    uint8_t* buffer_;       // 分配来自M0
    size_t capacity_bytes_; // 总容量
    size_t block_count_;    // 总块数

    // 私有函数工具 # 去BQLog查一下有没有相关实现
    size_t calculate_blocks_needed(size_t data_size);

public:
    spsc_ring_buffer() = default;
    ~spsc_ring_buffer();
    // 禁用拷贝
    spsc_ring_buffer(const spsc_ring_buffer&) = delete;
    spsc_ring_buffer& operator=(const spsc_ring_buffer&) = delete;

    [[nodiscard]] bool init(size_t capacity);

    // 重置缓冲区
    void reset();

    // 写端API
    // 分配写入的块
    [[nodiscard]] void* alloc_write_chunk(size_t size);
    // 提交写入
    void commit_write_chunk();

    // 读端API
    // 读取下一块可用数据
    [[nodiscard]] const void* read_chunk();
    // 提交读取的块（标记为已消费）
    void commit_read_chunk();

    // 查询接口
    size_t capacity() const
    {
        return capacity_bytes_;
    }
    size_t block_count() const
    {
        return block_count_;
    }
};
} // namespace qlog