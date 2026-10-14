#if defined(QLOG_HASH_MAP_TEST_REFERENCE)
#include "bq_common/types/hash_map.h"
namespace tested = bq;
#else
#include "qlog/utility/hash_map.hpp"
namespace tested = qlog::utility;
#endif

#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
struct checks {
    std::uint64_t count = 0;
    std::uint64_t digest = 1469598103934665603ULL;

    void expect(bool condition, std::string_view message) {
        ++count;
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    void observe(std::uint64_t value) {
        digest ^= value;
        digest *= 1099511628211ULL;
    }
};

struct all_collide_key {
    int id;
    std::uint64_t hash_code() const {
        return 0;
    }
    bool operator==(const all_collide_key& rhs) const {
        return id == rhs.id;
    }
};

struct collision_key {
    int id;
    std::uint64_t hash_code() const {
        return static_cast<std::uint64_t>(id) * 128;
    }
    bool operator==(const collision_key& rhs) const {
        return id == rhs.id;
    }
};

struct generic_key {
    int id;
    std::uint64_t hash_code() const {
        return static_cast<std::uint64_t>(id);
    }
    bool operator==(const generic_key& rhs) const {
        return id == rhs.id;
    }
};

struct tracked_value {
    static inline int live = 0;
    static inline int constructed = 0;
    static inline int destroyed = 0;
    std::string text;

