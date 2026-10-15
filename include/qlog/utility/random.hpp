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
#pragma once
#include <cstdint>
namespace qlog::utility {
void srand(std::uint32_t seed);
std::uint32_t rand();
void srand64(std::uint64_t seed);
std::uint64_t rand64();
}  // namespace qlog::utility
