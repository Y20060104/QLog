#pragma once

#include "qlog/primitives/aligned_alloc.h"
#include "qlog/primitives/atomic.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace qlog
{
constexpr size_t CACHE_LINE_SIZE = 64;
constexpr size_t CACHE_LINE_SIZE_LOG2 = 6;

// block_status 枚举
enum class block_status : uint8_t
{
    unused = 0,
    used = 1,
    invalid = 2
};

// block 结构
union alignas(CACHE_LINE_SIZE) block
{
    struct alignas(4) chunk_head_def
    {
    private:
        uint8_t block_num_[3]; // 2**24条
    public:
        // 1字节status
        block_status status;
        // 4字节data_size 有效字节数
        uint32_t data_size;
        // 数据起点
        uint8_t data[1];
        uint8_t padding[3];
        // 内联getter/setter
        inline uint32_t get_block_num() const
        {
            // 按小端序组装 3 个字节
            return (uint32_t(block_num_[0])) | (uint32_t(block_num_[1]) << 8) |
                   (uint32_t(block_num_[2]) << 16);
        }

        inline void set_block_num(uint32_t num)
        {
            // 按小端序拆解 32 位整数到 3 个字节
            block_num_[0] = num & 0xFF;
            block_num_[1] = (num >> 8) & 0xFF;
            block_num_[2] = (num >> 16) & 0xFF;
        }
    } chunk_head;

    uint8_t raw_data[CACHE_LINE_SIZE]; // 占位 确保block为64字节
};
// static_assert(sizeof(block)==64,"block size must equal cache line");
static_assert(alignof(block) == 64, "block must be cache-line aligned");

struct alignas(CACHE_LINE_SIZE) cursor_set
{
    atomic<uint32_t> write_cursor;
    uint8_t padding0_[CACHE_LINE_SIZE - sizeof(atomic<uint32_t>)];
    atomic<uint32_t> read_cursor;
    uint8_t padding1_[CACHE_LINE_SIZE - sizeof(atomic<uint32_t>)];
};
static_assert(sizeof(cursor_set) == 2 * CACHE_LINE_SIZE, "cursor_set must be 2 must cache lines");

struct tls_buffer_info
{
    uint32_t read_cursor_cache;
    bool is_new_created = true;
};
// // 使用方式
// static thread_local tls_buffer_info tls_info;

struct write_handle
{
    bool success = false;
    uint32_t cursor = 0;      // 从fetch_add获得的cursor
    uint8_t* data = nullptr;  // 指向可写入的数据区
    uint32_t block_count = 0; // 分配的块数
};

struct read_handle
{
    bool success = false;
    uint32_t cursor = 0; // 读取的起始cursor
    const uint8_t* data = nullptr;
    uint32_t data_size = 0;
    uint32_t block_count = 0; // 消息占用的块数
};

class alignas(CACHE_LINE_SIZE) mpsc_ring_buffer
{
private:
    // 配置信息
    size_t block_count_;
    uint32_t block_count_mask_; // block_count - 1,快速取模
    // 内存块数组
    block* blocks_;
    // 异步写游标
    cursor_set cursors_; // 指向对齐的块数组
    uint8_t* buffer_ptr_;

public:
    explicit mpsc_ring_buffer(uint32_t capacity_bytes);
    ~mpsc_ring_buffer();

    // 禁用拷贝
    mpsc_ring_buffer(const mpsc_ring_buffer&) = delete;
    mpsc_ring_buffer& operator=(const mpsc_ring_buffer&) = delete;

    // 生产者接口
    write_handle alloc_write_chunk(uint32_t size);
    void commit_write_chunk(const write_handle& handle);

    // 消费者接口
    read_handle read_chunk();
    void commit_read_chunk(const read_handle& handle);

    // 工具函数
    void reset();
    uint32_t capacity() const
    {
        return block_count_ * CACHE_LINE_SIZE;
    }

private:
    block* cursor_to_block(uint32_t cursor)
    {
        return &blocks_[cursor & block_count_mask_];
    }
};

} // namespace qlog
