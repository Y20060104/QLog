#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "argument_tag.hpp"
#include "qlog/arguments.hpp"

namespace qlog::detail {
struct NormalizedArgument final {
    std::uint64_t bits{};
    const std::byte* bytes{};
    std::uint32_t byte_count{};
    ArgumentTag tag{ArgumentTag::Invalid};
};
static_assert(std::is_standard_layout_v<NormalizedArgument>);
static_assert(std::is_trivially_copyable_v<NormalizedArgument>);

enum class ArgumentKind : std::uint8_t {
    Unsupported,
    Bool,
    Char,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    F32,
    F64,
    Pointer64,
    Utf8String,
    CStr,
};

template <class U>
[[nodiscard]] consteval ArgumentKind match_integer_kind() noexcept {
    if constexpr (sizeof(U) == 1U) {
        if constexpr (std::is_signed_v<U>) {
            return ArgumentKind::Int8;
        } else {
            return ArgumentKind::UInt8;
        }
    } else if constexpr (sizeof(U) == 2U) {
        if constexpr (std::is_signed_v<U>) {
            return ArgumentKind::Int16;
        } else {
            return ArgumentKind::UInt16;
        }
    } else if constexpr (sizeof(U) == 4U) {
        if constexpr (std::is_signed_v<U>) {
            return ArgumentKind::Int32;
        } else {
            return ArgumentKind::UInt32;
        }
    } else if constexpr (sizeof(U) == 8U) {
        if constexpr (std::is_signed_v<U>) {
            return ArgumentKind::Int64;
        } else {
            return ArgumentKind::UInt64;
        }
    } else {
        return ArgumentKind::Unsupported;
    }
}

template <class T>
[[nodiscard]] consteval ArgumentKind match_argument_kind() noexcept {
    using Raw = std::remove_reference_t<T>;

    if constexpr (std::is_array_v<Raw>) {
        using Element = std::remove_extent_t<Raw>;
        if constexpr (std::is_volatile_v<Element>) {
            return ArgumentKind::Unsupported;
        }

        using Character = std::remove_const_t<Element>;

        if constexpr (std::is_const_v<Element> &&
                      (std::is_same_v<char, Character> || std::is_same_v<char8_t, Character>)) {
            return ArgumentKind::Utf8String;
        } else {
            return ArgumentKind::Unsupported;
        }
    } else {
        if constexpr (std::is_volatile_v<Raw>) {
            return ArgumentKind::Unsupported;
        }

        using U = std::remove_const_t<Raw>;

        if constexpr (std::is_same_v<bool, U>) {
            return ArgumentKind::Bool;
        } else if constexpr (std::is_same_v<char, U>) {
            return ArgumentKind::Char;
        } else if constexpr (std::is_same_v<std::byte, U> || std::is_same_v<char8_t, U> ||
                             std::is_same_v<char16_t, U> || std::is_same_v<char32_t, U> ||
                             std::is_same_v<wchar_t, U>) {
            return ArgumentKind::Unsupported;
        } else if constexpr (std::is_integral_v<U>) {
            return match_integer_kind<U>();
        } else if constexpr (std::is_enum_v<U>) {
            using Underlying = std::underlying_type_t<U>;

            if constexpr (std::is_same_v<Underlying, bool>) {
                return ArgumentKind::Unsupported;
            } else if constexpr (std::is_same_v<Underlying, char>) {
                if constexpr (std::is_signed_v<char>) {
                    return ArgumentKind::Int8;
                } else {
                    return ArgumentKind::UInt8;
                }
            } else if constexpr (std::is_same_v<Underlying, char8_t> ||
                                 std::is_same_v<Underlying, char16_t> ||
                                 std::is_same_v<Underlying, char32_t> ||
                                 std::is_same_v<Underlying, wchar_t>) {
                return ArgumentKind::Unsupported;
            } else {
                return match_integer_kind<Underlying>();
            }
        } else if constexpr (std::is_same_v<float, U>) {
            return ArgumentKind::F32;
        } else if constexpr (std::is_same_v<double, U>) {
            return ArgumentKind::F64;
        } else if constexpr (std::is_same_v<std::nullptr_t, U>) {
            return ArgumentKind::Pointer64;
        } else if constexpr (std::is_same_v<PointerArgument, U>) {
            return ArgumentKind::Pointer64;
        } else if constexpr (std::is_same_v<std::string, U> ||
                             std::is_same_v<std::string_view, U> ||
                             std::is_same_v<std::u8string, U> ||
                             std::is_same_v<std::u8string_view, U>) {
            return ArgumentKind::Utf8String;
        } else if constexpr (std::is_same_v<CStrArgument, U>) {
            return ArgumentKind::CStr;
        } else {
            return ArgumentKind::Unsupported;
        }
    }
}

template <class T>
struct ArgumentTraits final {
    static constexpr ArgumentKind kind = match_argument_kind<T>();
    static constexpr bool supported = kind != ArgumentKind::Unsupported;
};

template <class T>
concept SupportedArgument = ArgumentTraits<T>::supported;

[[nodiscard]] constexpr ArgumentTag wire_tag_for(ArgumentKind kind) noexcept {
    switch (kind) {
        case ArgumentKind::Bool:
            return ArgumentTag::Bool;

        case ArgumentKind::Char:
            return ArgumentTag::Char;

        case ArgumentKind::Int8:
            return ArgumentTag::Int8;

        case ArgumentKind::UInt8:
            return ArgumentTag::UInt8;

        case ArgumentKind::Int16:
            return ArgumentTag::Int16;

        case ArgumentKind::UInt16:
            return ArgumentTag::UInt16;

        case ArgumentKind::Int32:
            return ArgumentTag::Int32;

        case ArgumentKind::UInt32:
            return ArgumentTag::UInt32;

        case ArgumentKind::Int64:
            return ArgumentTag::Int64;

        case ArgumentKind::UInt64:
            return ArgumentTag::UInt64;

        case ArgumentKind::F32:
            return ArgumentTag::F32;

        case ArgumentKind::F64:
            return ArgumentTag::F64;

        case ArgumentKind::Pointer64:
            return ArgumentTag::Pointer64;

        case ArgumentKind::Utf8String:
            return ArgumentTag::Utf8String;

        case ArgumentKind::Unsupported:
        case ArgumentKind::CStr:
            return ArgumentTag::Invalid;
    }

    return ArgumentTag::Invalid;
}

[[nodiscard]] constexpr bool encoded_size(const NormalizedArgument& argument,
                                          std::size_t& output) noexcept {
    switch (argument.tag) {
        case ArgumentTag::Bool:
        case ArgumentTag::Char:
        case ArgumentTag::Int8:
        case ArgumentTag::UInt8: {
            output = 1U + 1U;
            return true;
        }
        case ArgumentTag::Int16:
        case ArgumentTag::UInt16: {
            output = 1U + 2U;
            return true;
        }

        case ArgumentTag::Int32:
        case ArgumentTag::UInt32:
        case ArgumentTag::F32: {
            output = 1U + 4U;
            return true;
        }

        case ArgumentTag::Int64:
        case ArgumentTag::UInt64:
        case ArgumentTag::F64:
        case ArgumentTag::Pointer64:
            return 1U + 8U;

        case ArgumentTag::Utf8String: {
            output = 1U + 4U + static_cast<std::size_t>(argument.byte_count);
            return true;
        }

        case ArgumentTag::NullUtf8: {
            output = 1U;
            return true;
        }

        case ArgumentTag::Invalid: {
            output = 0U;
            return false;
        }
    }

    {
        output = 0U;
        return false;
    }
}

}  // namespace qlog::detail