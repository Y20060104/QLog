#include "qlog/config/property.hpp"

#include <cassert>
#include <cstddef>
#include <string_view>

#include "qlog/utility/string_utils.hpp"

namespace qlog::config {
void Property::set(const std::string& key, const std::string& default_value) {
    if (properties_.find(key) == properties_.end()) {
        key_list_.push_back(key);
    }

    properties_[key] = default_value;
}

std::string Property::get(const std::string& key, const std::string& default_value) {
    const auto it = properties_.find(key);

    if (it != properties_.end()) {
        return it->second;
    }

    return default_value;
}

std::string Property::serialize() const {
    std::string lines;

    for (const auto& [key, value] : properties_) {
        lines += key;
        lines += '=';

        for (const char c : value) {
            switch (c) {
                case '\n':
                    lines += "\\n";
                    break;

                case '=':
                    lines += "\\=";
                    break;

                case ':':
                    lines += "\\:";
                    break;  // CR:为什么= ：要+\\？

                default:
                    lines += c;
                    break;
            }
        }

        lines += '\n';
    }
    return lines;
}

namespace {
std::string replace_all(const std::string& value, std::string_view from, std::string_view to) {
    assert(!from.empty());

    std::string result;
    result.reserve(value.size());

    std::size_t cursor = 0;

    while (true) {
        const auto found = value.find(from, cursor);

        if (found == std::string::npos) {
            // npos是什么？
            result.append(value, cursor, std::string::npos);
            break;
        }

        result.append(value, cursor, found - cursor);
        result.append(to);  // 这是什么 意思? from为什么是固定非空串 要用来替换什么 value
                            // 为什么要加在found-cursor appende to是什么

        cursor = found + from.size();
    }

    return result;
}

std::vector<std::string> find_split(const std::string& line, char split_char) {
    std::vector<std::string> kv;

    for (std::size_t i = 1; i + 1 < line.size(); ++i) {
        if (line[i] == split_char && line[i - 1] != '\\') {
            kv.push_back(qlog::utility::trim(line.substr(0, i)));
            kv.push_back(qlog::utility::trim(line.substr(i + 1)));
            break;
        }
    }

    return kv;
}

}  // namespace

std::vector<Property::pair_type> Property::parse(const std::string& context) {
    std::vector<pair_type> vv;

    std::string file_context = replace_all(context, "\r\n", "\n");
    file_context = replace_all(file_context, "\t", "");

    auto lines = qlog::utility::split_nonempty(file_context, '\n');

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto line = qlog::utility::trim(lines[i]);

        if (line.empty() || line[0] == '#' || line[0] == '!') {
            continue;  //! 为什么要跳过
        }

        auto kv = find_split(line, '=');
        if (kv.size() < 2) {
            kv = find_split(line, ':');
        }

        if (kv.size() != 2) {
            continue;
        }
        // 为什么!=2 要继续？

        std::string key = qlog::utility::trim(kv[0]);
        std::string value = qlog::utility::trim(kv[1]);

        if (value.empty()) {
            continue;
        }

        bool unfinished = false;

        while (!value.empty() && value.back() == '\\') {
            if (i + 1 >= lines.size()) {
                unfinished = true;
                break;
            }

            ++i;
            value.pop_back();
            value += qlog::utility::trim(lines[i]);
        }

        if (unfinished) {
            continue;
        }

        value = replace_all(value, "\\n", "\n");
        value = replace_all(value, "\\=", "=");
        value = replace_all(value, "\\:", ":");

        vv.emplace_back(std::move(key), std::move(value));
    }
    return vv;
}

bool Property::load(const std::string& context) {
    auto vv = parse(context);

    for (const auto& [key, value] : vv) {
        set(key, value);
    }

    return !properties_.empty();
}

}  // namespace qlog::config