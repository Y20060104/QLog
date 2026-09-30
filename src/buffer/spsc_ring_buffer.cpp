#include "qlog/buffer/spsc_ring_buffer.hpp"

#include <cassert>
#include <cstring>
#include <limits>
#include <new>

#include "qlog/utility/utility.hpp"

namespace qlog::buffer {

namespace {
std::uint32_t load_u32(const void* address) noexcept {
    std::uint32_t value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

void store_u32(void* address, std::uint32_t value) noexcept {
    std::memcpy(address, &value, sizeof(value));
}
}  // namespace

std::uint32_t SpscRingBuffer::calculate_min_size_of_memory(std::uint32_t expected_buffer_size) {
    assert(expected_buffer_size <= (std::uint32_t{1} << 30));

    std::uint32_t rounded = qlog::utility::round_pow_of_two(expected_buffer_size);

    const std::uint64_t total_size = std::uint64_t{rounded} + sizeof(Head) + CACHE_LINE_SIZE;
    assert(total_size <= std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(total_size);
}

void SpscRingBuffer::init_cursors() {
    head_->rt_reading_cursor_cache_ = 0;
    head_->rt_writing_cursor_cache_ = 0;
    head_->wt_reading_cursor_cache_ = 0;
    head_->wt_writing_cursor_cache_ = 0;

    head_->reading_cursor_.store(0, std::memory_order_release);
    head_->writing_cursor_.store(0, std::memory_order_release);
}

void SpscRingBuffer::init_with_memory(void* buffer, std::size_t buffer_size) {
    assert(buffer != nullptr);

    assert(reinterpret_cast<std::uintptr_t>(buffer) % CACHE_LINE_SIZE == 0);

    assert(buffer_size >= sizeof(Head) + 2 * sizeof(Block));
    const std::size_t max_blocks = (buffer_size - sizeof(Head)) / sizeof(Block);
    // CR: 这样循环计算 开销如何？BQLog怎么做的？
    std::size_t count = 1;
    while (count <= max_blocks / 2) {
        count *= 2;
    }

    assert(count <=
           static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) / sizeof(Block));

    aligned_blocks_count_ = static_cast<std::uint32_t>(count);

    static_assert(std::is_same_v<std::uint8_t, unsigned char>);

    auto* storage = ::new (buffer) unsigned char[buffer_size];
    data_bytes_ = storage + sizeof(Head);

    aligned_blocks_ = ::new (static_cast<void*>(data_bytes_)) Block[aligned_blocks_count_];

    head_ = ::new (buffer) Head;
    head_->aligned_blocks_count_cache_ = aligned_blocks_count_;

    init_cursors();
    mmap_buffer_state_ = MemoryMapBufferState::init_with_memory;
}

SpscRingBuffer::SpscRingBuffer(void* buffer, std::size_t buffer_size, bool is_memory_recovery)
    : is_memory_recovery_(is_memory_recovery) {
    if (is_memory_recovery == false) {
        init_with_memory(buffer, buffer_size);
    } else {
        if (try_recover_from_exist_memory_map(buffer, buffer_size) == false) {
            init_with_memory_map(buffer, buffer_size);
        }
    }
}

SpscRingBuffer::~SpscRingBuffer() {}

void SpscRingBuffer::renew() {
    init_cursors();
#ifdef QLOG_DEBUG
    if (check_thread_) {
        write_thread_id_ = std::thread::id{};
        read_thread_id_ = std::thread::id{};
    }
#endif
}

std::uint32_t SpscRingBuffer::get_max_alloc_size() const {
    return aligned_blocks_count_ * static_cast<std::uint32_t>(BLOCK_SIZE) -
           static_cast<std::uint32_t>(offsetof(ChunkHead, data));
}

std::uint8_t* SpscRingBuffer::get_block_data_addr(Block& block) {
    const auto index = static_cast<std::uint32_t>(&block - aligned_blocks_);
    std::size_t data_size = load_u32(block.bytes + 4);

    const auto tail_bytes = std::uint64_t{aligned_blocks_count_ - index} * BLOCK_SIZE;

    if (std::uint64_t{data_size} + 8U > tail_bytes) {
        return data_bytes_;
    }

    return data_bytes_ + static_cast<std::size_t>(index) * BLOCK_SIZE + 8U;
}

LogBufferWriteHandle SpscRingBuffer::alloc_write_chunk(std::uint32_t size) {
#ifdef QLOG_DEBUG
    debug_check_write_thread();
#endif
    LogBufferWriteHandle handle{};
    const auto block_count = aligned_blocks_count_;
    const auto index = head_->wt_writing_cursor_cache_ & (block_count - 1U);
    const auto tail = block_count - index;

    const auto normal = (std::uint64_t{size} + 8U + 7U) >> BLOCK_SIZE_LOG2;

    if (normal > block_count || normal == 0 ||
        (normal > tail && normal + tail - 1U > block_count)) {
        handle.result = BufferResult::err_alloc_size_invalid;
        return handle;
    }
    // 如果尾部不足放下整个payload 那么从头部开始放 尾部空间计入本次占用块数
    const auto count64 = normal > tail
                             ? std::uint64_t{tail} + ((std::uint64_t{size} + 7U) >> BLOCK_SIZE_LOG2)
                             : normal;

    const auto chunk_blocks = static_cast<std::uint32_t>(count64);

    auto free_blocks = static_cast<std::uint32_t>(head_->wt_reading_cursor_cache_ + block_count -
                                                  head_->wt_writing_cursor_cache_);

    assert(free_blocks <= block_count);

    if (free_blocks < chunk_blocks) {
        head_->wt_reading_cursor_cache_ = head_->reading_cursor_.load(std::memory_order_acquire);

        free_blocks = static_cast<std::uint32_t>(head_->wt_reading_cursor_cache_ + block_count -
                                                 head_->wt_writing_cursor_cache_);
        assert(free_blocks <= block_count);
    }

    if (free_blocks < chunk_blocks) {
        handle.result = BufferResult::err_not_enough_space;
        return handle;
    }
    // CR:BQLog 也只刷新一次，不循环等待？

    auto& block = cursor_to_block(head_->wt_writing_cursor_cache_);
    store_u32(block.bytes, chunk_blocks);
    store_u32(block.bytes + 4, size);

    handle.data_addr = normal > tail
                           ? data_bytes_
                           : data_bytes_ + static_cast<std::size_t>(index) * BLOCK_SIZE + 8;

    handle.low_space_flag = std::uint64_t{block_count - free_blocks} * 2U >= block_count;

    handle.result = BufferResult::success;
    return handle;
}

void SpscRingBuffer::commit_write_chunk(const LogBufferWriteHandle& handle) {
    if (handle.result != BufferResult::success) {
        return;
    }

#ifdef QLOG_DEBUG
    debug_check_write_thread();
#endif

    auto& block = cursor_to_block(head_->wt_writing_cursor_cache_);
#ifdef QLOG_DEBUG
    assert(handle.data_addr == get_block_data_addr(block));
#endif

    const auto chunk_blocks = load_u32(block.bytes);

    assert(chunk_blocks > 0U && chunk_blocks <= aligned_blocks_count_);

    head_->wt_writing_cursor_cache_ += chunk_blocks;
    head_->writing_cursor_.store(head_->wt_writing_cursor_cache_, std::memory_order_release);
    // 再次 CR:你说“如果选择逐分支对应参考，则 payload 等于 data_bytes_ 时必须用 W
    // 定位尾部头，不能减；这里可以直接用当前 W 定位头，不必从 payload
    // 指针反推。这是相对参考地址分支的局部写法差异：在单个 outstanding
    // 写句柄的有效调用协议内，二者定位同一块；不会修改有效调用的行为。”是什么意思？
}

LogBufferReadHandle SpscRingBuffer::read_chunk() {
#ifdef QLOG_DEBUG
    assert(!is_read_chunk_waiting_for_return_);
    debug_check_read_thread();
    is_read_chunk_waiting_for_return_ = true;
#endif

    LogBufferReadHandle handle{};
    if (std::uint32_t(head_->rt_writing_cursor_cache_ - head_->rt_reading_cursor_cache_) == 0U) {
        head_->rt_writing_cursor_cache_ = head_->writing_cursor_.load(std::memory_order_acquire);
        if (std::uint32_t(head_->rt_writing_cursor_cache_ - head_->rt_reading_cursor_cache_) ==
            0U) {
            handle.result = BufferResult::err_empty_log_buffer;
            return handle;
        }
    }
    auto& block = cursor_to_block(head_->rt_reading_cursor_cache_);
    std::uint32_t data_size = load_u32(block.bytes + 4);

#ifdef QLOG_DEBUG
    const auto block_count = load_u32(block.bytes);
    assert(block_count > 0);
    assert(block_count <=
           std::uint32_t(head_->rt_writing_cursor_cache_ - head_->rt_reading_cursor_cache_));
    assert(std::uint32_t(head_->rt_writing_cursor_cache_ - head_->rt_reading_cursor_cache_) <=
           aligned_blocks_count_);

#endif

    handle.data_size = data_size;
    handle.data_addr = get_block_data_addr(block);
    handle.result = BufferResult::success;
    return handle;
}

void SpscRingBuffer::return_read_chunk(const LogBufferReadHandle& handle) {
#ifdef QLOG_DEBUG
    assert(is_read_chunk_waiting_for_return_);
    debug_check_read_thread();
    is_read_chunk_waiting_for_return_ = false;
#endif

    if (handle.result != BufferResult::success) {
        return;
    }
    auto& block = cursor_to_block(head_->rt_reading_cursor_cache_);

#ifdef QLOG_DEBUG
    assert(handle.data_addr == get_block_data_addr(block));
#endif

    std::uint32_t block_num = load_u32(block.bytes);

    head_->rt_reading_cursor_cache_ += block_num;
    head_->reading_cursor_.store(head_->rt_reading_cursor_cache_, std::memory_order_release);
}

void SpscRingBuffer::discard_read_chunk(LogBufferReadHandle& handle) {
#ifdef QLOG_DEBUG
    assert(is_read_chunk_waiting_for_return_);
    debug_check_read_thread();
    is_read_chunk_waiting_for_return_ = false;
#endif

    handle.result = BufferResult::err_empty_log_buffer;
}

LogBufferReadHandle SpscRingBuffer::read_an_empty_chunk() {
    LogBufferReadHandle handle{};
#ifdef QLOG_DEBUG
    assert(!is_read_chunk_waiting_for_return_);
    debug_check_read_thread();
    is_read_chunk_waiting_for_return_ = true;
#endif
    handle.result = BufferResult::err_empty_log_buffer;
    return handle;
}

SpscRingBuffer::BatchReadHandle SpscRingBuffer::batch_read() {
#ifdef QLOG_DEBUG
    assert(!is_read_chunk_waiting_for_return_);
    debug_check_read_thread();
    is_read_chunk_waiting_for_return_ = true;
#endif

    if (head_->rt_writing_cursor_cache_ == head_->rt_reading_cursor_cache_) {
        head_->rt_writing_cursor_cache_ = head_->writing_cursor_.load(std::memory_order_acquire);
    }
    BatchReadHandle handle;
    handle.parent_ = this;
    handle.start_cursor_ = head_->rt_reading_cursor_cache_;
    handle.current_cursor_ = head_->rt_reading_cursor_cache_;
    handle.end_cursor_ = head_->rt_writing_cursor_cache_;

    if (handle.start_cursor_ == handle.end_cursor_) {
        handle.result = BufferResult::err_empty_log_buffer;
        return handle;
    }

    handle.result = BufferResult::success;
    return handle;
}

LogBufferReadHandle SpscRingBuffer::BatchReadHandle::next() {
#ifdef QLOG_DEBUG
    assert(result == BufferResult::success);
    assert(has_next());
#endif

    std::uint32_t current_cursor = current_cursor_;
    Block& block = parent_->cursor_to_block(current_cursor);
    std::uint32_t block_num = load_u32(block.bytes);
    std::uint32_t data_size = load_u32(block.bytes + 4);

#ifdef QLOG_DEBUG
    assert(block_num > 0 && block_num <= std::uint32_t(end_cursor_ - current_cursor_));
#endif

    LogBufferReadHandle handle{};
    handle.result = result;
    handle.data_addr = parent_->get_block_data_addr(block);
    handle.data_size = data_size;

#ifdef QLOG_DEBUG
    last_cursor_ = current_cursor;
#endif

    current_cursor_ += block_num;
    parent_->head_->rt_reading_cursor_cache_ = current_cursor_;
    return handle;
}

void SpscRingBuffer::return_batch_read_chunks(const BatchReadHandle& handle) {
#ifdef QLOG_DEBUG

    assert(is_read_chunk_waiting_for_return_);
    debug_check_read_thread();
    is_read_chunk_waiting_for_return_ = false;
#endif

    if (handle.result != BufferResult::success) {
        return;
    }

#ifdef QLOG_DEBUG
    assert(handle.parent_ == this);
    assert(handle.current_cursor_ == handle.end_cursor_);
    assert(handle.start_cursor_ == head_->reading_cursor_.load(std::memory_order_relaxed));
    assert(handle.end_cursor_ == head_->rt_reading_cursor_cache_);
    assert(handle.end_cursor_ == head_->rt_writing_cursor_cache_);
#endif

    head_->reading_cursor_.store(head_->rt_reading_cursor_cache_, std::memory_order_release);
}

#ifdef QLOG_DEBUG
bool SpscRingBuffer::BatchReadHandle::verify_chunk(const LogBufferReadHandle& handle) const {
    if (result != BufferResult::success) {
        return false;
    }
    auto& block = parent_->cursor_to_block(last_cursor_);
    return handle.data_addr == parent_->get_block_data_addr(block);
}
#endif

void SpscRingBuffer::data_traverse(void (*callback)(std::uint8_t*, std::uint32_t, void*),
                                   void* user_data) {
#ifdef QLOG_DEBUG
    debug_check_read_thread();
    assert(callback != nullptr);
#endif

    std::uint32_t cursor = head_->rt_reading_cursor_cache_;
    while (true) {
        if (cursor == head_->rt_writing_cursor_cache_) {
            head_->rt_writing_cursor_cache_ =
                head_->writing_cursor_.load(std::memory_order_acquire);
            if (cursor == head_->rt_writing_cursor_cache_) {
                return;
            }
        }
        Block& block = cursor_to_block(cursor);
        std::uint32_t block_num = load_u32(block.bytes);
        std::uint32_t data_size = load_u32(block.bytes + 4);

#ifdef QLOG_DEBUG
        assert(block_num > 0);
        assert(block_num <= static_cast<std::uint32_t>(head_->rt_writing_cursor_cache_ - cursor));
#endif
        callback(get_block_data_addr(block), data_size, user_data);
        cursor += block_num;
    }
}

bool SpscRingBuffer::try_recover_from_exist_memory_map(void* buffer, std::size_t buffer_size) {
    // Storage validity is a caller precondition; invalid snapshot content returns false.
    assert(buffer != nullptr);
    assert(reinterpret_cast<std::uintptr_t>(buffer) % CACHE_LINE_SIZE == 0);
    assert(buffer_size >= sizeof(Head) + 2 * sizeof(Block));

    const auto max_blocks = (buffer_size - sizeof(Head)) / sizeof(Block);
    std::size_t count = 1;
    while (count <= max_blocks / 2) {
        count *= 2;
    }
    assert(count <=
           static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) / sizeof(Block));
    const auto blockcount = static_cast<std::uint32_t>(count);

