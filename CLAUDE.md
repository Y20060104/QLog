# CLAUDE.md — Claude Code 工作指南

本文件为 Claude Code 提供项目特定的工作指导。任何涉及本项目的对话都应遵循本指南。

---

## 1. 编码工作原则

### ❌ Claude 不应做的

- **不编写实现代码** — 除非用户明确指示或代码骨架需要完成
- **不直接修改源文件** — `src/qlog/**/*.cpp` 和 `src/qlog/**/*.h` 由用户编写
- **不创建核心模块** — M0-M8 的实现完全交由用户

### ✅ Claude 应做的

- **提供设计指导** — 讲解架构、算法、数据结构
- **代码审查** — 当用户请求时，审查代码正确性、性能、Thread-safety
- **编译与测试协助** — 帮助诊断构建错误、测试失败
- **文档与规范** — 维护和更新 RULES.md, STATE.md, 设计文档
- **脚本与工具配置** — 完全负责编写和维护构建脚本、CMakeLists.txt 等基础设施

### 编码职责边界

```
✅ Claude 负责                    ❌ Claude 不负责
├─ CMakeLists.txt               ├─ src/qlog/**/*.cpp
├─ scripts/                      ├─ src/qlog/**/*.h (实现)
├─ .clang-format                 ├─ test/cpp/**/*.cpp (用户编写)
├─ 文档与规范                    ├─ benchmark/**/*.cpp
├─ 设计讲解                      ├─ demo/**/*.cpp
├─ 代码审查与指导                ├─ wrapper/**/*
└─ 测试框架搭建                  └─ tools/**/* (代码生成工具)

【例外】
如果用户显式要求 Claude 投入实现（如 "帮我写 M0 的 atomic.h"），
则 Claude 可以生成代码骨架，但最终实现优化交由用户完成。
```

---

## 2. 脚本使用工作流

### 完整的开发周期

```
开始日常开发
   ↓
[编码 M0/M1/... 的源文件]
   ↓
修改完毕，提交前
   ↓
运行: ./scripts/format_code.sh     ⭐ 【每次提交前】
   ↓
运行: ./scripts/build.sh Release    ⭐ 【编译验证】
   ↓
运行: ./scripts/test.sh             ⭐ 【功能测试】
   ↓
开发中遇到竞争问题？
   ↓
运行: ./scripts/run_sanitizers.sh thread  ⭐ 【TSan 检查】
   ↓
所有测试通过 ✅
   ↓
git add . && git commit -m "..."
   ↓
完成 M0
   ↓
更新 STATE.md，标记 M0 完成 ✓
```

---

## 3. 脚本使用时机详解

### 📝 format_code.sh — 代码格式化

**何时使用**:
- ✅ **每次 Git 提交前** — 必须运行
- ✅ **修改任何 `.h` / `.cpp` 文件后** — 立即运行
- ✅ **团队 code review 前** — 确保一致性

**不需要**:
- ❌ 提交文档（`.md` 文件）时无需运行
- ❌ 修改 CMakeLists.txt 时无需运行

**示例工作流**:
```bash
# 编写或修改源代码
$ vim src/qlog/primitives/atomic.h
$ vim src/qlog/primitives/atomic.cpp

# 格式化
$ ./scripts/format_code.sh

# 构建验证
$ ./scripts/build.sh Release

# 提交
$ git add src/qlog/primitives/*.{h,cpp}
$ git commit -m "Implement: atomic.h wrapper for std::atomic"
```

---

### 🔨 build.sh — 项目构建

**何时使用**:
- ✅ **每次代码修改后** — 确保编译通过
- ✅ **创建新文件后** — 验证 CMakeLists.txt 配置正确
- ✅ **需要生成 compile_commands.json** — 给 IDE 使用

**使用方式**:
```bash
# Release 构建（最终版）
./scripts/build.sh Release

# Debug 构建（开发版）
./scripts/build.sh Debug
```

**输出**:
- `build/lib/libqlog.a` — 静态库
- `build/compile_commands.json` — 编译数据库

**不应该**:
- ❌ 手动调用 `cmake -B build ...`
- ❌ 修改 build/ 目录内的文件

---

### 🧪 test.sh — 运行单元测试

**何时使用**:
- ✅ **M0 完成后** — 首次有单元测试可运行
- ✅ **每个 Milestone 完成时** — 验证功能正确性
- ✅ **编写/修改测试代码后** — 立即运行

**工作流示例**（M0 完成后）:
```bash
$ ./scripts/test.sh
Running unit tests...
  ✓ test_atomic (0.05s)
  ✓ test_spin_lock (0.10s)
  ✓ test_aligned_alloc (0.02s)
100% tests passed!
```

**如果测试失败**:
```bash
$ ./scripts/test.sh        # 查看失败详情
$ # 根据失败信息修复代码
$ ./scripts/format_code.sh
$ ./scripts/build.sh Release
$ ./scripts/test.sh        # 重新运行
```

---

### 🔍 run_sanitizers.sh — 内存/竞争检查

