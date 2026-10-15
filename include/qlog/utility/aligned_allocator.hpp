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
#include <cstddef>
#include <new>
#include <type_traits>

#include "qlog/platform/aligned_memory.hpp"
namespace qlog::utility {
template <typename T, std::size_t Alignment>
class aligned_allocator {
   public:
    using value_type = T;
    using is_always_equal = std::true_type;
    template <typename U>
    struct rebind {
        using other = aligned_allocator<U, Alignment>;
    };
    aligned_allocator() = default;
    template <typename U>
    aligned_allocator(const aligned_allocator<U, Alignment>&) noexcept {}
    T* allocate(std::size_t n) {
        auto* ptr = static_cast<T*>(qlog::platform::aligned_alloc(Alignment, n * sizeof(T)));
        if (!ptr) {
            throw std::bad_alloc();
        }
        return ptr;
    }
    void deallocate(T* ptr, std::size_t) noexcept {
        qlog::platform::aligned_free(ptr);
    }
    bool operator==(const aligned_allocator&) const noexcept {
        return true;
    }
    bool operator!=(const aligned_allocator&) const noexcept {
        return false;
    }
};
}  // namespace qlog::utility
