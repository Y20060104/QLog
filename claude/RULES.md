# QLog C++20 编码规范与设计原则

## 1. 语言规范与编译约束

### 1.1 C++ 标准
- **最低标准**: C++20 (ISO/IEC 14882:2020)
- **编译器**: GCC 10+, Clang 12+, MSVC 2019+
- **禁用列表**:
  - ❌ C++ 异常 (编译 `-fno-exceptions`)
  - ❌ RTTI (编译 `-fno-rtti`)
  - ❌ 动态内存分配在 Hot Path
  - ❌ 虚函数指针间接调用在 SPSC/MPSC ring buffer

### 1.2 必需 C++20 特性
- ✅ `constexpr` / `consteval` 用于编译期计算（hash、format template）
- ✅ `concepts` 用于模板约束（参数序列化类型检查）
- ✅ `[[likely]]` / `[[unlikely]]` 用于分支提示
- ✅ `requires` 子句用于编译期概念检查
- ✅ `std::atomic` + memory_order 语义
- ✅ `std::bit_cast` 用于类型转换（避免 reinterpret_cast）

---

## 2. 命名约定

### 2.1 类型名称
| 类型 | 约定 | 示例 |
|------|------|------|
| class/struct | `snake_case` | `ring_buffer`, `log_buffer` |
| 类型别名 (using/typedef) | `_t` 后缀 | `size_t`, `handle_t` |
| 概念 (Concept) | `is_` / `can_` 前缀 | `is_serializable`, `can_fit` |
| constexpr 常量 | `k_snake_case` 或 `UPPER_CASE` | `k_default_buffer_size`, `MAX_ENTRIES` |
| enum class | `snake_case` + 枚举值 | `log_level::info`, `block_status::used` |

### 2.2 变量名称
| 场景 | 约定 | 示例 |
|------|------|------|
| 成员变量 | `_` 后缀 | `block_size_`, `write_cursor_` |
| 静态成员 | `s_` 前缀 | `s_thread_local_buffer` |
| 线程局部 | `thread_local` + `_` 后缀 | `thread_local_buffer_` |
| 模板参数 | `T`, `U`; 或说明性名 | `template<typename T>`, `template<typename Entry>` |
| 循环变量 | `i`, `j`, `k` (显然循环) | `for(int i=0; i<n; ++i)` |

### 2.3 函数名称
| 场景 | 约定 | 示例 |
|------|------|------|
| 普通方法 | `snake_case()` | `alloc_write_chunk()`, `commit()` |
| getter | `name()` | `buffer_size()`, `capacity()` |
| setter | `set_name()` | `set_log_level(level)` |
| 谓词 | `is_` / `has_` / `should_` | `is_enable_for()`, `has_capacity()` |
| 私有方法 | 同上 + `_impl` 后缀 | `serialize_impl()` |

### 2.4 文件命名
| 文件类型 | 约定 | 示例 |
|----------|------|------|
| 头文件 | `.h` | `ring_buffer.h` |
| 实现文件 | `.cpp` | `ring_buffer.cpp` |
| 内联实现 | `.inl` | `ring_buffer.inl` |
| 平台特定 | `._platform.h` | `atomic._linux.h` |

**头文件保护**: 使用 `#pragma once`，不用宏 guard。

---

## 3. 核心设计原则

### 3.1 Zero-Copy & Zero-Allocation
- **Hot Path 定义**: 用户线程调用日志 API 到数据写入 ring buffer 的全过程
- **约束**:
  - Hot Path 中 ≤ 300ns（Release 模式）
  - 不调用 `malloc` / `new` / 任何分配函数
  - 不涉及未预分配的堆对象创建
  - 使用栈、预分配池或 TLS 缓冲区
  
- **Cold Path 允许**:
  - 配置解析、初始化、清理内存分配
  - 拦截层通过 arena 或 pool 分配

### 3.2 Lock-Free & Wait-Free
- **SPSC Ring Buffer (HP 路径)**:
  - 只需 acquire/release load/store，无 CAS
  - 读写游标分离到不同 cache line（防 false sharing）
  
