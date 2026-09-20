#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>

namespace qlog::detail {
class OutputBatch final {
   public:
    explicit OutputBatch(std::size_t capacity);
    OutputBatch(const OutputBatch&) = delete;
    OutputBatch& operator=(const OutputBatch&) = delete;
    const std::byte* pending_data() const noexcept;
    std::size_t pending_size() const noexcept;
    std::size_t append_capacity() const noexcept;
    bool append(const std::byte* data, std::size_t size) noexcept;
    void consume(std::size_t size) noexcept;
    std::size_t discard_pending() noexcept;

   private:
    std::unique_ptr<std::byte[]> storage_;
    std::size_t capacity_{};
    std::size_t used_{};
    std::size_t written_{};
};
}  // namespace qlog::detail