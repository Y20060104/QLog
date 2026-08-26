#include "qlog/detail/spsc_ring_buffer.hpp"

#include <new>
#include <stdexcept>

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
    : cold_state_(validate_config(config)) {}

SpscRingBuffer::~SpscRingBuffer() noexcept = default;

}  // namespace qlog::detail