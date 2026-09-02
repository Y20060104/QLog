#include "qlog/detail/spsc_ring_buffer.hpp"

#include <cassert>
#include <cstring>
#include <new>
#include <stdexcept>

#include "build_config.hpp"
#include "qlog/detail/ring_geometry.hpp"

namespace qlog::detail {
void SpscRingBuffer::AlignedStorageDeleter::operator()(std::byte* pointer) const noexcept {
    ::operator delete(pointer, std::align_val_t{kStorageAlignment});
}

SpscRingBuffer::ColdState::ColdState(ValidatedConfig config)
    : config_(config),
      storage_(static_cast<std::byte*>(
          ::operator new(config.capacity_bytes, std::align_val_t{kStorageAlignment}))) {}

SpscRingBuffer::ValidatedConfig SpscRingBuffer::validate_config(SpscRingBufferConfig config) {
    if (config.capacity_bytes < 16U) {
        throw std::invalid_argument("capacity_bytes must be at least 16");
    }
    if ((config.capacity_bytes & (config.capacity_bytes - 1U)) != 0U) {
        throw std::invalid_argument("capacity_bytes must be a power of two");
    }
    if (config.capacity_bytes > (std::size_t{1} << 31U)) {
        throw std::invalid_argument("capacity_bytes must be at most 2^31");
    }
    if (config.max_payload_bytes == 0U) {
        throw std::invalid_argument("max_payload_bytes must be greater than zero");
    }
    if (config.max_payload_bytes > config.capacity_bytes / 2U) {
        throw std::invalid_argument("max_payload_bytes must be at most capacity_bytes / 2");
    }

    return {
        config.capacity_bytes,
        config.capacity_bytes - 1U,
        config.max_payload_bytes,
    };
}

SpscRingBuffer::SpscRingBuffer(SpscRingBufferConfig config)
    : cold_state_(validate_config(config)), writer_state_{}, cursors_{}, reader_state_{} {}

SpscRingBuffer::~SpscRingBuffer() noexcept = default;

WriteHandle SpscRingBuffer::try_reserve(std::size_t exact_payload_bytes) noexcept {
    if (writer_state_.reservation_pending_) {
        return WriteHandle{ReserveStatus::reservation_pending};
    }
    if (exact_payload_bytes > cold_state_.config_.max_payload_bytes) {
        return WriteHandle{ReserveStatus::payload_too_large};
    }
    FrameLayout layout =
        compute_frame_layout(writer_state_.current_write_cursor_, exact_payload_bytes,
                             cold_state_.config_.capacity_bytes);
    if (cold_state_.config_.capacity_bytes -
            (writer_state_.current_write_cursor_ - writer_state_.cached_read_cursor_) <
        layout.frame_bytes) {
        writer_state_.cached_read_cursor_ = cursors_.read_cursor_.load(std::memory_order_acquire);
        if (cold_state_.config_.capacity_bytes -
                (writer_state_.current_write_cursor_ - writer_state_.cached_read_cursor_) <
            layout.frame_bytes) {
            return WriteHandle{ReserveStatus::full};
        }
    }

    std::byte* const storage_base = cold_state_.storage_.get();
    std::byte* const header_address = storage_base + layout.header_offset;
    std::byte* const payload_address = storage_base + layout.payload_offset;
    const FrameHeader header{
        static_cast<std::uint32_t>(layout.frame_bytes),
        static_cast<std::uint32_t>(exact_payload_bytes),
    };
    std::memcpy(header_address, &header, sizeof(FrameHeader));

    writer_state_.reservation_pending_ = true;
    return WriteHandle{payload_address, static_cast<std::uint32_t>(exact_payload_bytes),
                       static_cast<std::uint32_t>(layout.frame_bytes)};
}

void SpscRingBuffer::commit(const WriteHandle& write_handle) noexcept {
    const std::uint32_t frame_bytes = write_handle.frame_bytes();
    if (frame_bytes == 0U) {
        return;
    }

#if QLOG_ENABLE_RING_VALIDATION
    assert(writer_state_.reservation_pending_ && "commit requires an active reservation");

    const FrameLayout expected =
        compute_frame_layout(writer_state_.current_write_cursor_, write_handle.payload_bytes_,
                             cold_state_.config_.capacity_bytes);
    assert(expected.frame_bytes == frame_bytes && "write handle frame size does not match");
    assert(cold_state_.storage_.get() + expected.payload_offset == write_handle.payload_ &&
           "write handle does not belong to the active reservation");
#endif

    writer_state_.current_write_cursor_ += frame_bytes;
    cursors_.write_cursor_.store(writer_state_.current_write_cursor_, std::memory_order_release);
    writer_state_.reservation_pending_ = false;
}

void SpscRingBuffer::abort(const WriteHandle& write_handle) noexcept {
    if (!write_handle) {
        return;
    }

#if QLOG_ENABLE_RING_VALIDATION
    assert(writer_state_.reservation_pending_ && "abort requires an active reservation");

    const FrameLayout expected =
        compute_frame_layout(writer_state_.current_write_cursor_, write_handle.payload_bytes_,
                             cold_state_.config_.capacity_bytes);
    assert(expected.frame_bytes == write_handle.frame_bytes() &&
           "write handle frame size does not match");
    assert(cold_state_.storage_.get() + expected.payload_offset == write_handle.payload_ &&
           "write handle does not belong to the active reservation");
#endif

    writer_state_.reservation_pending_ = false;
}

void SpscRingBuffer::abandon(const ReadHandle& read_handle) noexcept {
    if (!read_handle) {
        return;
    }

    reader_state_.read_pending_ = false;
}

ReadHandle SpscRingBuffer::try_read() noexcept {
    // 第一轮先保留，这是当前 API 契约。
    if (reader_state_.read_pending_) {
        return ReadHandle{ReadStatus::read_pending};
    }

    const std::uint64_t current_read = reader_state_.current_read_cursor_;

    if (current_read == reader_state_.cached_write_cursor_) {
        reader_state_.cached_write_cursor_ = cursors_.write_cursor_.load(std::memory_order_acquire);
    }

    const std::uint64_t available = reader_state_.cached_write_cursor_ - current_read;

    if (available == 0U) {
        publish_reclaimed();
        return ReadHandle{ReadStatus::empty};
    }

#if QLOG_ENABLE_RING_VALIDATION
    if (available > cold_state_.config_.capacity_bytes || available < sizeof(FrameHeader)) {
        publish_reclaimed();
        return ReadHandle{ReadStatus::corrupted};
    }
#endif

    const std::size_t physical =
        static_cast<std::size_t>(current_read & cold_state_.config_.capacity_mask);

    const std::byte* const header_address = cold_state_.storage_.get() + physical;

    FrameHeader header{};
    std::memcpy(&header, header_address, sizeof(header));

    std::size_t frame_bytes{};
    std::size_t payload_offset{};

#if QLOG_ENABLE_RING_VALIDATION

    if (header.payload_bytes > cold_state_.config_.max_payload_bytes) {
        publish_reclaimed();
        return ReadHandle{ReadStatus::corrupted};
    }

    const FrameLayout layout = compute_frame_layout(current_read, header.payload_bytes,
                                                    cold_state_.config_.capacity_bytes);

    if (layout.frame_bytes != header.frame_bytes || layout.frame_bytes > available) {
        publish_reclaimed();
        return ReadHandle{ReadStatus::corrupted};
    }

    frame_bytes = layout.frame_bytes;
    payload_offset = layout.payload_offset;

#else

    frame_bytes = header.frame_bytes;

    const std::size_t tail_bytes = cold_state_.config_.capacity_bytes - physical;

    payload_offset = frame_bytes > tail_bytes ? 0U : physical + sizeof(FrameHeader);
#endif
    reader_state_.read_pending_ = true;

    return ReadHandle{cold_state_.storage_.get() + payload_offset, header.payload_bytes,
                      static_cast<std::uint32_t>(frame_bytes)};
}

void SpscRingBuffer::release(const ReadHandle& read_handle) noexcept {
    const std::uint32_t frame_bytes = read_handle.frame_bytes();
    if (frame_bytes == 0U) {
        return;
    }

    reader_state_.current_read_cursor_ += frame_bytes;
    ++reader_state_.records_since_publish_;
    reader_state_.bytes_since_publish_ += frame_bytes;

    reader_state_.read_pending_ = false;

    if (reader_state_.records_since_publish_ >= 32U ||
        reader_state_.bytes_since_publish_ >= 4U * 1024U ||
        reader_state_.current_read_cursor_ == reader_state_.cached_write_cursor_) {
        publish_reclaimed();
    }
}

void SpscRingBuffer::publish_reclaimed() noexcept {
    if (reader_state_.records_since_publish_ == 0U) {
        return;
    }

    cursors_.read_cursor_.store(reader_state_.current_read_cursor_, std::memory_order_release);

    reader_state_.records_since_publish_ = 0U;
    reader_state_.bytes_since_publish_ = 0U;
}
}  // namespace qlog::detail