**何时使用**:
- ✅ **编写并发代码后** — 必须通过 TSan
- ✅ **发现诡异的多线程问题** — 用 TSan 诊断
- ✅ **性能优化后** — 确保安全性未下降
- ✅ **每个 Milestone 最后** — 最终验证

**三种检查方式**:

**1️⃣ ThreadSanitizer (TSan) — 数据竞争检查** ⭐ 最重要
```bash
$ ./scripts/run_sanitizers.sh thread

# 输出示例
================
WARNING: ThreadSanitizer: data race
  Write of size 4 at 0x...
  Previous write of size 4 at 0x...
================

# ❌ 如果看到 data race，必须修复
# ✅ 目标：0 warnings，正常完成测试
```

**2️⃣ AddressSanitizer (ASan) — 内存错误检查**
```bash
$ ./scripts/run_sanitizers.sh address

# 检查：
# - heap-buffer-overflow
# - use-after-free
# - memory leak
```

**3️⃣ UBSanitizer (UBSan) — 未定义行为检查**
```bash
$ ./scripts/run_sanitizers.sh undefined

# 检查：
# - 整数溢出
# - 无效转换
# - 越界访问
```

**推荐使用顺序**:
1. 先用 TSan: `./scripts/run_sanitizers.sh thread`（并发代码）
2. 再用 ASan: `./scripts/run_sanitizers.sh address`（内存操作）
3. 最后用 UBSan: `./scripts/run_sanitizers.sh undefined`（全面）

---

## 4. Milestone 完成工作流

### 每个 Milestone 的标准流程

```
┌─────────────────────────────────────────────────┐
│ Milestone N 开发开始（如 M0: 无锁原语）         │
└─────────────────────────────────────────────────┘
          ↓
┌─────────────────────────────────────────────────┐
│ 1️⃣ 编码实现                                      │
│   - 用户编写 src/qlog/primitives/*.h/.cpp       │
│   - 用户编写 test/cpp/test_*.cpp                │
│   - Claude 只在对话框提供指导                   │
└─────────────────────────────────────────────────┘
          ↓
┌─────────────────────────────────────────────────┐
│ 2️⃣ 每日工作循环（重复直到完成）                │
│   a) 编写代码                                    │
│   b) ./scripts/format_code.sh                   │
│   c) ./scripts/build.sh Release                 │
│   d) ./scripts/test.sh                          │
│   e) 编译/测试失败？ → 修复 → 回到 a)           │
│   f) ./scripts/run_sanitizers.sh thread         │
│   g) TSan 检测到竞争？ → 修复 → 回到 f)         │
└─────────────────────────────────────────────────┘
          ↓
┌─────────────────────────────────────────────────┐
│ 3️⃣ Milestone 完成检查                           │
│   ✓ 所有代码通过 clang-format                   │
│   ✓ 编译无警告                                  │
│   ✓ 所有单元测试通过                            │
│   ✓ 0 Tsan 竞争                                 │
│   ✓ 0 ASan 内存错误                             │
│   ✓ 目标性能指标达成                            │
└─────────────────────────────────────────────────┘
          ↓
┌─────────────────────────────────────────────────┐
│ 4️⃣ 提交与文档更新                             │
│   a) git add .                                   │
│   b) git commit -m "Implement: M0 ...    "      │
│   c) 更新 STATE.md（标记 M0 完成 ✓）           │
│   d) git add STATE.md                           │
│   e) git commit -m "Update: M0 complete"        │
│   f) git tag -a m0 -m "M0 implementation"       │
└─────────────────────────────────────────────────┘
          ↓
┌─────────────────────────────────────────────────┐
│ Milestone N+1 开始（如 M1: SPSC Ring Buffer）  │
└─────────────────────────────────────────────────┘
```

### 每次完成 Milestone 后

**不是**每次完成都运行脚本，而是按工作流运行：

| 阶段 | format_code.sh | build.sh | test.sh | sanitizers.sh |
|------|:--:|:--:|:--:|:--:|
| 编码中（每日） | ✅ 每天提交前 | ✅ 每天编译 | ✅ 每天测试 | ⏳ 有竞争时 |
| Milestone 完成 | ✅ 最终提交前 | ✅ 最终验证 | ✅ 最终验证 | ✅ 必需 |
| 性能调优（M12） | ✅ 每次调整后 | ✅ 每次编译 | ✅ 每次基准 | ✅ 每次检查 |

**简化答案**: **不是每次完成就 format_code。而是**：
- **编写代码时**: 每次编辑 `.h`/`.cpp` 文件后立即 `format_code.sh`
- **提交时**: 运行 `format_code.sh` → `build.sh` → `test.sh` 验证链
- **Milestone 完成**: 运行完整流程包括 `sanitizers.sh`

---

## 5. 常见场景的脚本使用

### 场景 A: 修改了一个源文件

```bash
$ vim src/qlog/primitives/spin_lock.cpp

# 立即
$ ./scripts/format_code.sh

# 验证编译
$ ./scripts/build.sh Release

# 如果编译成功且有测试
$ ./scripts/test.sh
```

