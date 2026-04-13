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

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

#include "../../src/qlog/primitives/platform_thread.h"

// ============================================================================
// Test 1: Get Current Thread ID
// ============================================================================

void test_platform_thread_current_id()
{
    std::cout << "Test: get current thread ID..." << std::endl;

    auto tid = qlog::platform::thread::current_thread_id();
    assert(tid > 0);

    std::cout << "  Current thread ID: " << tid << std::endl;
    std::cout << "✓ Current thread ID test passed" << std::endl;
}

// ============================================================================
// Test 2: Sleep Milliseconds
// ============================================================================

void test_platform_thread_sleep_ms()
{
    std::cout << "Test: sleep milliseconds..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    qlog::platform::sleep_milliseconds(100);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Allow some tolerance (50-200ms range)
    assert(duration_ms >= 50 && duration_ms <= 200);

    std::cout << "  Slept for: " << duration_ms << "ms" << std::endl;
    std::cout << "✓ Sleep milliseconds test passed" << std::endl;
}

// ============================================================================
// Test 3: Sleep Microseconds
// ============================================================================

void test_platform_thread_sleep_us()
{
    std::cout << "Test: sleep microseconds..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    qlog::platform::sleep_microseconds(1000); // 1ms
    auto end = std::chrono::high_resolution_clock::now();

    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // Allow tolerance for microsecond precision
    assert(duration_us >= 500 && duration_us <= 5000);

    std::cout << "  Slept for: " << duration_us << "us" << std::endl;
    std::cout << "✓ Sleep microseconds test passed" << std::endl;
}

// ============================================================================
// Test 4: Multiple Thread IDs
// ============================================================================

void test_platform_thread_multiple_ids()
{
    std::cout << "Test: different thread IDs for different threads..." << std::endl;

    auto main_tid = qlog::platform::thread::current_thread_id();
    std::atomic<qlog::platform::thread_id> other_tid(0);
    std::atomic<bool> thread_completed(false);

    // Lambda to call in another thread
    auto thread_func = [&]()
    {
        other_tid.store(qlog::platform::thread::current_thread_id());
        thread_completed.store(true);
    };

    // Create and run thread (using std::thread as fallback since qlog::thread not fully
    // implemented)
    std::thread t(thread_func);
    t.join();

    assert(thread_completed.load());
    assert(main_tid != other_tid.load());

    std::cout << "  Main thread ID: " << main_tid << std::endl;
    std::cout << "  Other thread ID: " << other_tid.load() << std::endl;
    std::cout << "✓ Multiple thread IDs test passed" << std::endl;
}

// ============================================================================
// Test 5: Zero Sleep (Yield)
// ============================================================================

void test_platform_thread_zero_sleep()
{
    std::cout << "Test: zero-duration sleeps..." << std::endl;

    // Zero milliseconds
    qlog::platform::sleep_milliseconds(0);

    // Very small microseconds
    qlog::platform::sleep_microseconds(1);

    std::cout << "✓ Zero sleep test passed" << std::endl;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main()
{
    std::cout << "\n========== QLog Platform Thread Tests ==========" << std::endl;

    test_platform_thread_current_id();
    test_platform_thread_sleep_ms();
    test_platform_thread_sleep_us();
    test_platform_thread_multiple_ids();
    test_platform_thread_zero_sleep();

    std::cout << "\n✓ All platform_thread tests passed!" << std::endl;
    return 0;
}
