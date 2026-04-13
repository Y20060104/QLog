# QLog 项目初始化完成 ✅

> **QLog** — 高性能工业级日志系统（复刻 BqLog）  
> **基于**: C++20, Lock-free 无锁设计, Zero-copy 零拷贝  
> **目标**: 5M+ entries/s, < 300ns per log call

---

## 📋 初始化清单

所有基础文件已生成，项目可以开始开发：

### ✅ 生成的文件

| 文件 | 用途 |
|------|------|
| **RULES.md** | C++20 编码规范、命名约定、设计原则 |
| **STATE.md** | 开发进度跟踪、Milestone 详情、技术难点 |
| **DIRECTORY_STRUCTURE.md** | 完整目录结构说明 + 扩展指南 |
| **CMakeLists.txt** | 现代 CMake 配置、多平台支持、C++20 |
| **.clang-format** | 代码格式化配置（Allman 风格，100 字符行限） |
| **.gitignore** | Git 忽略规则 |
| **scripts/build.sh** | 构建脚本 |
| **scripts/test.sh** | 测试脚本 |
| **scripts/format_code.sh** | 代码格式化脚本 |
| **scripts/run_sanitizers.sh** | 运行 ASan/TSan/UBSan |

### 📂 关键目录

```
QLog/
├── include/qlog/          # 公有 API 头文件
├── src/qlog/              # 核心实现
│   ├── primitives/        # M0: 无锁原语
│   ├── buffer/            # M1-M3: SPSC/MPSC + 调度
│   ├── entry/             # M4: 序列化
│   ├── layout/            # M5: 格式化
│   ├── appender/          # M6: 输出体系
│   ├── worker/            # M7: Worker 线程
│   └── log/               # M8: 管理器
├── test/cpp/              # 单元测试
├── benchmark/cpp/         # 性能基准
└── scripts/               # 构建脚本
```

---

## 🚀 快速开始

### 1. 初始化 Git 仓库

```bash
cd /home/qq344/QLog
git init
git add .
git commit -m "Initial commit: QLog project structure & docs"
```

### 2. 构建项目

```bash
# Release 构建（推荐）
./scripts/build.sh Release

# Debug 构建（开发模式）
./scripts/build.sh Debug
```

构建输出将在 `build/` 目录下。

### 3. 运行测试

```bash
./scripts/test.sh
```

### 4. 代码格式化

在提交前，运行格式化脚本确保代码风格一致：

```bash
./scripts/format_code.sh
```

### 5. 运行消毒器（可选）

在开发过程中检测内存问题和数据竞争：

```bash
# ThreadSanitizer（检测数据竞争）
./scripts/run_sanitizers.sh thread

# AddressSanitizer（检测内存问题）
./scripts/run_sanitizers.sh address

# UBSanitizer（检测未定义行为）
./scripts/run_sanitizers.sh undefined
```

---

## 📚 核心文档

### 1. **RULES.md** — 编码规范与设计原则
- C++20 语言约束（禁用异常、RTTI）
- 命名约定（snake_case, 成员变量 `_` 后缀）
- 核心原则（Zero-copy, Lock-free, Cache-line Alignment）
- 内存序语义（acquire/release vs seq_cst）
- 错误处理政策（无异常，返回码）

### 2. **STATE.md** — 开发进度跟踪
- 12 个 Milestone 的详细规划
- 当前进度（初始化完成 ✅）
- 待决策的技术问题
- 关键指标与目标
- 已决策项目

### 3. **DIRECTORY_STRUCTURE.md** — 目录说明
- 完整的文件树结构
- 每个目录的用途说明
- 命名约定总结
- 扩展指南（添加 Appender、平台支持、语言绑定）

### 4. **CMakeLists.txt** — 构建配置
- C++20 标准
- 跨平台支持（Linux, macOS, Windows, ARM）
- 编译 flags `(-fno-exceptions -fno-rtti -Wall -Wextra -Werror)`
- 可选 sanitizer 支持 (ASan, TSan, UBSan)
- Link-Time Optimization (LTO)

---

## 🎯 下一步：开始 M0（无锁原语）

### 开始实现 M0 前

1. **读取规范**
   ```bash
   cat RULES.md              # 编码规范
   cat STATE.md | head -50   # 进度与 M0 详情
   ```

2. **创建实现文件**
   - `src/qlog/primitives/atomic.h` — std::atomic 包装
   - `src/qlog/primitives/spin_lock.h` — 自旋锁
   - `src/qlog/primitives/aligned_alloc.h` — 对齐分配器
   - ... (参考 DIRECTORY_STRUCTURE.md)

3. **编写测试**
   - `test/cpp/test_atomic.cpp`
   - `test/cpp/test_spin_lock.cpp`
   - 目标：TSan 通过，无数据竞争

4. **性能基准**
   - `benchmark/cpp/bench_spin_lock.cpp`
   - 目标：自旋锁 > 1M ops/s vs std::mutex

### 推荐的开发流程

```
M0 实现（1周）
  ├─ atomic.h + spin_lock.h
  ├─ 单元测试 + TSan 验证
  └─ 性能基准对标 std::mutex
    ↓
M1 SPSC Ring Buffer（1周）
  ├─ siso_ring_buffer.h + implement
  ├─ 单线程正确性测试
  ├─ 双线程压测（TSan）
  └─ 吞吐量目标 > 3x std::queue
    ↓
M2 MPSC Ring Buffer（1周）
  ...
```

---

## 📊 性能目标

| 指标 | 目标 | 验证方法 |
|------|------|----------|
| Hot Path 单次调用 | < 300ns | Release 模式 benchmark |
| 单线程异步吞吐 | > 5M entries/s | bench_entry_write |
| 10线程异步吞吐 | > 20M entries/s | bench_concurrent_writes |
| 内存占用 | < 2MB (10线程×200万条) | object dumper |
| 压缩率 | < 15% vs 文本 | file size compare |

---

## 🔧 编译命令参考

```bash
# Debug 构建 + TSan
cmake -B build_debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DQLOG_ENABLE_TSAN=ON

# Release 构建 + LTO
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DQLOG_ENABLE_LTO=ON

# 生成编译数据库（给 clangd 用）
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

---

## 📞 关键资料链接

- **参考实现**: `/home/qq344/BqLog/`
- **BqLog CLAUDE.md**: 架构分析
- **BqLog plan.md**: 复刻计划

---

## ✨ 项目特点

✅ **现代 C++20** — concepts, constexpr, ranges  
✅ **完全无锁** — Hot Path ≤ 300ns  
✅ **零拷贝** — 用户线程不分配堆内存  
✅ **多平台** — Linux, macOS, Windows, ARM  
✅ **消毒器友好** — TSan/ASan/UBSan 通过  
✅ **高性能** — 5M+ entries/s per thread  

---

**开始日期**: 2026-04-11  
**项目状态**: 🟢 初始化完成，ready for M0 implementation

