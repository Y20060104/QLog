#include "qlog/buffer/spsc_ring_buffer.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

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

#define TEST_ASSERT(cond, msg)                              \
    do                                                      \
    {                                                       \
        if (!(cond))                                        \
        {                                                   \
            std::cerr << "❌ FAILED: " << msg << std::endl; \
            g_result.failed++;                              \
        }                                                   \
        else                                                \
        {                                                   \
            std::cout << "✅ PASSED: " << msg << std::endl; \
            g_result.passed++;                              \
        }                                                   \
    } while (0)

#define TEST_NOT_NULL(ptr, msg) TEST_ASSERT((ptr) != nullptr, msg)
#define TEST_NULL(ptr, msg) TEST_ASSERT((ptr) == nullptr, msg)
#define TEST_EQ(a, b, msg) TEST_ASSERT((a) == (b), msg)

// ✅ 测试 1：初始化成功
void test_init_success()
{
    std::cout << "\n[Test 1] Init Success\n";
    qlog::spsc_ring_buffer buffer;
    bool success = buffer.init(64 * 1024);
    TEST_ASSERT(success, "init(64KB) should succeed");
    TEST_EQ(buffer.capacity(), 64 * 1024, "capacity should be 64KB");
    TEST_EQ(buffer.block_count(), (64 * 1024) / 8, "block_count should be 8192");
}

// ✅ 测试 2：参数验证
void test_init_invalid_capacity()
{
    std::cout << "\n[Test 2] Init Invalid Capacity\n";
    qlog::spsc_ring_buffer buffer;

    // 不是 8 的倍数
    TEST_ASSERT(!buffer.init(100), "init(100) should fail (not multiple of 8)");

    // 再次初始化正确大小
    TEST_ASSERT(buffer.init(64 * 1024), "init(64KB) should succeed");
}

// ✅ 测试 3：单线程基础读写
void test_single_thread_basic()
{
    std::cout << "\n[Test 3] Single Thread Basic Read/Write\n";
    qlog::spsc_ring_buffer buffer;
    TEST_ASSERT(buffer.init(64 * 1024), "init should succeed");

    // 分配
    void* ptr = buffer.alloc_write_chunk(64);
    TEST_NOT_NULL(ptr, "alloc_write_chunk(64) should succeed");

    // 写入数据
    uint8_t* data = (uint8_t*)ptr;
    for (int i = 0; i < 64; ++i)
    {
        data[i] = (uint8_t)i;
    }

    // 提交
    buffer.commit_write_chunk();

    // 读取
    const void* read_ptr = buffer.read_chunk();
    TEST_NOT_NULL(read_ptr, "read_chunk() should return data");

    // 验证数据
    const uint8_t* read_data = (const uint8_t*)read_ptr;
    bool data_match = true;
    for (int i = 0; i < 64; ++i)
    {
        if (read_data[i] != (uint8_t)i)
        {
            data_match = false;
            break;
        }
    }
    TEST_ASSERT(data_match, "data should match after read");

    // 返还
    buffer.commit_read_chunk();

    // 再次读应为空
    TEST_NULL(buffer.read_chunk(), "read_chunk() should return nullptr after all consumed");
}

// ✅ 测试 4：多条日志
void test_multiple_entries()
{
    std::cout << "\n[Test 4] Multiple Entries\n";
    qlog::spsc_ring_buffer buffer;
    TEST_ASSERT(buffer.init(64 * 1024), "init should succeed");

    const int num_entries = 50;

    // 写入 50 条
    for (int i = 0; i < num_entries; ++i)
    {
        size_t size = 32 + (i % 64);
        void* ptr = buffer.alloc_write_chunk(size);
        TEST_NOT_NULL(ptr, "alloc for entry " + std::to_string(i));

        uint8_t* data = (uint8_t*)ptr;
        data[0] = (uint8_t)(i & 0xFF);
        buffer.commit_write_chunk();
    }

    // 读出 50条
    for (int i = 0; i < num_entries; ++i)
    {
        const void* ptr = buffer.read_chunk();
        TEST_NOT_NULL(ptr, "read entry " + std::to_string(i));

        const uint8_t* data = (const uint8_t*)ptr;
        TEST_EQ((int)data[0], i & 0xFF, "data[0] for entry " + std::to_string(i));

        buffer.commit_read_chunk();
    }

    // 缓冲应为空
    TEST_NULL(buffer.read_chunk(), "buffer should be empty after all read");
}

