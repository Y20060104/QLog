#include "qlog/utility/string_utils.hpp"

#include <cctype>
#include <cstddef>

namespace qlog::utility {
std::string trim(const std::string& value) {
    std::size_t first = 0;
    std::size_t last = value.size();

    while (first < last && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }

    while (first < last && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }

    return value.substr(first, last - first);
}

std::vector<std::string> split_nonempty(const std::string& value, char delimiter) {
    std::vector<std::string> result;
    std::size_t first = 0;

    while (true) {
        const auto found = value.find(delimiter, first);

        if (found == std::string::npos) {
            if (first < value.size()) {
                result.push_back(value.substr(first));
            }

            break;
        }
        // CR:first是什么？

        if (found > first) {
            result.push_back(value.substr(first, found - first));
        }
        // CR；substr的内容是什么
        first = found + 1;
    }

    return result;
    // CR:这个函数作用是什么？
}

bool equals_ignore_case(const std::string& value, std::string_view expected) {
    // CR：这个函数作用是什么 为什么value不与expected相等就false
    if (value.size() != expected.size()) {
        return false;
    }

    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto lhs = static_cast<unsigned char>(value[i]);
        const auto rhs = static_cast<unsigned char>(expected[i]);

        if (lhs != rhs && std::toupper(lhs) != std::toupper(rhs)) {
            return false;
        }
    }

    return true;
}

}  // namespace qlog::utility
