#pragma once
#include <cstddef>
#include <cstdint>

#include "qlog/utility/type_tools.hpp"

namespace qlog::buffer {
#define BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT(X, TYPE) \
    QLOG_PACK_ACCESS_BY_TYPE(X, qlog::platform::atomic<TYPE>)

#define QLOG_POD_RUNTIME_OFFSET_OF(Type, Field)                                               \
    static_cast<std::size_t>((reinterpret_cast<std::uintptr_t>(&(((Type*)0x10000)->Field))) - \
                             static_cast<std::uintptr_t>(0x10000))

enum class BufferResult {
    success = 0,
    err_empty_log_buffer,
    err_not_enough_space,
    err_wait_and_retry,
    err_data_not_contiguous,
    err_alloc_size_invalid,
    err_buffer_not_inited,
    err_io_failure_drop,
    result_code_count,
};

struct LogBufferWriteHandle {
    std::uint8_t* data_addr;
    BufferResult result = BufferResult::err_empty_log_buffer;
    bool low_space_flag = false;
};

struct LogBufferReadHandle {
    std::uint8_t* data_addr;
    BufferResult result = BufferResult::err_empty_log_buffer;
    std::uint32_t data_size;
};

enum class MemoryMapBufferState {
    init_with_memory,
    recover_from_memory_map,
    init_with_memmap,
};

enum class LogMemoryPolicy {
    discard_when_full,
    block_when_full,
    auto_expand_when_full,
};

enum class create_memory_map_result {
    failed,
    use_existed,
    new_created,
};

enum class block_list_type : std::uint8_t {
    list_none,
    list_free,
    list_stage,
    list_used,
    list_keep_prev,
};

template <typename BUFFER_TYPE>
class scoped_log_buffer_handle {
    enum class destruction_type : std::uint8_t {
        return_chunk,
        commit_chunk,
    };

   private:
    BUFFER_TYPE* buffer_;
    LogBufferReadHandle read_handle_;
    LogBufferWriteHandle write_handle_;
    destruction_type destruction_type_;

   public:
    scoped_log_buffer_handle() = delete;

    scoped_log_buffer_handle(BUFFER_TYPE& buffer, const LogBufferReadHandle& handle);

    scoped_log_buffer_handle(BUFFER_TYPE& buffer, const LogBufferWriteHandle& handle);

    ~scoped_log_buffer_handle();
};

template <typename BUFFER_TYPE>
scoped_log_buffer_handle<BUFFER_TYPE>::scoped_log_buffer_handle(BUFFER_TYPE& buffer,
                                                                const LogBufferWriteHandle& handle)
    : buffer_(&buffer),
      read_handle_(LogBufferReadHandle()),
      write_handle_(handle),
      destruction_type_(destruction_type::commit_chunk) {
    (void)read_handle_;
}

template <typename BUFFER_TYPE>
scoped_log_buffer_handle<BUFFER_TYPE>::~scoped_log_buffer_handle() {
    switch (destruction_type_) {
        case destruction_type::return_chunk:
            buffer_->return_read_chunk(read_handle_);
            break;
        case destruction_type::commit_chunk:
            buffer_->commit_write_chunk(write_handle_);
            break;
        default:
            break;
    }
}

template <typename BUFFER_TYPE>
scoped_log_buffer_handle<BUFFER_TYPE>::scoped_log_buffer_handle(BUFFER_TYPE& buffer,
                                                                const LogBufferReadHandle& handle)
    : buffer_(&buffer),
      read_handle_(handle),
      write_handle_(LogBufferWriteHandle()),
      destruction_type_(destruction_type::return_chunk) {
    (void)write_handle_;
}
// 这两个构造函数为什么分别调用readhandle /writehandle的无参构造函数？

}  // namespace qlog::buffer