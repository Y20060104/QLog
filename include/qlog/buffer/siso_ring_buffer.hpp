#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <type_traits>

#include "qlog/buffer/log_buffer_defs.hpp"
#include "qlog/utility/utility_types.hpp"

namespace qlog::buffer {
class siso_ring_buffer {
   private:
    static constexpr std::size_t BLOCK_SIZE = 8;
    static constexpr std::size_t BLOCK_SIZE_LOG2 = 3;
    static constexpr std::size_t CACHE_LINE_SIZE = qlog::utility::QLOG_CACHE_LINE_SIZE;

    struct alignas(4) chunk_head_def {
        std::uint32_t block_num;
        std::uint32_t data_size;
        std::uint8_t data[1];
        std::uint8_t padding[3];
    };

    static_assert(sizeof(chunk_head_def) == 12);
    static_assert(alignof(chunk_head_def) == 4);
    static_assert(offsetof(chunk_head_def, block_num) == 0);
    static_assert(offsetof(chunk_head_def, data_size) == 4);
    static_assert(offsetof(chunk_head_def, data) == 8);

    struct block {
        std::uint8_t padding[BLOCK_SIZE];
    };
    static_assert(sizeof(block) == BLOCK_SIZE);

    struct alignas(CACHE_LINE_SIZE) head {
        // CR: 补alignas()?
        std::uint32_t aligned_blocks_count_cache_;  // 初始化/恢复的容量快照
        std::uint32_t rt_reading_cursor_cache_;
        std::uint32_t rt_writing_cursor_cache_;

        alignas(CACHE_LINE_SIZE) std::uint32_t wt_reading_cursor_cache_;
        std::uint32_t wt_writing_cursor_cache_;

        alignas(CACHE_LINE_SIZE) std::atomic<std::uint32_t> reading_cursor_;
        alignas(CACHE_LINE_SIZE) std::atomic<std::uint32_t> writing_cursor_;
    };
    // CR: 为什么说后续不要照搬 block::to_chunk_head() 的类型重解释来访问外部字节；两个 u32 用 cpp
    // 内的 memcpy 辅助函数访问。payload 固定从 8 字节后开始，不能使用 sizeof(chunk_head_def)
    // 计算头开销，也不能整体写入 12 字节覆盖 payload。两种处理方式 BQLog性能更好
    // 还是QLog性能更好呢？

    static_assert(std::is_standard_layout_v<head>);
    static_assert(sizeof(std::atomic<std::uint32_t>) == 4);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(alignof(head) == CACHE_LINE_SIZE);
    static_assert(sizeof(head) == 4 * CACHE_LINE_SIZE);

    static_assert(offsetof(head, aligned_blocks_count_cache_) == 0);
    static_assert(offsetof(head, rt_reading_cursor_cache_) == 4);
    static_assert(offsetof(head, rt_writing_cursor_cache_) == 8);
    static_assert(offsetof(head, wt_reading_cursor_cache_) == CACHE_LINE_SIZE);
    static_assert(offsetof(head, wt_writing_cursor_cache_) == CACHE_LINE_SIZE + 4);
    static_assert(offsetof(head, reading_cursor_) == 2 * CACHE_LINE_SIZE);
    static_assert(offsetof(head, writing_cursor_) == 3 * CACHE_LINE_SIZE);

   public:
    struct siso_buffer_batch_read_handle {
        enum_buffer_result_code result = enum_buffer_result_code::err_empty_log_buffer;

        bool has_next() const {
            return current_cursor_ != end_cursor_;
        }

        log_buffer_read_handle next();
#ifdef QLOG_DEBUG
        bool verify_chunk(const log_buffer_read_handle& handle) const;
#endif

       private:
        friend class siso_ring_buffer;

        siso_ring_buffer* parent_;
        std::uint32_t start_cursor_;
        std::uint32_t current_cursor_;
        std::uint32_t end_cursor_;
#ifdef QLOG_DEBUG
        std::uint32_t last_cursor_ = UINT32_MAX;
#endif
    };

    siso_ring_buffer() = delete;
    siso_ring_buffer(void* buffer, std::size_t buffer_size, bool is_memory_recovery);

    siso_ring_buffer(const siso_ring_buffer&) = delete;
    siso_ring_buffer& operator=(const siso_ring_buffer&) = delete;

    ~siso_ring_buffer();

    void renew();

    log_buffer_write_handle alloc_write_chunk(std::uint32_t size);
    void commit_write_chunk(const log_buffer_write_handle& handle);

    log_buffer_read_handle read_chunk();
    log_buffer_read_handle read_an_empty_chunk();
    void discard_read_chunk(log_buffer_read_handle& handle);
    void return_read_chunk(const log_buffer_read_handle& handle);

    siso_buffer_batch_read_handle batch_read();
    void return_batch_read_chunks(const siso_buffer_batch_read_handle& handle);

    void data_traverse(void (*callback)(std::uint8_t*, std::uint32_t, void*), void* user_data);

    static std::uint32_t calculate_min_size_of_memory(std::uint32_t expected_buffer_size);

    void set_thread_check_enable(bool enabled);
    bool is_thread_check_enable() const;

    std::uint32_t get_max_alloc_size() const;

    memory_map_buffer_state get_memory_map_buffer_state() const {
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
    block& cursor_to_block(std::uint32_t cursor) {
        return aligned_blocks_[cursor & (aligned_blocks_count_ - 1U)];
    }

    std::uint8_t* get_block_data_addr(block& current_block);

    bool try_recover_from_exist_memory_map(void* buffer, std::size_t buffer_size);

    void init_with_memory_map(void* buffer, std::size_t buffer_size);

    void init_with_memory(void* buffer, std::size_t buffer_size);

    void init_cursors();

    head* head_;
    block* aligned_blocks_;
    std::uint32_t aligned_blocks_count_;
    bool is_memory_recovery_;
    memory_map_buffer_state mmap_buffer_state_;
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
