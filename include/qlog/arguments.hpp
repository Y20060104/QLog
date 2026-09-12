#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace qlog::detail {

struct PointerArgument final {
    std::uint64_t value{};
};

struct CStrArgument final {
    const char* data{};
};

template <class P>
concept QlogObjectPointer = [] {
    using U = std::remove_cvref_t<P>;

    if constexpr (!std::is_pointer_v<U>) {
        return false;
    } else {
        using T = std::remove_pointer_t<U>;
        return (std::is_object_v<T> || std::is_void_v<std::remove_cv_t<T>>) &&
               !std::is_volatile_v<T>;
    }
}();

}  // namespace qlog::detail

namespace qlog {
template <detail::QlogObjectPointer P>
[[nodiscard]] inline detail::PointerArgument ptr(P value) noexcept {
    return {static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value))};
}

[[nodiscard]] constexpr detail::PointerArgument ptr(std::nullptr_t) noexcept {
    return {};
}

// A non-null value must point to a readable NUL-terminated string.
[[nodiscard]] constexpr detail::CStrArgument cstr(const char* value) noexcept {
    return {value};
}
}  // namespace qlog
