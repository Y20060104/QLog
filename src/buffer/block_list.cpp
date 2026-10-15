#include "qlog/buffer/block_list.hpp"

#include <cstring>

namespace qlog::buffer {

block_node_head::block_node_head(block_list_type type, void* buffer, std::size_t buffer_size,
                                 bool is_memory_recovery) {
    if (type != block_list_type::list_keep_prev) {
        type_ = type;
    }

    (void)padding_;

    if (!is_memory_recovery) {
        next_.index() = static_cast<std::uint16_t>(-1);
        next_.aba_mark() = 0;
        std::memset(misc_data_, 0, sizeof(misc_data_));
    }

    new (static_cast<void*>(&get_buffer()), qlog::utility::enum_new_dummy::dummy) siso_ring_buffer(
        buffer, buffer_size, is_memory_recovery);  // 这是在buffer原地址 进行构造函数的调用嘛

    [[maybe_unused]] std::size_t min_size =
        qlog::utility::roundup_pow_of_two(buffer_size) == buffer_size
            ? buffer_size
            : (qlog::utility::roundup_pow_of_two(buffer_size) >> 1);  // 这是向下取2的幂次方嘛？

    assert(reinterpret_cast<std::uintptr_t>(static_cast<void*>(buffer_)) %
                   qlog::utility::QLOG_CACHE_LINE_SIZE ==
               0 &&
           "siso_ring_buffer is not properly aligned!");

    assert((reinterpret_cast<std::uintptr_t>(static_cast<void*>(buffer_)) -
            reinterpret_cast<std::uintptr_t>(static_cast<void*>(&next_))) ==
               qlog::utility::QLOG_CACHE_LINE_SIZE &&
           "siso_ring_buffer is not properly aligned!");

    assert(static_cast<std::size_t>(get_buffer().get_block_size() *
                                    get_buffer().get_total_blocks_count()) == min_size &&
           "siso_ring_buffer usable size is unexpected, please check "
           "calculation as memory alignment");
}

block_node_head::~block_node_head() {
    get_buffer().~siso_ring_buffer();
}

void block_node_head::alignment_assert() {
    assert(QLOG_POD_RUNTIME_OFFSET_OF(block_node_head, next_) == 0 &&
           "invalid alignment of block_node_head");

    assert(QLOG_POD_RUNTIME_OFFSET_OF(block_node_head, misc_data_) % 8 == 0 &&
           "invalid alignment of block_node_head");

    assert(QLOG_POD_RUNTIME_OFFSET_OF(block_node_head, buffer_) ==
               qlog::utility::QLOG_CACHE_LINE_SIZE &&
           "invalid alignment of block_node_head");
}

void block_node_head::set_misc_data(const void* data_src, std::size_t data_size) {
    assert(data_size <= sizeof(misc_data_) && "data_size is too large");
    std::memcpy(misc_data_, data_src, data_size);
}

void block_list::reset(std::uint16_t max_blocks_count, const std::uint8_t* buffers_base_addr,
                       std::size_t blocks_total_buffer_size) {
    head_.index() = static_cast<std::uint16_t>(-1);
    head_.aba_mark() = 0;

    std::ptrdiff_t offset = buffers_base_addr - reinterpret_cast<std::uint8_t*>(this);

    assert(offset <= UINT16_MAX && "block_list buffer offset too large");
    offset_ = static_cast<std::uint16_t>(offset);
    max_blocks_count_ = max_blocks_count;

    buffer_size_per_block_ = blocks_total_buffer_size / max_blocks_count;
    buffer_size_per_block_ -= buffer_size_per_block_ % qlog::utility::QLOG_CACHE_LINE_SIZE;

    data_range_start_ =
        reinterpret_cast<const std::uint8_t*>(this) + static_cast<std::ptrdiff_t>(offset_);

    data_range_end_ =
        data_range_start_ + static_cast<std::ptrdiff_t>(buffer_size_per_block_ * max_blocks_count_);

    assert(buffer_size_per_block_ >
               static_cast<std::size_t>(block_node_head::get_buffer_data_offset()) &&
           "too small of block buffer size");
}

bool block_list::try_recover_from_memory_map(std::uint16_t max_blocks_count,
                                             const std::uint8_t* buffers_base_addr,
                                             std::size_t blocks_total_buffer_size) {
    std::uint16_t& prev_max_blocks_count = *std::launder(&max_blocks_count_);
    std::uint16_t& prev_offset = *std::launder(&offset_);
    std::size_t& prev_buffer_size_per_block = *std::launder(&buffer_size_per_block_);

    if (prev_max_blocks_count != max_blocks_count) {
        return false;
    }

    const std::ptrdiff_t offset = buffers_base_addr - reinterpret_cast<std::uint8_t*>(this);

    assert(offset <= UINT16_MAX && "block_list buffer offset too large");

    if (prev_offset != offset) {
        return false;
    }

    auto buffer_size_per_block = blocks_total_buffer_size / max_blocks_count;

    buffer_size_per_block -= buffer_size_per_block % qlog::utility::QLOG_CACHE_LINE_SIZE;

    if (prev_buffer_size_per_block != buffer_size_per_block) {
        return false;
    }

    assert(prev_buffer_size_per_block >
               static_cast<std::size_t>(block_node_head::get_buffer_data_offset()) &&
           "too small of block buffer size");

    const block_node_head::pointer_type* current_ptr = std::launder(&head_);
    std::uint16_t current_blocks_count = 0;

    while (!current_ptr->is_empty() && current_blocks_count <= prev_max_blocks_count) {
        ++current_blocks_count;

        if (current_ptr->index() >= prev_max_blocks_count) {
            return false;
        }

        const block_node_head& next_block_head = get_block_head_by_index(current_ptr->index());

        if (next_block_head.get_type() != type_) {
            return false;
        }

        current_ptr = &next_block_head.next_;
    }

    if (current_blocks_count > prev_max_blocks_count) {
        return false;
    }

    data_range_start_ =
        reinterpret_cast<const std::uint8_t*>(this) + static_cast<std::ptrdiff_t>(prev_offset);

    data_range_end_ = data_range_start_ + static_cast<std::ptrdiff_t>(prev_buffer_size_per_block *
                                                                      prev_max_blocks_count);

    return true;
}

void block_list::recovery_blocks() {
    block_node_head* current_node = first();

    while (current_node) {
        new (static_cast<void*>(current_node), qlog::utility::enum_new_dummy::dummy)
            block_node_head(block_list_type::list_keep_prev,
                            reinterpret_cast<std::uint8_t*>(current_node) +
                                block_node_head::get_buffer_data_offset(),
                            buffer_size_per_block_ -
                                static_cast<std::size_t>(block_node_head::get_buffer_data_offset()),
                            true);

        current_node = next(current_node);
    }
}

block_list::block_list(block_list_type type, std::uint16_t max_blocks_count,
                       std::uint8_t* buffers_base_addr, std::size_t blocks_total_buffer_size,
                       bool is_memory_recovery)
    : type_(type) {
    assert(reinterpret_cast<std::uintptr_t>(buffers_base_addr) %
                   qlog::utility::QLOG_CACHE_LINE_SIZE ==
               0 &&
           "buffers_base_addr is not properly aligned!");

    if (!is_memory_recovery) {
        reset(max_blocks_count, buffers_base_addr, blocks_total_buffer_size);
        return;
    }

    if (!try_recover_from_memory_map(max_blocks_count, buffers_base_addr,
                                     blocks_total_buffer_size)) {
        reset(max_blocks_count, buffers_base_addr, blocks_total_buffer_size);
    }
}

block_list::~block_list() {}

}  // namespace qlog::buffer