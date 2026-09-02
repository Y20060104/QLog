#pragma once

#include <algorithm>
#include <cstddef>

#include "benchmark_types.hpp"
#include "qlog/detail/spsc_ring_buffer.hpp"

namespace qlog::bench {

class QlogSpscAdapter final {
   public:
    using WriteHandle = qlog::detail::WriteHandle;
    using ReadHandle = qlog::detail::ReadHandle;

    QlogSpscAdapter(std::size_t capacity_bytes, std::size_t payload_bytes)
        : ring_({capacity_bytes, std::max<std::size_t>(payload_bytes, 1U)}) {}

    [[nodiscard]] WriteHandle reserve(std::size_t payload_bytes) noexcept {
        return ring_.try_reserve(payload_bytes);
    }

    [[nodiscard]] static AttemptStatus write_status(const WriteHandle& handle) noexcept {
        switch (handle.status()) {
            case qlog::detail::ReserveStatus::ok:
                return AttemptStatus::success;
            case qlog::detail::ReserveStatus::full:
                return AttemptStatus::full;
            case qlog::detail::ReserveStatus::payload_too_large:
            case qlog::detail::ReserveStatus::reservation_pending:
                return AttemptStatus::unexpected;
        }
        return AttemptStatus::unexpected;
    }

    [[nodiscard]] static std::byte* write_data(WriteHandle& handle) noexcept {
        return handle.data();
    }

    void commit(const WriteHandle& handle) noexcept {
        ring_.commit(handle);
    }

    [[nodiscard]] ReadHandle read() noexcept {
        return ring_.try_read();
    }

    [[nodiscard]] static AttemptStatus read_status(const ReadHandle& handle) noexcept {
        switch (handle.status()) {
            case qlog::detail::ReadStatus::ok:
                return AttemptStatus::success;
            case qlog::detail::ReadStatus::empty:
                return AttemptStatus::empty;
            case qlog::detail::ReadStatus::corrupted:
            case qlog::detail::ReadStatus::read_pending:
                return AttemptStatus::unexpected;
        }
        return AttemptStatus::unexpected;
    }

    [[nodiscard]] static const std::byte* read_data(const ReadHandle& handle) noexcept {
        return handle.data();
    }

    [[nodiscard]] static std::size_t read_size(const ReadHandle& handle) noexcept {
        return handle.size();
    }

    void consume(const ReadHandle& handle) noexcept {
        ring_.release(handle);
    }

   private:
    qlog::detail::SpscRingBuffer ring_;
};

}  // namespace qlog::bench
