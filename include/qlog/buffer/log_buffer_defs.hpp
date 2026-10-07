#pragma once
#include <cstdint>

namespace qlog::buffer {
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

enum class LogMemoryPolicy{
    discard_when_full,
    block_when_full,
    auto_expand_when_full,
};

}  // namespace qlog::buffer