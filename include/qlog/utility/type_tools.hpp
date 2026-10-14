#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "qlog/platform/atomic/atomic.hpp"
#include "qlog/utility/type_traits.hpp"

namespace qlog::utility {

template <typename TO>
qlog_forceinline TO& __qlog_macro_force_cast_ignore_alignment_warning(const char* from) {
    return *reinterpret_cast<TO*>(const_cast<char*>(from));
}

enum class enum_new_dummy { dummy };
}  // namespace qlog::utility

#define QLOG_PACK_ACCESS_BY_TYPE(Var, Type) \
    qlog::utility::__qlog_macro_force_cast_ignore_alignment_warning<Type>((const char*)&Var)

#define QLOG_PACK_ACCESS(Var)                                                       \
    qlog::utility::__qlog_macro_force_cast_ignore_alignment_warning<decltype(Var)>( \
        (const char*)&Var)

inline void* operator new(std::size_t, void* p, qlog::utility::enum_new_dummy) noexcept {
    return p;
}

inline void* operator new[](std::size_t, void* p, qlog::utility::enum_new_dummy) noexcept {
    return p;
}

inline void operator delete(void*, void*, qlog::utility::enum_new_dummy) noexcept {}

inline void operator delete[](void*, void*, qlog::utility::enum_new_dummy) noexcept {}

namespace qlog::utility {
template <bool COND, typename T1, typename T2>
struct condition_type {
    typedef T1 type;
};

template <typename T1, typename T2>
struct condition_type<false, T1, T2> {
    typedef T2 type;
};

template <bool COND, typename T1, typename T2>
using condition_type_t = typename condition_type<COND, T1, T2>::type;

template <typename T>
struct integer_type_hash_calculator {
    typedef std::uint64_t hash_value_type;
    static inline hash_value_type hash_code(const T& value) {
        return static_cast<hash_value_type>(value);
    }
};

template <typename T>
struct decimal_type_hash_calculator {
    typedef std::uint64_t hash_value_type;

    union double_uint64_union_ {
       public:
        double d_value;
        std::uint64_t i_value;
    };

    static inline hash_value_type hash_code(const T& value) {
        static_assert(sizeof(hash_value_type) == sizeof(double), "wrong decimal type hash_code");

        static_assert(sizeof(double_uint64_union_) == sizeof(double),
                      "wrong double_uint64_union_ size");

        double_uint64_union_ value_tmp;
        value_tmp.d_value = (double)(value);

        return value_tmp.i_value;
    }
};

template <typename T, typename hash_value_type,
          qlog::utility::enable_if_t<qlog::utility::is_same<T, float>::value ||
                                         qlog::utility::is_same<T, double>::value,
                                     bool> = true>
inline hash_value_type __inner_hash_calculate(const T& value) {
    return decimal_type_hash_calculator<T>::hash_code(value);
}

template <typename T, typename hash_value_type,
          qlog::utility::enable_if_t<!(qlog::utility::is_same<T, float>::value ||
                                       qlog::utility::is_same<T, double>::value),
                                     bool> = true>
inline hash_value_type __inner_hash_calculate(const T& value) {
    return integer_type_hash_calculator<T>::hash_code(value);
}

template <typename T>
struct number_type_hash_calculator {
    typedef std::uint64_t hash_value_type;

    static inline hash_value_type hash_code(const T& value) {
        return __inner_hash_calculate<T, hash_value_type>(value);
    }
};

template <typename T>
struct pointer_type_hash_calculator {
    typedef std::uint64_t hash_value_type;

    static inline hash_value_type hash_code(const T& value) {
        static_assert(
            qlog::utility::is_pointer<T>::value || qlog::utility::is_null_pointer<T>::value,
            "wrong pointer type hash_code");

        std::uintptr_t ptr_value = reinterpret_cast<std::uintptr_t>(value);

        hash_value_type final_value = static_cast<hash_value_type>(ptr_value);

        return final_value;
    }
};

template <typename T>
struct generic_type_hash_calculator {
    typedef std::uint64_t hash_value_type;

