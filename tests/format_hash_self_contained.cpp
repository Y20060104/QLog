#include <type_traits>

#include "qlog/detail/format_hash.hpp"
static_assert(!std::is_default_constructible_v<qlog::detail::FormatHashDispatch>);
static_assert(!std::is_aggregate_v<qlog::detail::FormatHashDispatch>);
static_assert(!std::is_default_constructible_v<qlog::detail::HashResult>);
static_assert(!std::is_aggregate_v<qlog::detail::HashResult>);
