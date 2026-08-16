#pragma once

#include <string_view>

namespace qlog {

[[nodiscard]] std::string_view version() noexcept;

}  // namespace qlog