    static inline hash_value_type hash_code(const T& value) {
        return static_cast<hash_value_type>(value.hash_code());
    }
};

// std::string keeps the bq::string::get_hash rule for original HashMap keys.
template <>
struct generic_type_hash_calculator<std::string> {
    typedef std::uint64_t hash_value_type;

    static inline hash_value_type hash_code(const std::string& value) {
        hash_value_type hash = 0;
        for (std::string::size_type i = 0; i < value.size(); ++i) {
            hash *= 1099511628211ull;
            hash ^= static_cast<hash_value_type>(value[i]);
        }
        return hash;
    }
};

template <typename T>
struct object_constructor {
   public:
    template <typename... Args>
    static inline void construct(T* _ptr, Args&&... _args) {
        new (_ptr, qlog::utility::enum_new_dummy::dummy) T(qlog::utility::forward<Args>(_args)...);
    }
};

template <typename T>
struct object_destructor {
   private:
    struct trivial_destructor_type {};
    struct non_trivial_destructor_type {};

    template <typename U, typename destructor_type>
    struct destructor_impl;

    template <typename U>
    struct destructor_impl<U, trivial_destructor_type> {
        static inline void destruct(U*) {}
    };

    template <typename U>
    struct destructor_impl<U, non_trivial_destructor_type> {
        static inline void destruct(U* _ptr) {
            if (_ptr) {
                _ptr->~T();
            }
        }
    };

   public:
    static inline void destruct(T* _ptr) {
        using destructor_type = typename qlog::utility::condition_type<
            qlog::utility::is_trivially_destructible<T>::value, trivial_destructor_type,
            non_trivial_destructor_type>::type;

        destructor_impl<T, destructor_type>::destruct(_ptr);
    }
};

template <typename T>
struct object_array_destructor {
   private:
    struct trivial_destructor_type {};
    struct non_trivial_destructor_type {};

    template <typename U, typename destructor_type>
    struct destructor_impl;

    template <typename U>
    struct destructor_impl<U, trivial_destructor_type> {
        static inline void destruct(U*, std::size_t) {}
    };

    template <typename U>
    struct destructor_impl<U, non_trivial_destructor_type> {
        static inline void destruct(U* _ptr, std::size_t _count) {
            if (_ptr) {
                for (std::size_t i = 0; i < _count; ++i) {
                    object_destructor<T>::destruct(_ptr + i);
                }
            }
        }
    };

   public:
    static inline void destruct(T* _ptr, std::size_t count) {
        using destructor_type = typename qlog::utility::condition_type<
            qlog::utility::is_trivially_destructible<T>::value, trivial_destructor_type,
            non_trivial_destructor_type>::type;

        destructor_impl<T, destructor_type>::destruct(_ptr, count);
    }
};

template <typename T>
inline T roundup_pow_of_two(T x) {
    if ((x & (x - 1)) == 0) {
        return x;
    }

    T result = 1;
    while (x > 0) {
        x = x >> 1;
        result = result << 1;
    }
    return result;
}

template <typename T>
qlog_forceinline T* launder(T* p) noexcept {
#if defined(QLOG_MSVC)
#if defined(QLOG_CPP_17)
    return __builtin_launder(p);
#else
    T* result = p;
    _ReadWriteBarrier();
    return result;
#endif
#elif QLOG_GCC_CLANG_BUILTIN(__builtin_launder)
    return __builtin_launder(p);
#elif defined(QLOG_GCC)
    T* result = p;
    __asm__ __volatile__("" : "+r"(result) : : "memory");
    return result;
#else
    T* result = p;
    __asm__ __volatile__("" : "+r"(result) : : "memory");
    return result;
#endif
}
}  // namespace qlog::utility