    explicit tracked_value(std::string value = {}) : text(std::move(value)) {
        ++live;
        ++constructed;
    }
    tracked_value(const tracked_value& rhs) : text(rhs.text) {
        ++live;
        ++constructed;
    }
    tracked_value(tracked_value&& rhs) noexcept : text(std::move(rhs.text)) {
        ++live;
        ++constructed;
    }
    tracked_value& operator=(const tracked_value& rhs) {
        text = rhs.text;
        return *this;
    }
    tracked_value& operator=(tracked_value&& rhs) noexcept {
        text = std::move(rhs.text);
        return *this;
    }
    ~tracked_value() {
        --live;
        ++destroyed;
    }
};

template <typename K, typename V>
using ordinary_map = tested::hash_map<K, V>;

template <typename K, typename V>
using inlined_map = tested::hash_map_inline<K, V>;

template <template <typename, typename> class Family>
void basic(checks& result) {
    Family<std::uint32_t, std::string> map;
    result.expect(map.is_empty() && map.size() == 0, "new map must be empty");
    result.expect(map.get_expand_rate() == 1.5F, "original default expansion rate");
    result.expect(map.begin() == map.end(), "empty iteration");
    result.expect(!static_cast<bool>(map.find(99U)), "missing lookup");
    map.set_expand_rate(4);

    for (std::uint32_t i = 0; i < 96; ++i) {
        map[i] = std::to_string(i);
    }
    map[17U] = "updated";
    result.expect(map.size() == 96, "operator[] must not duplicate a key");

    const auto& view = map;
    for (std::uint32_t i = 0; i < 96; ++i) {
        auto iter = view.find(i);
        result.expect(static_cast<bool>(iter), "const lookup must find every key");
        result.expect(iter->value() == (i == 17 ? "updated" : std::to_string(i)),
                      "lookup must preserve values during growth");
    }
    auto first = map.begin();
    auto old = first++;
    result.expect(old == map.begin(), "postincrement returns the old iterator");
    auto& advanced = ++first;
    result.expect(&advanced == &first, "preincrement returns itself");
    typename decltype(map)::const_iterator converted(old);
    converted = old;
    const auto frozen = converted;
    result.expect((*frozen).key() == old->key(), "const iterator dereference");
    result.expect(frozen->value() == (*old).value(), "const iterator arrow");
    result.expect(map.end() == view.end(), "mutable/const end comparison");

    const auto bucket_count = map.buckets_size();
    const auto node_count = map.nodes_size();
    result.observe(bucket_count);
    result.observe(node_count);
    map.clear();
    result.expect(map.is_empty() && map.begin() == map.end(), "clear empties the map");
    result.expect(map.buckets_size() == bucket_count && map.nodes_size() == node_count,
                  "clear retains storage capacity");
    map.add(701U, std::string("reused"));
    result.expect(map[701U] == "reused", "reuse after clear");
    map.reset();
    result.expect(map.is_empty() && map.buckets_size() == 0 && map.nodes_size() == 0,
                  "reset releases both storage arrays");
    map[1U] = "after reset";
    result.expect(map[1U] == "after reset", "reuse after reset");

    alignas(128) int keys[3] = {1, 2, 3};
    Family<const int*, int> pointer_map;
    for (int i = 0; i < 3; ++i) {
        pointer_map[&keys[i]] = i + 10;
    }
    for (int i = 0; i < 3; ++i) {
        result.expect(pointer_map[&keys[i]] == i + 10, "pointer key lookup");
    }

    Family<float, int> floating_map;
    floating_map[0.125F] = 1;
    floating_map[0.25F] = 2;
    result.expect(floating_map[0.125F] == 1 && floating_map[0.25F] == 2,
                  "original floating key hashing");

    Family<generic_key, int> member_hash_map;
    member_hash_map[generic_key{3}] = 91;
    result.expect(member_hash_map[generic_key{3}] == 91, "member hash_code lookup");
}

template <typename Map>
void verify_collision_map(const Map& map, const std::map<int, int>& expected, checks& result) {
    result.expect(map.size() == expected.size(), "collision map size");
    std::map<int, int> observed;
    std::size_t visited = 0;
    for (auto iter = map.begin(); iter != map.end(); ++iter) {
        ++visited;
        result.expect(visited <= expected.size(), "iteration must terminate without cycles");
        result.expect(observed.emplace(iter->key().id, iter->value()).second,
                      "iteration must visit every key once");
    }
    result.expect(observed == expected, "iteration content must match the independent model");
    for (const auto& item : expected) {
        auto iter = map.find(typename Map::key_type{item.first});
        result.expect(static_cast<bool>(iter), "collision lookup");
        result.expect(iter->value() == item.second, "collision value");
        result.observe(static_cast<std::uint64_t>(item.first));
        result.observe(static_cast<std::uint64_t>(item.second));
    }
    result.observe(map.size());
    result.observe(map.buckets_size());
    result.observe(map.nodes_size());
}

template <template <typename, typename> class Family, typename Key>
void collision_case(checks& result) {
    Family<Key, int> map;
    std::map<int, int> expected;
    map.set_expand_rate(4);
    for (int i = 0; i < 192; ++i) {
        map.add(Key{i}, i * 7);
        expected[i] = i * 7;
        if (i % 17 == 0) {
            verify_collision_map(map, expected, result);
        }
    }
    verify_collision_map(map, expected, result);
    for (int i = 1; i < 192; i += 2) {
        result.expect(map.erase(Key{i}), "erase a colliding key");
        expected.erase(i);
    }
    verify_collision_map(map, expected, result);
    for (int i = 250; i < 370; ++i) {
        map[Key{i}] = i * 9;
        expected[i] = i * 9;
    }
    verify_collision_map(map, expected, result);
    for (int i : {0, 100, 190, 250, 300, 369}) {
        auto where = map.find(Key{i});
        result.expect(static_cast<bool>(where), "iterator erase lookup");
        result.expect(map.erase(where), "iterator erase");
        expected.erase(i);
        verify_collision_map(map, expected, result);
    }
    result.expect(!map.erase(Key{900}), "missing erase returns false");
    map.clear();
    expected.clear();
    verify_collision_map(map, expected, result);
    for (int i = 0; i < 40; ++i) {
        map[Key{i}] = i;
        expected[i] = i;
    }
    verify_collision_map(map, expected, result);
}

template <template <typename, typename> class Family>
void collision(checks& result) {
    collision_case<Family, all_collide_key>(result);
    collision_case<Family, collision_key>(result);
}

template <template <typename, typename> class Family>
void lifetime(checks& result) {
    result.expect(tracked_value::live == 0, "no tracked objects before the group");
    const int constructed_before = tracked_value::constructed;
    const int destroyed_before = tracked_value::destroyed;
    {
        Family<std::uint32_t, tracked_value> source;
        source.set_expand_rate(4);
        for (std::uint32_t i = 0; i < 80; ++i) {
            source.add(i, tracked_value(std::to_string(i)));
        }
        result.expect(tracked_value::live == 80, "growth must end old object lifetimes");
        auto copy = source;
        result.expect(tracked_value::live == 160, "copy owns independent values");
        result.expect(copy.get_expand_rate() == 1.5F, "copy retains the original rate contract");
        copy[7U].text = "copy only";
        result.expect(source[7U].text == "7", "copy mutation must not alter the source");
        auto moved = std::move(copy);
        result.expect(copy.is_empty() && copy.buckets_size() == 0 && copy.nodes_size() == 0,
                      "move leaves the source destructible and empty");
        result.expect(moved[7U].text == "copy only", "move preserves values");
        result.expect(moved.get_expand_rate() == 1.5F, "move retains the original rate contract");
        copy[1000U].text = "reused moved-from object";
        result.expect(copy.size() == 1, "moved-from map can be reused");
        copy = source;
        result.expect(copy.size() == source.size() && copy[7U].text == "7", "copy assignment");
        Family<std::uint32_t, tracked_value> destination;
        destination[400U].text = "old destination value";
        destination = std::move(copy);
        result.expect(copy.is_empty() && destination.size() == source.size(), "move assignment");
        moved.erase(7U);
        source.clear();
        source[1U].text = "after clear";
        destination.reset();
    }
    result.expect(tracked_value::live == 0, "all tracked map objects must be destroyed");
    result.expect(tracked_value::constructed - constructed_before ==
                      tracked_value::destroyed - destroyed_before,
                  "every constructed value must be destroyed exactly once");
    result.observe(static_cast<std::uint64_t>(tracked_value::constructed - constructed_before));

    tested::default_allocator<tracked_value> allocator;
    auto* values = allocator.allocate(3);
    result.expect(values != nullptr, "test allocation");
    allocator.construct(values, "first");
    allocator.construct(values + 1, "second");
    allocator.construct(values + 2);
    result.expect(tracked_value::live == 3, "allocator construct");
    allocator.destroy(values, 3);
    allocator.deallocate(values, 3);
    result.expect(tracked_value::live == 0, "allocator array destroy");
}
}  // namespace

int main(int argc, char** argv) {
    const std::string_view group = argc > 1 ? argv[1] : "all";
    checks result;
    try {
        if (group == "basic" || group == "all") {
            basic<ordinary_map>(result);
            basic<inlined_map>(result);
        }
        if (group == "collision" || group == "all") {
            collision<ordinary_map>(result);
            collision<inlined_map>(result);
        }
        if (group == "lifetime" || group == "all") {
            lifetime<ordinary_map>(result);
            lifetime<inlined_map>(result);
        }
        if (group != "all" && group != "basic" && group != "collision" && group != "lifetime") {
            throw std::runtime_error("unknown test group");
        }
        std::cout << "hash_map group=" << group << " checks=" << result.count
                  << " digest=" << result.digest << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "hash_map group=" << group << " failed after " << result.count
                  << " checks: " << error.what() << '\n';
        return 1;
    }
}
