#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "../../src/qlog/primitives/spin_lock_rw.h"

using namespace qlog;

/**
 * ============================================================================
 * spin_lock_rw 单元测试套件
 *
 * 测试范围：
 * 1. 基础功能：读锁、写锁的获取和释放
 * 2. 并发安全：多读者并发、读写互斥
 * 3. 无死锁：压力测试
 * 4. 公平性：写者不饥饿
 * ============================================================================
 */

// ============================================================================
// 组 1：基础功能测试
// ============================================================================

void test_basic_read_lock()
{
    std::cout << "Test: Basic read lock/unlock..." << std::endl;
    spin_lock_rw lock;

    lock.read_lock();
    lock.read_unlock();

    std::cout << "✓ Basic read lock passed" << std::endl;
}

void test_basic_write_lock()
{
    std::cout << "Test: Basic write lock/unlock..." << std::endl;
    spin_lock_rw lock;

    lock.write_lock();
    lock.write_unlock();

    std::cout << "✓ Basic write lock passed" << std::endl;
}

void test_multiple_read_locks()
{
    std::cout << "Test: Multiple read locks..." << std::endl;
    spin_lock_rw lock;

    lock.read_lock();
    lock.read_lock();
    lock.read_unlock();
    lock.read_unlock();

    std::cout << "✓ Multiple read locks passed" << std::endl;
}

void test_read_write_sequence()
{
    std::cout << "Test: Read/write sequence..." << std::endl;
    spin_lock_rw lock;

    lock.read_lock();
    lock.read_unlock();
    lock.write_lock();
    lock.write_unlock();
    lock.read_lock();
    lock.read_unlock();

    std::cout << "✓ Read/write sequence passed" << std::endl;
}

// ============================================================================
// 组 2：多读者并发测试
// ============================================================================

void test_multiple_readers_no_contention()
{
    std::cout << "Test: Multiple readers no contention..." << std::endl;

    spin_lock_rw lock;
    std::atomic<int> counter(0);
    std::vector<std::thread> threads;

    // 8 个读线程，各执行 1000 次读取
    for (int i = 0; i < 8; i++)
    {
        threads.emplace_back(
            [&lock, &counter]()
            {
                for (int j = 0; j < 1000; j++)
                {
                    lock.read_lock();
                    counter.fetch_add(1, std::memory_order_relaxed);
                    lock.read_unlock();
                }
            }
        );
    }

    for (auto& t : threads)
    {
        t.join();
    }

    assert(counter.load() == 8000);
    std::cout << "✓ Multiple readers passed (8000 ops)" << std::endl;
}

void test_multiple_readers_simultaneous()
{
    std::cout << "Test: Multiple readers simultaneous..." << std::endl;

    spin_lock_rw lock;
    std::atomic<int> max_concurrent_readers(0);
    std::atomic<int> current_readers(0);
    std::vector<std::thread> threads;

    // 4 个读线程，持有锁较久，测试并发读
    for (int i = 0; i < 4; i++)
    {
        threads.emplace_back(
            [&lock, &max_concurrent_readers, &current_readers]()
            {
                lock.read_lock();

                // 进入临界区，统计最大并发读者数
                int num = current_readers.fetch_add(1, std::memory_order_acquire) + 1;
                int old_max = max_concurrent_readers.load(std::memory_order_relaxed);
                while (num > old_max && !max_concurrent_readers.compare_exchange_weak(
                                            old_max, num, std::memory_order_relaxed
                                        ))
                {
                    old_max = max_concurrent_readers.load(std::memory_order_relaxed);
                }

                // 模拟临界区工作
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                current_readers.fetch_sub(1, std::memory_order_release);
                lock.read_unlock();
            }
        );
    }

    for (auto& t : threads)
    {
        t.join();
    }

    int max_readers = max_concurrent_readers.load();
    assert(max_readers >= 2);
    std::cout << "✓ Multiple readers simultaneous passed (max=" << max_readers << ")" << std::endl;
}

// ============================================================================
// 组 3：读写互斥测试
// ============================================================================

