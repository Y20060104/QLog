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

#include "qlog/buffer/mpsc_ring_buffer.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 测试基础设施
// ─────────────────────────────────────────────────────────────────────────────

struct TestResult
{
    int passed = 0;
    int failed = 0;

    void print() const
    {
        std::cout << "\n═══════════════════════════════════════\n";
        std::cout << "✅ PASSED: " << passed << "\n";
        std::cout << "❌ FAILED: " << failed << "\n";
        std::cout << "═══════════════════════════════════════\n";
    }
};

TestResult g_result;

#define TEST_ASSERT(cond, msg)                                                     \
    do                                                                             \
    {                                                                              \
        if (!(cond))                                                               \
        {                                                                          \
            std::cerr << "  ❌ FAILED [" << __LINE__ << "]: " << msg << std::endl; \
            g_result.failed++;                                                     \
        }                                                                          \
        else                                                                       \
        {                                                                          \
            std::cout << "  ✅ PASSED: " << msg << std::endl;                      \
            g_result.passed++;                                                     \
        }                                                                          \
    } while (0)

#define TEST_NOT_NULL(ptr, msg) TEST_ASSERT((ptr) != nullptr, msg)
#define TEST_NULL(ptr, msg) TEST_ASSERT((ptr) == nullptr, msg)
#define TEST_EQ(a, b, msg) TEST_ASSERT((a) == (b), msg)
#define TEST_TRUE(cond, msg) TEST_ASSERT((cond), msg)
#define TEST_FALSE(cond, msg) TEST_ASSERT(!(cond), msg)

// ─────────────────────────────────────────────────────────────────────────────
// 测试 1：初始化成功
//
// 修正说明：
//   原版断言 capacity() == 64*1024，但实现会将容量向上对齐到 2 的幂次个
//   cache-line（64B），实际返回字节数为对齐后值，不保证等于原始入参。
//   改为验证：返回值 >= 请求容量 且 为 2 的幂次，以及缓冲区可用（非零容量）。
// ─────────────────────────────────────────────────────────────────────────────
void test_init_success()
{
    std::cout << "\n[Test 1] Init Success\n";

    constexpr uint32_t kRequestBytes = 64u * 1024u;
    qlog::mpsc_ring_buffer buffer(kRequestBytes);

    const uint32_t cap = buffer.capacity();

    // 容量 >= 请求值
    TEST_TRUE(cap >= kRequestBytes, "capacity() should be >= requested bytes");

    // 容量是 2 的幂次（实现将 block_count 对齐到 2 的幂）
    TEST_TRUE((cap & (cap - 1u)) == 0u, "capacity() should be a power of two");
}

