#include "qlog/detail/output_batch.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>

namespace qlog::detail {
OutputBatch::OutputBatch(std::size_t capacity) : capacity_(capacity) {
    if (capacity == 0U) {
        throw std::invalid_argument("OutputBatch capacity must be nonzero");
    }

    storage_ = std::make_unique_for_overwrite<std::byte[]>(capacity);
}

const std::byte* OutputBatch::pending_data() const noexcept {
    return storage_.get() + written_;
}

std::size_t OutputBatch::pending_size() const noexcept {
    return used_ - written_;
}

std::size_t OutputBatch::append_capacity() const noexcept {
    if (written_ != 0U) {
        return 0U;
    }
    return capacity_ - used_;
}

bool OutputBatch::append(const std::byte* data, std::size_t size) noexcept {
    if (data == nullptr && size != 0) {
        return false;
    }

    if (size > append_capacity()) {
        return false;
    }

    if (size == 0) {
        return true;
    }

    std::memcpy(storage_.get() + used_, data, size);

    used_ += size;

    return true;
}

void OutputBatch::consume(std::size_t size) noexcept {
    if (size > pending_size()) {
        std::terminate();
    }
    written_ += size;
    if (written_ == used_) {
        written_ = 0U;
        used_ = 0U;
    }
}

std::size_t OutputBatch::discard_pending() noexcept {
    const auto discarded = pending_size();
    written_ = 0U;
    used_ = 0U;
    return discarded;
}
}  // namespace qlog::detail