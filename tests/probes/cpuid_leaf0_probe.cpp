#include <cpuid.h>

#include <cstddef>
#include <qlog/detail/format_hash.hpp>

#include "format_hash_core.hpp"
extern "C" void* memcpy(void* destination, const void* source, std::size_t size) noexcept {
    auto* d = static_cast<unsigned char*>(destination);
    const auto* s = static_cast<const unsigned char*>(source);
    for (std::size_t i = 0; i < size; ++i) d[i] = s[i];
    return destination;
}
extern "C" int probe() {
    if (__get_cpuid_max(0, nullptr) != 0) return 3;
    if (qlog::detail::hash_impl::x86_crc32c_available()) return 1;
    if (qlog::detail::FormatHashDispatch::automatic().backend() !=
        qlog::detail::HashBackend::software)
        return 2;
    return 0;
}
asm(R"(
.global _start
.type _start,@function
_start:
and $-16,%rsp
call probe
mov %eax,%edi
mov $60,%eax
syscall
)");
