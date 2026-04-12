#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>

#include "../../src/qlog/primitives/atomic.h"

// ============================================================================
// Test 1: Basic Load/Store
// ============================================================================

void test_atomic_basic_load_store()
{
    std::cout << "Test: atomic basic load/store..." << std::endl;

    qlog::atomic<int> x(10);

    // TODO: 问题 1
    // 验证：初始值是 10 吗？
    assert(x.load(std::memory_order_seq_cst) == 10);

    x.store(20, std::memory_order_seq_cst);

    // TODO: 问题 2
    // 验证：store 后 load 出来的值是 20 吗？
    assert(x.load() == 20);

    std::cout << "✓ Basic load/store passed" << std::endl;
}

// ============================================================================
// Test 2: Acquire/Release Synchronization
// ============================================================================

void test_atomic_acquire_release()
{
    std::cout << "Test: atomic acquire/release synchronization..." << std::endl;

    qlog::atomic<int> flag(0);
    int data = 0;

    // Thread 1: 写入数据和标志
    std::thread t1(
        [&]()
        {
            data = 42;
            flag.store(1, std::memory_order_release);
            // ↑ release: 确保 data=42 对 thread2 可见
        }
    );

    // Thread 2: 等待标志后读取数据
    std::thread t2(
        [&]()
        {
            while (flag.load(std::memory_order_acquire) == 0)
            {
                // ↑ acquire: 获取 release store 的效果
            }

            // TODO: 问题 3
            // 验证：data 必须是 42 吗（happens-before 关系）？
            // 这是 acquire/release 的核心验证！
            assert(data == 42);
        }
    );

    t1.join();
    t2.join();

    std::cout << "✓ Acquire/release synchronization passed" << std::endl;
}

// ============================================================================
// Test 3: Multiple Threads - Data Race Detection
// ============================================================================

void test_atomic_concurrent_access()
{
    std::cout << "Test: atomic concurrent access (TSan will check)..." << std::endl;

    qlog::atomic<int> counter(0);
    const int THREAD_COUNT = 4;
    const int ITERATIONS = 10000;

    std::thread threads[THREAD_COUNT];

    for (int i = 0; i < THREAD_COUNT; ++i)
    {
        threads[i] = std::thread(
            [&]()
            {
                for (int j = 0; j < ITERATIONS; ++j)
                {
                    // TODO: 问题 4
                    // 需要用原子操作递增计数器吗？
                    // 或者这里是在测试"不用原子操作会怎样"？
                    // 想想：如果不用 qlog::atomic，直接 int++，会发生什么？

                    // 方案 A：用 atomic（应该没问题）
                    int old_val = counter.load(std::memory_order_relaxed);
                    counter.store(old_val + 1, std::memory_order_relaxed);

                    // 方案 B：或者直接尝试不同的 memory_order
                }
            }
        );
    }

    for (int i = 0; i < THREAD_COUNT; ++i)
    {
        threads[i].join();
    }

    // TODO: 问题 5
    // 最终的 counter 值应该是多少？
    // 为什么用 seq_cst 来最后读取确保可见性？
    assert(counter.load(std::memory_order_seq_cst) == 40000);

    std::cout << "✓ Concurrent access passed (check TSan for races)" << std::endl;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main()
{
    std::cout << "\n========== QLog Atomic Tests ==========" << std::endl;

    test_atomic_basic_load_store();
    test_atomic_acquire_release();
    test_atomic_concurrent_access();

    std::cout << "\n✓ All atomic tests passed!" << std::endl;
    return 0;
}
