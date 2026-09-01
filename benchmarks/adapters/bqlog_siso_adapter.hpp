#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

#include "benchmark_types.hpp"
#include "bq_log/types/buffer/siso_ring_buffer.h"

namespace qlog::bench {

class BqlogSisoAdapter final {
   public:
    using WriteHandle = bq::log_buffer_write_handle;
    using ReadHandle = bq::log_buffer_read_handle;

    BqlogSisoAdapter(std::size_t capacity_bytes, std::size_t)
        : storage_(allocate_storage(capacity_bytes)),
          ring_(std::make_unique<bq::siso_ring_buffer>(storage_.get(),
                                                       storage_bytes(capacity_bytes), false)) {
        const std::size_t actual_capacity =
            static_cast<std::size_t>(ring_->get_block_size()) * ring_->get_total_blocks_count();
        if (actual_capacity != capacity_bytes) {
            throw std::runtime_error(
                "BQLog SISO usable capacity does not match requested capacity");
        }
    }

    [[nodiscard]] WriteHandle reserve(std::size_t payload_bytes) {
        if (payload_bytes > std::numeric_limits<std::uint32_t>::max()) {
            throw std::invalid_argument("BQLog SISO payload exceeds uint32_t");
        }
        return ring_->alloc_write_chunk(static_cast<std::uint32_t>(payload_bytes));
    }

    [[nodiscard]] static AttemptStatus write_status(const WriteHandle& handle) noexcept {
        switch (handle.result) {
            case bq::enum_buffer_result_code::success:
                return AttemptStatus::success;
            case bq::enum_buffer_result_code::err_not_enough_space:
                return AttemptStatus::full;
            default:
                return AttemptStatus::unexpected;
        }
    }

    [[nodiscard]] static std::byte* write_data(WriteHandle& handle) noexcept {
        return reinterpret_cast<std::byte*>(handle.data_addr);
    }

    void commit(WriteHandle& handle) noexcept {
        ring_->commit_write_chunk(handle);
    }

    [[nodiscard]] ReadHandle read() {
        return ring_->read_chunk();
    }

    [[nodiscard]] static AttemptStatus read_status(const ReadHandle& handle) noexcept {
        switch (handle.result) {
            case bq::enum_buffer_result_code::success:
                return AttemptStatus::success;
            case bq::enum_buffer_result_code::err_empty_log_buffer:
                return AttemptStatus::empty;
            default:
                return AttemptStatus::unexpected;
        }
    }

    [[nodiscard]] static const std::byte* read_data(const ReadHandle& handle) noexcept {
        return reinterpret_cast<const std::byte*>(handle.data_addr);
    }

    [[nodiscard]] static std::size_t read_size(const ReadHandle& handle) noexcept {
        return handle.data_size;
    }

    void consume(ReadHandle& handle) noexcept {
        ring_->return_read_chunk(handle);
    }

   private:
    struct AlignedDeleter {
        void operator()(std::byte* pointer) const noexcept {
            ::operator delete(pointer, std::align_val_t{bq::BQ_CACHE_LINE_SIZE});
        }
    };

    using StorageOwner = std::unique_ptr<std::byte, AlignedDeleter>;

    [[nodiscard]] static std::size_t storage_bytes(std::size_t capacity_bytes) {
        if (capacity_bytes > std::numeric_limits<std::uint32_t>::max()) {
            throw std::invalid_argument("BQLog SISO capacity exceeds uint32_t");
        }
        if (capacity_bytes > (std::size_t{1} << 31U)) {
            throw std::invalid_argument("BQLog SISO capacity exceeds the common 2^31 limit");
        }
        return bq::siso_ring_buffer::calculate_min_size_of_memory(
            static_cast<std::uint32_t>(capacity_bytes));
    }

    [[nodiscard]] static StorageOwner allocate_storage(std::size_t capacity_bytes) {
        const std::size_t bytes = storage_bytes(capacity_bytes);
        return StorageOwner(static_cast<std::byte*>(
            ::operator new(bytes, std::align_val_t{bq::BQ_CACHE_LINE_SIZE})));
    }

    StorageOwner storage_;
    std::unique_ptr<bq::siso_ring_buffer> ring_;
};

}  // namespace qlog::bench
