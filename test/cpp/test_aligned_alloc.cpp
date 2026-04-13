/*
 * Copyright (c) 2026 QLog Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cassert>
#include <cstdint>
#include <iostream>

#include "../../src/qlog/primitives/aligned_alloc.h"

// ============================================================================
// Test 1: Basic Allocation and Deallocation
// ============================================================================

void test_aligned_alloc_basic()
{
    std::cout << "Test: aligned allocation basic..." << std::endl;

    // Allocate 1024 bytes with 64-byte alignment
    void* ptr = qlog::aligned_alloc(64, 1024);
    assert(ptr != nullptr);
    assert(reinterpret_cast<uintptr_t>(ptr) % 64 == 0);

    qlog::aligned_free(ptr);
    std::cout << "✓ Basic allocation passed" << std::endl;
}

// ============================================================================
// Test 2: Various Alignments
// ============================================================================

void test_aligned_alloc_various_alignments()
{
    std::cout << "Test: various alignments..." << std::endl;

    // Test power-of-2 alignments
    int alignments[] = {16, 32, 64, 128, 256, 512, 1024};

    for (int alignment : alignments)
    {
        void* ptr = qlog::aligned_alloc(alignment, 4096);
        assert(ptr != nullptr);
        assert(reinterpret_cast<uintptr_t>(ptr) % alignment == 0);
        qlog::aligned_free(ptr);
    }

    std::cout << "✓ Various alignments passed" << std::endl;
}

// ============================================================================
// Test 3: Invalid Alignments (Non-Power-of-2)
// ============================================================================

void test_aligned_alloc_invalid_alignment()
{
    std::cout << "Test: invalid alignments (non-power-of-2)..." << std::endl;

    // Non-power-of-2 alignments should return nullptr
    int invalid_alignments[] = {3, 5, 7, 15, 17, 100};

    for (int alignment : invalid_alignments)
    {
        void* ptr = qlog::aligned_alloc(alignment, 1024);
        (void)ptr; // Suppress unused variable warning
        assert(ptr == nullptr);
    }

    std::cout << "✓ Invalid alignments rejection passed" << std::endl;
}

// ============================================================================
// Test 4: Zero Size Allocation
// ============================================================================

void test_aligned_alloc_zero_size()
{
    std::cout << "Test: zero-size allocation..." << std::endl;

    // Zero-size allocation behavior (platform-dependent)
    // Some implementations return nullptr, others return valid ptr
    void* ptr = qlog::aligned_alloc(64, 0);
    // Just verify it doesn't crash
    if (ptr != nullptr)
    {
        qlog::aligned_free(ptr);
    }

    std::cout << "✓ Zero-size allocation passed" << std::endl;
}

// ============================================================================
// Test 5: Multiple Allocations
// ============================================================================

void test_aligned_alloc_multiple()
{
    std::cout << "Test: multiple allocations..." << std::endl;

    const int ALLOC_COUNT = 100;
    void* ptrs[ALLOC_COUNT];

    // Allocate
    for (int i = 0; i < ALLOC_COUNT; ++i)
    {
        ptrs[i] = qlog::aligned_alloc(64, 256 * (i + 1));
        assert(ptrs[i] != nullptr);
        assert(reinterpret_cast<uintptr_t>(ptrs[i]) % 64 == 0);
    }

    // Verify each allocation is different
    for (int i = 0; i < ALLOC_COUNT; ++i)
    {
        for (int j = i + 1; j < ALLOC_COUNT; ++j)
        {
            assert(ptrs[i] != ptrs[j]);
        }
    }

    // Deallocate
    for (int i = 0; i < ALLOC_COUNT; ++i)
    {
        qlog::aligned_free(ptrs[i]);
    }

    std::cout << "✓ Multiple allocations passed" << std::endl;
}

// ============================================================================
// Test 6: Writing to Aligned Memory
// ============================================================================

void test_aligned_alloc_write_pattern()
{
    std::cout << "Test: write pattern to aligned memory..." << std::endl;

    const size_t size = 1024;
    uint64_t* ptr = static_cast<uint64_t*>(qlog::aligned_alloc(64, size));
    assert(ptr != nullptr);

    // Write pattern
    for (size_t i = 0; i < size / sizeof(uint64_t); ++i)
    {
        ptr[i] = 0xDEADBEEFCAFEBABEULL + i;
    }

    // Read back and verify
    for (size_t i = 0; i < size / sizeof(uint64_t); ++i)
    {
        assert(ptr[i] == (0xDEADBEEFCAFEBABEULL + i));
    }

    qlog::aligned_free(ptr);
    std::cout << "✓ Write pattern passed" << std::endl;
}

// ============================================================================
// Test 7: Null Pointer Deallocation
// ============================================================================

void test_aligned_free_null()
{
    std::cout << "Test: null pointer deallocation..." << std::endl;

    // Freeing nullptr should not crash
    qlog::aligned_free(nullptr);

    std::cout << "✓ Null free passed" << std::endl;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main()
{
    std::cout << "\n========== QLog Aligned Alloc Tests ==========" << std::endl;

    test_aligned_alloc_basic();
    test_aligned_alloc_various_alignments();
    test_aligned_alloc_invalid_alignment();
    test_aligned_alloc_zero_size();
    test_aligned_alloc_multiple();
    test_aligned_alloc_write_pattern();
    test_aligned_free_null();

    std::cout << "\n✓ All aligned_alloc tests passed!" << std::endl;
    return 0;
}
