#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef QLOG_HASH_MAP_TEST_REFERENCE
#include "bq_common/types/hash_map.h"
#include "bq_common/types/string.h"
#else
#include "qlog/config/property.hpp"
#include "qlog/config/property_value.hpp"
#include "qlog/global/common_vars.hpp"
#include "qlog/platform/file_io.hpp"
#include "qlog/utility/hash_map.hpp"
#endif

namespace {
std::size_t checks = 0;
std::uint64_t digest = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void record(std::uint64_t value) {
    digest = (digest * 1099511628211ull) ^ value;
}
#ifdef QLOG_HASH_MAP_TEST_REFERENCE
using Key = bq::string;
template <typename K, typename V>
using Map = bq::hash_map<K, V>;
std::uint64_t string_hash(const Key& key) {
    return key.hash_code();
}
#else
using Key = std::string;
template <typename K, typename V>
using Map = qlog::utility::hash_map<K, V>;
std::uint64_t string_hash(const Key& key) {
    return qlog::utility::generic_type_hash_calculator<Key>::hash_code(key);
}
#endif
Key key_of(const std::string& text) {
    return Key(text.data(), static_cast<typename Key::size_type>(text.size()));
}
void string_cases() {
    check(string_hash(key_of("")) == 0, "empty string hash");
    check(string_hash(key_of("a")) == 97, "one character hash");
    check(string_hash(key_of(std::string(1, static_cast<char>(0x80)))) ==
              static_cast<std::uint64_t>(static_cast<char>(0x80)),
          "character signedness follows the reference");
    std::vector<std::string> corpus{"",
                                    "a",
                                    "ab",
                                    "ba",
                                    std::string("a\0b", 3),
                                    std::string("a\0", 2),
                                    "\xE4\xB8\xAD",
                                    "\xF0\x9F\x98\x80"};
    std::string all_bytes;
    for (std::uint32_t i = 0; i < 256; ++i) all_bytes += static_cast<char>(i);
    corpus.push_back(all_bytes);
    for (std::uint32_t i = 0; i < 768; ++i) {
        std::string key = "section:" + std::to_string(i) + ':';
        for (std::uint32_t j = 0; j < i % 33; ++j)
            key += static_cast<char>((i * 13 + j * 7) & 255U);
        corpus.push_back(std::move(key));
    }
    Map<Key, std::uint32_t> map;
    for (std::size_t i = 0; i < corpus.size(); ++i) {
        const auto key = key_of(corpus[i]);
        record(string_hash(key));
        const auto value = static_cast<std::uint32_t>(i + 1);
        check(map.add(key, value)->value() == value, "string key add");
    }
    check(map.size() == corpus.size(), "string map size");
    for (std::size_t i = 0; i < corpus.size(); ++i) {
        const auto it = map.find(key_of(corpus[i]));
        check(it != map.end() && it->value() == i + 1, "string key lookup after growth");
    }
    // Include iteration order in the independent QLog/BQLog differential transcript.
    for (const auto& item : map) {
        record(string_hash(item.key()));
        record(item.value());
    }
    for (std::size_t i = 0; i < corpus.size(); i += 3)
        check(map.erase(key_of(corpus[i])), "string key erase");
    for (std::size_t i = 0; i < corpus.size(); i += 3) {
        const auto key = key_of(corpus[i]);
        check(map.find(key) == map.end(), "erased string key absent");
        map[key] = static_cast<std::uint32_t>(i + 1001);
    }
    auto copied = map;
    auto moved = std::move(copied);
    check(copied.is_empty() && moved.size() == corpus.size(), "string map copy/move");
    for (std::size_t i = 0; i < corpus.size(); ++i) {
        const auto it = moved.find(key_of(corpus[i]));
        const auto expected = i % 3 == 0 ? i + 1001 : i + 1;
        check(it != moved.end() && it->value() == expected, "string map copied values");
        record(it->value());
    }
}
#ifndef QLOG_HASH_MAP_TEST_REFERENCE
void property_cases() {
    using qlog::config::Property;
    using qlog::config::PropertyValue;
    static_assert(std::is_same_v<Property::hash_type, Map<std::string, std::string>>);
    static_assert(std::is_same_v<PropertyValue::object_type,
                                 Map<std::string, std::unique_ptr<PropertyValue>>>);
    Property flat;
    check(flat.load("root.name=first\nroot.flag=true\nroot.count=17\nroot.name=second\n"
                    "# comment\nunfinished=tail\\"),
          "load valid prefix");
    check(flat.keys() == std::vector<std::string>({"root.name", "root.flag", "root.count"}),
          "duplicate key keeps first key-list position");
    check(flat.get("root.name") == "second" && flat.get("unfinished", "absent") == "absent",
          "duplicate overwrite and unfinished EOF item");
    flat.set("escaped", "first\nsecond=third:fourth");
    Property restored;
    check(restored.load(flat.serialize()), "flat roundtrip load");
    check(restored.get("escaped") == flat.get("escaped"), "flat escaped value roundtrip");
    for (std::uint32_t i = 0; i < 768; ++i)
        flat.set("key-" + std::to_string(i), "value-" + std::to_string(i));
    for (std::uint32_t i = 0; i < 768; ++i)
        check(flat.get("key-" + std::to_string(i)) == "value-" + std::to_string(i),
              "flat lookup after node/bucket growth");
    auto flat_copy = flat;
    flat_copy.set("root.name", "copy");
    check(flat.get("root.name") == "second", "flat map copy owns strings");
    auto parsed = PropertyValue::create_from_string(
        "root.name=first\nroot.flag=true\nroot.count=17\nroot.name=second\n"
        "unfinished=tail\\");
    check(static_cast<std::string>(parsed["root"]["name"]) == "second", "tree maps iteration");
    check(static_cast<bool>(parsed["root"]["flag"]), "tree bool");
    check(static_cast<std::int64_t>(parsed["root"]["count"]) == 17, "tree integral");
    check(!parsed.has_object_key("unfinished"), "tree skips unfinished EOF item");
    PropertyValue tree;
    auto& pinned = tree["pinned"];
    pinned = "seed";
    const auto* pinned_address = &pinned;
    for (std::uint32_t i = 0; i < 768; ++i)
        tree["child-" + std::to_string(i)] = static_cast<std::int64_t>(i);
    check(&tree["pinned"] == pinned_address, "unique_ptr child address survives node growth");
    pinned = "after-growth";
    check(static_cast<std::string>(tree["pinned"]) == "after-growth", "saved child reference");
    tree["nested"]["array"].add_array_item(std::int64_t{7});
    tree["nested"]["array"].add_array_item("text");
    auto copied = tree;
    copied["pinned"] = "copy";
    copied["nested"]["array"][PropertyValue::array_type::size_type{0}] = std::int64_t{99};
    check(&copied["pinned"] != pinned_address, "deep copy creates independent child");
    check(static_cast<std::string>(tree["pinned"]) == "after-growth", "deep copy string");
    check(static_cast<std::int64_t>(
              tree["nested"]["array"][PropertyValue::array_type::size_type{0}]) == 7,
          "deep copy nested array");
    PropertyValue assigned;
    assigned = true;
    assigned = tree;
    check(assigned.object_size() == tree.object_size(), "deep copy assignment replaces scalar");
    auto moved = std::move(tree);
    check(tree.is_object() && tree.object_size() == 0, "moved-from object remains destructible");
    check(&moved["pinned"] == pinned_address, "tree move retains child address");
    for (std::uint32_t i = 0; i < 768; ++i)
        check(static_cast<std::int64_t>(moved["child-" + std::to_string(i)]) == i,
              "tree lookup after move");
    const auto& const_moved = moved;
    check(static_cast<std::string>(const_moved["pinned"]) == "after-growth",
          "const iterator value");
    moved.erase_object_item("child-1");
    check(!moved.has_object_key("child-1"), "tree key erase");
    auto keys = moved.get_object_key_set();
    std::sort(keys.begin(), keys.end());
    check(keys.size() == moved.object_size() &&
              std::adjacent_find(keys.begin(), keys.end()) == keys.end(),
          "object key set contains every key once");
    auto roundtrip = PropertyValue::create_from_string(parsed.serialize());
    check(static_cast<std::string>(roundtrip["root"]["name"]) == "second",
          "tree serialization after container migration");
}
struct TemporaryFile {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("qlog-hash-map-registry-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp");
    qlog::platform::platform_file_handle first = qlog::platform::invalid_platform_file_handle;
    qlog::platform::platform_file_handle second = qlog::platform::invalid_platform_file_handle;
    ~TemporaryFile() {
        if (qlog::platform::is_platform_handle_valid(first)) qlog::platform::close_file(first);
        if (qlog::platform::is_platform_handle_valid(second)) qlog::platform::close_file(second);
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
void registry_cases() {
    using namespace qlog::platform;
    static_assert(std::is_same_v<decltype(qlog::common_global_vars::file_exclusive_cache_),
                                 Map<file_node_info, file_open_mode_enum>>);
    auto& cache = qlog::common_global_vars::get().file_exclusive_cache_;
    const auto before = cache.size();
    TemporaryFile file;
    const std::string path = file.path.string();
    const auto exclusive = file_open_mode_enum::write | file_open_mode_enum::auto_create |
                           file_open_mode_enum::exclusive;
    check(open_file(path.c_str(), exclusive, file.first) == 0, "first exclusive open");
    check(cache.size() == before + 1, "exclusive registry entry added");
    check(open_file(path.c_str(), file_open_mode_enum::write, file.second) != 0,
          "competing writer rejected");
    check(!is_platform_handle_valid(file.second), "rejected handle invalidated");
    check(cache.size() == before + 1, "rejection keeps existing registry entry");
    check(close_file(file.first) == 0 && cache.size() == before, "close erases registry entry");
    check(open_file(path.c_str(), exclusive, file.first) == 0, "exclusive reopen after erase");
    check(close_file(file.first) == 0 && cache.size() == before, "reopen cleanup");
}
#endif
}  // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "expected test group");
        const std::string_view group = argv[1];
        if (group == "strings") string_cases();
#ifndef QLOG_HASH_MAP_TEST_REFERENCE
        else if (group == "properties")
            property_cases();
        else if (group == "registry")
            registry_cases();
#endif
        else
            throw std::runtime_error("unknown group");
        std::cout << "PASS " << group << " checks=" << checks << " digest=" << digest << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