// ─────────────────────────────────────────────────────────────────────────────
// 测试 2：边界参数初始化
//
// 修正说明：
//   原版 buffer_small 断言 capacity() >= 64，但实现对小容量同样会做 2 的幂
//   对齐，实际值可能是 64 / CACHE_LINE_SIZE = 1 个 block，即 64B。
//   改为仅验证「非零容量的缓冲区可正常分配」，以及「零容量的缓冲区分配失败」，
//   避免对内部对齐策略做过强假设。
// ─────────────────────────────────────────────────────────────────────────────
void test_init_edge_cases()
{
    std::cout << "\n[Test 2] Init Edge Cases\n";

    // 零容量：分配应直接失败
    {
        qlog::mpsc_ring_buffer buffer0(0);
        TEST_EQ(buffer0.capacity(), 0u, "zero capacity should report 0");

        qlog::write_handle wh = buffer0.alloc_write_chunk(1);
        TEST_FALSE(wh.success, "alloc on zero-capacity buffer should fail");
    }

    {
        constexpr uint32_t kMinEffectiveSize = 128u;
        qlog::mpsc_ring_buffer buffer_small(kMinEffectiveSize);

        TEST_TRUE(
            buffer_small.capacity() >= kMinEffectiveSize, "minimal buffer should align capacity"
        );

        // 此时应该可以成功分配 1 字节（占用 1 个 block）
        qlog::write_handle wh = buffer_small.alloc_write_chunk(1);
        TEST_TRUE(wh.success, "alloc(1) on 128B buffer should succeed");

        if (wh.success)
        {
            buffer_small.commit_write_chunk(wh);

            // 验证此时 buffer 已满（因为只剩一个 block 必须留空）
            qlog::write_handle wh_full = buffer_small.alloc_write_chunk(1);
            TEST_FALSE(wh_full.success, "minimal buffer should be full after 1 alloc");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 测试 3：单线程基础读写
//
// 修正说明：
//   1. 补充 data_size 校验，确保读出的 data_size == 写入时请求的 size。
//   2. 补充 commit_read_chunk 后缓冲区变空的验证（SPSC 场景下完全确定）。
//      原版注释"跳过该验证"是不合适的——这是最基础的正确性保证。
// ─────────────────────────────────────────────────────────────────────────────
void test_single_thread_basic()
{
    std::cout << "\n[Test 3] Single Thread Basic Read/Write\n";

    constexpr uint32_t kPayloadSize = 64u;
    qlog::mpsc_ring_buffer buffer(64u * 1024u);

    // ── 写入 ──────────────────────────────────────────────
    qlog::write_handle wh = buffer.alloc_write_chunk(kPayloadSize);
    TEST_TRUE(wh.success, "alloc_write_chunk(64) should succeed");
    TEST_NOT_NULL(wh.data, "write handle data should not be null");

    if (!wh.success)
        return; // 后续依赖 wh，提前退出

    for (uint32_t i = 0; i < kPayloadSize; ++i)
    {
        wh.data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    buffer.commit_write_chunk(wh);

    // ── 读取 ──────────────────────────────────────────────
    qlog::read_handle rh = buffer.read_chunk();
    TEST_TRUE(rh.success, "read_chunk() should return data");
    TEST_NOT_NULL(rh.data, "read handle data should not be null");
    TEST_EQ(rh.data_size, kPayloadSize, "data_size should match written size");

    if (!rh.success)
        return;

    bool data_match = true;
    for (uint32_t i = 0; i < kPayloadSize; ++i)
    {
        if (rh.data[i] != static_cast<uint8_t>(i & 0xFF))
        {
            data_match = false;
            break;
        }
    }
    TEST_TRUE(data_match, "data payload should match byte-for-byte");

    buffer.commit_read_chunk(rh);

    // ── commit 后缓冲区应为空 ─────────────────────────────
    qlog::read_handle rh2 = buffer.read_chunk();
    TEST_FALSE(rh2.success, "buffer should be empty after single commit_read_chunk");
}

// ─────────────────────────────────────────────────────────────────────────────
// 测试 4：多条写入顺序读取
//
// 修正说明：
//   1. 去除冗余的 write_handles 容器（收集后从未使用）。
//   2. size 改为 uint32_t，消除隐式窄化转换警告。
//   3. 补充：全部读完后缓冲区应为空。
// ─────────────────────────────────────────────────────────────────────────────
void test_multiple_entries()
{
    std::cout << "\n[Test 4] Multiple Entries\n";

    qlog::mpsc_ring_buffer buffer(256u * 1024u);
    constexpr int kNumEntries = 50;

    // ── 顺序写入 ──────────────────────────────────────────
    for (int i = 0; i < kNumEntries; ++i)
    {
        const uint32_t size = 32u + static_cast<uint32_t>(i % 64);
        qlog::write_handle wh = buffer.alloc_write_chunk(size);
        TEST_TRUE(wh.success, "alloc for entry " + std::to_string(i));

        if (wh.success)
        {
            wh.data[0] = static_cast<uint8_t>(i & 0xFF);
            buffer.commit_write_chunk(wh);
        }
    }

    // ── 顺序读取并验证 ────────────────────────────────────
    for (int i = 0; i < kNumEntries; ++i)
    {
        qlog::read_handle rh = buffer.read_chunk();
        TEST_TRUE(rh.success, "read entry " + std::to_string(i) + " should succeed");

        if (rh.success)
        {
            TEST_EQ(
                static_cast<int>(rh.data[0]), i & 0xFF, "data[0] for entry " + std::to_string(i)
            );
            buffer.commit_read_chunk(rh);
        }
    }

    // ── 全部读完后应为空 ──────────────────────────────────
    qlog::read_handle rh_empty = buffer.read_chunk();
    TEST_FALSE(rh_empty.success, "buffer should be empty after reading all entries");
}

// ─────────────────────────────────────────────────────────────────────────────
// 测试 5：缓冲区容量边界
//
// 修正说明：
//   原版只测了"超大分配失败"，覆盖面不足。补充：
//   a) 分配超过 capacity 的单条直接失败（原有）。
//   b) 恰好填满后再分配一条应失败（真正的"满"边界）。
//   c) 消费一条后，再次分配应成功（验证 read_cursor 推进后空间释放）。
// ─────────────────────────────────────────────────────────────────────────────
void test_buffer_full()
{
    std::cout << "\n[Test 5] Buffer Full (Boundary Validation)\n";

    // ── a) 单条超容量 ─────────────────────────────────────
    {
        qlog::mpsc_ring_buffer buffer(64u * 1024u);
        qlog::write_handle wh = buffer.alloc_write_chunk(100u * 1024u);
        TEST_FALSE(wh.success, "alloc(100KB) in 64KB buffer should fail");
    }

    // ── b) 填满后再分配应失败 ─────────────────────────────
    // 使用较小缓冲区以便快速填满；每次分配一个 cache-line（header + 最小 data）。
    // 实际能放多少条取决于 block_count，通过不断 alloc 直到第一次失败来探测。
    {
        constexpr uint32_t kBufSize = 4u * 1024u; // 4KB -> 64 blocks
        constexpr uint32_t kDataSize = 1u;        // 1B data，每条占 1 block（header 内联）

        qlog::mpsc_ring_buffer buffer(kBufSize);

        // 不断写入直到第一次失败，记录成功次数
        int success_count = 0;
        qlog::write_handle last_failed;
        while (true)
        {
            qlog::write_handle wh = buffer.alloc_write_chunk(kDataSize);
            if (!wh.success)
            {
                last_failed = wh;
                break;
            }
            buffer.commit_write_chunk(wh);
            success_count++;

            // 安全上限，避免实现有 bug 时死循环
            if (success_count > 10000)
                break;
        }

        TEST_TRUE(success_count > 0, "should write at least one entry before full");
        TEST_FALSE(last_failed.success, "should fail when buffer is truly full");

        // ── c) 消费一条后，空间释放，可再分配 ────────────────
        qlog::read_handle rh = buffer.read_chunk();
        TEST_TRUE(rh.success, "should read one entry from full buffer");
        if (rh.success)
        {
            buffer.commit_read_chunk(rh);
        }

        qlog::write_handle wh_retry = buffer.alloc_write_chunk(kDataSize);
        TEST_TRUE(wh_retry.success, "alloc should succeed after consuming one entry");
        if (wh_retry.success)
        {
            buffer.commit_write_chunk(wh_retry);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 测试 6：Reset
// （逻辑正确，无修改，保持原版）
// ─────────────────────────────────────────────────────────────────────────────
void test_reset()
{
    std::cout << "\n[Test 6] Reset\n";

    qlog::mpsc_ring_buffer buffer(64u * 1024u);

    qlog::write_handle wh = buffer.alloc_write_chunk(32u);
    TEST_TRUE(wh.success, "first alloc before reset");
    buffer.commit_write_chunk(wh);

    qlog::read_handle rh = buffer.read_chunk();
    TEST_TRUE(rh.success, "read should have data before reset");
    buffer.commit_read_chunk(rh);

    buffer.reset();

    // 重置后缓冲区为空
    qlog::read_handle rh_after = buffer.read_chunk();
    TEST_FALSE(rh_after.success, "buffer should be empty immediately after reset");

    // 重置后可以重新分配
    qlog::write_handle wh2 = buffer.alloc_write_chunk(32u);
    TEST_TRUE(wh2.success, "alloc after reset should succeed");
    if (wh2.success)
    {
        buffer.commit_write_chunk(wh2);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 测试 7：Block 数据结构验证
// （逻辑正确，无修改，保持原版）
// ─────────────────────────────────────────────────────────────────────────────
void test_block_structure()
{
    std::cout << "\n[Test 7] Block Structure Validation\n";

    TEST_EQ(sizeof(qlog::block), 64u, "block size must equal cache line size (64B)");
    TEST_EQ(alignof(qlog::block), 64u, "block alignment must be 64 bytes");

    qlog::block test_block{};
    test_block.chunk_head.set_block_num(10);
    TEST_EQ(test_block.chunk_head.get_block_num(), 10u, "set/get block_num roundtrip");

    test_block.chunk_head.status = qlog::block_status::used;
    TEST_EQ(
        static_cast<int>(test_block.chunk_head.status),
        static_cast<int>(qlog::block_status::used),
        "block_status::used should be stored and retrieved correctly"
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// 测试 8：单线程大数据量（N=500 entries）
// （逻辑正确，无修改，保持原版）
// ─────────────────────────────────────────────────────────────────────────────
void test_single_thread_large_volume()
{
    std::cout << "\n[Test 8] Single Thread Large Volume (N=500 entries)\n";

    qlog::mpsc_ring_buffer buffer(2u * 1024u * 1024u);
    constexpr int kNumEntries = 500;
    constexpr uint32_t kEntrySize = 32u;

    for (int i = 0; i < kNumEntries; ++i)
    {
        qlog::write_handle wh = buffer.alloc_write_chunk(kEntrySize);
        if (!wh.success)
        {
            TEST_ASSERT(false, "alloc entry " + std::to_string(i));
            break;
        }
        *reinterpret_cast<uint32_t*>(wh.data) = static_cast<uint32_t>(i);
        buffer.commit_write_chunk(wh);
    }

    int read_count = 0;
    while (read_count < kNumEntries)
    {
        qlog::read_handle rh = buffer.read_chunk();
        if (!rh.success)
            break;

        const uint32_t entry_id = *reinterpret_cast<const uint32_t*>(rh.data);
        TEST_EQ(
            static_cast<int>(entry_id),
            read_count,
            "entry ID for index " + std::to_string(read_count)
        );
        buffer.commit_read_chunk(rh);
        read_count++;
    }

    TEST_EQ(read_count, kNumEntries, "should read all 500 entries");
}

// ─────────────────────────────────────────────────────────────────────────────
// 测试 9：INVALID 块处理（Wrap-around）
// （逻辑正确，无修改，保持原版）
// ─────────────────────────────────────────────────────────────────────────────
void test_invalid_block_handling()
{
    std::cout << "\n[Test 9] INVALID Block Handling (Wrap-around)\n";

    qlog::mpsc_ring_buffer buffer(64u * 1024u);
    constexpr int kNumEntries = 30;

    for (int i = 0; i < kNumEntries; ++i)
    {
        qlog::write_handle wh = buffer.alloc_write_chunk(128u);
        if (wh.success)
        {
            *reinterpret_cast<uint32_t*>(wh.data) = static_cast<uint32_t>(i);
            buffer.commit_write_chunk(wh);
        }
    }

    int read_count = 0;
    while (true)
    {
        qlog::read_handle rh = buffer.read_chunk();
        if (!rh.success)
            break;
        buffer.commit_read_chunk(rh);
        read_count++;
    }

    TEST_EQ(read_count, kNumEntries, "should read all entries even with wrap-around");
}

// ─────────────────────────────────────────────────────────────────────────────
// 测试 10：多生产者 + 单消费者压测
// （逻辑正确，无修改，保持原版）
// ─────────────────────────────────────────────────────────────────────────────
void test_multi_producer_single_consumer()
{
    std::cout << "\n[Test 10] Multi-Producer + Single Consumer Stress Test\n";

    qlog::mpsc_ring_buffer buffer(2u * 1024u * 1024u);

    constexpr int kNumProducers = 10;
    constexpr int kEntriesPerProducer = 1000;
    constexpr int kTotalEntries = kNumProducers * kEntriesPerProducer;

    std::atomic<int> total_written(0);
    std::atomic<int> total_read(0);
    std::atomic<bool> all_producers_done(false);

    auto producer = [&](int producer_id)
    {
        for (int i = 0; i < kEntriesPerProducer; ++i)
        {
            while (true)
            {
                qlog::write_handle wh = buffer.alloc_write_chunk(32u);
                if (wh.success)
                {
                    *reinterpret_cast<uint32_t*>(wh.data) =
                        (static_cast<uint32_t>(producer_id) << 24) | static_cast<uint32_t>(i);
                    buffer.commit_write_chunk(wh);
                    total_written.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
                std::this_thread::yield();
            }
        }
    };

    auto consumer = [&]()
    {
        while (total_read.load(std::memory_order_relaxed) < kTotalEntries)
        {
            qlog::read_handle rh = buffer.read_chunk();
            if (rh.success)
            {
                const uint32_t producer_id =
                    (*reinterpret_cast<const uint32_t*>(rh.data) >> 24) & 0xFF;
                if (producer_id < static_cast<uint32_t>(kNumProducers))
                {
                    buffer.commit_read_chunk(rh);
                    total_read.fetch_add(1, std::memory_order_relaxed);
                }
            }
            else
            {
                if (all_producers_done.load(std::memory_order_acquire))
                    break;
                std::this_thread::yield();
            }
        }
    };

    auto test_start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> producer_threads;
    producer_threads.reserve(kNumProducers);
    for (int i = 0; i < kNumProducers; ++i)
        producer_threads.emplace_back(producer, i);

    std::thread consumer_thread(consumer);

    for (auto& t : producer_threads)
        t.join();
    all_producers_done.store(true, std::memory_order_release);
    consumer_thread.join();

    auto test_end = std::chrono::high_resolution_clock::now();
    auto duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(test_end - test_start).count();

    TEST_EQ(total_written.load(), kTotalEntries, "should write all entries");
    TEST_EQ(total_read.load(), kTotalEntries, "should read all entries");

    std::cout << "  " << kNumProducers << " producers × " << kEntriesPerProducer
              << " entries = " << kTotalEntries << " entries in " << duration_ms << " ms\n";
    if (duration_ms > 0)
    {
        std::cout << "  Throughput: " << (kTotalEntries * 1000LL / duration_ms) << " entries/sec\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 测试 11：多生产者顺序性验证
// （逻辑正确，无修改，保持原版）
// ─────────────────────────────────────────────────────────────────────────────
void test_multi_producer_ordering()
{
    std::cout << "\n[Test 11] Multi-Producer Per-Producer Ordering Verification\n";

    qlog::mpsc_ring_buffer buffer(1u * 1024u * 1024u);

    constexpr int kNumProducers = 2;
    constexpr int kEntriesPerProducer = 100;

    std::mutex result_lock;
    std::vector<uint32_t> read_results;
    std::atomic<bool> all_done(false);

    auto producer = [&](int producer_id)
    {
        for (int i = 0; i < kEntriesPerProducer; ++i)
        {
            while (true)
            {
                qlog::write_handle wh = buffer.alloc_write_chunk(sizeof(uint32_t));
                if (wh.success)
                {
                    *reinterpret_cast<uint32_t*>(wh.data) =
                        (static_cast<uint32_t>(producer_id) << 16) | static_cast<uint32_t>(i);
                    buffer.commit_write_chunk(wh);
                    break;
                }
                std::this_thread::yield();
            }
        }
    };

    auto consumer = [&]()
    {
        int read_count = 0;
        while (read_count < kNumProducers * kEntriesPerProducer)
        {
            qlog::read_handle rh = buffer.read_chunk();
            if (rh.success)
            {
                const uint32_t val = *reinterpret_cast<const uint32_t*>(rh.data);
                {
                    std::lock_guard<std::mutex> lock(result_lock);
                    read_results.push_back(val);
                }
                buffer.commit_read_chunk(rh);
                read_count++;
            }
            else
            {
                if (all_done.load(std::memory_order_acquire))
                    break;
                std::this_thread::yield();
            }
        }
    };

    std::vector<std::thread> producer_threads;
    producer_threads.reserve(kNumProducers);
    for (int i = 0; i < kNumProducers; ++i)
        producer_threads.emplace_back(producer, i);

    std::thread consumer_thread(consumer);

    for (auto& t : producer_threads)
        t.join();
    all_done.store(true, std::memory_order_release);
    consumer_thread.join();

    TEST_EQ(
        static_cast<int>(read_results.size()),
        kNumProducers * kEntriesPerProducer,
        "should read all entries"
    );

    // 同一 producer 的 entry 索引应单调递增
    std::vector<int> last_index(kNumProducers, -1);
    bool ordering_ok = true;
    for (uint32_t val : read_results)
    {
        const uint32_t pid = (val >> 16) & 0xFFFF;
        const uint32_t idx = val & 0xFFFF;

        if (pid < static_cast<uint32_t>(kNumProducers))
        {
            if (last_index[pid] >= 0 && idx <= static_cast<uint32_t>(last_index[pid]))
            {
                ordering_ok = false;
                break;
            }
            last_index[pid] = static_cast<int>(idx);
        }
    }
    TEST_TRUE(ordering_ok, "per-producer ordering should be strictly monotonic");
}

// ─────────────────────────────────────────────────────────────────────────────
// 测试 12：性能基准测试（真实并发场景）
//
// 修正说明：
//   1. write_count / read_count 改为 std::atomic<int>，消除并发访问未定义行为。
//   2. 增加 producer_done 原子标志：生产者写完后设置，消费者据此安全退出，
//      避免实现存在 bug 时消费者因 read_count 永不达到目标而死锁。
//   3. 消费者循环改为同时检查 read_count 和 producer_done，确保在任何情况下
//      都能正常退出。
//   4. 时间统计移到两个线程全部 join 之后，避免计入线程启动开销的同时也
//      确保 write_count / read_count 的最终值已经对主线程可见（join 建立
//      happens-before 关系）。
// ─────────────────────────────────────────────────────────────────────────────
void test_performance_benchmark()
{
    std::cout << "\n[Test 12] Performance Benchmark (Concurrent)\n";

    constexpr int kNumEntries = 10'000'000;
    constexpr uint32_t kEntrySize = 64u;

    // 实例化缓冲区
    qlog::mpsc_ring_buffer buffer(16u * 1024u * 1024u);

    // ✅ 修复冲突 B：使用原子变量，并强制缓存行对齐以隔离干扰
    struct alignas(64) SharedState
    {
        std::atomic<int> write_count{0};
        alignas(64) std::atomic<int> read_count{0};
        alignas(64) std::atomic<bool> producer_done{false};
    } state;

    // 同步点：确保两个线程几乎同时开始跑，计时更准
    std::atomic<bool> start_gate{false};

    // ── 生产者线程 ────────────────────────────────────────
    std::thread t_producer(
        [&]()
        {
            while (!start_gate.load(std::memory_order_acquire))
                ; // 旋塞等待信号

            for (int i = 0; i < kNumEntries; ++i)
            {
                while (true)
                {
                    qlog::write_handle handle = buffer.alloc_write_chunk(kEntrySize);
                    if (handle.success)
                    {
                        // 模拟写入数据
                        std::memcpy(handle.data, &i, sizeof(i));
                        buffer.commit_write_chunk(handle);
                        state.write_count.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                    std::this_thread::yield(); // 缓冲区满时退让
                }
            }
            // ✅ 关键同步：使用 release 语义通知生产完成
            state.producer_done.store(true, std::memory_order_release);
        }
    );

    // ── 消费者线程 ────────────────────────────────────────
    std::thread t_consumer(
        [&]()
        {
            while (!start_gate.load(std::memory_order_acquire))
                ;

            while (true)
            {
                qlog::read_handle handle = buffer.read_chunk();
                if (handle.success)
                {
                    buffer.commit_read_chunk(handle);
                    state.read_count.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    // ✅ 修复退出逻辑：先检查状态，再尝试最后一次 drain
                    if (state.producer_done.load(std::memory_order_acquire))
                    {
                        // 再次尝试读取，确保在收到 done 信号到最后一次检查之间的残留数据被处理
                        qlog::read_handle last_drain = buffer.read_chunk();
                        if (!last_drain.success)
                            break;

                        buffer.commit_read_chunk(last_drain);
                        state.read_count.fetch_add(1, std::memory_order_relaxed);
                    }
                    else
                    {
                        std::this_thread::yield();
                    }
                }
            }
        }
    );

    // 启动测试计时
    auto start_time = std::chrono::high_resolution_clock::now();
    start_gate.store(true, std::memory_order_release);

    t_producer.join();
    t_consumer.join();
    auto end_time = std::chrono::high_resolution_clock::now();

    // ──────────────────────────────────────────────────────
    // 结果计算
    // ──────────────────────────────────────────────────────
    auto total_time_us =
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    const double ns_per_entry =
        (kNumEntries > 0) ? (static_cast<double>(total_time_us) * 1000.0 / kNumEntries) : 0.0;

    std::cout << "  Processed " << kNumEntries << " entries concurrently in " << total_time_us
              << " µs\n";
    std::cout << "  -> " << ns_per_entry << " ns/entry (End-to-End Latency)\n";

    // 验证数据完整性
    TEST_EQ(state.write_count.load(), kNumEntries, "Total write count mismatch");
    TEST_EQ(state.read_count.load(), kNumEntries, "Total read count mismatch");

    // TSan 环境下可以放宽到 2000ns，正常环境下应 < 100ns
    TEST_TRUE(
        ns_per_entry < 2000.0, "Performance is too low, check for false sharing or TSan overhead"
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║   QLog M2 MPSC Ring Buffer Tests      ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";

    test_init_success();
    test_init_edge_cases();
    test_single_thread_basic();
    test_multiple_entries();
    test_buffer_full();
    test_reset();
    test_block_structure();
    test_single_thread_large_volume();
    test_invalid_block_handling();
    test_multi_producer_single_consumer();
    test_multi_producer_ordering();
    test_performance_benchmark();

    g_result.print();
    return (g_result.failed == 0) ? 0 : 1;
}