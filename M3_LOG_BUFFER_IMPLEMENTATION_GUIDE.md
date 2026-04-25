# M3_LOG_BUFFER_IMPLEMENTATION_GUIDE.md
# QLog M3 双路 Buffer 调度器完整实现指导

**Milestone**: M3 — `log_buffer` 双路调度器  
**依赖**: M0 (无锁原语) + M1 (spsc_ring_buffer) + M2 (mpsc_ring_buffer)  
**BqLog 对齐度**: 数据结构 100%，算法核心 100%，辅助细节简化  
**参考源码**: `/home/qq344/BqLog/src/bq_log/types/buffer/log_buffer.{h,cpp}`  
**最后更新**: 2026-04-25  
**版本**: 1.0

---

## 目录

1. [架构概述与设计思想](#1-架构概述与设计思想)
2. [BqLog 对齐表](#2-bqlog-对齐表)
3. [核心数据结构设计](#3-核心数据结构设计)
4. [算法实现详解](#4-算法实现详解)
5. [文件结构与接口定义](#5-文件结构与接口定义)
6. [逐步实现任务清单](#6-逐步实现任务清单)
7. [单元测试规范](#7-单元测试规范)
8. [验收检查清单](#8-验收检查清单)
9. [常见陷阱与调试指南](#9-常见陷阱与调试指南)

---

## 1. 架构概述与设计思想

### 1.1 核心问题：为什么需要双路设计？

```
╔══════════════════════════════════════════════════════════════════╗
║  问题：多线程日志在性能和内存之间的矛盾                           ║
╠══════════════════════════════════════════════════════════════════╣
║  ① HP 路径 (High Performance)                                   ║
║    每个线程有独立的 spsc_ring_buffer (ZERO 竞争)                  ║
║    优点：极低延迟 < 50ns，无锁竞争                               ║
║    缺点：内存消耗 = 线程数 × 缓冲区大小                          ║
║    适合：写频率 > threshold 的高频线程                           ║
║                                                                  ║
║  ② LP 路径 (Low Performance，实际仍高性能)                       ║
║    所有线程共享一个 mpsc_ring_buffer (少量 CAS 竞争)              ║
║    优点：内存固定，N 个线程共用同一缓冲区                         ║
║    缺点：有 CAS 竞争，延迟 ~200ns                               ║
║    适合：写频率 < threshold 的低频线程                           ║
╚══════════════════════════════════════════════════════════════════╝
```

### 1.2 路由决策流程

```
线程 T 调用 log_buffer::alloc_write_chunk(size)
            │
            ▼
┌──────────────────────────────────┐
│  1. 获取 TLS buffer_info         │
│     (每个线程×每个 log_buffer     │
│      有独立的 TLS 状态)           │
└─────────────────┬────────────────┘
                  │
                  ▼
┌──────────────────────────────────┐
│  2. 频率检测                      │
│  update_times_++ 超过阈值?        │
│  last_update_epoch_ms_ 超时?      │
└─────────┬──────────┬─────────────┘
          │YES       │NO
          ▼          ▼
   is_high_freq    is_low_freq
          │          │
          ▼          ▼
┌─────────────┐  ┌─────────────────────┐
│ HP 路径      │  │ LP 路径              │
│ spsc per    │  │ mpsc shared         │
│ thread      │  │ + context_head(16B) │
└─────────────┘  └─────────────────────┘
```

### 1.3 LP 路径为何需要 context_head？

HP 路径因为每个线程独占一个 spsc，消费者只需顺序消费即可。  
LP 路径多个线程写入同一个 mpsc，必须区分"哪条 entry 是哪个线程写的第几条"，才能在消费时保证 **每个线程的 entry 有序**（跨线程可以乱序，但同一线程内有序）。

```
mpsc_ring_buffer 内容示意：
┌──────────────────────────────────────────────────────────────────┐
│ [ctx: T1/seq=0][data_A] [ctx: T2/seq=0][data_B] [ctx: T1/seq=1][data_C] │
└──────────────────────────────────────────────────────────────────┘

消费者读取时：
- T1/seq=0 (data_A) → T1 正常推进，输出 data_A
- T2/seq=0 (data_B) → T2 正常推进，输出 data_B  
- T1/seq=1 (data_C) → T1 seq=1==expected=1，输出 data_C ✓
```

---

## 2. BqLog 对齐表

### 2.1 数据结构对齐（必须 100% 对齐）

| 项目 | BqLog 位置 | BqLog 实现 | QLog M3 实现 | 对齐度 | 备注 |
|------|----------|-----------|------------|--------|------|
| `context_head` 大小 | log_buffer.h:208 | 16 bytes | 16 bytes | ✅ 100% | version(2) + is_finished(1) + is_external(1) + seq(4) + ptr(8) |
| `context_head` 对齐 | log_buffer.h:208 | alignas(8) | alignas(8) | ✅ 100% | 8 字节对齐，兼容 32/64 位 |
| `log_tls_buffer_info` 对齐 | log_buffer.h:143 | alignas(64) | alignas(64) | ✅ 100% | cache-line 对齐 |
| `wt_data_` 偏移 | log_buffer.h:163 | alignas(64) | alignas(64) | ✅ 100% | 写侧 seq 独占 cache line |
| `rt_data_` 偏移 | log_buffer.h:167 | alignas(64) | alignas(64) | ✅ 100% | 读侧 seq 独占 cache line |
| `log_buffer` 对齐 | log_buffer.h:34 | alignas(64) | alignas(64) | ✅ 100% | 整体 cache-line 对齐 |
| HP buffer 类型 | log_buffer.h:252 | `group_list` | `spsc_ring_buffer` (per-TLS) | ⚠️ 简化 | 去掉 group_list 层，直接 TLS |
| LP buffer 类型 | log_buffer.h:253 | `miso_ring_buffer` | `mpsc_ring_buffer` | ✅ 100% | M2 实现直接复用 |
| HP_CALL_FREQUENCY_CHECK_INTERVAL | log_buffer.h:39 | 1000 | 1000 | ✅ 100% | 毫秒单位 |

### 2.2 算法对齐（必须 100% 对齐）

| 算法点 | BqLog 位置 | BqLog 实现 | QLog M3 实现 | 对齐度 |
|--------|----------|-----------|------------|--------|
| 频率检测方式 | log_buffer.cpp:65-82 | epoch_ms + 计数器双重检测 | epoch_ms + 计数器双重检测 | ✅ 100% |
| LP 写入时 context 填充 | log_buffer.cpp:90-100 | version+seq+tls_ptr 写入 header | 相同 | ✅ 100% |
| HP 写入时找/创建块 | log_buffer.cpp:82-86 | `alloc_new_hp_block()` | `get_or_create_hp_buffer()` | ✅ 等价 |
| `commit` LP 路径偏移修正 | log_buffer.cpp:124-130 | `data_addr -= sizeof(context_head)` | 相同 | ✅ 100% |
| 消费者优先消费 HP | log_buffer.cpp:155-200 | HP 优先遍历 | HP 优先遍历 | ✅ 100% |
| seq 验证逻辑 | log_buffer.cpp:349-380 | TLS seq 递增校验 | TLS seq 递增校验 | ✅ 100% |
| `return_read_chunk` LP 路径 | log_buffer.cpp:210-230 | `-= sizeof(context_head)` 修正 | 相同 | ✅ 100% |

### 2.3 允许的简化（QLog M3 vs BqLog）

| BqLog 功能 | QLog M3 决策 | 理由 |
|-----------|------------|------|
| `group_list` 动态伸缩块池 | ❌ 简化为 TLS `spsc_ring_buffer` 静态分配 | group_list 依赖复杂 block_list，超出 M3 范围 |
| mmap 崩溃恢复 | ❌ 不实现 | M9 专项实现 |
| `oversize_buffer` | ⚠️ 可选实现 | 超大 entry 支持 |
| Java JNI 相关 | ❌ 不实现 | 不相关平台 |
| `BQ_LOG_BUFFER_DEBUG` 调试统计 | ❌ 不实现 | 可选优化 |
| `MAX_RECOVERY_VERSION_RANGE` | ❌ 不实现 | 依赖 mmap |

---

## 3. 核心数据结构设计

### 3.1 context_head（LP 路径条目前缀）

**严格对标 BqLog log_buffer.h:196-225**，这是保证 LP 路径有序性的关键结构。

```cpp
// src/qlog/buffer/log_buffer.h

#pragma pack(push, 1)
struct alignas(8) context_head
{
    // LP buffer 的版本号（简化版始终为 0，无 mmap 恢复需求）
    // BqLog: 用于 mmap 崩溃恢复时区分新旧数据
    // QLog M3: 保留字段，始终写 0，但保留 16 字节总大小
    uint16_t version_;

    // 标记此 TLS 对应的线程是否已退出
    // 消费者读到此标记时，应释放对应的 tls_buffer_info
    bool is_thread_finished_;

    // 预留（BqLog 中用于 oversize 路径，M3 可选）
    bool is_external_ref_;

    // 同一线程的 entry 单调递增序列号
    // 消费者通过 seq == expected_seq 验证顺序正确性
    uint32_t seq_;

    // 指向写入本条 entry 的线程的 TLS 状态
    // 消费者通过此指针找到对应线程的 expected_seq
    // 注意：32/64 位系统指针大小不同，需确保总大小 == 16
#if defined(__LP64__) || defined(_WIN64)
    // 64 位系统：直接存指针（8B）
    struct alignas(8) { void* ptr; } tls_info_;
#else
    // 32 位系统：存指针(4B) + padding(4B) 凑足 8 字节
    struct alignas(8) { void* ptr; uint32_t _pad; } tls_info_;
#endif

    inline void* get_tls_ptr() const { return tls_info_.ptr; }
    inline void set_tls_ptr(void* p) { tls_info_.ptr = p; }
};
#pragma pack(pop)

static_assert(sizeof(context_head) == 16, "context_head MUST be 16 bytes");
static_assert(alignof(context_head) == 8, "context_head MUST align to 8 bytes");
```

> ⚠️ **关键约束**: `sizeof(context_head) == 16` 不可更改。这是 LP 路径 `data_addr` 偏移修正的基准（`data_addr -= 16`）。

### 3.2 log_tls_buffer_info（每线程每 log_buffer 状态）

```
内存布局（必须如此，否则 false sharing 性能下降 10x）：

偏移 0~63:    冷数据区 (last_update_epoch_ms_, update_times_, cur_hp_buffer_, ...)
             ───────────────────────── cache line 边界 ─────────────────────────
偏移 64~127: 写侧热数据 (current_write_seq_)
             ───────────────────────── cache line 边界 ─────────────────────────
偏移 128~191: 读侧热数据 (current_read_seq_)

关键：current_write_seq_ 和 current_read_seq_ 在不同 cache line！
写线程修改 write_seq，读线程修改 read_seq，二者完全不会产生 false sharing。
```

```cpp
struct alignas(64) log_tls_buffer_info
{
    // ── 冷数据：不频繁访问 ────────────────────────────────
    uint64_t last_update_epoch_ms_ = 0;   // 上次频率检测时间戳
    uint64_t update_times_         = 0;   // 当前检测窗口内的写入次数

    // HP 路径：当前线程使用的 spsc_ring_buffer（nullptr 表示用 LP 路径）
    spsc_ring_buffer* cur_hp_buffer_ = nullptr;

    // 反向引用，析构时需要找到 log_buffer 做清理
    log_buffer* owner_buffer_ = nullptr;

    // 线程退出标记 & 生命周期保护（用于安全析构）
    bool is_thread_finished_ = false;

    // 填充到 64 字节（防止与热数据 false sharing）
    char _pad0[64
               - sizeof(uint64_t) * 2
               - sizeof(spsc_ring_buffer*)
               - sizeof(log_buffer*)
               - sizeof(bool)];

    // ── 写侧热数据：写线程频繁访问，独占 cache line ──────
    alignas(64) struct
    {
        uint32_t current_write_seq_ = 0;  // LP 路径写入的 entry 序列号（单调递增）
        char _pad[60];
    } wt_data_;

    // ── 读侧热数据：读线程频繁访问，独占 cache line ──────
    alignas(64) struct
    {
        uint32_t current_read_seq_ = 0;   // 消费者期望的下一条 entry 序列号
        char _pad[60];
    } rt_data_;
};

// 严格对齐验证（编译期检查）
static_assert(sizeof(log_tls_buffer_info) % 64 == 0,
    "log_tls_buffer_info must be cache-line sized");
static_assert(offsetof(log_tls_buffer_info, wt_data_) % 64 == 0,
    "wt_data_ must be cache-line aligned");
static_assert(offsetof(log_tls_buffer_info, rt_data_) % 64 == 0,
    "rt_data_ must be cache-line aligned");
```

### 3.3 log_buffer 类结构

```cpp
class alignas(64) log_buffer
{
public:
    // ── 常量定义（与 BqLog 对齐）─────────────────────────
    static constexpr uint64_t HP_CALL_FREQUENCY_CHECK_INTERVAL_MS = 1000;
    static constexpr uint64_t DEFAULT_HP_THRESHOLD = 1000; // entries/second

public:
    // 构造：capacity_bytes 为 LP buffer 大小
    explicit log_buffer(uint32_t lp_capacity_bytes,
                        uint32_t hp_capacity_per_thread_bytes,
                        uint64_t hp_threshold = DEFAULT_HP_THRESHOLD);
    ~log_buffer();

    // 禁用拷贝
    log_buffer(const log_buffer&) = delete;
    log_buffer& operator=(const log_buffer&) = delete;

    // ── 生产者接口（多线程安全）──────────────────────────
    // 返回可写入数据的指针，失败返回 nullptr
    // current_time_ms: 当前时间（毫秒），用于频率检测
    void* alloc_write_chunk(uint32_t size, uint64_t current_time_ms);

    // 提交写入（必须与 alloc_write_chunk 配对调用，无论是否成功）
    void commit_write_chunk(void* data_ptr);

    // ── 消费者接口（单线程）──────────────────────────────
    // 读取下一条 entry（已保序）
    // 返回 nullptr 表示暂无数据
    const void* read_chunk(uint32_t& out_size);

    // 返还已读 entry（释放空间）
    void return_read_chunk(const void* data_ptr);

    // 强制刷新：等待当前所有写入的 entry 均可被 read_chunk 消费
    void flush();

private:
    // ── 内部类型 ─────────────────────────────────────────

    // HP buffer 池：每个线程有独立的 spsc_ring_buffer
    // key = TLS 指针（线程唯一标识），value = spsc 实例
    // 访问时需持有 hp_pool_lock_
    struct hp_buffer_entry
    {
        spsc_ring_buffer buffer;
        bool is_active = true;  // 线程退出后设为 false，等待消费完毕再释放
        log_tls_buffer_info* tls_info = nullptr;
    };

    // 当前正在读的 entry 类型（HP 还是 LP），供 return_read_chunk 用
    enum class active_read_src : uint8_t
    {
        none,
        hp,
        lp
    };

private:
    // ── 生产者端私有方法 ──────────────────────────────────

    // 获取（或创建）当前线程的 HP buffer
    spsc_ring_buffer* get_or_create_hp_buffer(log_tls_buffer_info& tls_info);

    // 获取（或创建）当前线程的 TLS 状态
    log_tls_buffer_info& get_tls_buffer_info();

    // ── 消费者端私有方法 ──────────────────────────────────

    // 从 LP buffer 读取并验证 context
    const void* rt_read_from_lp(uint32_t& out_size);

    // 从 HP buffer 池读取
    const void* rt_read_from_hp(uint32_t& out_size);

    // 验证 LP entry 的 context（seq 校验）
    bool rt_verify_lp_context(const context_head& ctx);

    // ── 成员变量 ──────────────────────────────────────────

    // LP buffer（低频线程共享）
    mpsc_ring_buffer lp_buffer_;

    // HP buffer 容量（每线程独立 spsc 的大小）
    uint32_t hp_capacity_per_thread_;

    // 频率检测阈值（每秒写入次数超过此值 → HP）
    uint64_t hp_threshold_;

    // HP buffer 池（需要 spin_lock_rw 保护，读多写少）
    spin_lock_rw hp_pool_lock_;
    // 使用 std::vector 或侵入式链表（此处为简单版）
    // 生产：线程注册时 push，线程退出后标记 inactive
    // 消费：轮询所有 active + inactive（直到 empty）entry
    // 注意：此处允许 heap 分配，因为线程注册是冷路径
    std::vector<hp_buffer_entry*> hp_pool_;

    // TLS key：每个线程的独立状态（析构时自动清理）
    // 使用 pthread_key 或 thread_local（选一种，保持一致）
    // QLog M3 推荐：thread_local + 弱引用回调
    static thread_local log_tls_buffer_info* tls_current_info_;

    // 消费者状态（单消费者，无竞争）
    struct {
        size_t hp_pool_read_index_ = 0;   // 当前正在读的 HP buffer 下标（轮询）
        const void* pending_lp_ptr_ = nullptr;   // 等待 return 的 LP entry
        const void* pending_hp_ptr_ = nullptr;   // 等待 return 的 HP entry
        hp_buffer_entry* pending_hp_entry_ = nullptr;
        active_read_src last_read_src_ = active_read_src::none;
    } rt_state_;
};
```

---

## 4. 算法实现详解

### 4.1 alloc_write_chunk() — 核心路由算法

**严格对标 BqLog log_buffer.cpp:55-120**

```cpp
void* log_buffer::alloc_write_chunk(uint32_t size, uint64_t current_time_ms)
{
    // ── Step 1: 获取当前线程的 TLS 状态 ──────────────────
    log_tls_buffer_info& tls = get_tls_buffer_info();

    // ── Step 2: 频率检测（对标 BqLog log_buffer.cpp:65-82）──
    // BqLog 使用双重检测：时间窗口 + 计数器
    //   ① 超过 HP_CALL_FREQUENCY_CHECK_INTERVAL_MS 后重置计数
    //   ② 计数超过 threshold 时标记为高频
    bool is_high_freq = (tls.cur_hp_buffer_ != nullptr);

    if (current_time_ms >= tls.last_update_epoch_ms_ + HP_CALL_FREQUENCY_CHECK_INTERVAL_MS)
    {
        // 时间窗口结束：评估本窗口内写入次数
        if (tls.update_times_ < hp_threshold_)
        {
            // 本窗口低频：切换到 LP 路径
            is_high_freq = false;
            if (tls.cur_hp_buffer_ != nullptr)
            {
                // 释放 HP buffer（标记为 inactive，等消费者排干后回收）
                // 注意：不能 delete，消费者可能还在读
                tls.cur_hp_buffer_ = nullptr;
            }
        }
        // 重置窗口
        tls.last_update_epoch_ms_ = current_time_ms;
        tls.update_times_        = 0;
    }

    // 递增计数，若超过阈值则立即升级为高频
    if (++tls.update_times_ >= hp_threshold_)
    {
        is_high_freq = true;
        tls.last_update_epoch_ms_ = current_time_ms;
        tls.update_times_         = 0;
    }

    // ── Step 3: 路由到 HP 或 LP 路径 ─────────────────────
    if (is_high_freq)
    {
        // ─────────────── HP 路径 ─────────────────────────
        // 确保当前线程有 HP buffer
        if (tls.cur_hp_buffer_ == nullptr)
        {
            tls.cur_hp_buffer_ = get_or_create_hp_buffer(tls);
        }

        void* ptr = tls.cur_hp_buffer_->alloc_write_chunk(size);
        if (ptr != nullptr)
        {
            return ptr;
        }

        // HP buffer 满：按 BqLog 策略，不阻塞，直接降级到 LP 路径
        // （BqLog: auto_expand 时重新申请块；简化版直接降级）
        tls.cur_hp_buffer_ = nullptr;
        is_high_freq = false;
        // fall through to LP
    }

    // ─────────────────── LP 路径 ─────────────────────────
    // LP 路径在真实数据前需要预留 sizeof(context_head) = 16 字节
    // context_head 由 log_buffer 内部填充，对上层调用方透明
    uint32_t total_alloc = size + static_cast<uint32_t>(sizeof(context_head));
    write_handle wh = lp_buffer_.alloc_write_chunk(total_alloc);
    if (!wh.success)
    {
        // 空间不足：根据策略丢弃或返回 nullptr
        lp_buffer_.commit_write_chunk(wh); // 必须调用，释放占位
        return nullptr;
    }

    // 填充 context_head（对标 BqLog log_buffer.cpp:90-100）
    auto* ctx = reinterpret_cast<context_head*>(wh.data);
    ctx->version_           = 0;  // QLog M3 无 mmap，始终为 0
    ctx->is_thread_finished_= false;
    ctx->is_external_ref_   = false;
    ctx->seq_               = tls.wt_data_.current_write_seq_++;  // 原子性由单线程写保证
    ctx->set_tls_ptr(&tls);

    // 返回 context_head 之后的地址（用户可写入区域）
    // 注意：lp_write_cursor_ 指向 wh 的地址，commit 时需要修正
    // 我们用 wh.data 存储 LP handle，commit_write_chunk 时根据此区分 HP/LP
    return wh.data + sizeof(context_head);
}
```

> ⚠️ **设计关键**:  
> `alloc_write_chunk` 返回给用户的指针是 `data + sizeof(context_head)`。  
> 但底层 LP `write_handle` 的 `data` 指向的是 context_head 起始位置。  
> `commit_write_chunk` 必须能从用户指针反推回 write_handle。

### 4.2 commit_write_chunk() — 路径分发

```cpp
void log_buffer::commit_write_chunk(void* data_ptr)
{
    if (data_ptr == nullptr)
        return;

    log_tls_buffer_info& tls = get_tls_buffer_info();

    if (tls.cur_hp_buffer_ != nullptr)
    {
        // ─── HP 路径 ─────────────────────────────────────
        // HP 路径：data_ptr 直接来自 spsc，不需要偏移修正
        tls.cur_hp_buffer_->commit_write_chunk();
    }
    else
    {
        // ─── LP 路径 ─────────────────────────────────────
        // 对标 BqLog log_buffer.cpp:124-130
        // 用户拿到的是 data_ptr = context_head 后的地址
        // lp 的 write_handle 需要 context_head 起始地址
        // 因此：实际 lp_data = data_ptr - sizeof(context_head)
        write_handle wh;
        wh.success     = true;
        wh.data        = static_cast<uint8_t*>(data_ptr)
                         - static_cast<ptrdiff_t>(sizeof(context_head));
        // block_count 从已写入的 context_head 中取 block_num 重建
        // （实现时可在 alloc 时把 wh 缓存到 TLS，避免重建）
        lp_buffer_.commit_write_chunk(wh);
    }
}
```

> 💡 **实现建议**: 为避免 `commit` 时重建 `write_handle`，可以在 `alloc` 成功后将 `write_handle` 缓存到 `tls.pending_lp_wh_`，`commit` 时直接使用。这是 BqLog 的实际做法。

### 4.3 get_tls_buffer_info() — TLS 注册与生命周期

```cpp
// TLS 变量（每线程独立，析构时自动触发回调）
thread_local log_tls_buffer_info* log_buffer::tls_current_info_ = nullptr;

log_tls_buffer_info& log_buffer::get_tls_buffer_info()
{
    // 快速路径：已初始化
    if (tls_current_info_ != nullptr && tls_current_info_->owner_buffer_ == this)
    {
        return *tls_current_info_;
    }

    // 慢路径：为当前线程创建 TLS 状态
    // 注意：这是冷路径，允许 heap 分配
    auto* info = new log_tls_buffer_info();
    info->owner_buffer_ = this;

    // 注册线程退出回调（当线程销毁时通知 log_buffer）
    // 方式 1：pthread_key_t + destructor（Linux/macOS/Windows）
    // 方式 2：利用 thread_local 对象析构（更现代的方式）
    // QLog M3 推荐方式 2：
    struct tls_guard
    {
        log_tls_buffer_info* info;
        ~tls_guard()
        {
            if (info && info->owner_buffer_)
            {
                info->owner_buffer_->on_thread_exit(info);
            }
        }
    };
    static thread_local tls_guard guard{info};

    tls_current_info_ = info;
    return *info;
}

void log_buffer::on_thread_exit(log_tls_buffer_info* info)
{
    // 通知消费者：此线程不再写入
    // 对标 BqLog log_tls_info::~log_tls_info()
    // 在 LP buffer 中写一条 is_thread_finished=true 的 entry
    // 消费者读到后调用 delete info（延迟释放，保证消费者访问安全）
    info->is_thread_finished_ = true;

    // 标记 HP buffer 为 inactive（如果有）
    if (info->cur_hp_buffer_ != nullptr)
    {
        // hp_entry 的 is_active 设为 false，消费者排干后释放
        // （具体实现见 HP 池管理）
        info->cur_hp_buffer_ = nullptr;
    }

    // 写入 LP finish 标记（通知消费者）
    // 使用简单方案：在 LP buffer 写入 size=0 的特殊 entry
    uint64_t now_ms = 0; // 退出时不检测频率
    void* ptr = alloc_write_chunk_internal_lp(0, info);
    if (ptr != nullptr)
    {
        // context_head 已填充 is_thread_finished=true
        commit_write_chunk(ptr);
    }
}
```

### 4.4 read_chunk() — 消费者有序读取

这是 M3 中最复杂的部分。消费者需要从 HP pool 和 LP buffer 中有序地读取数据。

```
消费者读取策略（对标 BqLog log_buffer.cpp:155-210）：

优先级：HP pool（轮询） > LP buffer

原因：HP path 为高频线程提供独立缓冲，消费者需要优先排干
HP entries 以防止 HP buffer 满导致生产者降级。

LP 路径的有序性保证（对标 BqLog context_verify_result 逻辑）：
1. 每个 LP entry 前有 context_head，包含 tls_ptr + seq
2. 消费者维护每个 tls_ptr 的 expected_seq
3. 读到一条 LP entry 时：
   a. seq == expected_seq → 合法，输出，expected_seq++
   b. seq > expected_seq  → 该线程还有更早的 entry 尚未到达，
                            discard_read（不消费），等下轮
   c. seq < expected_seq  → 来自已 finished 线程的过期数据，跳过
```

```cpp
const void* log_buffer::read_chunk(uint32_t& out_size)
{
    // ── 优先读 HP pool ─────────────────────────────────────
    {
        // 用读锁遍历 hp_pool_（生产者偶尔新增 entry，所以读多写少）
        // BqLog: group_list 遍历；QLog M3: 简单 vector 遍历
        std::shared_lock<spin_lock_rw> rlock(hp_pool_lock_); // 简化：用 shared_lock

        size_t n = hp_pool_.size();
        for (size_t i = 0; i < n; ++i)
        {
            size_t idx = (rt_state_.hp_pool_read_index_ + i) % n;
            hp_buffer_entry* entry = hp_pool_[idx];

            const void* ptr = entry->buffer.read_chunk();
            if (ptr != nullptr)
            {
                // 读到数据，记录当前 entry 供 return_read_chunk 使用
                rt_state_.hp_pool_read_index_  = idx;
                rt_state_.pending_hp_ptr_   = ptr;
                rt_state_.pending_hp_entry_ = entry;
                rt_state_.last_read_src_    = active_read_src::hp;

                // HP 路径无 context_head，直接返回数据
                // 大小需要从 spsc block_header 中取
                // 这里调用 spsc 的辅助方法获取 data_size
                out_size = entry->buffer.last_read_data_size();
                return ptr;
            }
        }
    }

    // ── 读 LP buffer ─────────────────────────────────────
    {
        read_handle rh = lp_buffer_.read_chunk();
        if (!rh.success)
            return nullptr;

        // LP 路径验证 context_head（对标 BqLog verify_context）
        const auto* ctx = reinterpret_cast<const context_head*>(rh.data);

        if (!rt_verify_lp_context(*ctx))
        {
            // 顺序不对：当前 entry 属于某线程但该线程有更早的 entry 还没到
            // 不消费，放回（BqLog: discard_read_chunk）
            lp_buffer_.commit_read_chunk(rh);  // 仍然需要推进 read_cursor
            return nullptr;  // 告知上层暂无就绪数据
        }

        // 合法：context 验证通过
        rt_state_.pending_lp_ptr_    = rh.data;
        rt_state_.last_read_src_     = active_read_src::lp;

        // 更新 expected_seq
        auto* tls_info = reinterpret_cast<log_tls_buffer_info*>(ctx->get_tls_ptr());
        tls_info->rt_data_.current_read_seq_++;

        // 处理线程退出标记
        if (ctx->is_thread_finished_)
        {
            // 消费者负责释放 tls_buffer_info（生产者已退出）
            delete tls_info;
            lp_buffer_.commit_read_chunk(rh);
            // 递归调用读下一条（此条是 finish 标记，无用户数据）
            return read_chunk(out_size);
        }

        // 返回 context_head 之后的用户数据
        out_size = rh.data_size - static_cast<uint32_t>(sizeof(context_head));
        return rh.data + sizeof(context_head);
    }
}

bool log_buffer::rt_verify_lp_context(const context_head& ctx)
{
    auto* tls_info = reinterpret_cast<log_tls_buffer_info*>(ctx.get_tls_ptr());
    if (tls_info == nullptr)
        return false;

    uint32_t expected = tls_info->rt_data_.current_read_seq_;

    if (ctx.seq_ == expected)
        return true;   // 正常：当前序列号匹配

    if (ctx.seq_ > expected)
        return false;  // 乱序：此线程更早的 entry 尚未到达

    // ctx.seq_ < expected：来自已退出线程的过期数据（M3 不含 mmap 恢复，直接跳过）
    return false;
}
```

### 4.5 return_read_chunk() — 释放已读 entry

```cpp
void log_buffer::return_read_chunk(const void* data_ptr)
{
    if (data_ptr == nullptr)
        return;

    switch (rt_state_.last_read_src_)
    {
    case active_read_src::hp:
    {
        // HP 路径：直接调用 spsc commit_read_chunk
        rt_state_.pending_hp_entry_->buffer.commit_read_chunk();
        rt_state_.pending_hp_ptr_   = nullptr;
        rt_state_.pending_hp_entry_ = nullptr;
        break;
    }
    case active_read_src::lp:
    {
        // LP 路径：需要将 data_ptr 退回到 context_head 起始位置
        // 对标 BqLog log_buffer.cpp:210-230
        const uint8_t* lp_data_start =
            static_cast<const uint8_t*>(rt_state_.pending_lp_ptr_);

        read_handle rh;
        rh.success    = true;
        rh.data       = const_cast<uint8_t*>(lp_data_start);
        rh.cursor     = 0;  // mpsc 的 cursor 需要从内部状态恢复
        rh.data_size  = 0;  // 同上，mpsc 内部已记录
        rh.block_count = 0;

        // 实际实现中，需要将 rh.cursor 和 rh.block_count 缓存到 rt_state_
        // 这里简化展示逻辑
        lp_buffer_.commit_read_chunk(rh);
        rt_state_.pending_lp_ptr_ = nullptr;
        break;
    }
    default:
        break;
    }

    rt_state_.last_read_src_ = active_read_src::none;
}
```

### 4.6 HP Buffer 池管理

```cpp
spsc_ring_buffer* log_buffer::get_or_create_hp_buffer(log_tls_buffer_info& tls_info)
{
    // 冷路径：创建新的 HP buffer 并注册到池中
    // 允许 new（冷路径，不在 hot path 上）
    auto* entry = new hp_buffer_entry();
    entry->tls_info = &tls_info;

    bool ok = entry->buffer.init(hp_capacity_per_thread_);
    if (!ok)
    {
        delete entry;
        return nullptr;
    }

    // 加写锁，将新 entry 添加到 pool
    // BqLog: 对应 group_list::alloc_new_block()
    {
        // 写锁（新线程注册是冷路径，写锁开销可接受）
        spin_lock_write_guard wg(hp_pool_lock_);
        hp_pool_.push_back(entry);
    }

    return &entry->buffer;
}
```

---

## 5. 文件结构与接口定义

### 5.1 新增文件

```
src/qlog/buffer/
├── log_buffer.h          ← 主头文件（接口定义 + 关键数据结构）
├── log_buffer.cpp        ← 实现文件
└── log_buffer_defs.h     ← context_head、log_tls_buffer_info 等基础类型
```

### 5.2 log_buffer_defs.h 完整定义

```cpp
// src/qlog/buffer/log_buffer_defs.h
#pragma once

#include <cstdint>
#include <cstddef>
#include "qlog/primitives/atomic.h"

namespace qlog
{

// ─────────────────────────────────────────────────────────────────────────────
// context_head — LP 路径 entry 前缀
// 严格对标 BqLog log_buffer.h:196-225
// sizeof(context_head) MUST == 16
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)
struct alignas(8) context_head
{
    uint16_t version_;          // QLog M3: 始终为 0（无 mmap 恢复）
    bool     is_thread_finished_;
    bool     is_external_ref_;  // QLog M3: 始终为 false（无 oversize 路径）
    uint32_t seq_;              // 同一线程单调递增

    // 跨平台指针存储（确保 32/64 位下总大小均为 16 字节）
#if defined(__LP64__) || defined(_WIN64)
    struct alignas(8) { void* ptr; } tls_info_;
#else
    struct alignas(8) { void* ptr; uint32_t _pad; } tls_info_;
#endif

    void* get_tls_ptr() const   { return tls_info_.ptr; }
    void  set_tls_ptr(void* p)  { tls_info_.ptr = p; }
};
#pragma pack(pop)

static_assert(sizeof(context_head) == 16,
    "[QLog] context_head MUST be 16 bytes (BqLog alignment)");
static_assert(alignof(context_head) == 8,
    "[QLog] context_head MUST be aligned to 8 bytes");
static_assert(sizeof(context_head) % 8 == 0,
    "[QLog] context_head size MUST be a multiple of 8");


// ─────────────────────────────────────────────────────────────────────────────
// log_tls_buffer_info — 每线程每 log_buffer 的 TLS 状态
// 严格对标 BqLog log_buffer.h:143-172
// 三段 cache line 布局（冷 / 写热 / 读热）
// ─────────────────────────────────────────────────────────────────────────────
class spsc_ring_buffer;  // 前向声明
class log_buffer;

struct alignas(64) log_tls_buffer_info
{
    // ── 冷数据段（cache line 0）────────────────────────────
    uint64_t last_update_epoch_ms_ = 0;
    uint64_t update_times_         = 0;
    spsc_ring_buffer* cur_hp_buffer_ = nullptr;
    log_buffer*       owner_buffer_  = nullptr;
    bool              is_thread_finished_ = false;

    // LP 路径 pending write handle（对标 BqLog cur_block_ 缓存机制）
    // 在 alloc 与 commit 之间缓存，避免重建
    bool     has_pending_lp_write_ = false;
    uint32_t pending_lp_cursor_    = 0;
    uint32_t pending_lp_blocks_    = 0;

    char _pad0[64
               - sizeof(uint64_t) * 2
               - sizeof(spsc_ring_buffer*)
               - sizeof(log_buffer*)
               - sizeof(bool) * 2
               - sizeof(uint32_t) * 2];

    // ── 写侧热数据段（cache line 1）────────────────────────
    // 写线程频繁递增 current_write_seq_，独占 cache line 防止 false sharing
    alignas(64) struct
    {
        uint32_t current_write_seq_ = 0;
        char     _pad[60];
    } wt_data_;

    // ── 读侧热数据段（cache line 2）────────────────────────
    // 消费者频繁读取/递增 current_read_seq_，独占 cache line
    alignas(64) struct
    {
        uint32_t current_read_seq_ = 0;
        char     _pad[60];
    } rt_data_;

    ~log_tls_buffer_info();
};

static_assert(sizeof(log_tls_buffer_info) % 64 == 0,
    "[QLog] log_tls_buffer_info must be cache-line sized (multiple of 64)");
static_assert(offsetof(log_tls_buffer_info, wt_data_) % 64 == 0,
    "[QLog] wt_data_ must be on its own cache line");
static_assert(offsetof(log_tls_buffer_info, rt_data_) % 64 == 0,
    "[QLog] rt_data_ must be on its own cache line");

} // namespace qlog
```

### 5.3 log_buffer.h 接口骨架

```cpp
// src/qlog/buffer/log_buffer.h
#pragma once

#include <cstdint>
#include <vector>
#include "qlog/buffer/log_buffer_defs.h"
#include "qlog/buffer/spsc_ring_buffer.h"
#include "qlog/buffer/mpsc_ring_buffer.h"
#include "qlog/primitives/spin_lock_rw.h"

namespace qlog
{

class alignas(64) log_buffer
{
public:
    // BqLog 对标常量
    static constexpr uint64_t HP_CALL_FREQUENCY_CHECK_INTERVAL_MS = 1000;
    static constexpr uint64_t DEFAULT_HP_THRESHOLD                = 1000;

    explicit log_buffer(
        uint32_t lp_capacity_bytes,
        uint32_t hp_capacity_per_thread_bytes,
        uint64_t hp_threshold = DEFAULT_HP_THRESHOLD);

    ~log_buffer();

    log_buffer(const log_buffer&)            = delete;
    log_buffer& operator=(const log_buffer&) = delete;

    // ── 生产者接口（多线程安全）──────────────────────────
    [[nodiscard]] void* alloc_write_chunk(uint32_t size, uint64_t current_time_ms);
    void                commit_write_chunk(void* data_ptr);

    // ── 消费者接口（单线程）──────────────────────────────
    [[nodiscard]] const void* read_chunk(uint32_t& out_size);
    void                      return_read_chunk(const void* data_ptr);

    // 等待所有已提交数据可被消费（用于 flush/force_flush）
    void flush();

private:
    // ... (内部类型和方法)
    struct hp_buffer_entry { /* ... */ };

    log_tls_buffer_info& get_tls_buffer_info();
    spsc_ring_buffer*    get_or_create_hp_buffer(log_tls_buffer_info& tls);
    void                 on_thread_exit(log_tls_buffer_info* info);
    bool                 rt_verify_lp_context(const context_head& ctx);
    const void*          rt_read_from_hp(uint32_t& out_size);
    const void*          rt_read_from_lp(uint32_t& out_size);

    // ── 成员变量 ──────────────────────────────────────────
    mpsc_ring_buffer  lp_buffer_;
    uint32_t          hp_capacity_per_thread_;
    uint64_t          hp_threshold_;

    spin_lock_rw                 hp_pool_lock_;
    std::vector<hp_buffer_entry*> hp_pool_;

    // 消费者状态（单线程，无需保护）
    struct rt_state_t { /* ... */ } rt_state_;

    static thread_local log_tls_buffer_info* tls_current_info_;
};

} // namespace qlog
```

---

## 6. 逐步实现任务清单

按以下顺序实现，每步均可独立编译验证。

### Phase 1: 数据结构（1-2 天）

```
任务 1.1 — 实现 log_buffer_defs.h
  [ ] context_head 结构体（必须通过 sizeof==16 的 static_assert）
  [ ] log_tls_buffer_info 结构体（三段 cache line 布局）
  [ ] offsetof 验证所有 static_assert 通过
  [ ] 编写独立的 struct_size_test 验证布局

验证命令：
  ./scripts/build.sh Debug
  # 如果 static_assert 失败，说明布局错误

任务 1.2 — 实现 log_buffer.h 接口
  [ ] hp_buffer_entry 内部结构
  [ ] rt_state_t 消费者状态
  [ ] 所有公开方法声明
  [ ] CMakeLists.txt 添加 log_buffer 到 qlog 库源文件
```

### Phase 2: 生产者路径（2-3 天）

```
任务 2.1 — get_tls_buffer_info()
  [ ] thread_local 变量声明与初始化
  [ ] 线程退出回调（tls_guard 方案）
  [ ] on_thread_exit() 基础实现（标记 is_thread_finished）

任务 2.2 — get_or_create_hp_buffer()
  [ ] hp_buffer_entry 构造
  [ ] hp_pool_ 注册（写锁）
  [ ] 返回已初始化的 spsc_ring_buffer*

任务 2.3 — alloc_write_chunk()
  [ ] 频率检测双重逻辑（epoch_ms + update_times_）
  [ ] HP 路径分支（spsc alloc）
  [ ] LP 路径分支（mpsc alloc + context_head 填充）
  [ ] TLS pending_lp_* 缓存（避免 commit 时重建 handle）

任务 2.4 — commit_write_chunk()
  [ ] HP 路径：spsc commit
  [ ] LP 路径：从 pending_lp_* 恢复 handle，mpsc commit

单元测试：test_log_buffer_producer.cpp（Phase 2 结束后运行）
  [ ] 单线程写入（HP 路径验证，检查无 context_head）
  [ ] 单线程写入（LP 路径验证，检查 context_head 正确填充）
  [ ] 频率检测：写 1001 次后应切换 HP
```

### Phase 3: 消费者路径（2-3 天）

```
任务 3.1 — rt_verify_lp_context()
  [ ] seq == expected → true
  [ ] seq > expected  → false（等待更早 entry）
  [ ] seq < expected  → false（过期，跳过）

任务 3.2 — read_chunk() HP 分支
  [ ] 遍历 hp_pool_（读锁）
  [ ] 轮询策略：round-robin
  [ ] 记录 pending_hp_ptr_ 和 pending_hp_entry_

任务 3.3 — read_chunk() LP 分支
  [ ] mpsc read_chunk()
  [ ] context 验证（rt_verify_lp_context）
  [ ] 验证通过：expected_seq++，返回 data + 16
  [ ] is_thread_finished：delete tls_info，递归读下条

任务 3.4 — return_read_chunk()
  [ ] HP 路径：spsc commit_read_chunk
  [ ] LP 路径：mpsc commit_read_chunk（data_ptr - 16 修正）

任务 3.5 — flush()
  [ ] 自旋等待所有 HP buffer 均为空
  [ ] 自旋等待 LP buffer 为空

单元测试：test_log_buffer_consumer.cpp
  [ ] 单生产者单消费者（HP 路径）
  [ ] 单生产者单消费者（LP 路径，验证 context_head 透明）
  [ ] LP 路径有序性（seq 验证）
  [ ] 线程退出后消费者正确释放 TLS
```

### Phase 4: 集成与压测（1-2 天）

```
任务 4.1 — 混合路径测试
  [ ] 8 个高频线程（HP 路径）+ 2 个低频线程（LP 路径）
  [ ] 消费者正确消费所有数据（无丢失、无乱序）

任务 4.2 — Sanitizer 验证（必须全部通过）
  [ ] TSan: ./scripts/run_sanitizers.sh thread
  [ ] ASan: ./scripts/run_sanitizers.sh address
  [ ] UBSan: ./scripts/run_sanitizers.sh undefined

任务 4.3 — 性能验证
  [ ] HP 路径延迟 < 100ns (alloc+commit)
  [ ] LP 路径延迟 < 300ns (alloc+commit)
```

---

## 7. 单元测试规范

新建文件 `test/cpp/test_log_buffer.cpp`，覆盖以下 10 个场景：

```cpp
// ── Test 1: context_head 布局验证 ─────────────────────────────
// 验证 sizeof(context_head)==16, alignof==8, seq/tls_ptr 字段偏移正确
void test_context_head_layout();

// ── Test 2: log_tls_buffer_info 对齐验证 ─────────────────────────
// 验证三段 cache line 布局，offsetof(wt_data_) % 64 == 0
void test_tls_buffer_info_alignment();

// ── Test 3: 单线程 LP 路径基础读写 ───────────────────────────────
// alloc(32) → 写入数据 → commit → read_chunk → 验证数据 → return
// 注意：消费者看到的数据不含 context_head（透明）
void test_single_thread_lp_basic();

// ── Test 4: 单线程 HP 路径基础读写 ───────────────────────────────
// 写 1001 次触发 HP 切换 → 继续写 → read_chunk → 验证
void test_single_thread_hp_switch();

// ── Test 5: LP 路径 seq 有序性 ────────────────────────────────────
// 同一线程连续写 100 条，消费者按 seq 顺序读出，验证 seq 0~99 有序
void test_lp_ordering_single_thread();

// ── Test 6: 两个 LP 线程，消费者有序读取各自条目 ─────────────────
// T1 写 [T1-0, T1-1, ..., T1-99]，T2 写 [T2-0, ..., T2-99]
// 消费者验证：T1 的条目 seq 单调，T2 的条目 seq 单调（跨线程可乱序）
void test_lp_ordering_two_threads();

// ── Test 7: 线程退出后消费者清理 TLS ─────────────────────────────
// 创建线程，写入数据，线程退出，消费者读取，验证 TLS 被释放（无泄漏）
void test_thread_exit_cleanup();

// ── Test 8: HP + LP 混合路径 ─────────────────────────────────────
// 2 个高频线程 + 2 个低频线程同时写，消费者全部读出（无丢失）
void test_mixed_hp_lp_concurrent();

// ── Test 9: flush() 语义 ─────────────────────────────────────────
// 写入 1000 条 → flush() → 消费者能立即读出所有 1000 条
void test_flush_semantics();

// ── Test 10: 压力测试 ─────────────────────────────────────────────
// 8 个高频线程 × 10000 条，1 个消费者，总计 80000 条全部消费
// 性能指标：< 500ms 完成（非 Sanitizer 环境）
void test_stress_8threads_10k_entries();
```

---

## 8. 验收检查清单

### 8.1 数据结构对齐检查

```
[ ] sizeof(context_head) == 16                ← 编译期 static_assert
[ ] alignof(context_head) == 8                ← 编译期 static_assert
[ ] sizeof(log_tls_buffer_info) % 64 == 0     ← 编译期 static_assert
[ ] offsetof(log_tls_buffer_info, wt_data_) % 64 == 0  ← 编译期 static_assert
[ ] offsetof(log_tls_buffer_info, rt_data_) % 64 == 0  ← 编译期 static_assert
[ ] sizeof(log_buffer) % 64 == 0（或 alignof(log_buffer) == 64）
```

### 8.2 算法正确性检查

```
[ ] LP 路径 alloc 返回的地址 == mpsc_data + 16（context_head 之后）
[ ] LP 路径 context_head.seq_ 从 0 单调递增（每线程独立计数）
[ ] LP 路径 commit 时使用 pending_lp_cursor_ 还原 mpsc handle（无重建）
[ ] read_chunk LP 路径：data_ptr 指向 context_head + 16 的位置
[ ] return_read_chunk LP 路径：传给 mpsc 的是 data_ptr - 16（context_head 起始）
[ ] 消费者读到 is_thread_finished=true 时，delete tls_info（无悬空指针）
[ ] HP 路径：alloc/commit 不经过 context_head（零额外开销）
```

### 8.3 并发安全检查

```
[ ] TSan 通过：0 data races（./scripts/run_sanitizers.sh thread）
[ ] ASan 通过：0 memory errors（./scripts/run_sanitizers.sh address）
[ ] hp_pool_ 访问：读操作持有读锁，写操作（新线程注册）持有写锁
[ ] tls_current_info_ 是 thread_local，无跨线程访问
[ ] wt_data_.current_write_seq_ 仅由生产者线程写，仅由消费者线程读（acquire/release）
[ ] rt_data_.current_read_seq_ 仅由消费者线程访问（无竞争）
```

### 8.4 性能指标检查

```
[ ] HP 路径 alloc+commit < 100ns（Release 模式，非 Sanitizer）
[ ] LP 路径 alloc+commit < 300ns（Release 模式，非 Sanitizer）
[ ] 10 个线程×10000 条 < 500ms（含消费）
[ ] 无 seq_cst 操作（检查所有 atomic 调用使用 acquire/release/relaxed）
```

### 8.5 测试覆盖检查

```
[ ] 10 个单元测试全部通过（含 Sanitizer 环境）
[ ] test_log_buffer.cpp 在 test/CMakeLists.txt 中注册
[ ] 运行 ./scripts/test.sh 显示 test_log_buffer 通过
```

---

## 9. 常见陷阱与调试指南

### 陷阱 1: context_head 透明性

**错误**: 上层调用者拿到的 `data_ptr` 直接传给 `return_read_chunk`，但内部偏移修正错误。

**检测**: 如果 `commit_read_chunk` 导致 mpsc cursor 跳到错误位置，下一次 read 会读到垃圾数据。

**修复**: 在 `rt_state_` 中分别缓存 `pending_lp_mpsc_cursor_` 和 `pending_lp_block_count_`，`return_read_chunk` 直接使用缓存值，不从 `data_ptr` 推算。

---

### 陷阱 2: 频率检测窗口重置时机

**错误**: 只在超过时间窗口时重置 `update_times_`，导致线程 A 写了 1000 次后一直保持 HP 状态，即使它后来每秒只写 1 次。

**BqLog 逻辑** (log_buffer.cpp:65-82): 每次超过时间窗口都重新评估，如果本窗口写入次数 < threshold，降级为 LP。

**验证**: 单元测试 Test 4 覆盖此场景（高频 → 低频切换）。

---

### 陷阱 3: TLS 析构与消费者的竞争

**问题**: 线程退出时，TLS `log_tls_buffer_info` 被析构，但消费者可能还持有指向它的 `context_head.tls_ptr`。

**解决方案** (对标 BqLog on_thread_exit 逻辑):
1. 线程退出时，在 LP buffer 写一条 `is_thread_finished = true` 的特殊 entry。
2. 消费者读到此 entry 后，负责 `delete tls_buffer_info`（延迟释放）。
3. 线程退出回调只做标记，**不做 delete**。

---

### 陷阱 4: HP buffer 池的 ABA 问题

**问题**: 线程退出后 HP buffer 被回收，新线程恰好分配到相同地址，消费者可能误认为是同一线程。

**解决方案**: HP buffer entry 使用唯一 ID（递增序号 + 地址），消费者通过 ID 而非指针地址区分。

---

### 陷阱 5: LP 路径的 pending seq 问题

**场景**: 消费者读到线程 T1 的 seq=2，但 T1 的 seq=1 还没到，此时应 `discard_read_chunk`（不推进 read_cursor）而非 `commit_read_chunk`（推进 read_cursor）。

**BqLog 解决方案**: `discard_read_chunk` 不推进 read_cursor，下次 `read_chunk` 重新读到这条 entry。

**QLog M3 实现**: mpsc 的 `read_chunk` 只是返回数据指针，不推进 cursor；只有 `commit_read_chunk` 才推进。因此 "discard" 等价于不调用 `commit_read_chunk`，直接重新 `read_chunk`（这时候会重新读到同一条）。

> ⚠️ **注意**: mpsc 实现中 `read_chunk` 是否会自动移动游标？需要确认 M2 的实现行为。如果 `read_chunk` 不移动游标，则 discard 只需不调 `commit_read_chunk` 即可。

---

## 附录 A: CMakeLists.txt 更新

在 `src/CMakeLists.txt` 中的 `QLOG_BUFFER_SOURCES` 新增：

```cmake
set(QLOG_BUFFER_SOURCES
    qlog/buffer/spsc_ring_buffer.h
    qlog/buffer/spsc_ring_buffer.cpp
    qlog/buffer/mpsc_ring_buffer.h
    qlog/buffer/mpsc_ring_buffer.cpp
    # M3 新增
    qlog/buffer/log_buffer_defs.h
    qlog/buffer/log_buffer.h
    qlog/buffer/log_buffer.cpp
)
```

在 `test/CMakeLists.txt` 中新增：

```cmake
# M3 单元测试 - log_buffer
add_executable(test_log_buffer cpp/test_log_buffer.cpp)
target_link_libraries(test_log_buffer PRIVATE qlog Threads::Threads)
target_include_directories(test_log_buffer PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}
)
set_target_properties(test_log_buffer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME log_buffer_tests COMMAND test_log_buffer)

message(STATUS "QLog: M3 test targets configured (log_buffer)")
```

---

## 附录 B: 标准开发工作流

```bash
# 1. 实现数据结构（Phase 1）
vim src/qlog/buffer/log_buffer_defs.h
vim src/qlog/buffer/log_buffer.h

# 2. 格式化 + 编译验证
./scripts/format_code.sh
./scripts/build.sh Debug

# 3. 实现生产者路径（Phase 2）
vim src/qlog/buffer/log_buffer.cpp
./scripts/format_code.sh
./scripts/build.sh Release

# 4. 实现消费者路径（Phase 3）
vim src/qlog/buffer/log_buffer.cpp  # 继续
./scripts/format_code.sh
./scripts/build.sh Release

# 5. 运行测试
./scripts/test.sh

# 6. Sanitizer 验证（M3 完成必须通过）
./scripts/run_sanitizers.sh thread
./scripts/run_sanitizers.sh address

# 7. 提交
git add src/qlog/buffer/log_buffer*.{h,cpp}
git add test/cpp/test_log_buffer.cpp
git add src/CMakeLists.txt test/CMakeLists.txt
git commit -m "Implement: M3 log_buffer dual-path dispatcher (HP/LP routing)"

# 8. 更新 STATE.md
vim claude/STATE.md  # 标记 M3 完成 ✅
git add claude/STATE.md
git commit -m "Update: M3 log_buffer complete and verified"
git tag -a m3 -m "Milestone 3: Dual-path buffer dispatcher"
```

---

**文档版本**: 1.0  
**适用 QLog 版本**: M3（依赖 M0/M1/M2）  
**BqLog 参考版本**: v3 (`/home/qq344/BqLog/src/bq_log/types/buffer/log_buffer.{h,cpp}`)
