#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

#include "../../src/qlog/primitives/spin_lock.h"

// ============================================================================
// Test 1: Basic Lock/Unlock
// ============================================================================

void test_spin_lock_basic()
{
    std::cout << "Test: spin_lock basic lock/unlock..." << std::endl;

    qlog::spin_lock lock;

    // TODO: 问题 1
    // 单线程下 lock/unlock 应该能正常工作
    lock.lock();
    // 此时持有锁
    lock.unlock();
    // 验证：lock/unlock 后还能再 lock 吗？
    lock.lock();
    lock.unlock();
    assert(true); // If we reach here, no deadlock occurred

    std::cout << "✓ Basic lock/unlock passed" << std::endl;
}

// ============================================================================
// Test 2: Mutual Exclusion (互斥性)
// ============================================================================

void test_spin_lock_mutual_exclusion()
{
    std::cout << "Test: spin_lock mutual exclusion..." << std::endl;

    qlog::spin_lock lock;
    int critical_section_count = 0; // 计数同时进入临界区的线程数
    const int THREAD_COUNT = 4;

    std::thread threads[THREAD_COUNT];

    for (int i = 0; i < THREAD_COUNT; ++i)
    {
        threads[i] = std::thread(
            [&]()
            {
                for (int j = 0; j < 1000; ++j)
                {
                    lock.lock();

                    // TODO: 问题 2
                    // 进入临界区，计数器+1
                    critical_section_count++;

                    // TODO: 问题 3
                    // 验证：计数器最多只能是 1 吗？
                    // 不是的话说明两个线程同时进入了临界区（互斥失败）
                    assert(critical_section_count == 1);

                    // TODO: 问题 4
                    // 在临界区内做一些工作（否则too fast优化器可能优化掉）
                    uint64_t x = 0;
                    for (int k = 0; k < 100; ++k)
                        x++;
                    // TODO: 问题 5
                    // 离开临界区，计数器-1
                    critical_section_count--;

                    lock.unlock();
                }
            }
        );
    }

    for (int i = 0; i < THREAD_COUNT; ++i)
    {
        threads[i].join();
    }

    // TODO: 问题 6
    // 所有线程都离开了，计数器应该是 0
    assert(critical_section_count == 0);

    std::cout << "✓ Mutual exclusion passed" << std::endl;
}

// ============================================================================
// Test 3: No Deadlock (无死锁)
// ============================================================================

void test_spin_lock_no_deadlock()
{
    std::cout << "Test: spin_lock no deadlock..." << std::endl;

    qlog::spin_lock lock;
    std::atomic<bool> completed(false);

    // TODO: 问题 7
    // 启动一个线程尝试 lock/unlock
    // 设置 5 秒超时，如果超过就说明死锁了

    std::thread worker(
        [&]()
        {
            lock.lock();
            completed = true; // ← 标记完成
            lock.unlock();
        }
    );

    // TODO: 问题 8
    // 等待最多 5 秒，看 worker 是否完成
    auto start = std::chrono::steady_clock::now();
    worker.join();
    auto elapsed = std::chrono::steady_clock::now() - start;

    if (elapsed > std::chrono::seconds(5))
    {
        // 超时 = 死锁
        assert(false && "Deadlock detected!");
    }
    assert(completed);

    std::cout << "✓ No deadlock passed" << std::endl;
}

// ============================================================================
// Test 4: Throughput Benchmark (吞吐量)
// ============================================================================

void test_spin_lock_throughput()
{
    std::cout << "Test: spin_lock throughput..." << std::endl;

    qlog::spin_lock lock;
    uint64_t operations = 0;
    const int DURATION_MS = 100;

    std::thread worker(
        [&]()
        {
            auto start = std::chrono::steady_clock::now();

            while (true)
            {
                lock.lock();
                operations++;
                lock.unlock();

                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start
                );
                if (elapsed.count() >= DURATION_MS)
                    break;
            }
        }
    );

    worker.join();

    // TODO: 问题 9
    // 计算吞吐量：operations per second
    uint64_t ops_per_sec = operations * 1000 / DURATION_MS;
    //
    // 目标：> 1M ops/s
    std::cout << "  Throughput: " << ops_per_sec / 1000000 << " M ops/sec" << std::endl;
    assert(ops_per_sec > 1000000);

    std::cout << "✓ Throughput benchmark passed" << std::endl;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main()
{
    std::cout << "\n========== QLog Spin Lock Tests ==========" << std::endl;

    test_spin_lock_basic();
    test_spin_lock_mutual_exclusion();
    test_spin_lock_no_deadlock();
    test_spin_lock_throughput();

    std::cout << "\n✓ All spin_lock tests passed!" << std::endl;
    return 0;
}
