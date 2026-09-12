#include <qlog/detail/argument_traits.hpp>

static_assert(qlog::detail::SupportedArgument<int>);
static_assert(!qlog::detail::SupportedArgument<const char*>);