void test_writer_blocks_readers()
{
    std::cout << "Test: Writer blocks readers..." << std::endl;

    spin_lock_rw lock;
    std::atomic<bool> writer_has_lock(false);
    std::atomic<int> readers_during_write(0);

    auto reader = std::thread(
        [&lock, &writer_has_lock, &readers_during_write]()
        {
            lock.read_lock();

            // 检查写者是否同时持有锁
            if (writer_has_lock.load(std::memory_order_acquire))
            {
                readers_during_write.fetch_add(1);
            }

            // 给写者时间获取锁
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            lock.read_unlock();
        }
    );

    auto writer = std::thread(
        [&lock, &writer_has_lock]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            lock.write_lock();

            writer_has_lock.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            writer_has_lock.store(false, std::memory_order_release);

            lock.write_unlock();
        }
    );

    reader.join();
    writer.join();

    assert(readers_during_write.load() == 0);
    std::cout << "✓ Writer blocks readers passed" << std::endl;
}

void test_reader_blocks_writer()
{
    std::cout << "Test: Reader blocks writer..." << std::endl;

    spin_lock_rw lock;
    std::atomic<bool> reader_has_lock(false);
    std::atomic<int> writer_wait_time_ms(0);

    auto reader = std::thread(
        [&lock, &reader_has_lock]()
        {
            lock.read_lock();
            reader_has_lock.store(true, std::memory_order_release);

            // 持有读锁 100ms
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            reader_has_lock.store(false, std::memory_order_release);
            lock.read_unlock();
        }
    );

    auto writer = std::thread(
        [&lock, &reader_has_lock, &writer_wait_time_ms]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            auto start = std::chrono::steady_clock::now();
            lock.write_lock();
            auto end = std::chrono::steady_clock::now();

            auto wait_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            writer_wait_time_ms.store(wait_time.count());

            // 验证读者已经完全离开
            assert(!reader_has_lock.load(std::memory_order_acquire));

            lock.write_unlock();
        }
    );

    reader.join();
    writer.join();

    assert(writer_wait_time_ms.load() > 80);
    std::cout << "✓ Reader blocks writer passed (wait=" << writer_wait_time_ms.load() << "ms)"
              << std::endl;
}

// ============================================================================
// 组 4：压力测试（无死锁）
// ============================================================================

void test_stress_8readers_1writer()
{
    std::cout << "Test: Stress test (8 readers + 1 writer)..." << std::endl;

    spin_lock_rw lock;
    std::atomic<int> reader_ops(0);
    std::atomic<int> writer_ops(0);

    std::vector<std::thread> threads;

    // 8 个读者
    for (int i = 0; i < 8; i++)
    {
        threads.emplace_back(
            [&lock, &reader_ops]()
            {
                for (int j = 0; j < 1000; j++)
                {
                    lock.read_lock();
                    reader_ops.fetch_add(1, std::memory_order_relaxed);
                    lock.read_unlock();
                }
            }
        );
    }

    // 1 个写者
    threads.emplace_back(
        [&lock, &writer_ops]()
        {
            for (int j = 0; j < 100; j++)
            {
                lock.write_lock();
                writer_ops.fetch_add(1, std::memory_order_relaxed);
                lock.write_unlock();
            }
        }
    );

    for (auto& t : threads)
    {
        t.join();
    }

    assert(reader_ops.load() == 8000);
    assert(writer_ops.load() == 100);
    std::cout << "✓ Stress test passed (8000 reads + 100 writes)" << std::endl;
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "     spin_lock_rw Unit Tests" << std::endl;
    std::cout << std::string(70, '=') << "\n" << std::endl;

    // 组 1：基础功能
    test_basic_read_lock();
    test_basic_write_lock();
    test_multiple_read_locks();
    test_read_write_sequence();

    // 组 2：多读者并发
    test_multiple_readers_no_contention();
    test_multiple_readers_simultaneous();

    // 组 3：读写互斥
    test_writer_blocks_readers();
    test_reader_blocks_writer();

    // 组 4：压力测试
    test_stress_8readers_1writer();

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "     ✓ All tests passed!" << std::endl;
    std::cout << std::string(70, '=') << "\n" << std::endl;

    return 0;
}
