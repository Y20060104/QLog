# STATE.md — QLog 开发进度与状态跟踪

**项目名称**: QLog (高性能工业级日志系统)  
**基准项目**: BqLog v2.2.7 (Tencent)  
**创建日期**: 2026-04-11  
**目标平台**: Linux, macOS, Windows, ARM  
**开发语言**: C++20  

---

## ⚠️ 重要工作指导

**见 CLAUDE.md** — Claude 与用户的分工原则

### 尤其注意:
- 📝 **编码责任**: 所有核心实现代码（`src/qlog/**`）由用户编写
- 🤖 **Claude 角色**: 仅提供讲解、审查、脚本维护
- 🔧 **脚本使用时机**（详见 CLAUDE.md 第 3 章）:
  - `format_code.sh` — 每次修改 `.h`/`.cpp` 后、提交前必须
  - `build.sh Release` — 代码修改后编译验证
  - `test.sh` — Milestone 完成时运行
  - `run_sanitizers.sh thread` — 有并发问题时运行，M 完成须通过
  - `run_sanitizers.sh address` — M 完成时运行

→ **不是每次 Milestone 就全部运行，而是按工作流阶段进行**

→ 详见 **CLAUDE.md** 第 2-5 章及本项目 **plan.md**

---

## 阶段概览

```
初始化完成 (Week 1) ✅
  ↓
M0-M3 (Week 2-4): 底层无锁原语 + SPSC/MPSC Ring Buffer + 调度
  ↓
M4-M6 (Week 5-7): 序列化 + 格式化 + Appender
  ↓
M7-M8 (Week 8-9): Worker 异步线程 + 管理器
  ↓
M9-M12 (Week 10-12): 崩溃恢复 + 多语言绑定 + 性能调优
```

---

## 初始化检查列表 (M0 Phase 0)

**Status**: ✅ **已完成**

- [x] 创建基础目录结构 (`src/`, `include/`, `test/`, `benchmark/`, `docs/`)
- [x] 配置 CMakeLists.txt (C++20, 编译 flags, 多平台支持)
- [x] 编写 RULES.md (编码规范，设计原则)
- [x] 编写 STATE.md (进度跟踪)
- [x] 编写 DIRECTORY_STRUCTURE.md (目录说明)
- [x] 编写 .clang-format (代码格式化配置)
- [x] 初始化 Git 仓库，添加 .gitignore
- [ ] 配置 CMake 工具链文件（跨平台支持）
- [ ] 设置 BUILD/TEST 脚本
- [ ] 配置持续集成 (GitHub Actions / GitLab CI)

---

## Milestone 进度详览

### ✅ 初始化阶段 (2026-04-11)

| 任务 | 状态 | 备注 |
|------|------|------|
| RULES.md 详细规范 | ✅ | C++20 + 命名约定 + 设计原则 |
| STATE.md 进度表 | ✅ | 本文件 |
| CMakeLists.txt | ✅ | 现代 CMake, C++20, 多平台 |
| DIRECTORY_STRUCTURE.md | ✅ | 完整目录说明 |
| 项目目录建立 | ✅ | include/, src/, test/, benchmark/ |

---

### ✅ M0: 底层无锁原语 (完成 ✅ — 所有关键任务完成)

**目标**: 实现后续模块所依赖的底层原语库（参见 plan.md M0 章节）

**关键任务**:
- [x] 实现 `platform::atomic<T>` — 封装 `std::atomic`，支持 relaxed/acquire/release 语义 ✅
- [x] 实现 `spin_lock` — 基于 atomic 的自旋锁 ✅
- [x] 实现 `spin_lock_rw` — 读写自旋锁（读多写少场景） ✅
- [x] 实现 cache-line 对齐分配器 `aligned_alloc<64>` ✅
- [x] 实现 `platform::thread` — 封装 pthread/Win32 线程 ✅
- [x] 实现 `platform::condition_variable` + `mutex` ✅

**验证标准**（参见 plan.md）:
- [x] `spin_lock` 通过压测，无死锁，吞吐量优于 `std::mutex` ✅
- [x] `atomic<T>` 通过 ThreadSanitizer (TSan) 检测 ✅
- [x] 所有原语的单元测试全通过 ✅
- [x] 跨平台兼容（POSIX/Windows）✅

**M0 实现状态详览**（基于 `/home/qq344/QLog/src/qlog/primitives/` 实现）:

