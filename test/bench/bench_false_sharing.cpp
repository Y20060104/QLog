#include <atomic>
#include <cstdint>
#include <thread>
#include <chrono>
#include <iostream>
#include <iomanip>

/**
 * False Sharing 演示
 *
 * 演示场景：
 * - 两个线程分别修改同一结构体的两个字段
 * - Unaligned: a 和 b 在同一 cache line（会产生 false sharing）
 * - Aligned: a 和 b 在不同 cache line（无 false sharing）
 *
 * 预期结果：Unaligned 会慢 10-100 倍
 */

// ❌ 无对齐版本：false sharing
struct Unaligned {
    std::atomic<uint64_t> a;  // 8 bytes
    std::atomic<uint64_t> b;  // 8 bytes, 在同一 cache line!
};

// ✅ 有对齐版本：无 false sharing
struct Aligned {
    char pad0[64];             // padding 使 a 在一个 cache line
    std::atomic<uint64_t> a;  // 8 bytes
    char pad[56];              // padding 使 b 在下一个 cache line
    std::atomic<uint64_t> b;  // 8 bytes, 不同 cache line
    char pad2[56];             // 额外 padding 确保结构体大小为 128 bytes
};

double benchmark_unaligned() {
    Unaligned data;
    data.a.store(0, std::memory_order_relaxed);
    data.b.store(0, std::memory_order_relaxed);

    auto start = std::chrono::high_resolution_clock::now();

    // 线程 1: 不断修改字段 a
    std::thread t1([&]() {
        for (int i = 0; i < 10000000; ++i) {
            data.a.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // 线程 2: 不断修改字段 b（在同一 cache line！）
    std::thread t2([&]() {
        for (int i = 0; i < 10000000; ++i) {
            data.b.fetch_add(1, std::memory_order_relaxed);
        }
    });

    t1.join();
    t2.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << std::left << std::setw(30) << "Unaligned (false sharing):"
              << std::setw(10) << elapsed.count() << " seconds" << std::endl;

    return elapsed.count();
}

double benchmark_aligned() {
    Aligned data;
    data.a.store(0, std::memory_order_relaxed);
    data.b.store(0, std::memory_order_relaxed);

    auto start = std::chrono::high_resolution_clock::now();

    // 线程 1: 修改字段 a
    std::thread t1([&]() {
        for (int i = 0; i < 10000000; ++i) {
            data.a.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // 线程 2: 修改字段 b（在不同 cache line！）
    std::thread t2([&]() {
        for (int i = 0; i < 10000000; ++i) {
            data.b.fetch_add(1, std::memory_order_relaxed);
        }
    });

    t1.join();
    t2.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << std::left << std::setw(30) << "Aligned (no false sharing):"
              << std::setw(10) << elapsed.count() << " seconds" << std::endl;

    return elapsed.count();
}

int main() {
    std::cout << "\n========================================\n";
    std::cout << "False Sharing Benchmark\n";
    std::cout << "========================================\n\n";

    std::cout << "Structure sizes:\n";
    std::cout << "  Unaligned: " << sizeof(Unaligned) << " bytes\n";
    std::cout << "  Aligned:   " << sizeof(Aligned) << " bytes\n";
    std::cout << "  Cache line size: 64 bytes\n\n";

    std::cout << "Running benchmark (10M iterations per thread)...\n\n";

    double time_unaligned = benchmark_unaligned();
    double time_aligned = benchmark_aligned();

    double ratio = time_unaligned / time_aligned;

    std::cout << "\n========================================\n";
    std::cout << "Results:\n";
    std::cout << "  Ratio: " << std::fixed << std::setprecision(2)
              << ratio << "x\n";

    if (ratio > 5.0) {
        std::cout << "  ✓ False sharing clearly visible!\n";
    } else {
        std::cout << "  ⚠ False sharing not prominent (may depend on CPU)\n";
    }

    std::cout << "========================================\n\n";

    return 0;
}
