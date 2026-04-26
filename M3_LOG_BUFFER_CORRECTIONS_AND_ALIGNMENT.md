# M3_LOG_BUFFER_CORRECTIONS_AND_ALIGNMENT.md
# QLog M3 代码审查：问题修正与 BqLog 对齐分析

**版本**: 1.0  
**日期**: 2026-04-26  
**审查范围**: `log_buffer.cpp` / `log_buffer.h` / `log_buffer_defs.h`  
**参考 BqLog**: `/home/qq344/BqLog/src/bq_log/types/buffer/log_buffer.{h,cpp}`

---

## 目录

1. [问题总览](#1-问题总览)
2. [BqLog 核心模式分析（先读）](#2-bqlog-核心模式分析先读)
3. [文件一：`spin_lock_rw.h` — 添加 STL 兼容接口](#3-文件一spin_lock_rwh--添加-stl-兼容接口)
4. [文件二：`spsc_ring_buffer.h/.cpp` — 添加 `last_read_data_size()`](#4-文件二spsc_ring_bufferh--cpp--添加-last_read_data_size)
5. [文件三：`log_buffer_defs.h` — 修正 padding 计算](#5-文件三log_buffer_defsh--修正-padding-计算)
6. [文件四：`log_buffer.h` — 补充 `rt_state_t` 缓存字段](#6-文件四log_bufferh--补充-rt_state_t-缓存字段)
7. [文件五：`log_buffer.cpp` — 全部修正（逐函数）](#7-文件五log_buffercpp--全部修正逐函数)
8. [BqLog 对齐验证表](#8-bqlog-对齐验证表)
9. [修改后快速验证清单](#9-修改后快速验证清单)

---

## 1. 问题总览

| # | 文件 | 函数 | 问题类型 | 严重度 |
|---|------|------|---------|--------|
| P1 | `spin_lock_rw.h` | — | 缺少 STL 兼容接口，`std::shared_lock<spin_lock_rw>` 无法编译 | 🔴 编译失败 |
| P2 | `spsc_ring_buffer.h` | `read_chunk` | 无法获取读取到的数据大小，`out_size` 无法赋值 | 🔴 逻辑错误 |
| P3 | `log_buffer_defs.h` | `padding0_` | 计算未考虑编译器自动插入的对齐 padding，可能导致 `static_assert` 失败 | 🟡 潜在崩溃 |
| P4 | `log_buffer.h` | `rt_state_t` | 缺少 `pending_lp_rh_` 字段，`commit_read_chunk` LP 路径无法还原 `read_handle` | 🔴 运行时错误 |
| P5 | `log_buffer.cpp` | `commit_write_chunk` | LP 路径错误地覆写 `pending_lp_wh_.data`，破坏 cursor 信息 | 🔴 数据丢失/错位 |
| P6 | `log_buffer.cpp` | `read_chunk` (HP) | 缺少读锁保护；`out_size` 赋值不正确 | 🔴 数据竞争 (TSan 失败) |
| P7 | `log_buffer.cpp` | `read_chunk` (LP) | 使用递归而非 BqLog 的 `while(true)` 循环；LP `read_handle` 未缓存到 `rt_state_` | 🔴 栈溢出风险 + 逻辑错误 |
| P8 | `log_buffer.cpp` | `commit_read_chunk` (LP) | 变量名 bug：`pending_hp_ptr_` 应为 `pending_lp_ptr_`；`cursor`/`block_count` 为 0 | 🔴 崩溃/内存破坏 |
| P9 | `log_buffer.cpp` | `get_or_create_hp_buffer` | 缺少写锁，多线程并发添加 HP buffer 时有数据竞争 | 🔴 数据竞争 (TSan 失败) |

---

## 2. BqLog 核心模式分析（先读）

在修改代码前，先理解 BqLog M3 核心的三个设计决策。

### 2.1 LP 路径读取：`while(true)` 循环而非递归

**BqLog 位置**: `log_buffer.cpp::rt_read_from_lp_buffer()`（文档 document #31）

```cpp
// BqLog 的 LP 读取核心模式
bool log_buffer::rt_read_from_lp_buffer(log_buffer_read_handle& out_handle)
{
    while (true) {                            // ← while 循环，不是递归
        auto read_handle = lp_buffer_.read_chunk();
        if (read_handle.result != success) {
            lp_buffer_.return_read_chunk(read_handle);
            return false;                     // 缓冲区空，返回 false
        }
        const auto& ctx = *reinterpret_cast<const context_head*>(read_handle.data_addr);
        auto verify = verify_context(ctx);

        switch (verify) {
        case valid:
            if (!ctx.is_thread_finished_) {
                out_handle = read_handle;     // ← 有效数据：输出
                return true;
            }
            // is_thread_finished: 清理 TLS，消费掉，continue 循环
            deregister_seq(ctx);
            lp_buffer_.return_read_chunk(read_handle);
            break;                            // ← break switch = continue while(true)

        case seq_invalid:
        case version_invalid:
            lp_buffer_.return_read_chunk(read_handle); // 消费并跳过
            break;

        case seq_pending:
        case version_pending:
            lp_buffer_.discard_read_chunk(read_handle); // ← 不消费！保持游标不动
            return false;                     // 本轮无法读取，等下次
        }
    }
}
```

**QLog M3 对应决策**：
- `is_thread_finished` → 消费（`commit_read_chunk`） + **循环**，不返回
- `seq_invalid`（seq < expected，过期数据）→ 消费 + 循环
- `seq_pending`（seq > expected，更早 entry 还未到）→ **不消费** + 返回 nullptr

### 2.2 LP `discard_read_chunk` 的语义

BqLog 的 `discard_read_chunk` = 设置 handle.result 为 empty，**不推进 read_cursor**。  
QLog 的 `mpsc_ring_buffer::read_chunk()` 本身就不推进游标（只有 `commit_read_chunk` 才推进），因此：

```
QLog "discard" = 直接 return nullptr，不调用 commit_read_chunk
下次 read_chunk() 重新读到同一条 entry ← 游标未变
```

### 2.3 HP 路径大小获取

BqLog 的 `siso_ring_buffer` 的 `read_chunk()` 返回 `log_buffer_read_handle`，其中包含 `data_size`。  
QLog 的 `spsc_ring_buffer::read_chunk()` 返回 `const void*`，大小藏在 `block_header` 中，需要额外接口暴露。

---

## 3. 文件一：`spin_lock_rw.h` — 添加 STL 兼容接口

### 问题（P1）

```cpp
// log_buffer.cpp 中的用法（当前无法编译）
std::shared_lock<spin_lock_rw> rlock(hp_pool_lock_);
```

`std::shared_lock<T>` 要求 T 实现 `lock_shared()` / `unlock_shared()`；  
`std::unique_lock<T>` 要求 T 实现 `lock()` / `unlock()`。  
用户的 `spin_lock_rw` 只有 `read_lock()`/`read_unlock()`/`write_lock()`/`write_unlock()`。

### BqLog 参考

BqLog 使用自己的 `scoped_spin_lock_read_crazy` / `scoped_spin_lock_write_crazy` RAII 包装。  
QLog 应同样封装，最简单方式：在 `spin_lock_rw` 中添加别名方法。

### 修正方案

在 `src/qlog/primitives/spin_lock_rw.h` 的 `public:` 区域末尾添加：

```cpp
// STL SharedMutex 兼容接口（用于 std::shared_lock / std::unique_lock）
void lock_shared()   { read_lock(); }
void unlock_shared() { read_unlock(); }
void lock()          { write_lock(); }
void unlock()        { write_unlock(); }
bool try_lock()
{
    uint32_t state = state_.load(std::memory_order_relaxed);
    if (state & WRITE_BIT) return false;
    uint32_t desired = state | WRITE_BIT;
    return state_.compare_exchange_strong(
        state, desired, std::memory_order_acquire, std::memory_order_relaxed);
}
```

这样 `std::shared_lock<spin_lock_rw>` 和 `std::unique_lock<spin_lock_rw>` 均可使用。

---

## 4. 文件二：`spsc_ring_buffer.h/.cpp` — 添加 `last_read_data_size()`

### 问题（P2）

`log_buffer::read_chunk()` 的 HP 分支需要返回 `out_size`，但 `spsc_ring_buffer::read_chunk()` 返回 `const void*`，没有暴露数据大小。

### BqLog 参考

BqLog `siso_ring_buffer::read_chunk()` 返回 `log_buffer_read_handle`，其中包含 `data_size`。  
QLog 简化版中 `block_header` 里已有 `data_size` 字段，只需用辅助方法暴露。

### 修正：`spsc_ring_buffer.h` 添加声明

在 `public:` 的查询接口区域添加：

```cpp
// 返回上次 read_chunk() 读到的数据字节数（不含 block_header）
// 必须在 read_chunk() 返回非 nullptr 后、commit_read_chunk() 之前调用
uint32_t last_read_data_size() const;
```

### 修正：`spsc_ring_buffer.cpp` 添加实现

```cpp
uint32_t spsc_ring_buffer::last_read_data_size() const
{
    if (!buffer_) return 0;
    // rt_read_cursor_cached_ 指向当前未消费的块的起始位置
    // 该块的 block_header.data_size 即为用户数据大小
    const uint8_t* header_ptr =
        buffer_ + (rt_read_cursor_cached_ << k_block_size_log2);
    return reinterpret_cast<const block_header*>(header_ptr)->data_size;
}
```

> ⚠️ **调用时机约束**：此方法只能在 `read_chunk()` 返回非 nullptr 后、`commit_read_chunk()` 之前调用。  
> 如果 `read_chunk()` 返回 nullptr，调用结果未定义。

---

## 5. 文件三：`log_buffer_defs.h` — 修正 padding 计算

### 问题（P3）

当前 `padding0_` 的计算：

```cpp
char padding0_[64 - sizeof(uint64_t)*2 - sizeof(spsc_ring_buffer*)
               - sizeof(log_buffer*) - sizeof(bool) - sizeof(write_handle)];
```

**Bug**：`sizeof(write_handle)` 本身是 24 字节，但编译器在 `bool is_thread_finished_` 和 `write_handle pending_lp_wh_` 之间会自动插入 **7 字节对齐 padding**（因为 `write_handle` 的对齐要求是 8 字节）。

实际内存布局：

```
偏移  0: last_update_epoch_ms_ (8B)
偏移  8: update_times_         (8B)
偏移 16: cur_hp_buffer_        (8B)
偏移 24: owner_buffer_         (8B)
偏移 32: is_thread_finished_   (1B)
偏移 33: [编译器插入 7B padding]
偏移 40: pending_lp_wh_        (24B)
偏移 64: padding0_[7]          ← 溢出！冷段已满，这 7B 属于 wt_data_ 区域
```

`static_assert(sizeof(log_tls_buffer_info) % 64 == 0)` 会因总大小变为 192+7=199 字节而**编译失败**。

### 修正方案

移除手动 `padding0_`，让编译器自动管理冷段对齐。冷段（0\~63）由以下字段自然填满：

```
8 + 8 + 8 + 8 + 1 + 7(编译器padding) + 24 = 64 字节
```

完整的 `log_buffer_defs.h` 修正版本如下：

```cpp
// src/qlog/buffer/log_buffer_defs.h
#pragma once

#include "qlog/buffer/mpsc_ring_buffer.h"
#include "qlog/primitives/aligned_alloc.h"
#include "qlog/primitives/atomic.h"

#include <cstddef>
#include <cstdint>

namespace qlog
{

// ─────────────────────────────────────────────────────────────────────────────
// context_head — LP 路径每条 entry 的前缀（16 字节）
// 严格对标 BqLog log_buffer.h:208
// sizeof == 16，alignof == 8，不可更改
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)
struct alignas(8) context_head
{
    uint16_t version_;             // QLog M3: 始终为 0（无 mmap 恢复）
    bool     is_thread_finished_;  // 线程退出标记
    bool     is_external_ref_;     // QLog M3: 始终为 false（无 oversize 路径）
    uint32_t seq_;                 // 同一线程单调递增的写入序列号

    // 跨平台指针存储（确保 32/64 位下总大小均为 16 字节）
#if defined(__LP64__) || defined(_WIN64)
    struct alignas(8) { void* ptr; } tls_info_;
#else
    struct alignas(8) { void* ptr; uint32_t _pad; } tls_info_;
#endif

    void* get_tls_ptr() const   { return tls_info_.ptr; }
    void  set_tls_info_ptr(void* p) { tls_info_.ptr = p; }
};
#pragma pack(pop)

static_assert(sizeof(context_head) == 16,
    "[QLog] context_head MUST be 16 bytes");
static_assert(alignof(context_head) == 8,
    "[QLog] context_head MUST be aligned to 8 bytes");


// ─────────────────────────────────────────────────────────────────────────────
// log_tls_buffer_info — 每线程每 log_buffer 的 TLS 状态
// 严格对标 BqLog log_buffer.h:143
// 三段 cache line 布局：冷数据(64B) / 写侧热(64B) / 读侧热(64B)
// ─────────────────────────────────────────────────────────────────────────────
class spsc_ring_buffer;
class log_buffer;

struct alignas(64) log_tls_buffer_info
{
    // ── 冷数据段（cache line 0，偏移 0~63）────────────────────────────────
    // 字段布局（64 位系统）：
    //   [0 ]  last_update_epoch_ms_  8B
    //   [8 ]  update_times_          8B
    //   [16]  cur_hp_buffer_         8B
    //   [24]  owner_buffer_          8B
    //   [32]  is_thread_finished_    1B
    //   [33]  <编译器插入 7B padding 以对齐 write_handle>
    //   [40]  pending_lp_wh_         24B（bool1+pad3+u32_4+ptr8+u32_4+pad4）
    //   [64]  → cache line 边界
    // 合计：64 字节（无需手动 padding）
    uint64_t          last_update_epoch_ms_ = 0;
    uint64_t          update_times_         = 0;
    spsc_ring_buffer* cur_hp_buffer_        = nullptr;
    log_buffer*       owner_buffer_         = nullptr;
    bool              is_thread_finished_   = false;
    write_handle      pending_lp_wh_;  // alloc 到 commit 之间缓存，避免 commit 时重建

    // ── 写侧热数据段（cache line 1，偏移 64~127）──────────────────────────
    // 写线程频繁递增 current_write_seq_，独占 cache line 防止 false sharing
    alignas(64) struct
    {
        uint32_t current_write_seq_ = 0;
        char     _pad[64 - sizeof(uint32_t)];
    } wt_data_;

    // ── 读侧热数据段（cache line 2，偏移 128~191）─────────────────────────
    // 消费者频繁读取/递增 current_read_seq_，独占 cache line
    alignas(64) struct
    {
        uint32_t current_read_seq_ = 0;
        char     _pad[64 - sizeof(uint32_t)];
    } rt_data_;

    ~log_tls_buffer_info() = default;
};

// ── 布局验证（编译期，必须全部通过）──────────────────────────────────────
static_assert(sizeof(log_tls_buffer_info) % 64 == 0,
    "[QLog] log_tls_buffer_info must be cache-line sized");
static_assert(offsetof(log_tls_buffer_info, wt_data_) % 64 == 0,
    "[QLog] wt_data_ must be cache-line aligned");
static_assert(offsetof(log_tls_buffer_info, rt_data_) % 64 == 0,
    "[QLog] rt_data_ must be cache-line aligned");

} // namespace qlog
```

---

## 6. 文件四：`log_buffer.h` — 补充 `rt_state_t` 缓存字段

### 问题（P4）

`commit_read_chunk` LP 路径需要用 `read_handle`（含 `cursor` 和 `block_count`）调用 `lp_buffer_.commit_read_chunk(rh)`，但当前 `rt_state_t` 只存了 `pending_lp_ptr_`，没有存完整的 `read_handle`。

### 修正

在 `log_buffer.h` 中将 `rt_state_t` 修改为：

```cpp
// 消费者状态（单消费者，无竞争）
struct rt_state_t
{
    size_t           hp_pool_read_index_  = 0;       // 当前 HP pool 轮询下标
    const void*      pending_lp_ptr_      = nullptr; // 等待 commit 的 LP entry 数据指针
    const void*      pending_hp_ptr_      = nullptr; // 等待 commit 的 HP entry 数据指针
    hp_buffer_entry* pending_hp_entry_    = nullptr; // 对应的 HP buffer entry
    read_handle      pending_lp_rh_;                 // ← 新增：缓存完整的 LP read_handle
                                                     //   用于 commit_read_chunk 还原 cursor
    active_read_src  last_read_src_ = active_read_src::none;
} rt_state_;
```

> **原因**：`mpsc_ring_buffer::commit_read_chunk(rh)` 需要 `rh.cursor` 和 `rh.block_count`，  
> 这两个值只在 `read_chunk()` 时获得，必须缓存到下一次 `commit_read_chunk()` 被调用。

---

## 7. 文件五：`log_buffer.cpp` — 全部修正（逐函数）

### 7.1 `alloc_write_chunk` — LP 路径（轻微修正）

当前代码逻辑正确（`pending_lp_wh_ = wh`），但需确认 `has_pending_lp_write_` 字段  
在 `log_buffer_defs.h` 新版中已移除，代码中也不再需要这个标志位。

**无需修改 `alloc_write_chunk` 的主体逻辑**，仅确认以下片段正确：

```cpp
// ✅ 正确：将完整 wh 缓存到 tls（包含 cursor、block_count、data 指针）
tls.pending_lp_wh_ = wh;
return wh.data + sizeof(context_head); // 返回 context_head 之后的用户写入区域
```

---

### 7.2 `commit_write_chunk` — LP 路径（修正 P5）

**当前错误代码**：

```cpp
// ❌ 错误：覆写了 pending_lp_wh_.data，破坏了 cursor 信息
tls.pending_lp_wh_.data =
    static_cast<uint8_t*>(data_ptr) - static_cast<ptrdiff_t>(sizeof(context_head));
lp_buffer_.commit_write_chunk(tls.pending_lp_wh_);
```

**问题**：`mpsc_ring_buffer::commit_write_chunk` 只用 `handle.cursor` 来定位 block（不用 `handle.data`）。  
覆写 `data` 字段无意义，且覆写操作本身表明设计者对 `pending_lp_wh_` 存储目的有误解。

**BqLog 对应位置**：`log_buffer.cpp:124-130`，LP commit 时直接用缓存的 context_head 起始指针。

**修正后**：

```cpp
void log_buffer::commit_write_chunk(void* data_ptr)
{
    if (data_ptr == nullptr)
        return;

    log_tls_buffer_info& tls = get_tls_buffer_info();

    if (tls.cur_hp_buffer_ != nullptr)
    {
        // ── HP 路径 ──────────────────────────────────────────────────────
        // spsc_ring_buffer::commit_write_chunk() 无参数，内部自动推进 cursor
        tls.cur_hp_buffer_->commit_write_chunk();
    }
    else
    {
        // ── LP 路径 ──────────────────────────────────────────────────────
        // pending_lp_wh_ 在 alloc 时已完整缓存（cursor、block_count 均正确）
        // mpsc::commit_write_chunk 只使用 cursor，不使用 data 指针
        // ✅ 直接提交，无需任何修正
        lp_buffer_.commit_write_chunk(tls.pending_lp_wh_);
    }
}
```

---

### 7.3 `read_chunk` HP 分支（修正 P6）

**当前问题**：
1. 锁被注释掉（`// std::shared_lock<spin_lock_rw> rlock(hp_pool_lock_);`）
2. `out_size` 未赋值（`out_size=entry->buffer;` 是错误代码）

**BqLog 对应**：HP 路径读取用读锁保护池遍历，`data_size` 从 HP buffer 的 handle 中取。

**修正后**：

```cpp
const void* log_buffer::read_chunk(uint32_t& out_size)
{
    // ── 优先读 HP pool（对标 BqLog HP 优先遍历策略）──────────────────────
    {
        // 读锁：保护 hp_pool_ 向量（生产者偶尔 push_back 新 entry）
        // 修复 P1 后 std::shared_lock<spin_lock_rw> 已可编译
        std::shared_lock<spin_lock_rw> rlock(hp_pool_lock_);

        const size_t n = hp_pool_.size();
        for (size_t i = 0; i < n; ++i)
        {
            const size_t idx = (rt_state_.hp_pool_read_index_ + i) % n;
            hp_buffer_entry* entry = hp_pool_[idx];

            const void* ptr = entry->buffer.read_chunk();
            if (ptr != nullptr)
            {
                rt_state_.hp_pool_read_index_ = idx;
                rt_state_.pending_hp_ptr_     = ptr;
                rt_state_.pending_hp_entry_   = entry;
                rt_state_.last_read_src_       = active_read_src::hp;

                // ✅ 修复 P6：从 block_header.data_size 取大小
                // last_read_data_size() 读取 rt_read_cursor_cached_ 对应块的 header
                out_size = entry->buffer.last_read_data_size();
                return ptr;
            }
        }
    } // 读锁释放

    // ── 读 LP buffer ─────────────────────────────────────────────────────
    return rt_read_from_lp(out_size);
}
```

---

### 7.4 `rt_read_from_lp`（修正 P7，新函数提取）

将 LP 读取逻辑提取为单独的 `rt_read_from_lp()`，避免 `read_chunk` 过长。  
这里实现 **BqLog `rt_read_from_lp_buffer` 的核心模式：`while(true)` 循环**。

**BqLog 模式对比**：

| BqLog 情形 | BqLog 处理 | QLog M3 处理 |
|-----------|-----------|------------|
| `is_thread_finished = true` | `deregister_seq` + `return_read_chunk` + break（continue loop） | 递增 seq + delete tls_info + `commit_read_chunk` + continue |
| `seq_invalid`（seq < expected） | `return_read_chunk` + break | `commit_read_chunk`（跳过） + continue |
| `seq_pending`（seq > expected） | `discard_read_chunk` + return false | **不调用** `commit_read_chunk` + return nullptr |
| `valid` + 正常数据 | 输出 + return true | 缓存 `rh` + return data |

**修正后的完整 `rt_read_from_lp`**：

```cpp
const void* log_buffer::rt_read_from_lp(uint32_t& out_size)
{
    // ── BqLog 对标：while(true) 循环（非递归）────────────────────────────
    // 对应 BqLog log_buffer.cpp::rt_read_from_lp_buffer() 的核心 while 循环
    while (true)
    {
        read_handle rh = lp_buffer_.read_chunk();
        if (!rh.success)
        {
            // LP buffer 为空，本轮无数据
            return nullptr;
        }

        // ── 解析 context_head ─────────────────────────────────────────
        const auto* ctx = reinterpret_cast<const context_head*>(rh.data);

        // 取出对应线程的 TLS 状态
        auto* tls_info = reinterpret_cast<log_tls_buffer_info*>(ctx->get_tls_ptr());
        if (tls_info == nullptr)
        {
            // 无效 context，跳过（消费掉）
            lp_buffer_.commit_read_chunk(rh);
            continue;
        }

        const uint32_t expected_seq = tls_info->rt_data_.current_read_seq_;

        // ── seq 校验（对标 BqLog verify_context）────────────────────────
        if (ctx->seq_ == expected_seq)
        {
            // ── 情形 A：seq 匹配，合法数据 ───────────────────────────
            tls_info->rt_data_.current_read_seq_++;  // 推进 expected_seq

            if (ctx->is_thread_finished_)
            {
                // ── 情形 A1：线程退出标记（is_thread_finished = true）
                // 对标 BqLog deregister_seq + return_read_chunk + break（continue loop）
                // 1. 消费这条 finish 标记（推进 mpsc read_cursor）
                lp_buffer_.commit_read_chunk(rh);
                // 2. 延迟释放 TLS（消费者负责，生产者线程已退出）
                delete tls_info;
                // 3. 继续循环，尝试读下一条（finish 标记无用户数据）
                continue;
            }

            // ── 情形 A2：正常数据 ─────────────────────────────────────
            // 缓存完整 read_handle 供 commit_read_chunk 使用（修复 P4/P8）
            rt_state_.pending_lp_rh_  = rh;
            rt_state_.pending_lp_ptr_ = rh.data + sizeof(context_head);
            rt_state_.last_read_src_  = active_read_src::lp;

            // 返回用户数据（跳过 context_head 前缀，透明给上层）
            out_size = rh.data_size - static_cast<uint32_t>(sizeof(context_head));
            return rt_state_.pending_lp_ptr_;
        }
        else if (ctx->seq_ > expected_seq)
        {
            // ── 情形 B：seq_pending（更早的 entry 还未到达）
            // 对标 BqLog discard_read_chunk：不推进 mpsc read_cursor
            // 下次 read_chunk() 仍读到这条 entry
            // 不调用 commit_read_chunk ← 这就是 QLog 的 discard 语义
            return nullptr;
        }
        else
        {
            // ── 情形 C：seq_invalid（seq < expected，过期/已处理数据）
            // 对标 BqLog version_invalid/seq_invalid：消费并跳过
            lp_buffer_.commit_read_chunk(rh);
            continue;
        }
    }
}
```

> ⚠️ **注意**：`rt_read_from_lp` 需要在 `log_buffer.h` 中声明：
> ```cpp
> const void* rt_read_from_lp(uint32_t& out_size);
> ```

---

### 7.5 `commit_read_chunk`（修正 P8）

**当前错误代码**：

```cpp
case active_read_src::lp:
{
    // ❌ Bug 1: pending_hp_ptr_ 应为 pending_lp_ptr_
    const uint8_t* lp_data_start =
        static_cast<const uint8_t*>(rt_state_.pending_hp_ptr_);
    read_handle rh;
    rh.success = true;
    rh.data = const_cast<uint8_t*>(lp_data_start);
    rh.cursor = 0;      // ❌ Bug 2: cursor = 0，mpsc 会从错误位置操作
    rh.data_size = 0;
    rh.block_count = 0; // ❌ Bug 3: block_count = 0，read_cursor 不会正确推进
    lp_buffer_.commit_read_chunk(rh);
    ...
}
```

**修正后**：

```cpp
void log_buffer::commit_read_chunk(const void* data_ptr)
{
    if (data_ptr == nullptr)
        return;

    switch (rt_state_.last_read_src_)
    {
    case active_read_src::hp:
    {
        // ── HP 路径：spsc commit（无参数，内部用 rt_read_cursor_cached_ 定位）
        rt_state_.pending_hp_entry_->buffer.commit_read_chunk();
        rt_state_.pending_hp_ptr_   = nullptr;
        rt_state_.pending_hp_entry_ = nullptr;
        break;
    }
    case active_read_src::lp:
    {
        // ── LP 路径：使用缓存的完整 read_handle（含正确的 cursor + block_count）
        // 修复 P8：
        //   - 不再从 data_ptr 反推（容易出错）
        //   - 直接使用 rt_state_.pending_lp_rh_（在 rt_read_from_lp 中缓存）
        lp_buffer_.commit_read_chunk(rt_state_.pending_lp_rh_);
        rt_state_.pending_lp_ptr_              = nullptr;
        rt_state_.pending_lp_rh_.success       = false; // 标记为已处理
        break;
    }
    default:
        break;
    }

    rt_state_.last_read_src_ = active_read_src::none;
}
```

---

### 7.6 `get_or_create_hp_buffer`（修正 P9）

**当前问题**：写锁被注释掉（"你这样设计需要修改我的原实现吧"）

**解答**：在 P1 中已为 `spin_lock_rw` 添加了 `lock()`/`unlock()` 接口，现在可以直接使用 `std::unique_lock<spin_lock_rw>` 加写锁。

**BqLog 对应**：`group_list::alloc_new_block()` 持有写锁添加新节点。

**修正后**：

```cpp
spsc_ring_buffer* log_buffer::get_or_create_hp_buffer(log_tls_buffer_info& tls_info)
{
    // 冷路径：创建新的 HP buffer 并注册到池中
    // 允许 new（冷路径，线程首次注册，不在 hot path 上）
    auto* entry = new hp_buffer_entry();
    entry->tls_info = &tls_info;

    if (!entry->buffer.init(hp_capacity_per_thread_))
    {
        delete entry;
        return nullptr;
    }

    // ✅ 写锁保护 hp_pool_.push_back
    // 对标 BqLog group_list::alloc_new_block() 持有 write_lock
    // 修复 P9：确保多线程同时创建 HP buffer 时不产生数据竞争
    {
        std::unique_lock<spin_lock_rw> wlock(hp_pool_lock_);
        hp_pool_.push_back(entry);
    }

    return &entry->buffer;
}
```

---

### 7.7 `on_thread_exit` — 补充说明

当前实现结构正确，但调用 `alloc_write_chunk_internal_lp` 是不存在的函数。  
正确做法是复用现有接口，但需绕过频率检测（退出时强制走 LP 路径）。

**修正后**：

```cpp
void log_buffer::on_thread_exit(log_tls_buffer_info* info)
{
    // 对标 BqLog log_tls_info::~log_tls_info()
    // 在 LP buffer 中写入 size=0 的 is_thread_finished 标记
    // 消费者读到后负责 delete info

    info->is_thread_finished_ = true;

    // 清理 HP buffer 引用（标记 inactive，消费者排干后自然回收）
    if (info->cur_hp_buffer_ != nullptr)
    {
        // 找到对应的 hp_buffer_entry，标记 is_active = false
        std::shared_lock<spin_lock_rw> rlock(hp_pool_lock_);
        for (hp_buffer_entry* entry : hp_pool_)
        {
            if (entry->tls_info == info)
            {
                entry->is_active = false;
                break;
            }
        }
        info->cur_hp_buffer_ = nullptr;
    }

    // 向 LP buffer 写入 is_thread_finished 标记（size=0，纯 context_head）
    // 直接调用 mpsc alloc，绕过频率检测（退出时强制 LP）
    constexpr uint32_t k_finish_payload_size = 0;
    const uint32_t total_alloc =
        k_finish_payload_size + static_cast<uint32_t>(sizeof(context_head));

    write_handle wh = lp_buffer_.alloc_write_chunk(total_alloc);
    if (wh.success)
    {
        auto* ctx = reinterpret_cast<context_head*>(wh.data);
        ctx->version_            = 0;
        ctx->is_thread_finished_ = true;   // ← 关键标记
        ctx->is_external_ref_    = false;
        ctx->seq_                = info->wt_data_.current_write_seq_++;
        ctx->set_tls_info_ptr(info);

        // 直接提交（不经过 pending_lp_wh_ 缓存，因为 tls 即将失效）
        lp_buffer_.commit_write_chunk(wh);
    }
    // 如果 LP buffer 满：finish 标记写入失败，TLS 可能泄漏
    // M3 简化处理：接受此边界情况（BqLog 有重试机制，QLog M3 不实现）
}
```

---

## 8. BqLog 对齐验证表

### 8.1 数据结构对齐

| 项目 | BqLog 值 | QLog M3 修正后 | 对齐度 |
|------|---------|--------------|--------|
| `sizeof(context_head)` | 16 | 16 | ✅ 100% |
| `alignof(context_head)` | 8 | 8 | ✅ 100% |
| `sizeof(log_tls_buffer_info)` | 192（3×64） | 192（3×64） | ✅ 100% |
| `offsetof(wt_data_)` | 64 | 64 | ✅ 100% |
| `offsetof(rt_data_)` | 128 | 128 | ✅ 100% |
| LP entry 前缀大小 | 16（context_head） | 16 | ✅ 100% |

### 8.2 算法对齐

| 算法点 | BqLog 实现 | QLog M3 修正后 | 对齐度 |
|--------|-----------|--------------|--------|
| LP 读取循环结构 | `while(true)` | `while(true)` | ✅ 100% |
| `is_thread_finished` 处理 | deregister + return_chunk + break(continue) | commit + delete + continue | ✅ 等价 |
| `seq_pending` 处理 | `discard_read_chunk`（不推进游标） | 不调 `commit_read_chunk`（同效果） | ✅ 等价 |
| `seq_invalid` 处理 | `return_read_chunk` + break(continue) | `commit_read_chunk` + continue | ✅ 等价 |
| HP 路径写 commit | `spsc::commit_write_chunk`（无参数） | 同 | ✅ 100% |
| LP 路径写 commit | 直接用缓存 handle | 直接用 `pending_lp_wh_`（cursor 正确） | ✅ 100% |
| HP 读取优先于 LP | HP pool 先遍历 | HP pool 先遍历 | ✅ 100% |
| HP 读锁粒度 | 读锁保护 group_list 遍历 | 读锁保护 hp_pool_ 遍历 | ✅ 等价 |
| 新 HP buffer 写锁 | write_lock 添加新 block | write_lock 添加 entry | ✅ 100% |
| context 透明性 | 上层拿到 data_addr+16 | 上层拿到 data+16 | ✅ 100% |
| return 时偏移修正 | `data_addr -= 16` | 用缓存 `pending_lp_rh_`（含原始游标） | ✅ 等价 |

### 8.3 允许的差异（QLog M3 简化）

| BqLog 功能 | QLog M3 | 理由 |
|-----------|---------|------|
| `group_list` 动态块池 | TLS `spsc_ring_buffer` 静态分配 | group_list 超出 M3 范围 |
| mmap 崩溃恢复 | 不实现 | M9 专项 |
| `version` 多代数据验证 | 版本固定为 0 | 无 mmap 则无版本概念 |
| `MAX_RECOVERY_VERSION_RANGE` | 不实现 | 依赖 mmap |
| `oversize_buffer` | 不实现 | 可选功能 |

---

## 9. 修改后快速验证清单

按以下顺序验证，任意一步失败即停止修复。

### Step 1：编译时 static_assert 验证

```bash
./scripts/build.sh Debug 2>&1 | grep -E "static_assert|error:"
# 期望：0 个 static_assert 错误
# 重点检查：
#   sizeof(context_head) == 16
#   sizeof(log_tls_buffer_info) % 64 == 0
#   offsetof(wt_data_) % 64 == 0
#   offsetof(rt_data_) % 64 == 0
```

### Step 2：单元测试

```bash
./scripts/build.sh Release
./scripts/test.sh
# 期望：test_log_buffer（如已编写）全部通过
```

### Step 3：TSan（必须 0 races）

```bash
./scripts/run_sanitizers.sh thread
# 关注以下并发访问点：
#   hp_pool_ 的读写（修复 P6/P9）
#   tls_current_info_ 的线程局部性
#   wt_data_ / rt_data_ 的 false sharing 防护
```

### Step 4：ASan（必须 0 errors）

```bash
./scripts/run_sanitizers.sh address
# 关注：
#   on_thread_exit 中 delete tls_info 的时机
#   pending_lp_rh_ 使用已 commit 的 handle
```

### Step 5：代码对齐复核

完成以上测试后，在 `STATE.md` 中将 M3 状态更新为：

```markdown
### ✅ M3: 双路 Buffer 调度器 (完成 ✅)
- [x] alloc_write_chunk 频率检测 + HP/LP 路由 ✅
- [x] commit_write_chunk LP/HP 路径正确 ✅
- [x] read_chunk while(true) 循环（BqLog 对齐）✅
- [x] commit_read_chunk LP cursor 正确还原 ✅
- [x] 线程退出 is_thread_finished 处理 ✅
- [x] hp_pool_ 读写锁保护 ✅
- [x] context_head 透明（上层无感知）✅
- [x] TSan 0 races ✅
- [x] ASan 0 errors ✅
```

---

## 附录：修改文件汇总

| 文件 | 修改内容 | 影响范围 |
|------|---------|---------|
| `src/qlog/primitives/spin_lock_rw.h` | 添加 `lock_shared` / `unlock_shared` / `lock` / `unlock` / `try_lock` 别名方法 | 解锁 `std::shared_lock` / `std::unique_lock` 的使用 |
| `src/qlog/buffer/spsc_ring_buffer.h` | 添加 `last_read_data_size()` 声明 | HP 读取时 `out_size` 赋值 |
| `src/qlog/buffer/spsc_ring_buffer.cpp` | 实现 `last_read_data_size()` | 同上 |
| `src/qlog/buffer/log_buffer_defs.h` | 移除 `padding0_`，修正 `log_tls_buffer_info` 布局注释 | 修正 `static_assert` 潜在失败 |
| `src/qlog/buffer/log_buffer.h` | `rt_state_t` 新增 `pending_lp_rh_` 字段；声明 `rt_read_from_lp` | LP commit 路径 |
| `src/qlog/buffer/log_buffer.cpp` | 5 处修正（见 §7.2~7.6）；提取 `rt_read_from_lp` 函数 | 全部生产者/消费者逻辑 |

---

**文档版本**: 1.0  
**适用 QLog 版本**: M3（依赖 M0/M1/M2）  
**BqLog 参考版本**: v3（`/home/qq344/BqLog/src/bq_log/types/buffer/log_buffer.{h,cpp}`）
