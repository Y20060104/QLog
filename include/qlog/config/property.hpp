#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace qlog::config {

class Property {
   public:
    using hash_type = std::unordered_map<std::string, std::string>;

    using pair_type = std::pair<std::string, std::string>;

    bool load(const std::string& context);

    void set(const std::string& key, const std::string& default_value = "");

    std::string get(const std::string& key, const std::string& default_value = "");

    hash_type& maps() {
        return properties_;
    }

    std::vector<std::string>& keys() {
        return key_list_;
    }

    std::string serialize() const;

    static std::vector<pair_type> parse(const std::string& context);

   private:
    hash_type properties_;
    std::vector<std::string> key_list_;
};
}  // namespace qlog::config