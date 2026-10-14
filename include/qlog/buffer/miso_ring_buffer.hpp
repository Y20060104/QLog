#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "qlog/buffer/log_buffer_config.hpp"
#include "qlog/buffer/log_buffer_defs.hpp"
#include "qlog/buffer/normal_buffer.hpp"
#include "qlog/platform/atomic/atomic.hpp"
#include "qlog/platform/macros.hpp"
#include "qlog/platform/thread_ops.hpp"
#include "qlog/utility/utility_types.hpp"

namespace qlog::buffer {

class alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) miso_ring_buffer {
   private:
    static_assert(qlog::utility::QLOG_CACHE_LINE_SIZE >> qlog::utility::QLOG_CACHE_LINE_SIZE_LOG2 ==
                      1,
                  "invalid cache line size information");

    enum class block_status : std::uint8_t { unused, used, invalid };

    union block {
       public:
        QLOG_PACK_BEGIN
        struct alignas(4) chunk_head_def {
           private:
            char block_num_[3];

           public:
            block_status status;
            std::uint32_t data_size;
            std::uint8_t data[1];
            std::uint8_t padding[3];

           public:
            qlog_forceinline std::uint32_t get_block_num() const {
                return (*(const std::uint32_t*)block_num_) & (0xFFFFFF);
            }

            qlog_forceinline void set_block_num(std::uint32_t num) {
                *(std::uint32_t*)block_num_ = num;
            }
        } QLOG_PACK_END

            public : static constexpr std::uint32_t MAX_BLOCK_NUM_PER_CHUNK = 0xFFFFFF;

        chunk_head_def chunk_head;

       private:
        std::uint8_t data
            [qlog::utility::QLOG_CACHE_LINE_SIZE];  // 为什么需要64？这样不就超过了cachelinesize吗
    };

    QLOG_PACK_BEGIN
    struct alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) head {
        std::uint32_t read_cursor_cache_;
        std::uint32_t read_cursor_start_cache_;
        std::uint64_t log_checksum_;

        char mmap_misc_data_[qlog::utility::QLOG_CACHE_LINE_SIZE - sizeof(read_cursor_cache_) -
                             sizeof(log_checksum_) - sizeof(read_cursor_start_cache_)];

    } QLOG_PACK_END

        static_assert(sizeof(head) == qlog::utility::QLOG_CACHE_LINE_SIZE,
                      "the size of head should be equal to cache line size");

    static_assert(sizeof(block) == qlog::utility::QLOG_CACHE_LINE_SIZE,
                  "the size of block should be equal to cache line size");

    const std::ptrdiff_t data_block_offset =
        reinterpret_cast<std::ptrdiff_t>(((block*)0)->chunk_head.data);

    static_assert(sizeof(block::chunk_head) < sizeof(block),
                  "the size of chunk_head should be less than that of block");

    struct alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) cursors_set {
        qlog::platform::atomic<std::uint32_t> write_cursor_;

        char cache_line_padding0_[qlog::utility::QLOG_CACHE_LINE_SIZE - sizeof(write_cursor_)];

        qlog::platform::atomic<std::uint32_t> read_cursor_;

        char cache_line_padding1_[qlog::utility::QLOG_CACHE_LINE_SIZE - sizeof(write_cursor_)];
    };

    static_assert(sizeof(cursors_set) == qlog::utility::QLOG_CACHE_LINE_SIZE * 2,
                  "invalid cursors_set size");

    LogBufferConfig config_;
    MemoryMapBufferState mmap_buffer_state_;
    cursors_set cursors_;

    head* head_;
    block* aligned_blocks_;
    std::uint32_t aligned_blocks_count_;

    std::unique_ptr<normal_buffer> buffer_entity_;

#if defined(QLOG_DEBUG)
    alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) bool check_thread_ = true;

    qlog::platform::thread_id empty_thread_id_ = 0;
    qlog::platform::thread_id read_thread_id_ = 0;

    qlog::platform::atomic<std::uint32_t>
        result_code_statistics_[(std::int32_t)BufferResult::result_code_count];

    qlog::platform::atomic<std::uint64_t> total_write_bytes_;
    qlog::platform::atomic<std::uint64_t> total_read_bytes_;

    bool is_read_chunk_waiting_for_return_ = false;
#endif

   public:
    miso_ring_buffer() = delete;

    miso_ring_buffer(const LogBufferConfig& config);

    miso_ring_buffer(const miso_ring_buffer& rhs) = delete;

    ~miso_ring_buffer();

    LogBufferWriteHandle alloc_write_chunk(std::uint32_t size);

    void commit_write_chunk(const LogBufferWriteHandle& handle);

    LogBufferReadHandle read_chunk();

    LogBufferReadHandle read_an_empty_chunk();

    void discard_read_chunk(LogBufferReadHandle& handle);

    void return_read_chunk(const LogBufferReadHandle& handle);

    void data_traverse(void (*in_callback)(std::uint8_t* data, std::uint32_t size, void* user_data),
                       void* in_user_data);  // in_user_data和传入的user_data有什么区别

    void set_thread_check_enable(bool in_enable);

    bool is_thread_check_enable() const;

    qlog_forceinline const LogBufferConfig& get_config() const {
        return config_;
    }

    qlog_forceinline MemoryMapBufferState get_memory_map_buffer_state() const {
        return mmap_buffer_state_;
    }

    qlog_forceinline std::uint32_t get_block_size() const {
        return sizeof(block);
    }

    qlog_forceinline const std::uint8_t* get_buffer_addr() const {
        return (std::uint8_t*)aligned_blocks_;
    }

    qlog_forceinline std::uint32_t get_total_blocks_count() const {
        return aligned_blocks_count_;
    }

    template <typename T>
    qlog_forceinline T& get_mmap_misc_data() {
        static_assert(sizeof(T) <= sizeof(head_->mmap_misc_data_), "size of T too large");

        return *((T*)(void*)head_->mmap_misc_data_);  // T* void* 是cast转换吗？
    }

   private:
    qlog_forceinline block& cursor_to_block(std::uint32_t cursor) {
        return aligned_blocks_[cursor & (aligned_blocks_count_ - 1)];
    }

    create_memory_map_result create_memory_map();

    bool try_recover_from_exist_memory_map();

    void init_with_memory_map();

    void init_with_memory();
};

}  // namespace qlog::buffer