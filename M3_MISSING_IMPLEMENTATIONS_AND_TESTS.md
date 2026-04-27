# M3_MISSING_IMPLEMENTATIONS_AND_TESTS.md
# QLog M3 缺失实现补全 + 完整单元测试

**日期**: 2026-04-27  
**涉及文件**: `log_buffer_defs.h` · `log_buffer.cpp` · `test/cpp/test_log_buffer.cpp`

---

## 目录

1. [构造函数审查](#1-构造函数审查)
2. [`~log_tls_buffer_info()` 实现](#2-log_tls_buffer_info-实现)
3. [`flush()` 实现](#3-flush-实现)
4. [`tls_guard` 防重入修正](#4-tls_guard-防重入修正)
5. [完整代码块汇总](#5-完整代码块汇总)
6. [完整单元测试文件](#6-完整单元测试文件)

---

## 1. 构造函数审查

### 现有代码

```cpp
log_buffer::log_buffer(
    uint32_t lp_capacity_bytes,
    uint32_t hp_capacity_per_thread_bytes,
    uint64_t hp_threshould
)
    : lp_buffer_(lp_capacity_bytes)
    , hp_capacity_per_thread_(hp_capacity_per_thread_bytes)
    , hp_threshold_(hp_threshould)
{}
```

### ✅ 结论：正确，无需修改

`lp_buffer_` 已通过初始化列表构造，`mpsc_ring_buffer` 接受 `capacity_bytes`
参数并在内部完成内存分配、block 初始化、cursor 清零，不需要再调用 `init()`。
两个数值成员同样正确初始化。

### 析构函数补充说明

```cpp
log_buffer::~log_buffer()
{
    // ✅ 现有实现正确：持写锁，delete 所有 hp_pool_ 条目
    std::unique_lock<spin_lock_rw> wlock(hp_pool_lock_);
    for (auto* entry : hp_pool_)
        delete entry;
    hp_pool_.clear();
    // ⚠️ M3 已知限制：析构时若有 LP 路径线程仍存活，其 tls_info->owner_buffer_
    //    将成为悬空指针。M3 假设 log_buffer 生命期 >= 所有写入线程的生命期。
}
```

---

## 2. `~log_tls_buffer_info()` 实现

### BqLog 对标分析

BqLog 的 `log_buffer::log_tls_buffer_info::~log_tls_buffer_info()`（`log_buffer.cpp` 顶部）
只清理 Java JNI 全局引用，C++ 侧无额外逻辑。

**QLog 析构流程**：

```
on_thread_exit(info)          ← 由 tls_guard::~tls_guard() 触发（线程退出时）
    └─ 写 is_thread_finished 标记到 LP buffer
    └─ info->is_thread_finished_ = true
    └─ info->cur_hp_buffer_ = nullptr

rt_read_from_lp()             ← 消费者读到 is_thread_finished 标记
    └─ commit_read_chunk(rh)  ← 消费该 LP entry
    └─ delete tls_info        ← 触发 ~log_tls_buffer_info()

~log_tls_buffer_info()        ← 对象被销毁，仅需防御性清零
    └─ cur_hp_buffer_ = nullptr
    └─ owner_buffer_  = nullptr
```

### 修改点：`log_buffer_defs.h`

将 `= default` 改为声明：

```cpp
// 修改前
~log_tls_buffer_info() = default;

// 修改后
~log_tls_buffer_info();   // 实现在 log_buffer.cpp
```

### 实现：`log_buffer.cpp`

```cpp
// ─────────────────────────────────────────────────────────────────────────────
// log_tls_buffer_info 析构函数
//
// 调用时机：消费者在 rt_read_from_lp() 中读到 is_thread_finished 标记后
//           执行 delete tls_info，触发此析构函数。
//
// 此时保证：
//   1. 对应生产者线程已退出（on_thread_exit 已写入 LP finish 标记）
//   2. 该线程的所有 LP/HP 写入均已被消费者读取并 commit
//   3. is_thread_finished_ == true（由 on_thread_exit 设置）
//
// BqLog 对标：log_buffer::log_tls_buffer_info::~log_tls_buffer_info()
//             （log_buffer.cpp 顶部，仅清理 Java 引用；C++ 侧同样为防御性清零）
// ─────────────────────────────────────────────────────────────────────────────
log_tls_buffer_info::~log_tls_buffer_info()
{
    // 防御性清零：防止析构后通过野指针访问已释放内存
    // on_thread_exit() 已将 cur_hp_buffer_ 设为 nullptr；此处再次确认
    cur_hp_buffer_ = nullptr;
    owner_buffer_  = nullptr;
}
```

---

## 3. `flush()` 实现

### 语义定义

`flush()` 的契约（见 `log_buffer.h` 注释）：

> "等待所有已提交数据可被消费"

即：调用 `flush()` 后，消费者线程发出的下一个 `read_chunk()` 的 `load_acquire`
能够观察到 `flush()` 之前所有 `commit_write_chunk()` 的写入。

### BqLog 对标

BqLog 无直接等价的 `log_buffer::flush()`；其刷新语义由 M7 的 Worker 线程的
`force_flush()` 提供（阻塞等待 Worker 消费完缓冲区）。QLog M3 尚无 Worker，
此处实现**内存可见性保证语义**，不等待消费完成。

### 实现：`log_buffer.cpp`

```cpp
// ─────────────────────────────────────────────────────────────────────────────
// flush() — 确保调用前所有已提交的写入对消费者线程可见
//
// 实现原理：
//   commit_write_chunk() 在 LP 路径通过 mpsc::commit_write_chunk() 的
//   atomic_signal_fence(release) 发布写入；在 HP 路径通过
//   spsc::commit_write_chunk() 的 write_cursor_.store_release() 发布。
//
//   flush() 在此基础上额外插入 seq_cst 全屏障，确保：
//   1. 本线程所有 store 在屏障前完成（StoreStore + StoreLoad 屏障）
//   2. 其他线程（消费者）随后的 load_acquire 能够观察到这些 store
//   3. 在弱序架构（ARM/POWER）上尤其必要
//
// 注意：flush() 仅保证"可见性"，不等待消费者读取完毕。
//       如需等待消费完成（drain），调用方应循环读取直到 read_chunk() 返回 nullptr。
//
// BqLog 对标：M7 worker::force_flush()（M3 简化版，仅内存屏障）
// ─────────────────────────────────────────────────────────────────────────────
void log_buffer::flush()
{
    std::atomic_thread_fence(std::memory_order_seq_cst);
}
```

---

## 4. `tls_guard` 防重入修正

### 问题

`get_tls_buffer_info()` 中的 `tls_guard::~tls_guard()` 缺少防重入检查。若
`on_thread_exit` 已经被调用（`is_thread_finished_ == true`），析构函数
不应再次调用。双重调用会导致：

1. LP buffer 中写入两条 finish 标记 → 消费者多执行一次 `delete tls_info`
   → **double-free / UAF**
2. `rt_data_.current_read_seq_` 被 delete 后访问 → **UB**

### 修正：`get_tls_buffer_info()` 中的 `tls_guard`

```cpp
log_tls_buffer_info& log_buffer::get_tls_buffer_info()
{
    if (tls_current_info_ != nullptr && tls_current_info_->owner_buffer_ == this)
        return *tls_current_info_;

    auto* info = new log_tls_buffer_info();
    info->owner_buffer_ = this;

    // RAII 守卫：线程退出时触发 on_thread_exit
    // static thread_local 确保每线程只初始化一次（M3 单 log_buffer 限制）
    struct tls_guard
    {
        log_tls_buffer_info* info;
        ~tls_guard()
        {
            // ✅ 防重入检查：is_thread_finished_ == true 表示已通知过消费者
            if (info != nullptr
                && info->owner_buffer_ != nullptr
                && !info->is_thread_finished_)
            {
                info->owner_buffer_->on_thread_exit(info);
            }
        }
    };
    static thread_local tls_guard guard{info};

    tls_current_info_ = info;
    return *info;
}
```

> ⚠️ **M3 已知限制**：`static thread_local tls_guard guard{info}` 只在
> 每个线程第一次调用此函数时初始化。若同一线程使用多个 `log_buffer` 实例，
> 仅第一个会在线程退出时触发 finish 标记。M3 假设每线程使用单一 `log_buffer`。

---

## 5. 完整代码块汇总

以下为三个文件的最终修改摘要，可直接对照现有代码做 patch。

### `src/qlog/buffer/log_buffer_defs.h`（一处修改）

```cpp
// 第 58 行附近，将
~log_tls_buffer_info() = default;
// 改为
~log_tls_buffer_info();
```

### `src/qlog/buffer/log_buffer.cpp`（三处新增/修改）

**新增 1** — 在文件顶部（命名空间 qlog 内，构造函数之前）添加析构函数：

```cpp
log_tls_buffer_info::~log_tls_buffer_info()
{
    cur_hp_buffer_ = nullptr;
    owner_buffer_  = nullptr;
}
```

**新增 2** — 在 `~log_buffer()` 之后添加 `flush()`：

```cpp
void log_buffer::flush()
{
    std::atomic_thread_fence(std::memory_order_seq_cst);
}
```

**修改 3** — 在 `get_tls_buffer_info()` 中修正 `tls_guard` 析构函数（见 §4）。

---

## 6. 完整单元测试文件

```cpp
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

/**
 * test_log_buffer.cpp — QLog M3 双路调度器单元测试
 *
 * 测试覆盖范围：
 *   Test 01  构造与析构
 *   Test 02  LP 路径：单次写入/读取/数据一致性
 *   Test 03  LP 路径：多条 entry 顺序读取
 *   Test 04  LP 路径：out_size 精度验证
 *   Test 05  LP 路径：seq 单调性验证（context_head 机制）
 *   Test 06  线程退出标记（is_thread_finished）自动处理
 *   Test 07  HP 路径触发与读取
 *   Test 08  HP/LP 混合场景（部分线程高频/部分低频）
 *   Test 09  多生产者 LP 压测（10 线程 × 1000 条）
 *   Test 10  flush() 内存屏障语义
 *   Test 11  缓冲区满时 alloc 失败处理
 *   Test 12  性能基准测试
 */

#include "qlog/buffer/log_buffer.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
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
        std::cout << "\n═══════════════════════════════════════════\n";
        std::cout << "  ✅ PASSED: " << passed << "\n";
        std::cout << "  ❌ FAILED: " << failed << "\n";
        std::cout << "═══════════════════════════════════════════\n";
    }
};

static TestResult g_result;

#define TEST_ASSERT(cond, msg)                                                        \
    do                                                                                \
    {                                                                                 \
        if (!(cond))                                                                  \
        {                                                                             \
            std::cerr << "  ❌ FAIL [line " << __LINE__ << "]: " << msg << "\n";      \
            g_result.failed++;                                                        \
        }                                                                             \
        else                                                                          \
        {                                                                             \
            std::cout << "  ✅ PASS: " << msg << "\n";                                \
            g_result.passed++;                                                        \
        }                                                                             \
    } while (0)

#define TEST_TRUE(cond, msg)  TEST_ASSERT((cond), msg)
#define TEST_FALSE(cond, msg) TEST_ASSERT(!(cond), msg)
#define TEST_EQ(a, b, msg)    TEST_ASSERT((a) == (b), msg)
#define TEST_NOT_NULL(p, msg) TEST_ASSERT((p) != nullptr, msg)
#define TEST_NULL(p, msg)     TEST_ASSERT((p) == nullptr, msg)

// ─────────────────────────────────────────────────────────────────────────────
// 辅助工具
// ─────────────────────────────────────────────────────────────────────────────

// 返回当前稳定时钟毫秒数（单调递增，适合 alloc_write_chunk 的 current_time_ms 参数）
static uint64_t now_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        )
            .count()
    );
}

// HP 路径触发时间戳：大于初始窗口 (0 + 1000ms)，确保第一次调用触发区间重置
// 之后固定使用此时间戳，使写入次数在同一区间内累积，从而触发高频判定
static constexpr uint64_t k_hp_trigger_time = 5000;

// LP 专用时间戳：通过极高阈值确保永远不升级到 HP
static constexpr uint64_t k_lp_force_time = 5000;

// 辅助：向 buffer 写入 N 条定长 entry，返回实际写入条数
// 参数说明：
//   buf        — 目标 log_buffer
//   n          — 希望写入的条数
//   payload    — 每条 entry 的 payload 大小（字节）
//   time_ms    — 传入 alloc_write_chunk 的时间戳
//   tag        — 写入每条 entry 首字节的标识（便于后续验证）
static int write_n_entries(
    qlog::log_buffer& buf, int n, uint32_t payload, uint64_t time_ms, uint8_t tag = 0
)
{
    int count = 0;
    for (int i = 0; i < n; ++i)
    {
        void* ptr = buf.alloc_write_chunk(payload, time_ms);
        if (ptr == nullptr)
            break;
        static_cast<uint8_t*>(ptr)[0] = tag;
        if (payload >= 4)
            *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(ptr) + 0) =
                static_cast<uint32_t>(i);
        buf.commit_write_chunk(ptr);
        ++count;
    }
    return count;
}

// 辅助：从 buffer 读出所有可读 entry，返回读取条数
// 消费者接口；验证 out_size 满足 expected_payload（0 表示不验证）
static int drain_all(
    qlog::log_buffer& buf, uint32_t expected_payload = 0, bool verify_seq = false
)
{
    int count = 0;
    uint32_t out_size = 0;
    int expected_val = 0;
    while (const void* ptr = buf.read_chunk(out_size))
    {
        if (expected_payload != 0)
        {
            assert(out_size == expected_payload);
        }
        if (verify_seq && out_size >= 4)
        {
            uint32_t val = *reinterpret_cast<const uint32_t*>(ptr);
            assert(val == static_cast<uint32_t>(expected_val));
            ++expected_val;
        }
        buf.commit_read_chunk(ptr);
        ++count;
    }
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 01：构造与析构
// ─────────────────────────────────────────────────────────────────────────────
void test_01_construction()
{
    std::cout << "\n[Test 01] Construction & Destruction\n";

    // 正常构造
    {
        qlog::log_buffer buf(64 * 1024, 32 * 1024);
        // 未调用任何 alloc/read，析构时应无崩溃
    }

    // 极小容量
    {
        qlog::log_buffer buf(4 * 1024, 4 * 1024);
        // 同样不崩溃
    }

    // 自定义 HP 阈值
    {
        qlog::log_buffer buf(128 * 1024, 32 * 1024, /* hp_threshold= */ 5);
    }

    TEST_TRUE(true, "construction and destruction complete without crash");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 02：LP 路径 — 单次写入/读取/数据一致性
// ─────────────────────────────────────────────────────────────────────────────
void test_02_lp_single_write_read()
{
    std::cout << "\n[Test 02] LP Path: Single Write / Read / Data Integrity\n";

    // 使用极高 HP 阈值强制走 LP 路径
    qlog::log_buffer buf(256 * 1024, 64 * 1024, /* hp_threshold= */ 100'000);

    constexpr uint32_t kPayload = 64;
    void* wptr = buf.alloc_write_chunk(kPayload, k_lp_force_time);
    TEST_NOT_NULL(wptr, "alloc_write_chunk(64) should succeed");

    if (wptr == nullptr)
        return;

    // 写入已知模式
    auto* data = static_cast<uint8_t*>(wptr);
    for (uint32_t i = 0; i < kPayload; ++i)
        data[i] = static_cast<uint8_t>(i ^ 0xAB);

    buf.commit_write_chunk(wptr);
    buf.flush();

    // 读取并验证
    uint32_t out_size = 0;
    const void* rptr = buf.read_chunk(out_size);
    TEST_NOT_NULL(rptr, "read_chunk should return data");
    TEST_EQ(out_size, kPayload, "out_size should equal payload size");

    if (rptr != nullptr)
    {
        const auto* rdata = static_cast<const uint8_t*>(rptr);
        bool match = true;
        for (uint32_t i = 0; i < kPayload; ++i)
        {
            if (rdata[i] != static_cast<uint8_t>(i ^ 0xAB))
            {
                match = false;
                break;
            }
        }
        TEST_TRUE(match, "data pattern should match byte-for-byte");
        buf.commit_read_chunk(rptr);
    }

    // 消费后缓冲区应为空
    uint32_t sz2 = 0;
    TEST_NULL(buf.read_chunk(sz2), "buffer should be empty after consuming single entry");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 03：LP 路径 — 多条 entry 顺序读取
// ─────────────────────────────────────────────────────────────────────────────
void test_03_lp_multiple_entries()
{
    std::cout << "\n[Test 03] LP Path: Multiple Entries in Order\n";

    qlog::log_buffer buf(512 * 1024, 64 * 1024, 100'000);

    constexpr int     kN       = 50;
    constexpr uint32_t kPayload = 32;

    // 写入 50 条，每条首 4 字节存序号
    for (int i = 0; i < kN; ++i)
    {
        void* ptr = buf.alloc_write_chunk(kPayload, k_lp_force_time);
        TEST_NOT_NULL(ptr, "alloc entry " + std::to_string(i));
        if (ptr)
        {
            *reinterpret_cast<uint32_t*>(ptr) = static_cast<uint32_t>(i);
            buf.commit_write_chunk(ptr);
        }
    }
    buf.flush();

    // 读取并验证顺序
    int read_count = 0;
    uint32_t out_size = 0;
    while (const void* rptr = buf.read_chunk(out_size))
    {
        uint32_t val = *reinterpret_cast<const uint32_t*>(rptr);
        TEST_EQ(static_cast<int>(val), read_count,
                "entry value should equal read order index " + std::to_string(read_count));
        TEST_EQ(out_size, kPayload, "out_size should be consistent");
        buf.commit_read_chunk(rptr);
        ++read_count;
    }
    TEST_EQ(read_count, kN, "should read exactly kN entries");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 04：LP 路径 — out_size 精度验证（变长 payload）
// ─────────────────────────────────────────────────────────────────────────────
void test_04_lp_out_size_accuracy()
{
    std::cout << "\n[Test 04] LP Path: out_size Accuracy (Variable Payload)\n";

    qlog::log_buffer buf(512 * 1024, 64 * 1024, 100'000);

    // 写入不同大小的 payload
    const uint32_t sizes[] = {1, 8, 16, 32, 64, 128, 256};
    for (uint32_t sz : sizes)
    {
        void* ptr = buf.alloc_write_chunk(sz, k_lp_force_time);
        TEST_NOT_NULL(ptr, "alloc size=" + std::to_string(sz));
        if (ptr)
            buf.commit_write_chunk(ptr);
    }
    buf.flush();

    int idx = 0;
    uint32_t out_size = 0;
    while (const void* rptr = buf.read_chunk(out_size))
    {
        TEST_EQ(out_size, sizes[idx],
                "out_size should match written size " + std::to_string(sizes[idx]));
        buf.commit_read_chunk(rptr);
        ++idx;
    }
    TEST_EQ(idx, static_cast<int>(sizeof(sizes) / sizeof(sizes[0])),
            "should read all variable-size entries");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 05：LP 路径 — seq 单调性（context_head 机制保证）
// ─────────────────────────────────────────────────────────────────────────────
void test_05_lp_seq_monotonicity()
{
    std::cout << "\n[Test 05] LP Path: Seq Monotonicity (context_head)\n";

    // 使用单独线程写入，使其拥有独立的 TLS seq 计数
    qlog::log_buffer buf(512 * 1024, 64 * 1024, 100'000);

    constexpr int kN = 100;
    std::thread producer([&]()
    {
        for (int i = 0; i < kN; ++i)
        {
            void* ptr = buf.alloc_write_chunk(sizeof(int), k_lp_force_time);
            if (ptr)
            {
                *static_cast<int*>(ptr) = i;
                buf.commit_write_chunk(ptr);
            }
        }
        // 线程退出 → tls_guard::~tls_guard() → on_thread_exit() → finish 标记
    });
    producer.join();  // 确保 finish 标记已写入
    buf.flush();

    // 消费者读取：顺序应严格递增
    int expected = 0;
    uint32_t out_size = 0;
    while (const void* rptr = buf.read_chunk(out_size))
    {
        int val = *static_cast<const int*>(rptr);
        TEST_EQ(val, expected, "seq should be monotonically increasing: " + std::to_string(expected));
        buf.commit_read_chunk(rptr);
        ++expected;
    }
    TEST_EQ(expected, kN, "should have read all kN entries in order");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 06：线程退出标记（is_thread_finished）自动处理
//
// 验证：线程退出后，消费者能读取线程写入的所有数据，
//       且 is_thread_finished 标记由 rt_read_from_lp 内部消费（不暴露给调用方），
//       最终 read_chunk() 返回 nullptr（而非多一条"空"entry）。
// ─────────────────────────────────────────────────────────────────────────────
void test_06_thread_exit_marker()
{
    std::cout << "\n[Test 06] Thread Exit Marker (is_thread_finished auto-consumed)\n";

    qlog::log_buffer buf(256 * 1024, 64 * 1024, 100'000);

    constexpr int     kN       = 10;
    constexpr uint32_t kPayload = sizeof(int);

    std::thread producer([&]()
    {
        for (int i = 0; i < kN; ++i)
        {
            void* ptr = buf.alloc_write_chunk(kPayload, k_lp_force_time);
            if (ptr)
            {
                *static_cast<int*>(ptr) = i;
                buf.commit_write_chunk(ptr);
            }
        }
        // 退出时 tls_guard 写 finish 标记到 LP buffer
    });
    producer.join();
    buf.flush();

    // 消费者读取：不应看到 finish 标记（它被 rt_read_from_lp 内部消费）
    int read_count = 0;
    int extra_null_after_first = 0;
    uint32_t out_size = 0;

    while (const void* rptr = buf.read_chunk(out_size))
    {
        int val = *static_cast<const int*>(rptr);
        TEST_EQ(val, read_count, "value should match index " + std::to_string(read_count));
        TEST_EQ(out_size, kPayload, "out_size should equal kPayload");
        buf.commit_read_chunk(rptr);
        ++read_count;
    }

    TEST_EQ(read_count, kN, "should read exactly kN user entries (finish marker NOT exposed)");

    // 再次 read_chunk 应返回 nullptr（缓冲区已空，finish 标记已消费）
    uint32_t sz2 = 0;
    const void* extra = buf.read_chunk(sz2);
    TEST_NULL(extra, "subsequent read_chunk should return nullptr");
    (void)extra_null_after_first;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 07：HP 路径触发与读取
//
// 触发条件：同一时间窗口内写入次数 >= hp_threshold
// 设置 hp_threshold=3，使用固定时间戳 k_hp_trigger_time
// ─────────────────────────────────────────────────────────────────────────────
void test_07_hp_path_trigger_and_read()
{
    std::cout << "\n[Test 07] HP Path: Trigger and Read\n";

    // hp_threshold=3：写 3 次后触发 HP
    qlog::log_buffer buf(256 * 1024, 128 * 1024, /* hp_threshold= */ 3);

    constexpr uint32_t kPayload = sizeof(uint32_t);
    constexpr int kTotal = 10;  // 写 10 条，前 3 条 LP，后 7 条 HP

    for (int i = 0; i < kTotal; ++i)
    {
        // 使用固定时间戳确保所有写入在同一时间窗口内
        void* ptr = buf.alloc_write_chunk(kPayload, k_hp_trigger_time);
        TEST_NOT_NULL(ptr, "alloc entry " + std::to_string(i) + " should succeed");
        if (ptr)
        {
            *reinterpret_cast<uint32_t*>(ptr) = static_cast<uint32_t>(i);
            buf.commit_write_chunk(ptr);
        }
    }
    buf.flush();

    // 读取验证（HP + LP 混合结果，顺序可能因路由不同而略有差异）
    // 此处只验证总数和数据有效性
    int read_count = 0;
    uint32_t out_size = 0;
    while (const void* rptr = buf.read_chunk(out_size))
    {
        TEST_EQ(out_size, kPayload, "out_size should equal kPayload for HP entry");
        buf.commit_read_chunk(rptr);
        ++read_count;
    }
    TEST_EQ(read_count, kTotal, "should read all entries (HP + LP combined)");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 08：HP/LP 混合场景（两个线程：一个高频触发 HP，一个低频走 LP）
// ─────────────────────────────────────────────────────────────────────────────
void test_08_hp_lp_mixed()
{
    std::cout << "\n[Test 08] HP/LP Mixed: One High-Freq Thread + One Low-Freq Thread\n";

    // hp_threshold=5：写 5 次触发 HP
    qlog::log_buffer buf(512 * 1024, 128 * 1024, /* hp_threshold= */ 5);

    constexpr int     kHighFreqN = 50;   // 高频线程写入条数
    constexpr int     kLowFreqN  = 10;   // 低频线程写入条数
    constexpr uint32_t kPayload  = sizeof(uint32_t);

    std::atomic<int> total_written{0};

    // 高频线程：固定时间戳，第 5 次起触发 HP
    std::thread high_freq([&]()
    {
        for (int i = 0; i < kHighFreqN; ++i)
        {
            void* ptr = buf.alloc_write_chunk(kPayload, k_hp_trigger_time);
            if (ptr)
            {
                *reinterpret_cast<uint32_t*>(ptr) = static_cast<uint32_t>(i) | 0x80000000u;
                buf.commit_write_chunk(ptr);
                total_written.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    // 低频线程：使用递增时间戳，每次都在新的时间窗口，写入次数不累积 → 始终 LP
    std::thread low_freq([&]()
    {
        for (int i = 0; i < kLowFreqN; ++i)
        {
            // 每次用新时间戳（递增超过 1000ms），确保每次都触发区间重置 → 始终低频
            uint64_t t = k_hp_trigger_time + static_cast<uint64_t>(i + 1) * 2000;
            void* ptr = buf.alloc_write_chunk(kPayload, t);
            if (ptr)
            {
                *reinterpret_cast<uint32_t*>(ptr) = static_cast<uint32_t>(i) | 0x40000000u;
                buf.commit_write_chunk(ptr);
                total_written.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    high_freq.join();
    low_freq.join();
    buf.flush();

    int read_count = 0;
    uint32_t out_size = 0;
    while (const void* rptr = buf.read_chunk(out_size))
    {
        TEST_EQ(out_size, kPayload, "out_size should equal kPayload");
        buf.commit_read_chunk(rptr);
        ++read_count;
    }

    TEST_EQ(read_count, total_written.load(), "should read all written entries (HP + LP)");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 09：多生产者 LP 压测（10 线程 × 1000 条）
//
// 验证 MPSC LP buffer 在真实多线程场景下的正确性。
// 重点：总读取数 == 总写入数，且无 TSan 警告。
// ─────────────────────────────────────────────────────────────────────────────
void test_09_multi_producer_lp_stress()
{
    std::cout << "\n[Test 09] Multi-Producer LP Stress (10 threads × 1000 entries)\n";

    qlog::log_buffer buf(8 * 1024 * 1024, 64 * 1024, 100'000);

    constexpr int kNumProducers = 10;
    constexpr int kPerProducer  = 1000;
    constexpr uint32_t kPayload = sizeof(uint32_t);

    std::atomic<int> total_written{0};
    std::atomic<bool> all_done{false};

    // 生产者线程组
    std::vector<std::thread> producers;
    producers.reserve(kNumProducers);
    for (int p = 0; p < kNumProducers; ++p)
    {
        producers.emplace_back([&, p]()
        {
            for (int i = 0; i < kPerProducer; ++i)
            {
                // 每条 entry 使用递增时间戳（同线程同窗口内写 1000 次仍不触发 HP，
                // 但使用 100'000 阈值更安全）
                void* ptr = buf.alloc_write_chunk(kPayload, k_lp_force_time);
                while (ptr == nullptr)
                {
                    std::this_thread::yield();
                    ptr = buf.alloc_write_chunk(kPayload, k_lp_force_time);
                }
                // 高 16 位存 producer id，低 16 位存 entry index
                *reinterpret_cast<uint32_t*>(ptr) =
                    (static_cast<uint32_t>(p) << 16) | static_cast<uint32_t>(i);
                buf.commit_write_chunk(ptr);
                total_written.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // 消费者（主线程）
    int read_count = 0;
    int total_expected = kNumProducers * kPerProducer;
    uint32_t out_size = 0;

    while (read_count < total_expected)
    {
        const void* rptr = buf.read_chunk(out_size);
        if (rptr != nullptr)
        {
            buf.commit_read_chunk(rptr);
            ++read_count;
        }
        else
        {
            bool any_alive = false;
            for (auto& t : producers)
            {
                if (t.joinable())
                {
                    any_alive = true;
                    break;
                }
            }
            if (!any_alive && total_written.load() == total_expected)
                break;
            std::this_thread::yield();
        }
    }

    for (auto& t : producers)
        t.join();

    // 再 drain 一次防止还有剩余
    while (const void* rptr = buf.read_chunk(out_size))
    {
        buf.commit_read_chunk(rptr);
        ++read_count;
    }

    TEST_EQ(read_count, total_expected,
            "multi-producer: should read all " + std::to_string(total_expected) + " entries");
    TEST_EQ(total_written.load(), total_expected, "total_written should equal expected");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 10：flush() 内存屏障语义
//
// 验证：生产者写入后调用 flush()，消费者立即能读到数据。
// 注意：flush() 保证可见性，不等待消费完成（这是调用方的责任）。
// ─────────────────────────────────────────────────────────────────────────────
void test_10_flush_memory_barrier()
{
    std::cout << "\n[Test 10] flush() Memory Barrier Semantics\n";

    qlog::log_buffer buf(256 * 1024, 64 * 1024, 100'000);

    constexpr int     kN       = 20;
    constexpr uint32_t kPayload = sizeof(uint32_t);

    // 生产者线程写入 + flush
    std::atomic<bool> producer_flushed{false};
    std::thread producer([&]()
    {
        for (int i = 0; i < kN; ++i)
        {
            void* ptr = buf.alloc_write_chunk(kPayload, k_lp_force_time);
            if (ptr)
            {
                *reinterpret_cast<uint32_t*>(ptr) = static_cast<uint32_t>(i);
                buf.commit_write_chunk(ptr);
            }
        }
        buf.flush();
        producer_flushed.store(true, std::memory_order_release);
    });

    // 消费者等待 flush 信号后读取
    while (!producer_flushed.load(std::memory_order_acquire))
        std::this_thread::yield();

    producer.join();

    int read_count = 0;
    uint32_t out_size = 0;
    while (const void* rptr = buf.read_chunk(out_size))
    {
        buf.commit_read_chunk(rptr);
        ++read_count;
    }
    TEST_EQ(read_count, kN, "after flush, all writes should be visible to consumer");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 11：缓冲区满时 alloc 失败处理
//
// 验证：LP buffer 满时 alloc_write_chunk 返回 nullptr，
//       消费一条后可以再次 alloc 成功（空间释放）。
// ─────────────────────────────────────────────────────────────────────────────
void test_11_buffer_full_handling()
{
    std::cout << "\n[Test 11] Buffer Full: alloc returns nullptr, then recovers\n";

    // 小 buffer：容易填满
    qlog::log_buffer buf(16 * 1024, 4 * 1024, 100'000);

    constexpr uint32_t kPayload = 64;
    int success_count = 0;

    // 不断写入直到第一次失败
    while (true)
    {
        void* ptr = buf.alloc_write_chunk(kPayload, k_lp_force_time);
        if (ptr == nullptr)
            break;
        buf.commit_write_chunk(ptr);
        ++success_count;
        if (success_count > 10000)
            break;  // 安全上限
    }
    TEST_TRUE(success_count > 0, "should succeed at least once before full");

    // 缓冲区满时再次 alloc 失败
    void* ptr_fail = buf.alloc_write_chunk(kPayload, k_lp_force_time);
    TEST_NULL(ptr_fail, "alloc should fail when buffer is full");

    // 消费一条后空间释放
    uint32_t out_size = 0;
    const void* rptr = buf.read_chunk(out_size);
    TEST_NOT_NULL(rptr, "should read one entry from full buffer");
    if (rptr)
        buf.commit_read_chunk(rptr);

    // 现在可以再次 alloc
    void* ptr_retry = buf.alloc_write_chunk(kPayload, k_lp_force_time);
    TEST_NOT_NULL(ptr_retry, "alloc should succeed after consuming one entry");
    if (ptr_retry)
        buf.commit_write_chunk(ptr_retry);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 12：性能基准测试
//
// 单生产者 + 单消费者并发，测量端到端吞吐量
// 目标（参考 BqLog 对标）：> 2M entries/s（LP 路径单线程）
// ─────────────────────────────────────────────────────────────────────────────
void test_12_performance_benchmark()
{
    std::cout << "\n[Test 12] Performance Benchmark (1 Producer + 1 Consumer)\n";

    constexpr int     kEntries  = 1'000'000;
    constexpr uint32_t kPayload = 32;

    qlog::log_buffer buf(32 * 1024 * 1024, 2 * 1024 * 1024, 100'000);

    std::atomic<bool>  start_gate{false};
    std::atomic<int>   write_count{0};
    std::atomic<int>   read_count{0};
    std::atomic<bool>  producer_done{false};

    std::thread producer([&]()
    {
        while (!start_gate.load(std::memory_order_acquire))
            ;
        for (int i = 0; i < kEntries; ++i)
        {
            void* ptr;
            do
            {
                ptr = buf.alloc_write_chunk(kPayload, k_lp_force_time);
                if (ptr == nullptr)
                    std::this_thread::yield();
            } while (ptr == nullptr);
            *reinterpret_cast<uint32_t*>(ptr) = static_cast<uint32_t>(i);
            buf.commit_write_chunk(ptr);
            write_count.fetch_add(1, std::memory_order_relaxed);
        }
        buf.flush();
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]()
    {
        while (!start_gate.load(std::memory_order_acquire))
            ;
        uint32_t out_size = 0;
        while (read_count.load(std::memory_order_relaxed) < kEntries)
        {
            const void* rptr = buf.read_chunk(out_size);
            if (rptr != nullptr)
            {
                buf.commit_read_chunk(rptr);
                read_count.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                if (producer_done.load(std::memory_order_acquire))
                    break;
                std::this_thread::yield();
            }
        }
        // 最后一次 drain（防止 producer_done 与 read 之间的竞争）
        while (const void* rptr = buf.read_chunk(out_size))
        {
            buf.commit_read_chunk(rptr);
            read_count.fetch_add(1, std::memory_order_relaxed);
        }
    });

    auto start_time = std::chrono::high_resolution_clock::now();
    start_gate.store(true, std::memory_order_release);

    producer.join();
    consumer.join();

    auto end_time   = std::chrono::high_resolution_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                          end_time - start_time)
                          .count();

    TEST_EQ(write_count.load(), kEntries, "should write all entries");
    TEST_EQ(read_count.load(),  kEntries, "should read all entries");

    double ns_per_entry = elapsed_us > 0
        ? static_cast<double>(elapsed_us) * 1000.0 / kEntries
        : 0.0;
    double entries_per_sec = elapsed_us > 0
        ? static_cast<double>(kEntries) * 1'000'000.0 / elapsed_us
        : 0.0;

    std::cout << "  Throughput: " << static_cast<int>(entries_per_sec / 1e6) << "."
              << (static_cast<int>(entries_per_sec / 1e5) % 10) << "M entries/s\n";
    std::cout << "  Latency:    " << ns_per_entry << " ns/entry\n";

    TEST_TRUE(entries_per_sec > 500'000.0, "throughput should exceed 500K entries/s");
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    std::cout << "╔══════════════════════════════════════════╗\n";
    std::cout << "║   QLog M3 log_buffer Unit Tests         ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";

    test_01_construction();
    test_02_lp_single_write_read();
    test_03_lp_multiple_entries();
    test_04_lp_out_size_accuracy();
    test_05_lp_seq_monotonicity();
    test_06_thread_exit_marker();
    test_07_hp_path_trigger_and_read();
    test_08_hp_lp_mixed();
    test_09_multi_producer_lp_stress();
    test_10_flush_memory_barrier();
    test_11_buffer_full_handling();
    test_12_performance_benchmark();

    g_result.print();
    return g_result.failed == 0 ? 0 : 1;
}
```

---

## 附录 A：各函数 BqLog 对标速查

| QLog 函数 | BqLog 对应 | 关键对齐点 |
|-----------|-----------|-----------|
| `~log_tls_buffer_info()` | `log_buffer::log_tls_buffer_info::~log_tls_buffer_info()` | 防御性清零；消费者调用 delete |
| `flush()` | M7 `worker::force_flush()` | M3 简化为内存屏障；M7 实现等待 Worker 消费完毕 |
| `tls_guard::~tls_guard()` | `log_tls_info::~log_tls_info()` | 写 finish 标记；需防重入（`is_thread_finished_` 检查） |

---

## 附录 B：已知限制（M3 范围内可接受）

| 限制 | 描述 | 计划修复 |
|------|------|---------|
| 单 log_buffer TLS | `static thread_local tls_guard` 每线程只绑定第一个 log_buffer；多实例场景后续 buffer 的 finish 标记不发送 | M8 log_manager 统一管理多 buffer TLS |
| flush() 不等待消费完成 | 仅内存屏障，不阻塞直到 read_chunk() 返回 nullptr | M7 Worker 实现真正的 force_flush() |
| log_buffer 先于线程销毁 | 若 log_buffer 析构时线程仍存活，TLS 中的 `owner_buffer_` 成为悬空指针 | M8 log_manager 管理生命期顺序 |
| mpsc status 非原子写 | `commit_write_chunk` 使用 `atomic_signal_fence` 而非原子 store；在弱序架构上可能有可见性问题 | M2 补丁：改用 `reinterpret_cast<atomic<uint8_t>&>` |

---

**文档版本**: 1.0  
**适用 QLog**: M3  
**BqLog 参考**: v3 `log_buffer.{h,cpp}`
