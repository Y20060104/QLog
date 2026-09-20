// Boundary/property checks for measurement primitives, outside timed trials.
#define main native_benchmark_entrypoint_for_test
#include "native_benchmark.cpp"
#undef main
#include <limits>

int main() {
    auto require = [](bool ok) { if (!ok) throw std::runtime_error("measurement primitive check failed"); };
    std::vector<std::uint64_t> values;
    for (std::uint64_t n = 0; n < 4096; ++n) values.push_back(n);
    for (unsigned bit = 12; bit < 64; ++bit) {
        const auto edge = std::uint64_t{1} << bit;
        for (auto n : {edge - 1, edge, edge + 1}) values.push_back(n);
    }
    values.push_back(std::numeric_limits<std::uint64_t>::max());
    for (const auto n : values) {
        Histogram h;
        h.add(n);
        const auto bound = h.quantile(1);
        require(h.count == 1 && h.max == n && bound >= n);
        require(bound - n <= n / 64);
    }
    Histogram h;
    require(h.quantile(.99) == 0);
    for (std::uint64_t n = 0; n < 100; ++n) h.add(n);
    require(h.quantile(.5) == 49 && h.quantile(.99) == 98);
    Histogram other; other.add(1000); h.merge(other);
    require(h.count == 101 && h.max == 1000);
    Digest first, same, reordered, missing;
    for (auto n : {1ULL, 3ULL, 9ULL}) { first.add(n); same.add(n); }
    for (auto n : {9ULL, 3ULL, 1ULL}) reordered.add(n);
    for (auto n : {1ULL, 9ULL}) missing.add(n);
    require(first == same && !(first == reordered) && !(first == missing));
    std::string_view valid = " p=123 tail";
    require(parse(valid, " p=") == 123 && valid == " tail");
    bool rejected = false;
    try { std::string_view bad = " p=bad"; (void)parse(bad, " p="); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected);
    std::cout << "Histogram boundaries/quantiles, digest order/loss, and malformed numeric field: passed\n";
}