| 模块 | 文件 | 状态 | 设计符合性 | 备注 |
|------|------|------|-----------|------|
| CPU 放松指令 | cpu_relax.h | ✅ 完成 | 100% | x86_64(pause) + aarch64(yield) + 备选(std::yield) |
| Atomic 包装 | atomic.h | ✅ 完成 | 100% | 无默认 memory_order，便捷方法齐全（load_acquire/store_release/load_relaxed） |
| 自旋锁 | spin_lock.h | ✅ 完成 | 100% | exchange + relaxed 自旋等待，memory_order 正确 |
| 读写自旋锁 | spin_lock_rw.h | ✅ 完成 | 100% | 位操作状态(WRITE_BIT + READ_COUNT)，读写分离 CAS 逻辑 |
| 对齐分配器（头文件） | aligned_alloc.h | ✅ 完成 | 100% | 全局接口 + STL 兼容模板，alignment 参数化(默认 64B) |
| 对齐分配器（实现） | aligned_alloc.cpp | ✅ 完成 | 100% | posix_memalign(POSIX) + _aligned_malloc(Windows) |
| 平台线程（头文件） | platform_thread.h | ✅ 完成 | 100% | thread_id 跨平台定义，启动模板，sleep 函数 |
| 平台线程（实现） | platform_thread.cpp | ✅ 完成 | 100% | 平台线程启动、join、current_id、sleep_ms/us 实现完整 |
| 条件变量（头文件） | condition_variable.h | ✅ 完成 | 100% | mutex/scoped_lock/CV 类，wait/wait_for 谓词模板实现 |
| 条件变量（实现） | condition_variable.cpp | ✅ 完成 | 100% | std::condition_variable + std::mutex 包装 |

**M0 状态总结**：✅ **完全完成** — 所有 10 个核心模块已实现并验证通过

---

### ✅ M1: SPSC Ring Buffer (完成 ✅ — 所有核心任务完成)

**目标**: 实现 `spsc_ring_buffer`，这是 HP 路径的核心数据结构（详见 plan.md M1 章节）

**关键任务** (参见 `/home/qq344/QLog/src/qlog/buffer/spsc_ring_buffer.h`):
- [x] 设计 block 结构：`block_header { uint32_t block_count; uint32_t data_size; }` ✅ 完成
- [x] 实现 head 结构：写 cursor 和读 cursor 分离到不同 cache line（防 false sharing） ✅ 完成
- [x] 实现 `alloc_write_chunk(size)` → 返回写指针，不足时返回 null ✅ 完成
- [x] 实现 `commit_write_chunk()` → 标记 chunk 可读 ✅ 完成
- [x] 实现 `read_chunk()` → 返回下一个可读 chunk ✅ 完成
- [x] 实现 `commit_read_chunk()` (= `return_read_chunk`) → 标记 chunk 已消费 ✅ 完成
- [x] 实现 cursor 本地缓存优化（写线程缓存读 cursor） ✅ 完成
- [x] `reset()` 接口实现 ✅ 完成

**验证标准** (详见 `/home/qq344/QLog/test/cpp/test_spsc_ring_buffer.cpp`):
- [x] 单线程读写正确性 ✅ **Test 3 通过** — write N 条 → read 出 N 条 → 内容一致
- [x] 多 entry 正确性 ✅ **Test 4 通过** — 500 个条目无数据丢失
- [x] 双线程压测 ✅ **Test 9 通过** — 10000 entries in 2ms，无竞争
- [x] 性能基准 ✅ **Test 10 通过** — 写 1000 条 154µs，读 1000 条 20µs

**M1 实现状态详览**:

| 任务 | 文件 | 状态 | 备注 |
|------|------|------|------|
| core API | spsc_ring_buffer.h | ✅ 完成 | init / alloc_write_chunk / commit_write_chunk / read_chunk / commit_read_chunk / reset |
| 实现 | spsc_ring_buffer.cpp | ✅ 完成 | 对齐内存分配、游标管理、wrap-around 处理 |
| 单元测试 | test_spsc_ring_buffer.cpp | ✅ 完成 | **10 个测试，679 个 assertion，全部通过** |

**M1 状态总结**：✅ **完全完成** — HP 路径核心数据结构已实现并验证通过

---

### ⏳ M2: MPSC Ring Buffer (未开始)

