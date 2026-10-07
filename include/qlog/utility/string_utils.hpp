#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace qlog::utility {
std::string trim(const std::string& value);

std::vector<std::string> split_nonempty(const std::string& value, char delimiter);

bool equals_ignore_case(const std::string& value, std::string_view expected);
}  // namespace qlog::utility
