#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace qlog::detail {
// 内部函数指针
using RawHashFn = std::uint64_t (*)(const std::byte* source, std::size_t size) noexcept;
using RawCopyHashFn = std::uint64_t (*)(const std::byte* source, std::byte* destination,
                                        std::size_t size) noexcept;

enum class HashError : std::uint8_t {
    invalid_source_metadata,
    invalid_destination_metadata,
    destination_too_small
};

enum class HashBackend : std::uint8_t {
    software,
    x86_crc32c,
};

[[nodiscard]] constexpr std::uint64_t stored_hash_from_raw(std::uint64_t raw) noexcept {
    return raw == 0U ? 1U : raw;
}

class FormatHashDispatch final {
   public:
    [[nodiscard]] static FormatHashDispatch automatic() noexcept;
    [[nodiscard]] HashBackend backend() const noexcept {
        return backend_;
    }

    [[nodiscard]] std::uint64_t hash_raw_unchecked(const std::byte* source,
                                                   std::size_t size) const noexcept {
        return raw_hash_(source, size);
    }
    [[nodiscard]] std::uint64_t copy_raw_unchecked(const std::byte* source, std::byte* destination,
                                                   std::size_t size) const noexcept {
        return raw_copy_hash_(source, destination, size);
    }

   private:
    FormatHashDispatch(RawHashFn hash, RawCopyHashFn copy, HashBackend backend) noexcept
        : raw_hash_(hash), raw_copy_hash_(copy), backend_(backend) {}

   private:
    RawHashFn raw_hash_;
    RawCopyHashFn raw_copy_hash_;
    HashBackend backend_;
    friend struct FormatHashTestAccess;
};

class HashResult final {
   public:
    [[nodiscard]] bool succeeded() const noexcept {
        return stored_hash_.has_value();
    }

    [[nodiscard]] const std::uint64_t* stored_hash() const noexcept {
        return stored_hash_ ? &*stored_hash_ : nullptr;
    }

    [[nodiscard]] const HashError* failure() const noexcept {
        return stored_hash_ ? nullptr : &error_;
    }

   private:
    explicit HashResult(std::uint64_t stored) noexcept : stored_hash_(stored), error_{} {}
    explicit HashResult(HashError error) noexcept : stored_hash_(std::nullopt), error_(error) {}

    // 面向独立调用者的checked接口
    friend HashResult hash_format_stored(const FormatHashDispatch& dispatch,
                                         const std::byte* source, std::size_t source_size) noexcept;

    friend HashResult copy_and_hash_format_stored(const FormatHashDispatch& dispatch,
                                                  const std::byte* source, std::size_t source_size,
                                                  std::byte* destination,
                                                  std::size_t destination_capacity) noexcept;

   private:
    std::optional<std::uint64_t> stored_hash_;
    HashError error_{};
};
struct FormatHashTestAccess;

// 面向独立调用者的checked接口
[[nodiscard]] HashResult hash_format_stored(const FormatHashDispatch& dispatch,
                                            const std::byte* source,
                                            std::size_t source_size) noexcept;

[[nodiscard]] HashResult copy_and_hash_format_stored(const FormatHashDispatch& dispatch,
                                                     const std::byte* source,
                                                     std::size_t source_size,
                                                     std::byte* destination,
                                                     std::size_t destination_capacity) noexcept;

}  // namespace qlog::detail