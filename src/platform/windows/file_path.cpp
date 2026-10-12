#include "qlog/platform/file_path.hpp"

#include <direct.h>
#include <windows.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <vector>

#include "qlog/platform/windows/file_path_internal.hpp"
#include "qlog/utility/string_utils.hpp"
#include "qlog/utility/utf_conversion.hpp"

namespace qlog::platform {
bool is_absolute(const std::string& path) {
    const char first_char = path.empty() ? '\0' : path[0];

    if (first_char == '/' || first_char == '\\') {
        return true;
    }

    if (path.size() >= 2 && path[1] == ':' &&
        ((first_char >= 'A' && first_char <= 'Z') || (first_char >= 'a' && first_char <= 'z'))) {
        return true;
    }

    return false;
}

std::string get_lexically_path(const std::string& original_path) {
    std::string result;
    result.reserve(original_path.size());

    std::string trans_path = original_path;
    std::replace(trans_path.begin(), trans_path.end(), '\\', '/');

    const bool end_with_slash = !trans_path.empty() && trans_path.back() == '/';

    std::vector<std::string> split = qlog::utility::split_nonempty(trans_path, '/');
    // CR:split赋值为什么？
    std::vector<std::string> result_split;
    result_split.reserve(split.size());

    for (std::size_t i = 0; i < split.size(); ++i) {
        split[i] = qlog::utility::trim(split[i]);

        if (split[i] == "..") {
            if (!result_split.empty() && result_split.back() != "..") {
                result_split.pop_back();  // CR:不同就popback是为什么？
            } else {
                result_split.push_back(split[i]);
            }
        } else if (split[i] == ".") {
        } else {
            result_split.push_back(split[i]);
        }
    }

    for (std::size_t i = 0; i < result_split.size(); ++i) {
        if (i != 0) {
            result += '/';
        }
        result += result_split[i];
    }

    if (result.empty()) {
        result = "./";
    }

    if (!original_path.empty() && original_path[0] == '/') {
        result = "/" + result;
    }

    if (end_with_slash) {
        result += '/';
    }

    return result;
}

base_dir_initializer::base_dir_initializer() {
    static_assert(sizeof(char16_t) == sizeof(WCHAR), "WCHAR must be 16bits on WIndows Platform!");

    const char16_t* wpath = reinterpret_cast<const char16_t*>(::_wgetcwd(nullptr, 0));
    std::uint32_t wpath_len = static_cast<std::uint32_t>(
        std::wcslen(reinterpret_cast<LPCWSTR>(wpath)));  // LPCWSTR这是什么

    std::string base_dir;
    base_dir.resize(static_cast<std::size_t>(wpath_len) * 3 + 2);

    std::size_t utf8_len = qlog::utility::utf16_to_utf8_sw(
        wpath, wpath_len, base_dir.data(), static_cast<std::uint32_t>(base_dir.size()));

    assert(utf8_len < base_dir.size() && "base_dir utf16_to_utf8 size error!");

    base_dir.resize(utf8_len);
    set_base_dir_0(base_dir);
    set_base_dir_1(base_dir);
}

}  // namespace qlog::platform

namespace qlog::platform::windows {
std::u16string trans_to_windows_wide_string(const std::string utf8_str) {
    std::u16string result;
    if (utf8_str.empty()) {
        return result;
    }

    result.resize(utf8_str.size() + 1U);

    const std::uint32_t trans_size = qlog::utility::utf8_to_utf16_sw(
        utf8_str.data(), static_cast<std::uint32_t>(utf8_str.size()), result.data(),
        static_cast<std::uint32_t>(result.size()));

    assert(trans_size < result.size());

    result.resize(trans_size);

    std::replace(result.begin(), result.end(), u'/', u'\\');

    // CR:replace怎么用的？
    return result;
}
}  // namespace qlog::platform::windows