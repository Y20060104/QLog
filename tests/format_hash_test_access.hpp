#pragma once

#include <optional>
#include <qlog/detail/format_hash.hpp>

#include "format_hash_core.hpp"

namespace qlog::detail {
struct FormatHashTestAccess final {
    static FormatHashDispatch software() noexcept {
        return {hash_impl::software_hash_raw, hash_impl::software_copy_raw, HashBackend::software};
    }
    static std::optional<FormatHashDispatch> hardware() noexcept {
#if QLOG_TEST_HAS_X86_CRC32C
        if (hash_impl::x86_crc32c_available()) {
            return FormatHashDispatch{hash_impl::x86_hash_raw, hash_impl::x86_copy_raw,
                                      HashBackend::x86_crc32c};
        }
#endif
        return std::nullopt;
    }
    static FormatHashDispatch custom(RawHashFn hash, RawCopyHashFn copy) noexcept {
        return {hash, copy, HashBackend::software};
    }
};
}  // namespace qlog::detail
