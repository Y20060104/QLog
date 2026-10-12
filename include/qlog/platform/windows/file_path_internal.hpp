#pragma once

#include <string>

namespace qlog::platform::windows {

std::u16string trans_to_windows_wide_string(const std::string utf8_str);

}  // namespace qlog::platform::windows