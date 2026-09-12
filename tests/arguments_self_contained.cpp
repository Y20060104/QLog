#include <qlog/arguments.hpp>

static_assert(qlog::detail::QlogObjectPointer<int*>);
static_assert(!qlog::detail::QlogObjectPointer<void (*)()>);

constexpr auto null_cstr = qlog::cstr(nullptr);
static_assert(null_cstr.data == nullptr);
