#include "qlog/detail/spsc_ring_buffer.hpp"

#include <cstring>
#include <new>
#include <stdexcept>

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

WriteHandle::WriteHandle(WriteHandle&& other) noexcept {
    ring_ = other.ring_;
    payload_ = other.payload_;
    next_write_cursor_ = other.next_write_cursor_;
    payload_bytes_ = other.payload_bytes_;
    status_ = other.status_;
    other.deactivate();
}

WriteHandle& WriteHandle::operator=(WriteHandle&& other) noexcept {
    if (this != &other) {
        abort();
        ring_ = other.ring_;
        payload_ = other.payload_;
        next_write_cursor_ = other.next_write_cursor_;
        payload_bytes_ = other.payload_bytes_;
        status_ = other.status_;
        other.deactivate();
    }
    return *this;
}

WriteHandle::~WriteHandle() noexcept {
    abort();
}

ReadHandle::ReadHandle(ReadHandle&& other) noexcept {
    ring_ = other.ring_;
    payload_ = other.payload_;
    next_read_cursor_ = other.next_read_cursor_;
    payload_bytes_ = other.payload_bytes_;
    status_ = other.status_;
    other.deactivate();
}

ReadHandle& ReadHandle::operator=(ReadHandle&& other) noexcept {
    if (this != &other) {
        abandon();
        ring_ = other.ring_;
        payload_ = other.payload_;
        next_read_cursor_ = other.next_read_cursor_;
        payload_bytes_ = other.payload_bytes_;
        status_ = other.status_;
        other.deactivate();
    }
    return *this;
}

ReadHandle::~ReadHandle() noexcept {
    abandon();
}

WriteHandle SpscRingBuffer::try_reserve(std::size_t exact_payload_bytes) noexcept {
    WriteHandle write_handle;
    if (writer_state_.reservation_pending_ == true) {
        write_handle.status_ = ReserveStatus::reservation_pending;
        return write_handle;
    }
    if (exact_payload_bytes > cold_state_.config_.max_payload_bytes) {
        write_handle.status_ = ReserveStatus::payload_too_large;
        return write_handle;
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
            write_handle.status_ = ReserveStatus::full;
            return write_handle;
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
    write_handle.ring_ = this;
    write_handle.payload_ = payload_address;
    write_handle.next_write_cursor_ = writer_state_.current_write_cursor_ + layout.frame_bytes;
    write_handle.payload_bytes_ = static_cast<std::uint32_t>(exact_payload_bytes);
    write_handle.status_ = ReserveStatus::ok;

    writer_state_.reservation_pending_ = true;
    return write_handle;
}

void WriteHandle::commit() noexcept {
    if (ring_ != nullptr) {
        ring_->commit_write(*this);
    }
}

void WriteHandle::deactivate() noexcept {
    ring_ = nullptr;
    payload_ = nullptr;
    next_write_cursor_ = 0;
    payload_bytes_ = 0;
}

void SpscRingBuffer::commit_write(WriteHandle& write_handle) noexcept {
    writer_state_.current_write_cursor_ = write_handle.next_write_cursor_;
    cursors_.write_cursor_.store(write_handle.next_write_cursor_, std::memory_order_release);
    writer_state_.reservation_pending_ = false;
    write_handle.deactivate();
}

void WriteHandle::abort() noexcept {
    if (ring_ != nullptr) {
        ring_->abort_write(*this);
    }
}

void SpscRingBuffer::abort_write(WriteHandle& write_handle) noexcept {
    writer_state_.reservation_pending_ = false;
    write_handle.deactivate();
}

void ReadHandle::abandon() noexcept {
    if (ring_ != nullptr) {
        ring_->abandon_read(*this);
    }
}

void ReadHandle::deactivate() noexcept {
    ring_ = nullptr;
    payload_ = nullptr;
    next_read_cursor_ = 0;
    payload_bytes_ = 0;
}

void SpscRingBuffer::abandon_read(ReadHandle& read_handle) noexcept {
    reader_state_.read_pending_ = false;
    read_handle.deactivate();
}

void ReadHandle::consume() noexcept {
    if (ring_ != nullptr) {
        ring_->consume_read(*this);
    }
}

ReadHandle SpscRingBuffer::try_peek() noexcept {
    ReadHandle read_handle;
    if (reader_state_.read_pending_ == true) {
        read_handle.status_ = ReadStatus::read_pending;
        return read_handle;
    }

    if (reader_state_.current_read_cursor_ == reader_state_.cached_write_cursor_) {
        reader_state_.cached_write_cursor_ = cursors_.write_cursor_.load(std::memory_order_acquire);
    }
    if (reader_state_.cached_write_cursor_ - reader_state_.current_read_cursor_ == 0) {
        publish_reclaimed();
        read_handle.status_ = ReadStatus::empty;
        return read_handle;
    }

    if (reader_state_.cached_write_cursor_ - reader_state_.current_read_cursor_ >
            cold_state_.config_.capacity_bytes ||
        reader_state_.cached_write_cursor_ - reader_state_.current_read_cursor_ <
            sizeof(FrameHeader)) {
        publish_reclaimed();
        read_handle.status_ = ReadStatus::corrupted;
        return read_handle;
    }

    const std::byte* header_address = static_cast<std::size_t>(reader_state_.current_read_cursor_ &
                                                               cold_state_.config_.capacity_mask) +
                                      cold_state_.storage_.get();

    FrameHeader header{};

    std::memcpy(&header, header_address, sizeof(FrameHeader));

    if (header.payload_bytes > cold_state_.config_.max_payload_bytes) {
        publish_reclaimed();
        read_handle.status_ = ReadStatus::corrupted;
        return read_handle;
    }

    const FrameLayout layout =
        compute_frame_layout(reader_state_.current_read_cursor_, header.payload_bytes,
                             cold_state_.config_.capacity_bytes);

    if (layout.frame_bytes != header.frame_bytes ||
        layout.frame_bytes >
            reader_state_.cached_write_cursor_ - reader_state_.current_read_cursor_) {
        publish_reclaimed();
        read_handle.status_ = ReadStatus::corrupted;
        return read_handle;
    }

    read_handle.ring_ = this;
    read_handle.payload_ = cold_state_.storage_.get() + layout.payload_offset;
    read_handle.next_read_cursor_ = reader_state_.current_read_cursor_ + layout.frame_bytes;
    read_handle.payload_bytes_ = header.payload_bytes;
    read_handle.status_ = ReadStatus::ok;

    reader_state_.read_pending_ = true;
    return read_handle;
}

void SpscRingBuffer::consume_read(ReadHandle& read_handle) noexcept {
    const std::uint64_t frame_bytes =
        read_handle.next_read_cursor_ - reader_state_.current_read_cursor_;
    reader_state_.current_read_cursor_ = read_handle.next_read_cursor_;
    ++reader_state_.records_since_publish_;
    reader_state_.bytes_since_publish_ += static_cast<std::uint32_t>(frame_bytes);
    reader_state_.read_pending_ = false;
    read_handle.deactivate();

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
