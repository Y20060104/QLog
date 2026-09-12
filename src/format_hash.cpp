#include "qlog/detail/format_hash.hpp"

#include "format_hash_core.hpp"

#if QLOG_HAS_X86_CRC32C
#include <cpuid.h>
#endif

namespace qlog::detail::hash_impl {

bool x86_crc32c_available() noexcept {
#if QLOG_HAS_X86_CRC32C
    unsigned int eax{};
    unsigned int ebx{};
    unsigned int ecx{};
    unsigned int edx{};

    if (__get_cpuid(1U, &eax, &ebx, &ecx, &edx) == 0) {
        return false;
    }

    return (ecx & bit_SSE4_2) != 0U;
#else
    return false;
#endif
}

}  // namespace qlog::detail::hash_impl

namespace qlog::detail {
FormatHashDispatch FormatHashDispatch::automatic() noexcept {
#if QLOG_HAS_X86_CRC32C
    if (hash_impl::x86_crc32c_available()) {
        return FormatHashDispatch{
            &hash_impl::x86_hash_raw,
            &hash_impl::x86_copy_raw,
            HashBackend::x86_crc32c,
        };
    }
#endif

    return FormatHashDispatch{
        &hash_impl::software_hash_raw,
        &hash_impl::software_copy_raw,
        HashBackend::software,
    };
}

HashResult hash_format_stored(const FormatHashDispatch& dispatch, const std::byte* source,
                              std::size_t source_size) noexcept {
    if (source_size != 0 && source == nullptr) {
        return HashResult{HashError::invalid_source_metadata};
    }
    auto raw = dispatch.hash_raw_unchecked(source, source_size);
    auto stored = stored_hash_from_raw(raw);
    return HashResult{stored};
}

HashResult copy_and_hash_format_stored(const FormatHashDispatch& dispatch, const std::byte* source,
                                       std::size_t source_size, std::byte* destination,
                                       std::size_t destination_capacity) noexcept {
    if (source_size != 0 && source == nullptr) {
        return HashResult{HashError::invalid_source_metadata};
    }
    if (destination_capacity != 0 && destination == nullptr) {
        return HashResult{HashError::invalid_destination_metadata};
    }
    if (destination_capacity < source_size) {
        return HashResult{HashError::destination_too_small};
    }

    auto raw = dispatch.copy_raw_unchecked(source, destination, source_size);
    auto stored = stored_hash_from_raw(raw);
    return HashResult{stored};
}

}  // namespace qlog::detail