- **MPSC Ring Buffer (LP 路径)**:
  - 竞争点仅在 `alloc`，使用 CAS 抢占块
  - `commit` 时无竞争（release store）
  - `read` 时无竞争（消费者独占读）
  
- **Oversize Buffer**:
  - 使用 `spin_lock_rw` 保护（自旋，不会阻塞）
  - 1 秒超时后自动回收

### 3.3 Cache-Line Alignment
- **对齐粒度**: 64 bytes （x86/ARM 标准）
- **应用场景**:
  - Ring buffer 的读写游标（TLS `log_tls_buffer_info`）
  - MPSC block header（status 字段）
  - 高并发热点数据结构
  
- **验证方式**:
  ```cpp
  static_assert(sizeof(struct_t) % 64 == 0, "must be cache-line aligned");
  static_assert(alignof(struct_t) == 64, "alignment must be 64 bytes");
  ```

### 3.4 Deferred Formatting
- **Hot Path**: 只做二进制序列化
  - timestamp(8B) + thread_id(8B) + level(1B) + category_idx(2B) + fmt_hash(4B) + typed_params
  - 禁止调用 `std::to_string`, `snprintf`, 字符串拼接
  
- **Cold Path**: Worker 线程格式化
  - `layout::do_layout()` 将二进制 entry 还原为文本
  - 支持 Python-style 格式字符串 (`{:>10.2f}` 等)

### 3.5 内存序 (Memory Order) 语义
| Order | 用途 | 代价 |
|-------|------|------|
| `relaxed` | 计数器、非关键数据 | 最快 |
| `acquire` | 读端获取同步点 | 中 |
| `release` | 写端发布同步点 | 中 |
| `acq_rel` | 互斥同步 | 较贵 |
| `seq_cst` | 全序同步 | ❌ 禁用 |

**原则**: 优先用 `acquire/release` 替代 `seq_cst`

```cpp
// 写入数据后，用 release 保证对消费者可见
status_.store(block_status::used, std::memory_order_release);

// 消费者读取状态，用 acquire 建立 happens-before
auto s = status_.load(std::memory_order_acquire);

// 同线程内的计数器更新，用 relaxed
counter_.fetch_add(1, std::memory_order_relaxed);
```

### 3.6 性能约束
| 指标 | 目标 | 验证方法 |
|------|------|----------|
| 单次日志调用（Hot Path） | < 300ns | perf record / VTune |
| 单线程异步吞吐量 | > 5M entries/s | benchmark suite |
| 10线程异步吞吐量 | > 20M entries/s | benchmark suite |
| 内存占用（10线程×200万条） | < 2MB | object dumper |
| 压缩率 | < 15% 相比文本 | file size compare |

---

## 4. 错误处理政策

### 4.1 无异常策略
- 所有错误通过返回值、错误码或 `std::optional` 传递
- 使用 `[[nodiscard]]` 标记必须检查的返回值
- Hot Path 中永不使用 `try/catch`

### 4.2 断言与检查
```cpp
// Debug 编译检查（发布版可被优化掉）
QLOG_ASSERT(condition, "message");

// 产品代码防御性检查（不可恢复错误）
if (QLOG_UNLIKELY(!condition)) {
    return error_code::invalid_argument;
}

// 优雅降级（可恢复的错误）
if (uncommon_condition) {
    use_fallback_path();
}
```

### 4.3 返回值约定
```cpp
// 成功/失败标记
[[nodiscard]] bool try_operation();

// 详细错误码
[[nodiscard]] error_code do_something();

// 可选返回值
[[nodiscard]] std::optional<value_t> get_value();

// 句柄或 null 指针
[[nodiscard]] handle_t allocate();  // 返回 null 表示失败
```

---

## 5. 并发与线程安全

### 5.1 线程局部存储 (TLS)
```cpp
// 推荐做法（C++11 保证线程安全初始化）
thread_local tls_buffer_info_t buffer_info;

// 一次性初始化
thread_local std::once_flag init_flag;
std::call_once(init_flag, [] { /* 初始化 */ });
```

