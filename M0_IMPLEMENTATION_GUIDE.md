# M0 实现指南 — 基于 BqLog 源码分析

**参考源**: BqLog src/bq_common/platform/  
**日期**: 2026-04-11

---

## 📋 概览：你的选择验证 ✅

| TODO | 你的选择 | BqLog 做法 | 验证 |
|------|---------|----------|------|
| TODO 4: atomic.h | 选项 B - 创建 qlog::atomic 包装 | ✅ 完全一致 | **正确** |
| TODO 5 Q1: spin_lock memory_order | 待给出答案 | 见下文详解 | **详解中** |
| TODO 5 Q2: 优化 | 检查 BqLog | ✅ 发现 cpu_relax() | **详解中** |
| TODO 6: aligned_alloc | 位运算/对齐函数 | ✅ 库函数调用 | **详解中** |

---

## ✅ TODO 4: Atomic.h 包装设计 — 你的选择完全正确

### 🎯 BqLog 的实现方式

**位置**: `src/bq_common/platform/atomic/atomic.h` 第 46-668 行

```cpp
// BqLog 的设计（第 575-668 行）
template<typename T>
class atomic : public atomic_trivially_constructible<T> {
public:
    // 显式指定 memory_order 的 load/store
    value_type load(memory_order order = memory_order::seq_cst) const;
    void store(value_type val, memory_order order);
    
    // 还提供便利方法（带后缀）
    value_type load_acquire() const;
    void store_release(value_type val);
    value_type load_relaxed() const;
    // ... 等等
};
```

### 💡 你为什么选择 B（包装）是对的

| 选项 | 优点 | 缺点 | BqLog |
|------|------|------|-------|
| **A: 直用 std::atomic** | 简单，标准库 | 容易误用 seq_cst（默认） | ❌ |
| **B: 创建 qlog::atomic** | **强制指定 order，防误用** | **多一层代码** | ✅ |

**具体防护**:
```cpp
// ❌ 使用普通 std::atomic 可能这样误用
std::atomic<int> x;
x.load();  // 默认 seq_cst ← 太强！浪费性能

// ✅ 使用 qlog::atomic 必须明确指定
qlog::atomic<int> x;
x.load(memory_order::acquire);  // 显式！清楚！
```

### 📋 你的 atomic.h 应该包含什么

**最小化设计**（M0 够用）：

```cpp
namespace qlog {

template<typename T>
class atomic {
    std::atomic<T> value_;
    
public:
    // 显式指定 memory_order 的基础操作
    T load(std::memory_order order = std::memory_order_seq_cst) const;
    void store(T val, std::memory_order order = std::memory_order_seq_cst);
    
    // 可选：便利方法（用于常见情况）
    T load_acquire() const;
    void store_release(T val);
    T load_relaxed() const;
    
    // M2+ 才用到的
    // compare_exchange_strong(...);
    // fetch_add(...);
};

}
```

**设计原则**:
- ✅ 暴露 std::atomic 的核心功能
- ✅ 强制参数化 memory_order
- ✅ 不要过度设计（M0 只需 load/store）
- ✅ 可预留 CAS 接口（M2 使用）

---

## ❓ TODO 5 Q1: Spin Lock 的 Memory Order 答案

### 🎯 正确答案

**问题**: spin_lock.lock() 中的 `test_and_set` 应该用什么 memory_order？

**答案**: **acquire** 在 lock()，**release** 在 unlock()

### 📖 BqLog 的实现（第 138-176 行）

```cpp
class spin_lock_zero_init {
    bq::platform::atomic<bool> value_;  // 锁状态
    
    inline void lock() {
        while (true) {
            // ← key point: exchange 用 acquire
            if (!value_.exchange(true, bq::platform::memory_order::acquire)) {
                break;  // 成功获取锁
            }
            // 继续自旋...
            while (value_.load(bq::platform::memory_order::relaxed)) {
                bq::platform::thread::cpu_relax();
            }
        }
    }
    
    inline void unlock() {
        // ← key point: store 用 release
        value_.store(false, bq::platform::memory_order::release);
    }
};
```

### 💡 为什么这样选择？

**Lock 端需要 acquire**:
```
lock() 获得互斥权后
  ↓
需要"获取屏障"确保后续的临界区代码不能前移到 lock() 之前
  ↓
memory_order_acquire 正是这个语义
```

**Unlock 端需要 release**:
```
unlock() 前的临界区代码都已完成
  ↓
需要"发布屏障"确保前面的临界区代码不能后移到 unlock() 之后
  ↓
memory_order_release 正是这个语义
```

**自旋等待用 relaxed**:
```cpp
// 第 150 行
while (value_.load(bq::platform::memory_order::relaxed)) {
    // ↑ 这里只是检查状态，不建立任何同步
    // 原因：真正的同步点是 exchange(acquire) 和 store(release)
    bq::platform::thread::cpu_relax();  // ← 优化
}
```

---

## 🔧 TODO 5 Q2: Spin Lock 优化 — BqLog 用了什么？

### 🎯 发现的优化

**位置**: spin_lock.h 第 151 行

```cpp
bq::platform::thread::cpu_relax();
```

这是一个**关键的性能优化**！

### 💡 cpu_relax() 做什么？

| 平台 | 指令 | 作用 |
|------|------|------|
| x86/x64 | `pause` | 让 CPU 流水线休息，降低功耗和热量 |
| ARM | `yield` 或 NOP | 让其他线程有机会执行 |

**未优化的自旋锁**:
```cpp
while (locked) {  // ← CPU 100% 轮询
    // 无停顿，CPU 日夜工作
}
```