### 场景 B: 发现多线程问题

```bash
$ ./scripts/test.sh           # 测试通过，但行为诡异
$ ./scripts/run_sanitizers.sh thread  # 检查竞争

# 如果 TSan 报告 data race
$ # 修复代码...
$ ./scripts/format_code.sh
$ ./scripts/build.sh Release
$ ./scripts/run_sanitizers.sh thread  # 验证修复
```

### 场景 C: M0 完成，准备提交

```bash
# 写完所有代码
$ ./scripts/format_code.sh    # 确保格式一致
$ ./scripts/build.sh Release  # 编译验证
$ ./scripts/test.sh           # 功能测试
$ ./scripts/run_sanitizers.sh thread   # TSan
$ ./scripts/run_sanitizers.sh address  # ASan

# 一切通过 ✅
$ git add src/qlog/primitives test/cpp
$ git commit -m "Implement: M0 lock-free primitives (atomic, spin_lock, aligned_alloc)"

# 更新进度
$ vim STATE.md  # 标记 M0 完成
$ ./scripts/format_code.sh  # 格式化文档
$ git add STATE.md
$ git commit -m "Update: M0 implementation complete and verified"
$ git tag -a m0 -m "Milestone 0: Lock-free primitives"
```

---

## 6. Claude 不做的任务清单

### ❌ 以下任务用户需自己完成

1. **编写核心模块代码** — `src/qlog/**/*.cpp/.h`
2. **编写测试代码** — `test/cpp/**/*.cpp`
3. **编写基准代码** — `benchmark/cpp/**/*.cpp`
4. **编写示例代码** — `demo/**/*.cpp`
5. **多语言绑定** — `wrapper/**/*`（除非原理讲解）
6. **工具代码生成** — `tools/**/*`（除非设计讲解）
7. **运行脚本** — `./scripts/*.sh`（用户执行，Claude 只解释）
8. **Git 操作** — `git add/commit/push`（用户操作）

### ✅ Claude 会主动完成的任务

1. **创建和维护 CMakeLists.txt**
2. **创建和维护构建脚本**
3. **代码审查与性能建议**
4. **架构与算法讲解**
5. **与规范冲突的诊断**
6. **Thread-safety/竞争检查建议**
7. **文档编写与维护**
8. **设计决策讨论**

---

## 7. 对话中的交互模式

### Claude 的回应模式

**当用户说**: "帮我实现 M0"

**Claude 不应该**: 直接生成完整的 atomic.h + spin_lock.h + test

**Claude 应该**: 
```
🔍 理解需求:
- "M0 包括 atomic 包装、自旋锁、对齐分配器"
- "需要通过 TSan，目标 > 1M ops/s"

📚 提供讲解:
1. "atomic<T> 应该这样设计..."
2. "CAS 循环的生存性保证..."
3. "Cache-line 对齐的关键点..."

📋 建议实现步骤:
1. 先实现 atomic.h（最简单）
2. 再实现 spin_lock.h（需要 atomic）
3. 最后 aligned_alloc.h
4. 编写对应单元测试

🤔 有问题吗？你想从哪部分开始？
```

**当用户说**: "我写的代码编译出错"

**Claude 应该**:
```
✅ 要求用户贴出:
- 错误信息
- 相关代码片段

🔍 分析错误原因（不是替用户写）

💡 提供修复建议（指导，不是直接修改）

例:
"看起来是 memory_order 拼写错误。
应该是 std::memory_order_release，不是 release。
你能找到那一行吗？"
```

**当用户说**: "性能达不到目标"

**Claude 应该**:
```
📊 要求数据:
- 基准结果
- 预期 vs 实际
- 代码实现

🔍 分析瓶颈:
"看起来 false sharing 可能是问题...
你的 hot_data_ 是否 cache-line aligned？"

💡 建议优化:
"试试使用 padding 分离读写游标...
或者调整 memory_order..."

📈 指导验证:
"优化后用 perf 记录看看是否改善"
```

---

## 8. 记住的约束总结

为避免遗忘，这是最关键的三点：

### 🎯 核心原则

| 原则 | 详情 |
|------|------|
| **用户编码** | 所有 `src/qlog/**` 代码由用户编写。Claude 只提供设计讲解。 |
| **Claude 基础设施** | CMakeLists.txt、脚本、文档由 Claude 负责。 |
| **脚本时机** | format → build → test → sanitizers。不是每次 milestone 都 format。 |

### 📋 工作流

```
日常: 编码 → format_code.sh → build.sh → test.sh → [有问题→修复]
M完: 所有脚本通过 → 包括 run_sanitizers.sh → git commit+tag
```

### ✨ 对话模式

```
用户需求 → Claude 讲解/建议 → 用户编码 → Claude 审查 → 循环
✗ Claude 不替用户编码（除非明确要求骨架）
✓ Claude 在对话中提供指导和讲解
```

---

**最后更新**: 2026-04-11  
**版本**: 1.0
