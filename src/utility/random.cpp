/* Copyright (C) 2025 Tencent.
 * BQLOG is licensed under the Apache License, Version 2.0.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */
#include "qlog/utility/random.hpp"

#include <ctime>

#include "qlog/platform/macros.hpp"
namespace qlog::utility {
static QLOG_TLS uint32_t rand_seed = 0;
static QLOG_TLS uint64_t rand_seed_64 = 0;
void srand(uint32_t seed) {
    rand_seed = seed;
}
uint32_t rand() {
    if (rand_seed == 0) rand_seed = static_cast<uint32_t>(time(nullptr));
    uint32_t x = rand_seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rand_seed = x;
    return x;
}

void srand64(uint64_t seed) {
    rand_seed_64 = seed;
}

uint64_t rand64() {
    if (rand_seed_64 == 0) rand_seed_64 = rand();
    uint64_t x = rand_seed_64;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rand_seed_64 = x;
    return x;
}

}  // namespace qlog::utility
