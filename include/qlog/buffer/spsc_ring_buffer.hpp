#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <type_traits>

#include "qlog/buffer/log_buffer_defs.hpp"

namespace qlog::buffer {
class SpscRingBuffer {
   private:
    static constexpr std::size_t BLOCK_SIZE = 8;
    static constexpr std::size_t BLOCK_SIZE_LOG2 = 3;
    static constexpr std::size_t CACHE_LINE_SIZE = 64;

    struct alignas(4) ChunkHead {
        std::uint32_t block_num;
        std::uint32_t data_size;
        std::uint8_t data[1];
        std::uint8_t padding[3];
    };

    static_assert(sizeof(ChunkHead) == 12);
    static_assert(alignof(ChunkHead) == 4);
    static_assert(offsetof(ChunkHead, block_num) == 0);
    static_assert(offsetof(ChunkHead, data_size) == 4);
    static_assert(offsetof(ChunkHead, data) == 8);

    struct Block {
        std::uint8_t bytes[BLOCK_SIZE];
    };
    static_assert(sizeof(Block) == BLOCK_SIZE);

    struct alignas(CACHE_LINE_SIZE) Head {
        // CR: 补alignas()?
        std::uint32_t aligned_blocks_count_cache_;  // 初始化/恢复的容量快照
        std::uint32_t rt_reading_cursor_cache_;
        std::uint32_t rt_writing_cursor_cache_;

        alignas(CACHE_LINE_SIZE) std::uint32_t wt_reading_cursor_cache_;
        std::uint32_t wt_writing_cursor_cache_;

        alignas(CACHE_LINE_SIZE) std::atomic<std::uint32_t> reading_cursor_;
        alignas(CACHE_LINE_SIZE) std::atomic<std::uint32_t> writing_cursor_;
    };
    // CR: 为什么说后续不要照搬 Block::to_chunk_head() 的类型重解释来访问外部字节；两个 u32 用 cpp
    // 内的 memcpy 辅助函数访问。payload 固定从 8 字节后开始，不能使用 sizeof(ChunkHead)
    // 计算头开销，也不能整体写入 12 字节覆盖 payload。两种处理方式 BQLog性能更好
    // 还是QLog性能更好呢？

    static_assert(std::is_standard_layout_v<Head>);
    static_assert(sizeof(std::atomic<std::uint32_t>) == 4);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(alignof(Head) == CACHE_LINE_SIZE);
    static_assert(sizeof(Head) == 4 * CACHE_LINE_SIZE);

    static_assert(offsetof(Head, aligned_blocks_count_cache_) == 0);
    static_assert(offsetof(Head, rt_reading_cursor_cache_) == 4);
    static_assert(offsetof(Head, rt_writing_cursor_cache_) == 8);
    static_assert(offsetof(Head, wt_reading_cursor_cache_) == 64);
    static_assert(offsetof(Head, wt_writing_cursor_cache_) == 68);
    static_assert(offsetof(Head, reading_cursor_) == 128);
    static_assert(offsetof(Head, writing_cursor_) == 192);

   public:
    struct BatchReadHandle {
        BufferResult result = BufferResult::err_empty_log_buffer;

        bool has_next() const {
            return current_cursor_ != end_cursor_;
        }

        LogBufferReadHandle next();
#ifdef QLOG_DEBUG
        bool verify_chunk(const LogBufferReadHandle& handle) const;
#endif

       private:
        friend class SpscRingBuffer;

        SpscRingBuffer* parent_;
        std::uint32_t start_cursor_;
        std::uint32_t current_cursor_;
        std::uint32_t end_cursor_;
#ifdef QLOG_DEBUG
        std::uint32_t last_cursor_ = UINT32_MAX;
#endif
    };

    SpscRingBuffer() = delete;
    SpscRingBuffer(void* buffer, std::size_t buffer_size, bool is_memory_recovery);

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    ~SpscRingBuffer();

    void renew();

    LogBufferWriteHandle alloc_write_chunk(std::uint32_t size);
    void commit_write_chunk(const LogBufferWriteHandle& handle);

    LogBufferReadHandle read_chunk();
    LogBufferReadHandle read_an_empty_chunk();
    void discard_read_chunk(LogBufferReadHandle& handle);
    void return_read_chunk(const LogBufferReadHandle& handle);

    BatchReadHandle batch_read();
    void return_batch_read_chunks(const BatchReadHandle& handle);

    void data_traverse(void (*callback)(std::uint8_t*, std::uint32_t, void*), void* user_data);

    static std::uint32_t calculate_min_size_of_memory(std::uint32_t expected_buffer_size);

    void set_thread_check_enable(bool enabled);
    bool is_thread_check_enable() const;

    std::uint32_t get_max_alloc_size() const;

    MemoryMapBufferState get_memory_map_buffer_state() const {
        return mmap_buffer_state_;
    }

    const std::uint8_t* get_buffer_addr() const {
        return data_bytes_;
    }

    std::uint32_t get_block_size() const {
        return static_cast<std::uint32_t>(BLOCK_SIZE);
    }

    std::uint32_t get_total_blocks_count() const {
        return aligned_blocks_count_;
    }

    bool get_is_memory_recovery() const {
        return is_memory_recovery_;
    }

   private:
    Block& cursor_to_block(std::uint32_t cursor) {
        return aligned_blocks_[cursor & (aligned_blocks_count_ - 1U)];
    }

    std::uint8_t* get_block_data_addr(Block& block);

    bool try_recover_from_exist_memory_map(void* buffer, std::size_t buffer_size);

    void init_with_memory_map(void* buffer, std::size_t buffer_size);

    void init_with_memory(void* buffer, std::size_t buffer_size);

    void init_cursors();

    Head* head_;
    Block* aligned_blocks_;
    std::uint32_t aligned_blocks_count_;
    bool is_memory_recovery_;
    MemoryMapBufferState mmap_buffer_state_;
    std::uint8_t* data_bytes_;
#ifdef QLOG_DEBUG
    bool check_thread_ = true;
    std::thread::id write_thread_id_{};
    std::thread::id read_thread_id_{};
    bool is_read_chunk_waiting_for_return_ = false;

    void debug_check_write_thread();
    void debug_check_read_thread();
#endif
};
}  // namespace qlog::buffer
