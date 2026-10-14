#pragma once

#include "qlog/platform/atomic/atomic.hpp"

#if defined(QLOG_MSVC)

#define QLOG_PACK_BEGIN __pragma(pack(push, 1))

#define QLOG_PACK_END \
    ;                 \
    __pragma(pack(pop))

#else

#define QLOG_PACK_BEGIN

#define QLOG_PACK_END __attribute__((__packed__));

#endif

#if defined(__cplusplus) && (__cplusplus >= 201402L)
#define QLOG_CPP_14 1
#define QLOG_FUNC_RETURN_CONSTEXPR constexpr
#else
#define QLOG_FUNC_RETURN_CONSTEXPR
#endif

#if defined(__cplusplus) && (__cplusplus >= 201703L)
#define QLOG_CPP_17 1
#define QLOG_CONSTEXPR_IF if constexpr
#else
#define QLOG_CONSTEXPR_IF if
#endif

#if defined(__cplusplus) && (__cplusplus >= 202002L)
#define QLOG_CPP_20 1
#endif

#if defined(QLOG_MSVC) || defined(QLOG_VISUAL_STUDIO)
#define QLOG_TLS __declspec(thread)
#else
#define QLOG_TLS __thread
#endif

#define QLOG_TLS_CONCAT_INNER(a, b) a##b
#define QLOG_TLS_CONCAT(a, b) QLOG_TLS_CONCAT_INNER(a, b)

#define QLOG_TLS_DEFINE(Type, Name)                                              \
    QLOG_TLS Type* ____QLOG_TLS_##Name##_ptr;                                    \
    QLOG_TLS bool ____QLOG_TSL_##Name##_recycled;                                \
    struct QLOG_TLS_CONCAT(_qlog_non_pod_holder_##Name##_, __LINE__) {           \
        QLOG_TLS_CONCAT(_qlog_non_pod_holder_##Name##_, __LINE__)() {}           \
        ~QLOG_TLS_CONCAT(_qlog_non_pod_holder_##Name##_, __LINE__)() {           \
            if (____QLOG_TLS_##Name##_ptr) {                                     \
                delete ____QLOG_TLS_##Name##_ptr;                                \
                ____QLOG_TLS_##Name##_ptr = nullptr;                             \
                ____QLOG_TSL_##Name##_recycled = true;                           \
            }                                                                    \
        }                                                                        \
        qlog_forceinline operator bool() {                                       \
            return !____QLOG_TSL_##Name##_recycled;                              \
        }                                                                        \
        qlog_forceinline Type& get() {                                           \
            if (!____QLOG_TLS_##Name##_ptr) {                                    \
                ____QLOG_TLS_##Name##_ptr = new Type();                          \
            }                                                                    \
            return *____QLOG_TLS_##Name##_ptr;                                   \
        }                                                                        \
    };                                                                           \
    thread_local QLOG_TLS_CONCAT(_qlog_non_pod_holder_##Name##_, __LINE__) Name; \
    qlog_forceinline Type& QLOG_TLS_CONCAT(Name, _get_direct)() {                \
        Type* ____p = ____QLOG_TLS_##Name##_ptr;                                 \
        if (____p) {                                                             \
            return *____p;                                                       \
        }                                                                        \
        return Name.get();                                                       \
    }

#define QLOG_TLS_NON_POD(Type, Name) QLOG_TLS_DEFINE(Type, Name)

#if (defined(QLOG_CLANG) || defined(QLOG_GCC)) && defined(__has_builtin)
#define QLOG_GCC_CLANG_BUILTIN(X) __has_builtin(X)
#else
#define QLOG_GCC_CLANG_BUILTIN(X) 0
#endif

#if defined(QLOG_CPP_20)
#define QLOG_LIKELY_IF(expr) if (expr) [[likely]]
#define QLOG_UNLIKELY_IF(expr) if (expr) [[unlikely]]
#define QLOG_LIKELY_DEFINED
#elif defined(QLOG_GCC_CLANG_BUILTIN)
#if QLOG_GCC_CLANG_BUILTIN(__builtin_expect)
#define QLOG_LIKELY_IF(expr) if (__builtin_expect(!!(expr), 1))
#define QLOG_UNLIKELY_IF(expr) if (__builtin_expect(!!(expr), 0))
#define QLOG_LIKELY_DEFINED
#endif
#endif

#ifndef QLOG_LIKELY_DEFINED
#define QLOG_LIKELY_IF(expr) if (expr)
#define QLOG_UNLIKELY_IF(expr) if (expr)
#endif

#if defined(QLOG_GCC)

#define QLOG_SUPPRESS_MAYBE_UNINITIALIZED_BEGIN() \
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")

#define QLOG_SUPPRESS_MAYBE_UNINITIALIZED_END() _Pragma("GCC diagnostic pop")

#elif defined(QLOG_MSVC)

#define QLOG_SUPPRESS_MAYBE_UNINITIALIZED_BEGIN() \
    __pragma(warning(push)) __pragma(warning(disable : 4700)) __pragma(warning(disable : 4701))

#define QLOG_SUPPRESS_MAYBE_UNINITIALIZED_END() __pragma(warning(pop))

#else

#define QLOG_SUPPRESS_MAYBE_UNINITIALIZED_BEGIN()
#define QLOG_SUPPRESS_MAYBE_UNINITIALIZED_END()

#endif

#if defined(QLOG_GCC) || defined(QLOG_CLANG)

#define QLOG_SUPPRESS_NULL_DEREF_BEGIN() \
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wnull-dereference\"")

#define QLOG_SUPPRESS_NULL_DEREF_END() _Pragma("GCC diagnostic pop")

#elif defined(QLOG_MSVC)

#define QLOG_SUPPRESS_NULL_DEREF_BEGIN() \
    __pragma(warning(push)) __pragma(warning(disable : 6011)) __pragma(warning(disable : 6387))

#define QLOG_SUPPRESS_NULL_DEREF_END() __pragma(warning(pop))

#else

#define QLOG_SUPPRESS_NULL_DEREF_BEGIN()
#define QLOG_SUPPRESS_NULL_DEREF_END()

#endif