    // Default initialization preserves the mapped bytes. Do not construct Head yet:
    // its atomic constructors must not run before the snapshot has been validated.
    auto* storage = ::new (buffer) unsigned char[buffer_size];
    auto* data = storage + sizeof(Head);
    const auto saved_blockcount = load_u32(storage + offsetof(Head, aligned_blocks_count_cache_));
    const auto saved_read = load_u32(storage + offsetof(Head, rt_reading_cursor_cache_));
    const auto saved_write = load_u32(storage + offsetof(Head, wt_writing_cursor_cache_));

    if (saved_blockcount != blockcount) {
        return false;
    }
    auto remaining = static_cast<std::uint32_t>(saved_write - saved_read);
    if (remaining > blockcount) {
        return false;
    }

    // Unlike the reference's cursor < saved_write, this also covers uint32 wrap.
    auto cursor = saved_read;
    while (remaining != 0) {
        const auto index = cursor & (blockcount - 1U);
        const auto* chunk = data + static_cast<std::size_t>(index) * BLOCK_SIZE;
        const auto chunk_blocks = load_u32(chunk);
        const auto data_size = load_u32(chunk + sizeof(std::uint32_t));
        if (chunk_blocks == 0 || chunk_blocks > remaining) {
            return false;
        }

        const auto tail = blockcount - index;
        const auto normal = (std::uint64_t{data_size} + 8U + 7U) >> BLOCK_SIZE_LOG2;
        if (normal > blockcount || (normal > tail && normal + tail - 1U > blockcount)) {
            return false;
        }
        const auto expected_blocks =
            normal > tail
                ? std::uint64_t{tail} + ((std::uint64_t{data_size} + 7U) >> BLOCK_SIZE_LOG2)
                : normal;
        if (expected_blocks != chunk_blocks) {
            return false;
        }
        cursor += chunk_blocks;
        remaining -= chunk_blocks;
    }
    if (cursor != saved_write) {
        return false;
    }