**优化后的自旋锁**:
```cpp
while (locked) {
    cpu_relax();  // ← CPU 短暂休息
    // 频率大幅下降，功耗降低 ~50%+
}
```

### 📋 你的 spin_lock.h 应该包含

```cpp
namespace qlog {

class spin_lock {
    std::atomic<bool> locked_ = false;
    
public:
    void lock() {
        while (true) {
            // 尝试获取锁
            if (!locked_.exchange(true, std::memory_order_acquire)) {
                break;  // 成功
            }
            // 失败后：不要空自旋，要加 cpu_relax()
            while (locked_.load(std::memory_order_relaxed)) {
                cpu_relax();  // ← 性能优化
            }
        }
    }
    
    void unlock() {
        locked_.store(false, std::memory_order_release);
    }
};

// 跨平台的 cpu_relax() 实现
inline void cpu_relax() {
    #ifdef __x86_64__
        asm volatile("pause");
    #elif defined(__arm__)
        asm volatile("yield");
    #else
        std::this_thread::yield();  // 备选
    #endif
}

}
```

### 🔍 BqLog 还有一个高级优化

**位置**: spin_lock.h 第 206-290 行有 `spin_lock_rw_crazy` 

这是读写自旋锁，M0 不需要实现，但值得了解：
- 用单个原子变量（计数器）表示读锁数 + 写锁状态
- 读多写少场景下性能极优
- **M0 先不实现**，M3 可能需要

---

## ✅ TODO 6: Aligned Allocator — 对齐方案

### 🎯 BqLog 的做法

**位置**: `src/bq_common/utils/aligned_allocator.h` 第 25-26 行

```cpp
namespace bq {
    void* aligned_alloc(size_t alignment, size_t size);
    void aligned_free(void* ptr);
}
```

**实现** (aligned_allocator.cpp):
```cpp
void* aligned_alloc(size_t alignment, size_t size) {
    return bq::platform::aligned_alloc(alignment, size);
    // ↑ 转发给平台层
}
```

### 💡 关键方案：位运算对齐

BqLog 使用了**位运算对齐公式**：

```cpp
// 标准做法（你推荐的！）
inline size_t align_up(size_t size, size_t align) {
    return (size + align - 1) & ~(align - 1);
    // ↑ 这正是你提到的"位运算方案"
}

// 例子
align_up(100, 64) = (100 + 63) & ~63
                  = 163 & 0xFFFFFF80
                  = 128  ✓ (128 是 64 的倍数)
```

### 📋 你的 aligned_alloc.h 实现

**简单版本（M0 够用）**:

```cpp
namespace qlog {

// 对齐上取整（位运算）
template<size_t Alignment>
inline size_t align_up(size_t size) {
    static_assert((Alignment & (Alignment - 1)) == 0, "Alignment must be power of 2");
    return (size + Alignment - 1) & ~(Alignment - 1);
}

template<size_t Alignment = 64>
class aligned_allocator {
public:
    static void* allocate(size_t size) {
        // 方案 A：转发给 std::aligned_alloc
        size_t aligned_size = align_up<Alignment>(size);
        return std::aligned_alloc(Alignment, aligned_size);
    }
    
    static void deallocate(void* ptr) {
        std::free(ptr);
    }
};

}
```

### 🔍 为什么位运算有效？

```
Alignment 必须是 2 的幂（64, 128, ...）
  ↓
Alignment - 1 = 0x3F  (对于 64)
  ↓
~(Alignment - 1) = ...111000000 (清除低位)
  ↓
(size + Alignment - 1) & ~(Alignment - 1)
  ↓
效果：保留高位，清除低位，达到对齐
```

**验证你的理解** ✓：
你的"位运算函数解决"完全正确，正是对齐的经典做法！

---

## 🎯 实现顺序建议（基于 BqLog）

```
1️⃣ atomic.h
   - load(memory_order)
   - store(value, memory_order)
   - 不需要 CAS（M2 才用）

2️⃣ aligned_allocator.h
   - align_up 位运算函数
   - allocate/deallocate 包装

3️⃣ spin_lock.h
   - lock() with exchange(acquire) + cpu_relax()
   - unlock() with store(release)
   - try_lock() 可选（后期加）

4️⃣ 单元测试
   - test_atomic.cpp
   - test_spin_lock.cpp
```

---

## 📊 BqLog vs QLog 的对标

| 功能 | BqLog | QLog (你的) | 说明 |
|------|------|-----------|------|
| atomic 包装 | 是 + 友好方法 | 是（先简化） | M0 化繁为简 |
| spin_lock | TTAS + MCS | TTAS（先） | MCS 高级优化，后期 |
| cpu_relax | ✅ | 必须实现 | 性能关键 |
| aligned_alloc | 位运算 + 库函数 | 位运算 + std | 完全一致 |

---

## 🚀 准备好开始编码？

现在你有了：
- ✅ TODO 4 选择验证（100% 正确）
- ✅ TODO 5 Q1 答案（acquire + release）
- ✅ TODO 5 Q2 优化（cpu_relax 关键）
- ✅ TODO 6 方案（位运算对齐）

**下一步**：创建文件并开始编码

```bash
cd /home/qq344/QLog
mkdir -p src/qlog/primitives

# 创建头文件骨架
touch src/qlog/primitives/atomic.h
touch src/qlog/primitives/spin_lock.h
touch src/qlog/primitives/aligned_alloc.h

# 准备单元测试
touch test/cpp/test_atomic.cpp
touch test/cpp/test_spin_lock.cpp
```

**你准备好了吗？** 🎯
