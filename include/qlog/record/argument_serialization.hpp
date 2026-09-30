#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "qlog/record/argument_tag.hpp"
#include "qlog/utility/checked_size.hpp"

namespace qlog::record {
namespace detail {

constexpr std::size_t size_add(std::size_t a, std::size_t b) {
    std::size_t result = 0;

    const bool ok = qlog::utility::checked_add(a, b, result);

    assert(ok);
    (void)ok;  // CR:void ok这有什么用
    return result;
}

constexpr std::size_t size_mul(std::size_t a, std::size_t b) {
    std::size_t result = 0;

    const bool ok = qlog::utility::checked_mul(a, b, result);

    assert(ok);
    (void)ok;
    return result;
}

constexpr std::size_t align4(std::size_t n) {
    return size_add(n, 3U) & ~std::size_t{3};
}

template <typename P>
struct CustomCharPointer {
    using type = void;
};

template <>
struct CustomCharPointer<const char*> {
    using type = char;
};

template <>
struct CustomCharPointer<const char16_t*> {
    using type = char16_t;
};

template <>
struct CustomCharPointer<const char32_t*> {
    using type = char32_t;
};

template <>
struct CustomCharPointer<const wchar_t*> {
    using type = wchar_t;
};

template <typename T, typename = void>
struct MemberChars {
    using type = void;
};

template <typename T>
struct MemberChars<T, std::void_t<decltype(std::declval<const T&>().log_format_str_chars())>>
    : CustomCharPointer<std::decay_t<decltype(std::declval<const T&>().log_format_str_chars())>> {};

template <typename T, typename = void>
struct GlobalChars {
    using type = void;
};

template <typename T>
struct GlobalChars<T, std::void_t<decltype(log_format_str_chars(std::declval<const T&>()))>>
    : CustomCharPointer<decltype(log_format_str_chars(std::declval<const T&>()))> {};

template <typename T, typename C>
inline constexpr bool member_format_v = requires(const T& v, C* p, std::size_t n) {
    { v.log_custom_format(p, n) } -> std::same_as<void>;
};

template <typename T, typename C>
inline constexpr bool global_format_v = requires(const T& v, C* p, std::size_t n) {
    { log_custom_format(v, p, n) } -> std::same_as<void>;
};

template <typename T, bool Member>
struct FormatCallback {
    template <typename C>
    static constexpr bool accepts = Member ? member_format_v<T, C> : global_format_v<T, C>;

    static constexpr unsigned count = unsigned(accepts<char>) + unsigned(accepts<char16_t>) +
                                      unsigned(accepts<char32_t>) + unsigned(accepts<wchar_t>);

    static_assert(count <= 1, "custom format character type is ambiguous");

    using char_type = std::conditional_t<
        accepts<char>, char,
        std::conditional_t<
            accepts<char16_t>, char16_t,
            std::conditional_t<accepts<char32_t>, char32_t,
                               std::conditional_t<accepts<wchar_t>, wchar_t, void>>>>;
};

template <typename T>
struct CustomFormatTraits {
    using U = std::remove_cvref_t<T>;

    static constexpr bool has_member_size = requires(const U& v) {
        { v.log_format_str_size() } -> std::same_as<std::size_t>;
    };

    static constexpr bool has_global_size = !has_member_size && requires(const U& v) {
        { log_format_str_size(v) } -> std::same_as<std::size_t>;
    };
    using MemberCallback = FormatCallback<U, true>;

    static constexpr bool has_member_format = MemberCallback::count != 0;

    using GlobalCallback =
        std::conditional_t<has_member_format, std::type_identity<void>, FormatCallback<U, false>>;

    static constexpr bool has_global_format = [] {
        if constexpr (has_member_format) {
            return false;
        } else {
            return GlobalCallback::count != 0;
        }
    }();

    static constexpr bool has_member_chars =
        !has_member_format && !has_global_format && !std::is_void_v<typename MemberChars<U>::type>;

    static constexpr bool has_global_chars = !has_member_format && !has_global_format &&
                                             !has_member_chars &&
                                             !std::is_void_v<typename GlobalChars<U>::type>;