    // Establish runtime objects only after validation; never restore atomic bytes.
    aligned_blocks_count_ = blockcount;
    data_bytes_ = data;
    aligned_blocks_ = ::new (static_cast<void*>(data_bytes_)) Block[blockcount];
    head_ = ::new (buffer) Head;
    head_->aligned_blocks_count_cache_ = blockcount;
    head_->rt_reading_cursor_cache_ = saved_read;
    head_->rt_writing_cursor_cache_ = saved_write;
    head_->wt_reading_cursor_cache_ = saved_read;
    head_->wt_writing_cursor_cache_ = saved_write;
    head_->reading_cursor_.store(saved_read, std::memory_order_release);
    head_->writing_cursor_.store(saved_write, std::memory_order_release);
    mmap_buffer_state_ = MemoryMapBufferState::recover_from_memory_map;
    return true;
}

void SpscRingBuffer::init_with_memory_map(void* buffer, std::size_t buffer_size) {
    init_with_memory(buffer, buffer_size);
    mmap_buffer_state_ = MemoryMapBufferState::init_with_memmap;
}
#ifdef QLOG_DEBUG
void SpscRingBuffer::debug_check_write_thread() {
    if (!is_thread_check_enable()) {
        return;
    }
    auto id = std::this_thread::get_id();
    if (write_thread_id_ == std::thread::id{}) {  // Default id means no thread is bound yet.
        write_thread_id_ = id;
    } else {
        assert(id == write_thread_id_);
    }
}
void SpscRingBuffer::debug_check_read_thread() {
    if (!is_thread_check_enable()) {
        return;
    }
    auto id = std::this_thread::get_id();
    if (read_thread_id_ == std::thread::id{}) {
        read_thread_id_ = id;
    } else {
        assert(id == read_thread_id_);
    }
}

#endif

bool SpscRingBuffer::is_thread_check_enable() const {
#ifdef QLOG_DEBUG
    return check_thread_;
#else
    return false;
#endif
}

void SpscRingBuffer::set_thread_check_enable(bool enabled) {
#ifdef QLOG_DEBUG
    check_thread_ = enabled;
    if (!check_thread_) {
        write_thread_id_ = std::thread::id{};
        read_thread_id_ = std::thread::id{};
    }
#else
    (void)enabled;
#endif
}

}  // namespace qlog::buffer