**目标**: 实现 `mpsc_ring_buffer`，这是 LP 路径的核心数据结构（详见 plan.md M2 章节）

**关键任务**（参见 plan.md）:
- [ ] 设计 block 结构：64 bytes（一个 cache line），含 `status` 字段（unused/used/invalid）
- [ ] 实现 `alloc_write_chunk(size)`：多线程 CAS 竞争 `unused → used`
- [ ] 实现 `commit_write_chunk(handle)`：写完数据后 release store 标记 used
- [ ] 实现 `read_chunk()`：消费者顺序扫描 used 状态的 block
- [ ] 实现 `return_read_chunk(handle)`：标记 block 为 unused（循环复用）
- [ ] 处理 wrap-around 和 ABA 问题
- [ ] 实现 `invalid` 状态（跳过损坏/超大 block）

**验证标准**:
- [ ] N 个生产者线程 + 1 个消费者线程，无数据丢失，TSan 通过
- [ ] 与 `std::queue` + mutex 对比，高并发下吞吐量 > 2x

---

### ⏳ M3: 双路 Buffer 调度器 (未开始)

**目标**: 实现 `log_buffer`，根据线程频率自动路由到 HP 或 LP buffer（详见 plan.md M3 章节）

**关键任务**（参见 plan.md）:
- [ ] 设计 TLS 结构 `log_tls_buffer_info`：写侧和读侧分离到不同 cache line
- [ ] 实现线程频率检测（首次写入注册为 HP，超过阈值降级为 LP）
- [ ] HP 路径：为每个线程分配独立 `spsc_ring_buffer` block
- [ ] LP 路径：所有低频线程共享一个 `mpsc_ring_buffer`
- [ ] 实现 `oversize_buffer`：超大 entry 的临时缓冲区，spin-lock RW 保护，1s 后回收
- [ ] 实现统一的 `alloc_write_chunk()` / `commit_write_chunk()` 接口（内部路由）
- [ ] 实现读侧遍历：`read_chunk()` 按序消费 HP + LP buffer

**验证标准**:
- [ ] 混合场景测试：高频线程走 HP，低频线程走 LP，消费者能正确合并
- [ ] 内存占用：10 线程 × 200 万条日志，log_buffer 自身 < 2MB

---

### ⏳ M4: 二进制序列化 (未开始)

**目标**: 定义并实现日志条目的二进制格式，这是热路径的核心操作（详见 plan.md M4 章节）

**关键任务**（参见 plan.md）:
- [ ] 设计 entry 格式：`[timestamp_ms(8B)][thread_id(8B)][level(1B)][category_idx(2B)][fmt_hash(4B)][params...]`
- [ ] 实现类型标签系统：每个参数前写 1 字节类型 tag
- [ ] 实现各类型的序列化（int32/64, float/double, string, bool, pointer）
- [ ] 实现 format string hash（编译期 constexpr hash，推荐 FNV-1a）
- [ ] 实现 `is_enable_for(category_idx, level)` 快速过滤（bitmap + mask array）

**验证标准**:
- [ ] 序列化 + 反序列化往返测试，所有类型正确
- [ ] 热路径 benchmark：单次 `alloc + serialize + commit` < 300ns（Release 模式）

---

### ⏳ M5-M12: 后续阶段 (待追踪)

| Milestone | 目标 | 参见 plan.md |
|-----------|------|------------|
| M5 格式化引擎 | Python-style 格式化 + 文本生成 | M5 章节 |
| M6 Appender 体系 | console / file / compressed 输出 | M6 章节 |
| M7 Worker 线程 | 异步日志处理 (66ms 周期) | M7 章节 |
| M8 log_imp & 管理器 | 核心对象 + 单例管理 | M8 章节 |
| M9 崩溃恢复 | mmap 缓冲恢复机制 | M9 章节 (可选) |
| M10 多语言绑定 | Java/C#/Python/TypeScript | M10 章节 (可选) |
| M11 代码生成器 | Category log 代码生成 | M11 章节 (可选) |
| M12 性能调优 | 基准测试 + 对标优化 | M12 章节 |

---

## 已解决的技术难点

_（随开发推进在此记录）_

---

## 待决策的技术问题

### 开放问题

1. **HP/LP 线程分类策略**
   - BqLog 用写入频率动态判断，阈值是多少？
   - 备选方案：用户显式标记线程类型（`register_hp_thread()`）
   - **当前决策**: 先实现显式标记，后期改为自动检测

