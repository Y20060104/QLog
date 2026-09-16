#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace qlog {
class FormatView final {
   public:
    [[nodiscard]] const std::byte* data() const noexcept {
        return data_;
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }
    [[nodiscard]] std::uint64_t stored_hash() const noexcept {
        return stored_hash_;
    }

   private:
    friend FormatView runtime_format(const char*, std::size_t) noexcept;
    friend FormatView runtime_format(const char8_t*, std::size_t) noexcept;

    FormatView(const std::byte* data, std::size_t size) noexcept
        : data_(data), size_(size), stored_hash_(0u) {}

    const std::byte* data_;
    std::size_t size_;
    std::uint64_t stored_hash_;
};

[[nodiscard]] inline FormatView runtime_format(const char* data, std::size_t size) noexcept {
    return FormatView(reinterpret_cast<const std::byte*>(data), size);
}

[[nodiscard]] inline FormatView runtime_format(const char8_t* data, std::size_t size) noexcept {
    return FormatView(reinterpret_cast<const std::byte*>(data), size);
}

[[nodiscard]] inline FormatView runtime_format(std::string_view text) noexcept {
    return runtime_format(text.data(), text.size());
}

[[nodiscard]] inline FormatView runtime_format(std::u8string_view text) noexcept {
    return runtime_format(text.data(), text.size());
}
}  // namespace qlog