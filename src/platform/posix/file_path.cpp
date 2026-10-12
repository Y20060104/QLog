#include "qlog/platform/file_path.hpp"

#include <dirent.h>
#include <pwd.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "qlog/utility/diagnostics.hpp"
#include "qlog/utility/string_utils.hpp"

namespace qlog::platform {
bool is_absolute(const std::string& path) {
    return !path.empty() && (path[0] == '/' || path[0] == '~');
}

base_dir_initializer::base_dir_initializer() {
    std::vector<char> tmp;
    tmp.resize(1024);

    while (::getcwd(tmp.data(), tmp.size()) == nullptr) {
        tmp.resize(tmp.size() + 1024);
    }

    set_base_dir_0(tmp.data());
    set_base_dir_1(tmp.data());
}
std::string get_lexically_path(const std::string& original_path) {
    std::string result;
    result.reserve(original_path.size());

    const bool end_with_slash = !original_path.empty() && original_path.back() == '/';

    std::vector<std::string> split = qlog::utility::split_nonempty(original_path, '/');
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

#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
    if (!result.empty() && result[0] == '~') {
        if (result.size() == 1 || result[1] == '/') {
            auto home_dir_c_str = std::getenv("HOME");
            if (!home_dir_c_str) {
                home_dir_c_str = ::getpwuid(::getuid())->pw_dir;
            }

            result.erase(0, 1);
            std::string home_dir = home_dir_c_str;
            if (!home_dir.empty() && home_dir.back() == '/') {
                home_dir.erase(home_dir.end() - 1);
            }
            result = home_dir + result;
        } else {
            auto slash_index = result.find('/');
            std::string username = slash_index == std::string::npos
                                       ? result.substr(1)
                                       : result.substr(1, slash_index - 1);

            struct passwd* pw = ::getpwnam(username.c_str());
            if (!pw) {
                qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                        "unknown user:%s, when parsing path:%s",
                                                        username.c_str(), original_path.c_str());
            } else if (slash_index == std::string::npos) {
                result = pw->pw_dir;
            } else {
                std::string pw_dir = pw->pw_dir;
                if (pw_dir.empty() || pw_dir.back() != '/') {
                    result = pw_dir + result.substr(slash_index + 1);
                } else {
                    result = pw_dir + result.substr(slash_index);
                }
            }
        }
    }
#endif

    if (end_with_slash) {
        result += '/';
    }

    return result;
}

}  // namespace qlog::platform