### 5.2 同步原语使用
| 场景 | 用途 | 例子 |
|------|------|------|
| SPSC 通信 | acquire/release | HP ring buffer |
| MPSC 竞争 | CAS 循环 | 分配块状态转移 |
| 自旋等待 | `atomic<bool>` loop | 轮询唤醒标志 |
| 休眠/唤醒 | condition_variable + mutex | Worker 线程 66ms 周期 |

### 5.3 数据竞争检测
- 所有并发代码必须通过 ThreadSanitizer (TSan)
- 命令: `cmake ... -DQLOG_ENABLE_TSAN=ON`
- 禁止使用 `[[no_sanitize("thread")]]` 除非有明确注释

---

## 6. 模块间边界与 ABI

### 6.1 公有 API
- 头文件位置: `include/qlog/`
- 导出声明: `QLOG_EXPORT` (宏封装 visibility)
- C 语言绑定: `extern "C"` 接口在 `include/qlog/c_api.h`

### 6.2 内部实现
- 头文件位置: `src/qlog/` （不导出）
- 使用 `namespace qlog::internal` 隐藏实现细节
- 前向声明减少编译依赖

---

## 7. 代码风格

### 7.1 格式与缩进
- **缩进**: 4 个空格（无制表符）
- **行长**: 100 字符（强制）
- **大括号**: Allman 风格（新行放置）
  ```cpp
  if (condition)
  {
      do_something();
  }
  else
  {
      do_other_thing();
  }
  ```
- **工具**: `clang-format`，配置文件 `.clang-format`

### 7.2 编译宏约定
```cpp
QLOG_FORCE_INLINE   // __attribute__((always_inline)) / __forceinline
QLOG_LIKELY(x)      // __builtin_expect(!!(x), 1)
QLOG_UNLIKELY(x)    // __builtin_expect(!!(x), 0)
QLOG_RESTRICT       // __restrict__
QLOG_CACHELINE_SIZE // 64
```

### 7.3 代码审查清单
- [ ] 无 `new` / `delete` / `malloc` 在 Hot Path
- [ ] 所有 `std::atomic` 操作明确指定 memory_order
- [ ] 关键数据结构有 cache-line alignment 检查
- [ ] 并发代码通过 TSan
- [ ] 性能敏感路径通过基准测试

---

## 8. 依赖项原则

### 8.1 核心引擎约束
- 只依赖 C++ 标准库 + 平台 API（pthread / Win32）
- ❌ 禁止 Boost、Abseil 等第三方库进入 `src/qlog/`

### 8.2 第三方库使用范围
- ✅ `tools/` — 代码生成器、分析工具
- ✅ `test/` — 测试框架、benchmark 库
- ✅ `wrapper/` — 语言绑定平台库

---

## 9. 文件头模板

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

#pragma once

#include <cstdint>
#include <atomic>

