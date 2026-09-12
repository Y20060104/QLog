#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

#include "support/i1d_properties.hpp"
namespace {
TEST(RecordProperty, FixedSeedContracts) {
    constexpr std::uint64_t seed = 0x20260912ULL;
    std::mt19937_64 rng(seed);
    for (std::size_t c = 0; c < 10000; ++c) {
        std::vector<std::uint8_t> input(c < 129 ? c : static_cast<std::size_t>(rng() % 2049));
        for (auto& v : input) v = static_cast<std::uint8_t>(rng());
        // Printed before calling aborting checks so sanitizer failures remain reproducible.
        if (c % 1000 == 0)
            std::fprintf(stderr, "seed=%llu case=%zu size=%zu\n",
                         static_cast<unsigned long long>(seed), c, input.size());
        i1d::case_seed = seed;
        i1d::case_index = c;
        i1d::case_size = input.size();
        i1d::decode(input.data(), input.size());
        i1d::roundtrip(input.data(), input.size());
        i1d::hash(input.data(), input.size());
        i1d::cstr(input.data(), input.size());
    }
}
TEST(RecordProperty, PermanentCorpusReplay) {
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(QLOG_CORPUS_DIR)) {
        std::ifstream f(entry.path(), std::ios::binary);
        ASSERT_TRUE(f.good());
        std::vector<std::uint8_t> input((std::istreambuf_iterator<char>(f)), {});
        i1d::decode(input.data(), input.size());
        i1d::roundtrip(input.data(), input.size());
        i1d::hash(input.data(), input.size());
        i1d::cstr(input.data(), input.size());
        ++count;
    }
    EXPECT_GT(count, 0U);
}
}  // namespace