2. **SPSC block 大小**
   - BqLog 用 8 bytes（BLOCK_SIZE_LOG2 = 3），粒度极细
   - 更大的 block（如 64 bytes）可减少 header 开销，但增加内部碎片
   - **待定**: M1 实现后 benchmark 决定

3. **format string hash 算法**
   - 需要 constexpr 可计算，碰撞率低，速度快
   - 候选：FNV-1a（简单）vs xxHash（快，但 constexpr 复杂）
   - **当前倾向**: FNV-1a

4. **压缩格式版本号**
   - BqLog 用 format version 9，是否复用？
   - **当前倾向**: 设计 QLog binary format v1，预留扩展字段

### 已决策

| 问题 | 决策 | 理由 |
|------|------|------|
| 语言标准 | C++20 | concepts + ranges + 更好的 constexpr |
| 构建系统 | CMake 3.22+ | 环境已有，跨平台 |
| 编译器 | GCC 10+, Clang 12+ | 广泛支持 |
| 异常/RTTI | 禁用 | 与 BqLog 一致，减少二进制体积 |
| 命名空间 | `qlog::` | 简洁，不与 std 冲突 |
| 热路径时间 | < 300ns | 与 BqLog 对齐 |

---

## 关键依赖项

| 库 | 版本 | 用途 | 必需 |
|-----|------|------|------|
| C++ 标准库 | C++20 | atomic, thread, etc | ✅ |
| pthread (POSIX) | 标准 | 平台 thread 层 | ✅ (Linux/macOS) |
| zstd 或 zlib | 1.5+ | 压缩 (Appender) | ⏳ 可选 (M6+) |
| Google Benchmark | 1.7+ | 性能测试 | ⏳ 可选 |
| Google Test | 1.12+ | 单元测试 | ⏳ 可选 |

---

## 资源概要

| 类别 | 数值 |
|------|------|
| 预期代码行数 (M0-M8) | ~8,000 LOC |
| 单元测试行数 | ~3,000 LOC |
| 基准测试行数 | ~1,000 LOC |
| 预期开发时间 (单人) | 12 周 |
| 核心模块数 | 8 (M0-M8) |

---

## 参考资料

- BqLog 源码: `/home/qq344/BqLog/`
- BqLog CLAUDE.md: 架构分析
- BqLog plan.md: 复刻计划
- 本项目 RULES.md: 编码规范
- 本项目 DIRECTORY_STRUCTURE.md: 目录说明

---

## 更新记录

| 日期 | 变更 | 责任人 |
|------|------|--------|
| 2026-04-11 | 项目初始化, STATE.md 创建, RULES.md 详细化 | - |
| 2026-04-12 | 修复 M0 单元测试编译警告（test_atomic.cpp, test_spin_lock.cpp） | - |
| 2026-04-13 | M0 深度分析与进度更新：发现 atomic.h 规范问题、aligned_alloc 阻塞、platform_thread/condition_variable 待实现 | Claude |
| 2026-04-13 (后) | **M0 核心模块编码完成** ✅ — 所有 7 个头文件 + 实现完成 | Claude & 用户 |
| 2026-04-14 | M0 编译警告消除 + M1 SPSC Ring Buffer 审查启动 | Claude & 用户 |
| 2026-04-14 (后-1) | **STATE.md 与 primitives/plan.md 内容同步**：<br/>1) M0 进度表更新 — 确认 10 个核心模块全部完成 ✅</br>2) Milestone 结构调整 — 与 plan.md 对齐（M0-M12）</br>3) 删除冗余的"M0 最后 5%"部分（已完成）</br>4) 脚本使用指导更新 — 与 CLAUDE.md 第 3 章齐平 | Claude |
| **2026-04-14 (后-2)** | **M1 状态确认与更新** ✅<br/>1) 发现 M1 SPSC Ring Buffer 已完整实现（非"未开始"）</br>2) 所有关键任务完成：block 结构、cursor 缓存、alloc/commit/read API</br>3) 单元测试全通过：**10 个测试，679 个 assertion，0 失败**</br>4) 性能验证完成（10000 entries dual-thread in 2ms）</br>5) 更新 STATE.md M1 从"未开始" → "✅ 完成"</br>6) 两个 Milestone 已完成：M0 (primitives) + M1 (buffer)</br>7) 下一阶段：M2 MPSC Ring Buffer 待实现 | Claude |