    static constexpr bool is_valid =
        (has_member_size || has_global_size) &&
        (has_member_format || has_global_format || has_member_chars || has_global_chars);

    using char_type = typename decltype([] {
        if constexpr (has_member_format) {
            return std::type_identity<typename MemberCallback::char_type>{};
        } else if constexpr (has_global_format) {
            return std::type_identity<typename GlobalCallback::char_type>{};
        } else if constexpr (has_member_chars) {
            return std::type_identity<typename MemberChars<U>::type>{};
        } else if constexpr (has_global_chars) {
            return std::type_identity<typename GlobalChars<U>::type>{};
        } else {
            return std::type_identity<void>{};
        }
    }())::type;

    static std::size_t get_char_count(const U& v) {
        static_assert(is_valid, "invalid custom format protocol");
        if constexpr (has_member_size) {
            return v.log_format_str_size();
        } else {
            return log_format_str_size(v);
        }
    }

    static void copy_content(const U& v, std::uint8_t* dst, std::size_t bytes) {
        static_assert(is_valid, "invalid custom format protocol");

        static_assert(sizeof(char_type) <= 2,
                      "custom UTF-32 storage contract requires a separate decision");

        if (bytes == 0) {
            return;
        }
        if constexpr (has_member_format) {
            v.log_custom_format(reinterpret_cast<char_type*>(dst), bytes);
        } else if constexpr (has_global_format) {
            log_custom_format(v, reinterpret_cast<char_type*>(dst), bytes);
        } else {
            const char_type* src;

            if constexpr (has_member_chars) {
                src = v.log_format_str_chars();
            } else {
                src = log_format_str_chars(v);
            }
            // CR: v.log_format_str_chars()和log_format_str_chars(v) 有什么区别 为什么要这么设计？
            // CR: has member和global有什么区别？
            // CR:char type为什么被需要？如何判断type的？
            assert(src != nullptr);
            std::memcpy(dst, src, bytes);
        }
    }
};

template <typename T, typename = void>
struct ClassStringTraits {
    using char_type = void;

    static constexpr bool has_c_str = false;
    static constexpr bool valid = false;
    static constexpr std::size_t width = 0;
};

template <typename T>
struct ClassStringTraits<T, std::void_t<typename T::value_type, typename T::size_type>> {
    static constexpr bool has_size = requires(const T& v) {
        { v.size() } -> std::same_as<typename T::size_type>;
    };
    // CR:std::void_t<typename T::value_type,typename T::size_type>这是什么？我不懂这个代码
    using char_type = typename T::value_type;

    static constexpr bool has_c_str = requires(const T& v) {
        {
            v.c_str()
        } -> std::same_as<const char_type*>;  // CR:requires语法是什么 为什么要有大括号{}？ ->
                                              // 是什么？什么时候触发
    };

    static constexpr bool has_data = requires(const T& v) {
        { v.data() } -> std::same_as<const char_type*>;
    };

    static constexpr std::size_t width = [] {
        if constexpr (requires { sizeof(char_type); }) {
            return sizeof(char_type);
        } else {
            return std::size_t{0};
        }
    }();

