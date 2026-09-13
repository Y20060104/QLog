#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <qlog/detail/record_decoder.hpp>
#include <qlog/detail/record_encoder.hpp>
#include <string>
#include <string_view>

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
template <class... Args>
bool verify(const Args&... args) {
    const auto dispatch = FormatHashDispatch::automatic();
    const RecordValidationPolicy policy{{~0ULL, ~0ULL, ~0ULL, ~0ULL}, true};
    std::array<std::byte, 300000> destination{};
    std::array<DecodedArg, 32> workspace{};
    const auto before = allocations;
    for (std::size_t i = 0; i < 1000; ++i) {
        const auto measured = measure_record({}, destination.size(), args...);
        if (!measured.succeeded()) return false;
        const auto encoded = encode_v1(destination.data(), measured.prepared()->payload_size(),
                                       *measured.prepared(), {}, policy, dispatch);
        if (!encoded.succeeded()) return false;
        const auto decoded = decode_v1(destination.data(), encoded.bytes_written(),
                                       workspace.data(), workspace.size(), policy);
        if (!decoded.succeeded()) return false;
    }
    return allocations == before;
}
template <std::size_t... I>
bool many(const char* s, std::index_sequence<I...>) {
    return verify(((void)I, qlog::cstr(s))...);
}
}  // namespace
int main() {
    const std::string text(8192, 'x');
    const bool passed = verify() && verify(42, 99) &&
                        verify(true, 3.14, std::string_view(text), qlog::cstr(text.c_str())) &&
                        many(text.c_str(), std::make_index_sequence<32>{});
    std::puts(passed ? "4000 measure/encode/decode operations: zero C++ allocations"
                     : "allocation or codec contract failure");
    return passed ? 0 : 1;
}
