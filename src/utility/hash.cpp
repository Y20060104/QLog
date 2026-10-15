#include "qlog/utility/hash.hpp"

#include "hash_core.hpp"
#include "qlog/global/common_vars.hpp"

namespace qlog::utility {
namespace {
// Match BQLog's simd.cpp snapshot of the shared runtime capability.
bool crc32_supported_ = qlog::common_global_vars::get().crc32_supported_;
}  // namespace

std::uint64_t get_hash_64(const void* data, std::size_t size) {
    const auto* src = static_cast<const std::byte*>(data);
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    if (crc32_supported_) [[likely]] {
        return hash_impl::x86_hash_raw(src, size);
    }
#endif
    return hash_impl::software_hash_raw(src, size);
}

std::uint64_t qlog_memcpy_with_hash(void* QLOG_RESTRICT dst, const void* QLOG_RESTRICT src,
                                    std::size_t len) {
    const auto* source = static_cast<const std::byte*>(src);
    auto* destination = static_cast<std::byte*>(dst);
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    if (crc32_supported_) [[likely]] {
        return hash_impl::x86_copy_raw(source, destination, len);
    }
#endif
    return hash_impl::software_copy_raw(source, destination, len);
}
}  // namespace qlog::utility
