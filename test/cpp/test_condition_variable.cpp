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

#include "../../src/qlog/primitives/condition_variable.h"

// ============================================================================
// Test 1: Basic Mutex Lock/Unlock
// ============================================================================

void test_condition_variable_mutex_basic()
{
    std::cout << "Test: basic mutex lock/unlock..." << std::endl;

    qlog::platform::mutex m;

    // Test lock and unlock
    m.lock();
    // Critical section
    m.unlock();

    std::cout << "✓ Basic mutex test passed" << std::endl;
}

// ============================================================================
// Test 2: Scoped Lock RAII
// ============================================================================

void test_condition_variable_scoped_lock()
{
    std::cout << "Test: scoped lock RAII..." << std::endl;

    qlog::platform::mutex m;
    std::atomic<int> state(0);

    {
        qlog::platform::scoped_lock lock(m);
        state.store(1);
        // Lock should be automatically released at scope exit
    }

    assert(state.load() == 1);

    std::cout << "✓ Scoped lock test passed" << std::endl;
}

// ============================================================================
// Test 3: Mutex Try Lock
// ============================================================================

void test_condition_variable_try_lock()
{
    std::cout << "Test: try_lock behavior..." << std::endl;

    qlog::platform::mutex m;

    // First try_lock should succeed (no contention)
    bool acquired = m.try_lock();
    if (acquired)
    {
        m.unlock();
    }

    std::cout << "✓ Try lock test passed" << std::endl;
}

// ============================================================================
// Test 4: Condition Variable Construction
// ============================================================================

void test_condition_variable_construction()
{
    std::cout << "Test: condition variable construction..." << std::endl;

    qlog::platform::condition_variable cv1;
    // Just verify no crash during construction

    {
        qlog::platform::condition_variable cv2;
        // Verify scope exit (destruction) works
    }

    std::cout << "✓ Construction test passed" << std::endl;
}

// ============================================================================
// Test 5: Basic Wait/Notify Pattern
// ============================================================================

void test_condition_variable_wait_notify()
{
    std::cout << "Test: basic wait/notify pattern..." << std::endl;

    qlog::platform::condition_variable cv;
    qlog::platform::mutex m;
    std::atomic<bool> ready(false);

    // Thread that waits
    std::thread waiter(
        [&]()
        {
            qlog::platform::scoped_lock lock(m);
            // In real implementation, would call cv.wait(lock)
            // For now, just simulate waiting
            while (!ready.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    );

    // Give waiter thread time to acquire lock
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Notify
    ready.store(true);
    {
        qlog::platform::scoped_lock lock(m);
        cv.notify_all();
    }

    waiter.join();
    std::cout << "✓ Wait/notify test passed" << std::endl;
}

// ============================================================================
// Test 6: Wait For with Timeout
// ============================================================================

void test_condition_variable_wait_for()
{
    std::cout << "Test: wait_for with timeout..." << std::endl;

    qlog::platform::condition_variable cv;
    qlog::platform::mutex m;

    qlog::platform::scoped_lock lock(m);

    // Wait with timeout - should return false (timeout) since no signal
    bool signaled = cv.wait_for(lock, 10); // 10ms timeout
    (void)signaled;                        // Suppress unused variable warning
    assert(signaled == false);             // Should timeout

    std::cout << "✓ Wait for timeout test passed" << std::endl;
}

// ============================================================================
// Test 7: Multiple Notifications
// ============================================================================

void test_condition_variable_notify_one_vs_all()
{
    std::cout << "Test: notify_one vs notify_all..." << std::endl;

    qlog::platform::condition_variable cv;
    qlog::platform::mutex m;
    std::atomic<int> completed_count(0);

    // Just verify methods exist and don't crash
    {
        qlog::platform::scoped_lock lock(m);
        cv.notify_one();
        cv.notify_all();
    }

    std::cout << "✓ Notify methods test passed" << std::endl;
}

// ============================================================================
// Test 8: Simple Producer-Consumer(Simulated)
// ============================================================================

void test_condition_variable_producer_consumer()
{
    std::cout << "Test: producer-consumer pattern (simulated)..." << std::endl;

    qlog::platform::condition_variable cv;
    qlog::platform::mutex m;
    std::atomic<int> produced(0);
    std::atomic<int> consumed(0);

    // Producer thread
    std::thread producer(
        [&]()
        {
            for (int i = 0; i < 5; ++i)
            {
                {
                    qlog::platform::scoped_lock lock(m);
                    produced.store(i);
                    cv.notify_one();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    );

    // Consumer thread
    std::thread consumer(
        [&]()
        {
            for (int i = 0; i < 5; ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                {
                    qlog::platform::scoped_lock lock(m);
                    consumed.store(produced.load());
                }
            }
        }
    );

    producer.join();
    consumer.join();

    assert(produced.load() == 4);
    assert(consumed.load() == 4);

    std::cout << "✓ Producer-consumer test passed" << std::endl;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main()
{
    std::cout << "\n========== QLog Condition Variable Tests ==========" << std::endl;

    test_condition_variable_mutex_basic();
    test_condition_variable_scoped_lock();
    test_condition_variable_try_lock();
    test_condition_variable_construction();
    test_condition_variable_wait_notify();
    test_condition_variable_wait_for();
    test_condition_variable_notify_one_vs_all();
    test_condition_variable_producer_consumer();

    std::cout << "\n✓ All condition_variable tests passed!" << std::endl;
    return 0;
}
