#pragma once

#define QLOG_HASH_MAP_INLINE inline
#define QLOG_HASH_MAP_INLINE_MACRO(X) X

#define QLOG_HASH_MAP_CLS_NAME QLOG_HASH_MAP_INLINE_MACRO(hash_map)

#define QLOG_HASH_MAP_KV_CLS_NAME QLOG_HASH_MAP_INLINE_MACRO(kv_pair)

#define QLOG_HASH_MAP_ITER_CLS_NAME QLOG_HASH_MAP_INLINE_MACRO(hash_map_iterator)

#include "qlog/utility/hash_map_impl.hpp"

#undef QLOG_HASH_MAP_INLINE
#undef QLOG_HASH_MAP_INLINE_MACRO

#define QLOG_HASH_MAP_INLINE qlog_forceinline
#define QLOG_HASH_MAP_INLINE_MACRO(X) X##_inline

#include "qlog/utility/hash_map_impl.hpp"

#undef QLOG_HASH_MAP_CLS_NAME
#undef QLOG_HASH_MAP_KV_CLS_NAME
#undef QLOG_HASH_MAP_ITER_CLS_NAME
#undef QLOG_HASH_MAP_INLINE_MACRO
#undef QLOG_HASH_MAP_INLINE