namespace qlog {

// 实现内容

}  // namespace qlog
```

---

## 10. 性能基准对标

**目标**: 与 BqLog、spdlog、log4j2 对比

| 库 | 模式 | 吞吐量 | 对标目标 |
|-----|------|--------|---------|
| spdlog | async (1 worker) | ~2-3M entries/s | > 5M (QLog) |
| BqLog | async (HP) | ~5M entries/s | == (QLog目标) |
| log4j2 | async (1 appender) | ~1M entries/s | > 10x (QLog) |

---

## 10. BqLog 对齐验证框架

### 10.1 对齐验证原则

每个 Milestone（M0-M12）的实现必须与 BqLog 源码对齐，遵循以下原则：

**对齐层次**（从高到低）:
1. **数据结构对齐** (100% 必须) — 大小、字段、对齐要求
2. **算法对齐** (100% 必须) — 核心逻辑、内存序、同步语义
3. **API 签名对齐** (100% 必须) — 方法名、参数、返回值
4. **实现细节对齐** (尽最大努力) — 辅助函数、优化技巧
5. **功能对齐** (有选择性) — 不实现超出 plan.md 的内容（如 mmap）

**对齐差异处理**:
- 允许的差异：
  - ✅ 命名风格不同（BqLog 用 `_`，QLog 用 snake_case）
  - ✅ 具体实现优化（BqLog 用位操作，QLog 可用位移）
  - ✅ 代码组织（BqLog inline vs QLog 分离）
  
- 不允许的差异：
  - ❌ 数据结构字节大小、对齐方式不同
  - ❌ 算法逻辑本质改变（如 fetch_add 改为 CAS loop）
  - ❌ 内存序语义改变（release 改为 relaxed）

### 10.2 Milestone 指导文档要求

每个 Milestone 的设计文档必须包含：

1. **BqLog 对齐表** — 三列对比表，列出：项目、BqLog 位置、QLog 设计、对齐度、备注
2. **源码引用位置** — 精确指出 BqLog 源码位置和行号范围
3. **实现差异说明** — 明确允许的差异和禁止的差异
4. **验收清单** — 对齐度检查项（8-10 项）

### 10.3 指导文档结构模板

标准的 Milestone 指导文档应包含 5 个部分：
1. 核心概念与对齐确认
2. 数据结构对齐
3. 算法实现
4. 内存序与同步
5. 对齐检查表

### 10.4 代码审查检查点

在 Code Review 时，必须验证以下对齐点：

**数据结构审查**:
- ☐ sizeof(T) == BqLog sizeof(T)
- ☐ alignof(T) == BqLog alignof(T)
- ☐ offsetof(field) == BqLog offsetof(field)
- ☐ bit layout 与 BqLog 相同

**算法审查**:
- ☐ fetch_add 作为常路径（不是 CAS loop）
- ☐ CAS 仅在异常路径
- ☐ memory_order 标注与 BqLog 相同
- ☐ 无多余的 acquire/release（不要 seq_cst）

**并发安全审查**:
- ☐ 竞争点位置与 BqLog 相同
- ☐ TLS 缓存策略与 BqLog 相同
- ☐ false sharing 防护（cache-line 对齐）与 BqLog 相同

### 10.5 允许的优化与简化

**可以简化的部分** (超出 M0-M8 范围):
- ❌ mmap 支持（M9 单独实现）
- ❌ 多语言绑定（M10 单独实现）
- ❌ 调试模式下的详细统计（BqLog_BUFFER_DEBUG）
- ❌ 自定义 arena 内存分配

**必须保留的部分**:
- ✅ fetch_add + CAS 回滚（核心性能）
- ✅ TLS 缓存（减少竞争 98%）
- ✅ cache-line 分离（false sharing 防护）
- ✅ memory order 语义（正确性保证）

---

## 11. 版本记录与对齐历程

### 11.1 对齐验证历程表

| Milestone | 功能 | 对齐度 | 验证日期 | 难度 |
|-----------|------|--------|--------|------|
| M0 | 无锁原语 | ✅ 100% | 2026-04-13 | ⭐ |
| M1 | SPSC Buffer | ✅ 100% | 2026-04-14 | ⭐⭐ |
| M2 | MPSC Buffer | ✅ 100% | 2026-04-23 | ⭐⭐⭐ |
| M3 | Buffer 调度 | ⏳ 待验证 | - | ⭐⭐ |

---

## 12. BqLog 源码位置索引

| 功能模块 | 文件 | 行号范围 | 说明 |
|---------|------|---------|------|
| Atomic 包装 | bq_common.h | 100-200 | memory_order 定义 |
| Spin Lock | bq_common.h | 200-300 | spin_lock 实现 |
| SPSC Block | siso_ring_buffer.h | 60-80 | 8B header 结构 |
| SPSC Alloc | siso_ring_buffer.cpp | 80-150 | 无 CAS 算法 |
| MPSC Block | miso_ring_buffer.h | 66-92 | 64B union 结构 |
| MPSC Alloc | miso_ring_buffer.cpp | 114-220 | fetch_add + CAS |
| MPSC TLS | miso_ring_buffer.cpp | 17-60 | TLS 缓存结构 |

---

## 13. 参考文档
- C++20 Standard (ISO/IEC 14882:2020)
- [herb.sutter.com](https://herbsutter.com) - Concurrency Guidelines
- [isocpp.org Lock-Free FAQ](https://isocpp.org/wiki/faq/concurrency-multithread)
- BqLog 源码 - Reference Implementation