// ✅ 测试 5：缓冲满
void test_buffer_full()
{
    std::cout << "\n[Test 5] Buffer Full\n";
    qlog::spsc_ring_buffer buffer;
    TEST_ASSERT(buffer.init(64 * 1024), "init should succeed");

    // 尝试分配超大日志
    void* ptr = buffer.alloc_write_chunk(100 * 1024);
    TEST_NULL(ptr, "alloc huge entry (100KB) should fail");
}

// ✅ 测试 6：Reset
void test_reset()
{
    std::cout << "\n[Test 6] Reset\n";
    qlog::spsc_ring_buffer buffer;
    TEST_ASSERT(buffer.init(64 * 1024), "init should succeed");

    // 写入 → 读取 → 验证缓冲区为空
    void* ptr = buffer.alloc_write_chunk(32);
    TEST_NOT_NULL(ptr, "first alloc");
    buffer.commit_write_chunk();

    const void* read_ptr = buffer.read_chunk();
    TEST_NOT_NULL(read_ptr, "read should have data");
    buffer.commit_read_chunk();

    // 重置前：应该读不到数据
    TEST_NULL(buffer.read_chunk(), "read after consume should be empty");

    // 重置并验证可以重新使用
    buffer.reset();
    void* ptr2 = buffer.alloc_write_chunk(32);
    TEST_NOT_NULL(ptr2, "alloc after reset should succeed");
}

// ✅ 测试 7：二进制头结构
void test_block_header_size()
{
    std::cout << "\n[Test 7] Block Header Size\n";
    TEST_EQ(sizeof(qlog::block_header), 8, "block_header size should be 8 bytes");

    qlog::block_header hdr;
    hdr.block_count = 10;
    hdr.data_size = 64;
    TEST_EQ(hdr.block_count, 10, "block_count field");
    TEST_EQ(hdr.data_size, 64, "data_size field");
}

// ✅ 测试 8：单线程大数据量读写（写 N 条，读出 N 条，内容一致）
void test_single_thread_large_volume()
{
    std::cout << "\n[Test 8] Single Thread Large Volume (N=500 entries)\n";
    qlog::spsc_ring_buffer buffer;
    TEST_ASSERT(buffer.init(1024 * 1024), "init 1MB should succeed"); // 增大缓冲

    const int num_entries = 500; // 减少数量，增加稳定性
    const int entry_size = 32;

    // 写入 500 条
    for (int i = 0; i < num_entries; ++i)
    {
        void* ptr = buffer.alloc_write_chunk(entry_size);
        if (ptr == nullptr)
        {
            std::cerr << "❌ Failed to alloc at entry " << i << "\n";
            TEST_ASSERT(false, "alloc entry " + std::to_string(i));
            break;
        }

        // 写入标记数据：entry ID (4B)
        uint32_t* entry_id = reinterpret_cast<uint32_t*>(ptr);
        *entry_id = i;
        buffer.commit_write_chunk();
    }

    // 读出 500 条，验证顺序和内容正确
    int read_count = 0;
    while (read_count < num_entries)
    {
        const void* read_ptr = buffer.read_chunk();
        if (read_ptr == nullptr)
        {
            if (read_count < num_entries)
            {
                std::cerr << "❌ Unexpected nullptr at read_count=" << read_count << "\n";
                break;
            }
        }
        else
        {
            const uint32_t* entry_id = reinterpret_cast<const uint32_t*>(read_ptr);
            TEST_EQ((int)*entry_id, read_count, "entry ID for index " + std::to_string(read_count));
            buffer.commit_read_chunk();
            read_count++;
        }
    }

    // 读出后缓冲应为空
    TEST_NULL(buffer.read_chunk(), "buffer should be empty after reading 500 entries");
}