    static constexpr bool valid =
        has_size && (has_c_str || has_data) && (width == 1 || width == 2 || width == 4);
};

// CR:命名为BQ的为了配对BQLOG,QLOG保持为log

// CR:为什么用第三部分的结构化字符串检测替换现在的
// StandardStringTraits？StandardStringTraits是对齐BQLog的吧

enum class SerializeKind {
    custom,
    utf8_string,
    utf16_string,
    utf32_string,
    null_pointer,
    pointer,
    pod,
    unsupported,
};

template <typename T, typename C>
inline constexpr bool is_character_pointer_v =
    std::is_same_v<std::decay_t<T>, C*> || std::is_same_v<std::decay_t<T>, const C*>;

template <typename T>
constexpr SerializeKind get_serialize_kind() {
    using U = std::decay_t<T>;
    using S = ClassStringTraits<U>;

    if constexpr (CustomFormatTraits<U>::is_valid) {
        return SerializeKind::custom;
    } else if constexpr (is_character_pointer_v<T, char> || (S::valid && S::width == 1)) {
        return SerializeKind::utf8_string;
    } else if constexpr (is_character_pointer_v<T, char16_t> ||
                         (sizeof(wchar_t) == 2 && is_character_pointer_v<T, wchar_t>) ||
                         (S::valid && S::width == 2)) {
        return SerializeKind::utf16_string;
    } else if constexpr (is_character_pointer_v<T, char32_t> ||
                         (sizeof(wchar_t) == 4 && is_character_pointer_v<T, wchar_t>) ||
                         (S::valid && S::width == 4)) {
        return SerializeKind::utf32_string;
    } else if constexpr (std::is_null_pointer_v<U>) {
        return SerializeKind::null_pointer;
    } else if constexpr (std::is_pointer_v<U>) {
        return SerializeKind::pointer;
    } else if constexpr (std::is_trivial_v<U> && std::is_standard_layout_v<U>) {
        return SerializeKind::pod;
        // CR:: std::is_trivial_v<U> && std::is_standard_layout_v<U> 是什么
        // 带不带_v有什么区别？为什么可以判定为pod pod是什么？ 编译器会自动进行pod优化吗？
    } else {
        return SerializeKind::unsupported;
    }
}

template <typename T>
constexpr ArgumentTag get_log_param_type_enum() {
    using U = std::decay_t<T>;
    constexpr auto k = get_serialize_kind<T>();

    if constexpr (k == SerializeKind::custom) {
        using C = typename CustomFormatTraits<U>::char_type;  // CR:这里的typename 起什么作用？

        if constexpr (std::is_same_v<C, char>) {
            return ArgumentTag::string_utf8_type;
        } else {
            return ArgumentTag::string_utf16_type;
        }

    } else if constexpr (k == SerializeKind::utf8_string) {
        return ArgumentTag::string_utf8_type;
    } else if constexpr (k == SerializeKind::utf16_string) {
        return ArgumentTag::string_utf16_type;
    } else if constexpr (k == SerializeKind::utf32_string) {
        // Ordinary UTF-32 input is converted to UTF-16 by copy_string_content.
        return ArgumentTag::string_utf16_type;
        /*CR:你写的是：  else if constexpr (
            k == SerializeKind::utf16_string ||
            k == SerializeKind::utf32_string) {
            return ArgumentTag::string_utf16_type; 我将utf16和32分开了*/
    } else if constexpr (k == SerializeKind::null_pointer) {
        return ArgumentTag::null_type;
    } else if constexpr (k == SerializeKind::pointer) {
        return ArgumentTag::pointer_type;
    } else if constexpr (k != SerializeKind::pod) {
        return ArgumentTag::unsupported_type;
    } else if constexpr (std::is_same_v<U, bool>) {
        return ArgumentTag::bool_type;
    } else if constexpr (std::is_same_v<U, char>) {
        return ArgumentTag::char_type;
    } else if constexpr (std::is_same_v<U, char16_t> ||
                         (std::is_same_v<U, wchar_t> && sizeof(wchar_t) == 2)) {
        return ArgumentTag::char16_type;
    } else if constexpr (std::is_same_v<U, char32_t> ||
                         (std::is_same_v<U, wchar_t> && sizeof(wchar_t) == 4)) {
        // CR:为什么wchar会有==2 和==4两种情况
        return ArgumentTag::char32_type;
    } else if constexpr (std::is_same_v<U, float>) {
        return ArgumentTag::float_type;
    } else if constexpr (std::is_same_v<U, double>) {
        return ArgumentTag ::double_type;
    } else if constexpr (sizeof(U) == 1) {
        return std::is_unsigned_v<U> ? ArgumentTag::uint8_type : ArgumentTag::int8_type;
    } else if constexpr (sizeof(U) == 2) {
        return std::is_unsigned_v<U> ? ArgumentTag::uint16_type : ArgumentTag::int16_type;
    } else if constexpr (sizeof(U) == 4) {
        return std::is_unsigned_v<U> ? ArgumentTag::uint32_type : ArgumentTag::int32_type;
    } else if constexpr (sizeof(U) == 8) {
        return std::is_unsigned_v<U> ? ArgumentTag::uint64_type : ArgumentTag::int64_type;
    } else {
        return ArgumentTag::unsupported_type;
    }
}

template <typename C>
struct StringSource {
    const C* data;
    std::size_t limit;
};

inline constexpr std::size_t unbounded_string = std::numeric_limits<std::size_t>::max();

template <typename T>
auto get_string_source(const T& v) {
    using U = std::remove_cvref_t<T>;

    if constexpr (std::is_array_v<U>) {
        using C = std::remove_cv_t<std::remove_extent_t<U>>;

        constexpr auto count = std::extent_v<U>;  // CR:extent_v 是什么？
        auto limit = count;

        if constexpr (sizeof(C) != 4) {
            if (v[count - 1] == C{}) {
                --limit;  // CR::==C{}是什么？
            }
        }

        return StringSource<C>{v, limit};
    } else if constexpr (std::is_pointer_v<U>) {
        using C = std::remove_cv_t<std::remove_pointer_t<U>>;

        static constexpr C null_text[] = {C('n'), C('u'), C('l'), C('l'), C{}};
        return v ? StringSource<C>{v, unbounded_string} : StringSource<C>{null_text, 4};
    } else {
        using S = ClassStringTraits<U>;
        static_assert(S::valid, "not a compatible string");

        using C = typename S::char_type;
        const C* data;

        if constexpr (S::has_c_str) {
            data = v.c_str();
        } else {
            data = v.data();
        }

        return StringSource<C>{data, static_cast<std::size_t>(v.size())};
    }
}

template <typename C>
std::size_t measure_string_content(StringSource<C> s) {
    assert(s.limit == 0 || s.data != nullptr);
    // CR: 为什么要limit==0

    if constexpr (sizeof(C) == 4) {
        std::size_t bytes = 0;

        for (std::size_t i = 0; i < s.limit; ++i) {
            const auto c = static_cast<std::uint32_t>(s.data[i]);

            if (c == 0) {
                break;
            }

            bytes = size_add(bytes, c <= 0xFFFFU ? 2U : 4U);
        }
        return bytes;
    } else {
        auto count = s.limit;

        if (count == unbounded_string) {
            count = 0;

            while (s.data[count] != C{}) {
                ++count;
            }
        }
        return size_mul(count, sizeof(C));
    }
}

template <typename C>
void copy_string_content(StringSource<C> s, std::uint8_t* dst, std::size_t bytes) {
    if (bytes == 0) {
        return;
    }

    assert(s.data != nullptr);

    if constexpr (sizeof(C) != 4) {
        if (s.limit != unbounded_string) {
            assert(bytes == size_mul(s.limit, sizeof(C)));
        }

        std::memcpy(dst, s.data, bytes);
    } else {
        assert(bytes % 2U == 0);

        std::size_t in = 0;
        std::size_t out = 0;

        while (out < bytes) {
            assert(in < s.limit);

            auto c = static_cast<std::uint32_t>(s.data[in++]);  // 这是什么提取4字节？

            assert(c != 0);

            if (c <= 0xFFFFU) {
                assert(bytes - out >= 2U);  // CR:为什么要大于2

                const auto uint = static_cast<char16_t>(c);

                std::memcpy(dst + out, &uint, sizeof(uint));

                out += sizeof(uint);
            } else {
                assert(bytes - out >= 4U);
                c -= 0x10000U;  // CR:为什么要相减？ 为什么是这个数

                const char16_t uints[] = {
                    static_cast<char16_t>((c >> 10U) + 0xD800U),
                    static_cast<char16_t>((c & 0x3FFU) + 0xDC00U),
                };  // CR:为什么是这些数字？

                std::memcpy(dst + out, uints, sizeof(uints));

                out += sizeof(uints);
            }
        }
        assert(out == bytes);
    }
}

template <typename T>
struct StringStorage {
    static std::size_t get_storage_data_size(const T& v) {
        if constexpr (get_serialize_kind<T>() == SerializeKind::custom) {
            using F = CustomFormatTraits<T>;

            static_assert(sizeof(typename F::char_type) <= 2,
                          "custom UTF-32 storage contract requires a separate decision");

            return size_mul(F::get_char_count(v), sizeof(typename F::char_type));
        } else {
            return measure_string_content(get_string_source(v));
        }
    }

