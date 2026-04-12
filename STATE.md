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
- 🔧 **脚本使用时机**:
  - `format_code.sh` — 每次修改后、Every 提交前
  - `build.sh` — 每日编译验证
  - `test.sh` — 功能测试
  - `run_sanitizers.sh thread` — Milestone 完成时

→ **不是每次完成 Milestone 就 format，而是按工作流进行**

→ 详见 **CLAUDE.md** 第 2-5 章

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

### 🔄 M0: 底层无锁原语 (进行中)

**目标**: 建立后续模块所依赖的原语库

**关键任务**:
- [ ] `src/qlog/primitives/atomic.h` — `std::atomic` 包装 + memory_order 显式化
- [ ] `src/qlog/primitives/spin_lock.h` — 互斥自旋锁
- [ ] `src/qlog/primitives/spin_lock_rw.h` — 读写自旋锁
- [ ] `src/qlog/primitives/aligned_alloc.h` — Cache-line 64bytes 对齐分配器
- [ ] `src/qlog/primitives/platform_thread.h` — 跨平台 thread 包装
- [ ] `src/qlog/primitives/condition_variable.h` — CV + mutex 包装
- [ ] 单元测试 (test/cpp/test_atomic.cpp, test_spin_lock.cpp) — ⚠️ 编译警告已修复

**技术难点**:
- [ ] MESI 协议 + false sharing 理解与实现
- [ ] memory_order 语义正确性（acquire/release vs seq_cst）
- [ ] 跨平台原子操作差异（x86 vs ARM vs RISC-V）
- [ ] 自旋锁与 mutex 性能权衡

**验证标准**:
- [ ] ThreadSanitizer 通过（0 data race）
- [ ] spin_lock vs std::mutex 性能对比 (目标: > 1M ops/s 竞争场景)
- [ ] 所有原语通过 stress test (8 线程 × 10秒)

---

### ⏳ M1: SPSC Ring Buffer (未开始)

**目标**: 单生产者单消费者无锁环形缓冲区（HP 路径核心）

**关键任务**:
- [ ] 设计 block 结构（header + payload）
- [ ] 实现 wrap-around 处理（模 N 环形索引）
- [ ] 实现 `alloc_write_chunk(size)` 写端分配
- [ ] 实现 `commit_write_chunk()` 写端提交
- [ ] 实现 `read_chunk()` 读端消费
- [ ] 实现 `return_read_chunk()` 读端返还
- [ ] 实现游标缓存优化（写线程缓存读游标）
- [ ] 实现 `batch_read()` 批量读取接口

**技术难点**:
- [ ] 游标回绕（wrap-around）的正确性
- [ ] acquire/release 同步点的精确放置
- [ ] 缓冲空间不足时的处理策略 (返回 null vs 等待 vs 动态分配)

**验证标准**:
- [ ] 单线程正确性测试（write N → read N → 内容一致）
- [ ] 双线程压测 (TSan 通过, 无竞争)
- [ ] 吞吐量目标: > 3x std::queue + mutex

**关键参数**:
- 默认大小: 64 KB
- Block 粒度: 8 bytes (可配置)
- 目标吞吐: > 5M entries/s (单线程)

---

### ⏳ M2: MPSC Ring Buffer (未开始)

**目标**: 多生产者单消费者无锁环形缓冲区（LP 路径核心）

**关键任务**:
- [ ] 设计 block 结构（status: unused → used → invalid）
- [ ] 实现 CAS 循环分配块（多线程竞争）
- [ ] 实现 release store 提交（无竞争）
- [ ] 实现消费者顺序扫描
- [ ] 处理 ABA 问题（用 version 或 epoch）
- [ ] 实现 invalid 状态跳过（故障恢复）

**技术难点**:
- [ ] CAS 循环的生存性保证（性能 vs 正确性权衡）
- [ ] ABA 问题的防护（增加版本号的开销）
- [ ] Block 粒度对竞争的影响

**验证标准**:
- [ ] N 生产者 + 1 消费者无数据丢失
- [ ] TSan 通过 (无竞争)
- [ ] 高并发下吞吐量 > 2x std::queue + mutex

**关键参数**:
- Block 大小: 64 bytes (cache-line)
- 默认 block 数: 64
- 目标吞吐: > 10M entries/s (8 生产者)

---

### ⏳ M3: 双路缓冲调度器 (未开始)

**目标**: 自动路由日志到 HP (SPSC) 或 LP (MPSC) 缓冲区

**关键任务**:
- [ ] 设计 TLS 缓冲信息结构（写侧/读侧分离）
- [ ] 实现频率检测算法（首次标记 HP，超阈值降级 LP）
- [ ] HP 线程分配专属 SPSC buffer
- [ ] LP 线程共享单个 MPSC buffer
- [ ] 实现 oversize buffer（超大条目临时存储，spin-lock 保护）
- [ ] 实现统一的分配/提交接口（内部路由）
- [ ] 消费者端遍历 HP + LP 合并

**验证标准**:
- [ ] 混合负载测试（高频走 HP，低频走 LP）
- [ ] 内存占用: 10 线程 × 200 万条 < 2MB
- [ ] 消费顺序正确性保证

---

### ⏳ M4: 二进制序列化 (未开始)

**目标**: 日志条目的二进制格式与序列化

**关键任务**:
- [ ] 定义 entry 格式（时间戳 + 线程 ID + 日志级别 + 分类 + fmt hash + 参数...）
- [ ] 实现类型标签系统（每参数 1 字节 tag）
- [ ] 实现各基本类型序列化（int32/64, float/double, string, bool, pointer）
- [ ] 实现 format string hash（编译期 constexpr hash, 推荐 FNV-1a）
- [ ] 实现 `is_enable_for(category, level)` 高速过滤

**技术难点**:
- [ ] String 的长度编码（VLQ vs fixed-length 权衡）
- [ ] UTF-8 / UTF-16 的序列化
- [ ] constexpr hash 的碰撞率控制

**验证标准**:
- [ ] 序列化 + 反序列化往返测试
- [ ] 热路径基准: < 300ns/entry (Release)

---

### ⏳ M5-M12: 后续阶段 (待追踪)

| Milestone | 目标 | 时限 |
|-----------|------|------|
| M5 格式化引擎 | Python-style 格式字符串 + 文本生成 | Week 5 |
| M6 Appender 体系 | console / file / compressed 输出 | Week 6 |
| M7 Worker 线程 | 异步日志处理 (66ms 周期) | Week 7 |
| M8 log_imp & 管理器 | 核心对象 + 单例管理 | Week 8 |
| M9 崩溃恢复 | mmap 缓冲恢复机制 | Week 9 (可选) |
| M10 多语言绑定 | Java/C#/Python/TypeScript | Week 10 (可选) |
| M11 代码生成器 | Category log 代码生成 | Week 11 (可选) |
| M12 性能调优 | 基准测试 + 对标优化 | Week 12 |

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
| | | |
