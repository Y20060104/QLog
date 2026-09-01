#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <qlog/detail/spsc_ring_buffer.hpp>

namespace qlog::detail {

struct SpscRingBufferTestAccess final {
    struct ConfigSnapshot {
        std::size_t capacity_bytes;
        std::size_t capacity_mask;
        std::size_t max_payload_bytes;
    };

    [[nodiscard]] static ConfigSnapshot validate(SpscRingBufferConfig config) {
        const auto validated = SpscRingBuffer::validate_config(config);
        return {validated.capacity_bytes, validated.capacity_mask, validated.max_payload_bytes};
    }

    [[nodiscard]] static ConfigSnapshot config(const SpscRingBuffer& ring) noexcept {
        const auto& validated = ring.cold_state_.config_;
        return {validated.capacity_bytes, validated.capacity_mask, validated.max_payload_bytes};
    }

    [[nodiscard]] static const std::byte* storage(const SpscRingBuffer& ring) noexcept {
        return ring.cold_state_.storage_.get();
    }

    [[nodiscard]] static std::byte* storage(SpscRingBuffer& ring) noexcept {
        return ring.cold_state_.storage_.get();
    }

    [[nodiscard]] static constexpr std::size_t storage_alignment() noexcept {
        return SpscRingBuffer::kStorageAlignment;
    }

    [[nodiscard]] static constexpr std::size_t cache_line_size() noexcept {
        return kCacheLineSize;
    }

    [[nodiscard]] static constexpr std::size_t cursor_set_size() noexcept {
        return sizeof(SpscRingBuffer::CursorSet);
    }

    [[nodiscard]] static constexpr std::size_t cursor_set_alignment() noexcept {
        return alignof(SpscRingBuffer::CursorSet);
    }

    [[nodiscard]] static constexpr std::size_t writer_state_size() noexcept {
        return sizeof(SpscRingBuffer::WriterState);
    }

    [[nodiscard]] static constexpr std::size_t writer_state_alignment() noexcept {
        return alignof(SpscRingBuffer::WriterState);
    }

    [[nodiscard]] static constexpr std::size_t reader_state_size() noexcept {
        return sizeof(SpscRingBuffer::ReaderState);
    }

    [[nodiscard]] static constexpr std::size_t reader_state_alignment() noexcept {
        return alignof(SpscRingBuffer::ReaderState);
    }

    [[nodiscard]] static std::uintptr_t writer_state_address(const SpscRingBuffer& ring) noexcept {
        return reinterpret_cast<std::uintptr_t>(&ring.writer_state_);
    }

    [[nodiscard]] static std::uintptr_t write_cursor_address(const SpscRingBuffer& ring) noexcept {
        return reinterpret_cast<std::uintptr_t>(&ring.cursors_.write_cursor_);
    }

    [[nodiscard]] static std::uintptr_t read_cursor_address(const SpscRingBuffer& ring) noexcept {
        return reinterpret_cast<std::uintptr_t>(&ring.cursors_.read_cursor_);
    }

    [[nodiscard]] static std::uintptr_t reader_state_address(const SpscRingBuffer& ring) noexcept {
        return reinterpret_cast<std::uintptr_t>(&ring.reader_state_);
    }

    [[nodiscard]] static std::uint64_t write_cursor(const SpscRingBuffer& ring) noexcept {
        return ring.cursors_.write_cursor_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] static std::uint64_t read_cursor(const SpscRingBuffer& ring) noexcept {
        return ring.cursors_.read_cursor_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] static std::uint64_t current_write_cursor(const SpscRingBuffer& ring) noexcept {
        return ring.writer_state_.current_write_cursor_;
    }

    [[nodiscard]] static std::uint64_t cached_read_cursor(const SpscRingBuffer& ring) noexcept {
        return ring.writer_state_.cached_read_cursor_;
    }

    [[nodiscard]] static bool reservation_pending(const SpscRingBuffer& ring) noexcept {
        return ring.writer_state_.reservation_pending_;
    }

    [[nodiscard]] static std::uint64_t current_read_cursor(const SpscRingBuffer& ring) noexcept {
        return ring.reader_state_.current_read_cursor_;
    }

    [[nodiscard]] static std::uint64_t cached_write_cursor(const SpscRingBuffer& ring) noexcept {
        return ring.reader_state_.cached_write_cursor_;
    }

    [[nodiscard]] static std::uint32_t records_since_publish(const SpscRingBuffer& ring) noexcept {
        return ring.reader_state_.records_since_publish_;
    }

    [[nodiscard]] static std::uint32_t bytes_since_publish(const SpscRingBuffer& ring) noexcept {
        return ring.reader_state_.bytes_since_publish_;
    }

    [[nodiscard]] static bool read_pending(const SpscRingBuffer& ring) noexcept {
        return ring.reader_state_.read_pending_;
    }
};

}  // namespace qlog::detail