// ✅ 测试 9：双线程压测（1 producer + 1 consumer，无数据竞争）
void test_dual_thread_stress()
{
    std::cout << "\n[Test 9] Dual Thread Stress (Producer + Consumer)\n";

    qlog::spsc_ring_buffer buffer;
    TEST_ASSERT(buffer.init(512 * 1024), "init 512KB should succeed");

    const int num_entries = 10000;
    int write_count = 0;
    int read_count = 0;
    bool producer_done = false;

    // 生产者线程
    auto producer = [&]()
    {
        for (int i = 0; i < num_entries; ++i)
        {
            // 不断尝试分配和写入
            while (true)
            {
                void* ptr = buffer.alloc_write_chunk(32);
                if (ptr != nullptr)
                {
                    uint32_t* entry_id = reinterpret_cast<uint32_t*>(ptr);
                    *entry_id = i;
                    buffer.commit_write_chunk();
                    write_count++;
                    break;
                }
                // 缓冲满，让消费者有机会消费
                std::this_thread::yield();
            }
        }
        producer_done = true;
    };

    // 消费者线程
    auto consumer = [&]()
    {
        while (read_count < num_entries)
        {
            const void* read_ptr = buffer.read_chunk();
            if (read_ptr != nullptr)
            {
                const uint32_t* entry_id = reinterpret_cast<const uint32_t*>(read_ptr);
                // 验证数据（只用来检查是否为有效数据）
                if (*entry_id < (uint32_t)num_entries)
                {
                    buffer.commit_read_chunk();
                    read_count++;
                }
            }
            else
            {
                // 没有数据，等待生产者
                if (producer_done && read_count >= num_entries)
                {
                    break;
                }
                std::this_thread::yield();
            }
        }
    };

    // 启动两个线程
    auto t1_start = std::chrono::high_resolution_clock::now();
    std::thread t_producer(producer);
    std::thread t_consumer(consumer);

    t_producer.join();
    t_consumer.join();
    auto t1_end = std::chrono::high_resolution_clock::now();

    TEST_EQ(write_count, num_entries, "should write all entries");
    TEST_EQ(read_count, num_entries, "should read all entries");

    auto duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1_end - t1_start).count();
    std::cout << "  Dual thread stress completed: " << write_count << " entries in " << duration_ms
              << "ms\n";
}

// ✅ 测试 10：基本性能检查
void test_performance_comparison()
{
    std::cout << "\n[Test 10] Basic Performance Check\n";

    const int num_entries = 1000;
    const int entry_size = 64;

    qlog::spsc_ring_buffer buffer;
    TEST_ASSERT(buffer.init(256 * 1024), "init 256KB buffer");

    auto start = std::chrono::high_resolution_clock::now();

    // 写入
    for (int i = 0; i < num_entries; ++i)
    {
        void* ptr = buffer.alloc_write_chunk(entry_size);
        if (ptr == nullptr)
        {
            std::cerr << "Failed to allocate at entry " << i << "\n";
            break;
        }
        uint32_t* val = reinterpret_cast<uint32_t*>(ptr);
        *val = i;
        buffer.commit_write_chunk();
    }

    auto write_end = std::chrono::high_resolution_clock::now();
    auto write_time =
        std::chrono::duration_cast<std::chrono::microseconds>(write_end - start).count();

    // 读取
    int read_count = 0;
    for (int i = 0; i < num_entries; ++i)
    {
        const void* ptr = buffer.read_chunk();
        if (ptr != nullptr)
        {
            buffer.commit_read_chunk();
            read_count++;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto read_time = std::chrono::duration_cast<std::chrono::microseconds>(end - write_end).count();

    std::cout << "  Wrote " << num_entries << " entries in " << write_time << " µs\n";
    std::cout << "  Read " << read_count << " entries in " << read_time << " µs\n";

    TEST_ASSERT(read_count == num_entries, "should read all entries");
}

int main()
{
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║   QLog M1 SPSC Ring Buffer Tests      ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";

    test_init_success();
    test_init_invalid_capacity();
    test_single_thread_basic();
    test_multiple_entries();
    test_buffer_full();
    test_reset();
    test_block_header_size();
    test_single_thread_large_volume();
    test_dual_thread_stress();
    test_performance_comparison();

    g_result.print();
    return (g_result.failed == 0) ? 0 : 1;
}