    static void type_copy(const T& v, std::uint8_t* dst, std::size_t raw) {
        assert(dst != nullptr);
        assert(raw >= sizeof(std::uint32_t));

        const auto bytes = raw - sizeof(std::uint32_t);

        assert(bytes <= std::numeric_limits<std::uint32_t>::max());

        const auto length = static_cast<std::uint32_t>(bytes);

        std::memcpy(dst, &length, sizeof(length));

        if constexpr (get_serialize_kind<T>() == SerializeKind::custom) {
            CustomFormatTraits<T>::copy_content(v, dst + sizeof(length), bytes);
        } else {
            copy_string_content(get_string_source(v), dst + sizeof(length), bytes);
        }
    }
};

template <typename T>
inline constexpr bool is_type_constexpr_size_v =
    get_serialize_kind<T>() == SerializeKind::null_pointer ||
    get_serialize_kind<T>() == SerializeKind::pointer ||
    get_serialize_kind<T>() == SerializeKind::pod;

template <bool WithTag, typename T>
constexpr std::size_t get_storage_data_size_constexpr() {
    static_assert(get_log_param_type_enum<T>() != ArgumentTag::unsupported_type,
                  "unsupported log argument");

    constexpr auto k = get_serialize_kind<T>();
    using U = std::decay_t<T>;

    if constexpr (k == SerializeKind::null_pointer) {
        return WithTag ? 4U : 0U;
    } else if constexpr (k == SerializeKind::pointer) {
        return 8U + (WithTag ? 4U : 0U);
    } else {
        static_assert(k == SerializeKind::pod, "not a fixed-size argument");

        return sizeof(U) + (WithTag ? (sizeof(U) <= 2 ? 2U : 4U) : 0U);
    }  // CR:为什么会没有tag？也就是withtag==false？ 为什么最后一个 tag还分 2，4？
}

template <bool WithTag, typename T>
std::size_t get_storage_data_size(const T& v) {
    static_assert(get_log_param_type_enum<T>() != ArgumentTag::unsupported_type,
                  "unsupported log argument");

    if constexpr (is_type_constexpr_size_v<T>) {
        (void)v;
        return get_storage_data_size_constexpr<WithTag, T>();
    } else {
        const auto content = StringStorage<T>::get_storage_data_size(v);

        assert(content <= std::numeric_limits<std::uint32_t>::max());

        return size_add(content, WithTag ? 8U : 4U);
    }
}

template <bool WithTag, typename T>
void type_copy(const T& v, std::uint8_t* dst, std::size_t raw) {
    constexpr auto tag = get_log_param_type_enum<T>();

    static_assert(tag != ArgumentTag::unsupported_type, "unsupported log argument");

    constexpr auto k = get_serialize_kind<T>();
    using U = std::decay_t<T>;

    (void)raw;

    if constexpr (WithTag) {
        assert(dst != nullptr);
        dst[0] = static_cast<std::uint8_t>(tag);
    }

    if constexpr (k == SerializeKind::null_pointer) {
        assert((raw == get_storage_data_size_constexpr<WithTag, T>()));
        (void)v;
    } else if constexpr (k == SerializeKind::pointer) {
        assert((raw == get_storage_data_size_constexpr<WithTag, T>()));

        static_assert(sizeof(std::uintptr_t) <= sizeof(std::uint64_t));

        const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(v));

        std::memcpy(dst + (WithTag ? 4U : 0U), &address, sizeof(address));
    } else if constexpr (k == SerializeKind::pod) {
        assert((raw == get_storage_data_size_constexpr<WithTag, T>()));

        constexpr auto prefix = WithTag ? (sizeof(U) <= 2 ? 2U : 4U) : 0U;

        std::memcpy(
            dst + prefix, &v,
            sizeof(
                U));  // CR:这里留下的空位是tag的吗？tag会在什么时候写入呢？/为什么不在这个时候加入
    } else {
        constexpr auto prefix = WithTag ? 4U : 0U;

        assert(raw >= prefix + sizeof(std::uint32_t));

        StringStorage<T>::type_copy(v, dst + prefix, raw - prefix);
    }
}

template <std::size_t H, std::size_t V, bool Fixed>
struct size_seq_element;

template <std::size_t H, std::size_t V>
struct size_seq_element<H, V, true> {
    static constexpr bool is_constexpr = true;

