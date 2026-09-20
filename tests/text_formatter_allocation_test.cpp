#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <string_view>

#include "qlog/detail/text_formatter.hpp"
namespace {
std::size_t allocations = 0;
}
void* operator new(std::size_t size) {
    ++allocations;
    if (void* p = std::malloc(size ? size : 1U)) return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) {
    return ::operator new(size);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
void* operator new(std::size_t size, std::align_val_t alignment) {
    ++allocations;
    void* p = nullptr;
    if (posix_memalign(&p, static_cast<std::size_t>(alignment), size ? size : 1U) == 0) return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}
void operator delete(void* p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}

namespace {
using namespace qlog::detail;
DecodedArg scalar(ArgumentTag tag, std::uint64_t bits) {
    DecodedArg arg{};
    arg.tag = tag;
    arg.value.bits = bits;
    return arg;
}
bool verify() {
    std::array<std::byte, 4096> output{};
    const std::array<DecodedArg, 8> args{
        scalar(ArgumentTag::Int64, UINT64_MAX),
        scalar(ArgumentTag::UInt64, UINT64_MAX),
        scalar(ArgumentTag::F32, std::bit_cast<std::uint32_t>(1.875F)),
        scalar(ArgumentTag::F64, std::bit_cast<std::uint64_t>(123.5)),
        scalar(ArgumentTag::Pointer64, 0xAB),
        scalar(ArgumentTag::Bool, 1U),
        scalar(ArgumentTag::NullUtf8, 0U),
        scalar(ArgumentTag::F64, std::bit_cast<std::uint64_t>(std::numeric_limits<double>::max()))};
    constexpr std::string_view format = "{} {:#x} {:.2f} {:8.2e} {} {} {} {:.99}";
    const auto before = allocations;
    for (unsigned i = 0; i < 10000U; ++i) {
        const auto result =
            render_message_utf8(reinterpret_cast<const std::byte*>(format.data()), format.size(),
                                args.data(), args.size(), output.data(), output.size());
        if (result.failure || result.size == 0U) return false;
        const auto full =
            render_message_utf8(reinterpret_cast<const std::byte*>(format.data()), format.size(),
                                args.data(), args.size(), output.data(), 1U);
        if (!full.failure || full.size != 0U) return false;
    }
    return allocations == before;
}
}  // namespace
int main() {
    const bool passed = verify();
    std::puts(passed ? "20000 formatter calls: zero C++ allocations (including first call and "
                       "fixed fallback)"
                     : "formatter allocation or result failure");
    return passed ? 0 : 1;
}