    constexpr std::size_t get_size() const {
        return V;
    }

    constexpr std::size_t get_aligned_size() const {
        return align4(V);
    }
};

template <std::size_t H, std::size_t V>
struct size_seq_element<H, V, false> {
    static constexpr bool is_constexpr = false;

    std::size_t value = 0;

    std::size_t get_size() const {
        return value;
    }

    std::size_t get_aligned_size() const {
        return align4(value);
    }
};

template <bool WithTag, typename T>
constexpr std::size_t fixed_size_or_zero() {
    if constexpr (is_type_constexpr_size_v<T>) {
        return get_storage_data_size_constexpr<WithTag, T>();
    } else {
        return 0;
    }
}
// CR: 不能直接假定三元表达式会避开所有不合法模板实例化。这里显式使用 if constexpr。 constexpr
// 为什么需要显式使用？

template <bool WithTag, typename... Ts>
struct size_seq;

template <bool WithTag>
struct size_seq<WithTag> {
    constexpr std::size_t get_total() const {
        return 0;
    }
};

template <bool WithTag, typename First, typename... Rest>
struct size_seq<WithTag, First, Rest...>
    : size_seq_element<sizeof...(Rest) + 1, fixed_size_or_zero<WithTag, First>(),
                       is_type_constexpr_size_v<First>>,
      size_seq<WithTag, Rest...> {
    using element_type = size_seq_element<sizeof...(Rest) + 1, fixed_size_or_zero<WithTag, First>(),
                                          is_type_constexpr_size_v<First>>;

    using next_type = size_seq<WithTag, Rest...>;

    element_type& get_element() {
        return *this;
    }

    const element_type& get_element() const {
        return *this;
    }

    // CR:为什么要两个版本

    next_type& get_next() {
        return *this;
    }

    const next_type& get_next() const {
        return *this;
    }

    std::size_t get_total() const {
        return size_add(get_element().get_aligned_size(), get_next().get_total());
    }
};

template <bool WithTag>
void fill_size_seq(size_seq<WithTag>&) {}

template <bool WithTag, typename First, typename... Rest>
void fill_size_seq(size_seq<WithTag, First, Rest...>& seq, const First& first,
                   const Rest&... rest) {
    static_assert(get_log_param_type_enum<First>() != ArgumentTag::unsupported_type,
                  "unsupported log argument");

    if constexpr (!is_type_constexpr_size_v<First>) {
        seq.get_element().value = get_storage_data_size<WithTag>(first);
    } else {
        (void)first;
    }

    fill_size_seq(seq.get_next(), rest...);
}

template <bool WithTag, typename... Ts>
size_seq<WithTag, Ts...> make_size_seq(const Ts&... values) {
    // CR:Ts是什么的缩写/
    size_seq<WithTag, Ts...> result;

    fill_size_seq(result, values...);
    return result;
    // CR:这是在干什么？fill_size_seq 不是自己会展开到最后一层吗?
}

template <bool WithTag>
void fill_arguments(std::uint8_t*, const size_seq<WithTag>&) {}

template <bool WithTag, typename First, typename... Rest>
void fill_arguments(std::uint8_t* dst, const size_seq<WithTag, First, Rest...>& seq,
                    const First& first, const Rest&... rest) {
    type_copy<WithTag>(first, dst, seq.get_element().get_size());

    if constexpr (sizeof...(Rest) > 0) {
        fill_arguments(dst + seq.get_element().get_aligned_size(), seq.get_next(), rest...);
    }
}

}  // namespace detail
}  // namespace qlog::record