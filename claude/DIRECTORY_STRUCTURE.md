# QLog 项目目录结构

```
QLog/
├── CMakeLists.txt              # 根级 CMake 配置
├── RULES.md                    # C++20 编码规范与设计原则
├── STATE.md                    # 开发进度与状态跟踪
├── DIRECTORY_STRUCTURE.md      # 本文件
├── .clang-format               # clang-format 配置
├── .gitignore                  # Git 忽略规则
│
├── include/qlog/               # 公有 API 头文件
│   ├── qlog.h                  # 主入口头文件
│   ├── version.h               # 版本信息
│   ├── c_api.h                 # C 语言绑定（extern "C"）
│   ├── types.h                 # 公有类型定义
│   ├── log_level.h             # 日志级别枚举
│   └── categories.h            # 分类定义（生成）
│
├── src/qlog/                   # 核心实现
│   ├── CMakeLists.txt          # 库编译配置
│   │
│   ├── primitives/             # M0: 无锁原语
│   │   ├── atomic.h            # 原子操作包装
│   │   ├── atomic_linux.inl    # Linux 平台实现
│   │   ├── atomic_windows.inl  # Windows 平台实现
│   │   ├── spin_lock.h         # 自旋锁
│   │   ├── spin_lock_rw.h      # 读写自旋锁
│   │   ├── aligned_alloc.h     # Cache-line 对齐分配器
│   │   ├── platform_thread.h   # 平台 thread 包装
│   │   ├── platform_thread_linux.cpp
│   │   ├── platform_thread_windows.cpp
│   │   └── condition_variable.h
│   │
│   ├── buffer/                 # M1-M3: 环形缓冲区 + 调度
│   │   ├── siso_ring_buffer.h  # SPSC 无锁环形缓冲区 (M1)
│   │   ├── siso_ring_buffer.cpp
│   │   ├── miso_ring_buffer.h  # MPSC 无锁环形缓冲区 (M2)
│   │   ├── miso_ring_buffer.cpp
│   │   ├── log_buffer.h        # 双路调度器 (M3)
│   │   ├── log_buffer.cpp
│   │   └── oversize_buffer.h   # 超大条目临时缓冲区
│   │
│   ├── entry/                  # M4: 二进制序列化
│   │   ├── log_entry.h         # Entry 二进制格式定义
│   │   ├── entry_serializer.h  # 序列化器
│   │   ├── entry_serializer.cpp
│   │   ├── type_tag.h          # 类型标签枚举
│   │   └── format_hash.h       # Format string hash (constexpr)
│   │
│   ├── layout/                 # M5: 格式化引擎
│   │   ├── layout.h            # 格式化引擎
│   │   ├── layout.cpp
│   │   ├── format_parser.h     # Python-style 格式解析
│   │   ├── format_parser.cpp
│   │   ├── timestamp_format.h  # 时间戳格式化
│   │   └── utf_utils.h         # UTF-8/16/32 工具
│   │
│   ├── appender/               # M6: Appender 体系
│   │   ├── appender_base.h     # 抽象基类（模板方法）
│   │   ├── appender_console.h  # 控制台输出 + ANSI 颜色
│   │   ├── appender_console.cpp
│   │   ├── appender_file_text.h # 文本文件输出 + 日志滚动
│   │   ├── appender_file_text.cpp
│   │   ├── appender_file_compressed.h # 压缩文件输出 + VLQ
│   │   ├── appender_file_compressed.cpp
│   │   ├── compression_codec.h  # VLQ/zstd 编解码
│   │   └── file_roller.h        # 日志文件滚动策略
│   │
│   ├── worker/                 # M7: 异步 Worker 线程
│   │   ├── log_worker.h        # Worker 线程
│   │   ├── log_worker.cpp
│   │   └── watch_dog.h         # 看门狗（故障检测与恢复）
│   │
│   ├── log/                    # M8: 核心日志对象管理
│   │   ├── log_imp.h           # 日志对象实现
│   │   ├── log_imp.cpp
│   │   ├── log_manager.h       # 单例管理器
│   │   ├── log_manager.cpp
│   │   ├── log_config.h        # 配置结构 (JSON/INI 解析)
│   │   ├── config_parser.h
│   │   └── config_parser.cpp
│   │
│   ├── recovery/               # M9: 崩溃恢复（可选）
│   │   ├── mmap_buffer.h       # mmap 模式 ring buffer
│   │   ├── version_recovery.h  # Version 号恢复机制
│   │   └── checksum.h          # 校验和验证
│   │
│   ├── internal/               # 内部工具
│   │   ├── config.h            # 编译时配置常量
│   │   ├── constants.h         # 全局常量
│   │   ├── macros.h            # 宏定义（LIKELY/UNLIKELY 等）
│   │   ├── debug_utils.h       # 调试工具
│   │   ├── memory_utils.h      # 内存工具
│   │   └── time_utils.h        # 时间工具
│   │
│   └── CMakeLists.txt          # 子目录编译配置（分模块构建）
│
├── test/                       # 单元测试
│   ├── CMakeLists.txt
│   ├── cpp/
│   │   ├── test_atomic.cpp            # M0 原语测试
│   │   ├── test_spin_lock.cpp
│   │   ├── test_siso_ring_buffer.cpp  # M1 SPSC 测试
│   │   ├── test_miso_ring_buffer.cpp  # M2 MPSC 测试
│   │   ├── test_log_buffer.cpp        # M3 调度器测试
│   │   ├── test_entry_serializer.cpp  # M4 序列化测试
│   │   ├── test_layout.cpp            # M5 格式化测试
│   │   ├── test_appender.cpp          # M6 Appender 测试
│   │   ├── test_log_worker.cpp        # M7 Worker 测试
│   │   ├── test_log_manager.cpp       # M8 管理器测试
│   │   ├── test_concurrent.cpp        # 并发场景测试
│   │   └── CMakeLists.txt
│   │
│   └── fixtures/                # 测试数据与工具
│       ├── log_samples.cpp      # 示例日志数据
│       └── mock_appender.h      # Mock appender
│
├── benchmark/                  # 性能基准测试
│   ├── CMakeLists.txt
│   ├── cpp/
│   │   ├── bench_siso_ring_buffer.cpp
│   │   ├── bench_miso_ring_buffer.cpp
│   │   ├── bench_serialization.cpp
│   │   ├── bench_layout.cpp
│   │   ├── bench_entry_write.cpp      # 完整热路径基准
│   │   ├── bench_concurrent_writes.cpp # 多线程基准
│   │   └── CMakeLists.txt
│   │
│   └── runners/                # 基准运行脚本
│       ├── run_all_bench.sh
│       ├── compare_bench.py    # 对标 BqLog / spdlog / log4j2
│       └── report_bench.py     # 生成基准报告
│
├── demo/                       # 示例程序
│   ├── CMakeLists.txt
│   ├── basic_example.cpp       # 基本使用
│   ├── async_example.cpp       # 异步日志
│   ├── category_example.cpp    # 分类日志
│   ├── high_freq_example.cpp   # 高频线程测试
│   └── low_freq_example.cpp    # 低频线程测试
│
├── tools/                      # 工具与代码生成器
│   ├── CMakeLists.txt
│   ├── category_gen/           # 分类代码生成器 (M11)
│   │   ├── category_gen.h
│   │   ├── category_gen.cpp
│   │   └── config_parser.cpp   # 解析分类配置
│   │
│   ├── log_decoder/            # 压缩文件解码器
│   │   ├── decoder.cpp
│   │   └── decoder.h            # 二进制日志还原为文本
│   │
│   └── perf_analyzer/          # 性能分析工具
│       ├── analyzer.cpp
│       └── profile_util.h
│
├── wrapper/                    # 多语言绑定 (M10)
│   ├── c/                      # C 语言绑定
│   │   └── qlog_c_api.c
│   │
│   ├── cpp/                    # C++ 现代包装
│   │   └── qlog_cpp.h
│   │
│   ├── java/                   # Java JNI 绑定
│   │   ├── QLog.java
│   │   └── qlog_jni.cpp
│   │
│   ├── csharp/                 # C# P/Invoke 绑定
│   │   └── QLog.cs
│   │
│   ├── python/                 # CPython 扩展
│   │   └── qlog_module.c
│   │
│   └── typescript/             # Node-API 绑定
│       └── qlog_napi.cpp
│
├── docs/                       # 文档
│   ├── API_REFERENCE.md        # 公有 API 文档
│   ├── INTEGRATION_GUIDE.md    # 集成指南
│   ├── CONFIGURATION.md        # 配置说明
│   ├── ADVANCED_USAGE.md       # 高级用法
│   ├── ARCHITECTURE.md         # 架构设计文档
│   ├── PERFORMANCE_GUIDE.md    # 性能优化指南
│   ├── THREAD_SAFETY.md        # 线程安全说明
│   ├── CRASH_RECOVERY.md       # 崩溃恢复机制
│   └── MIGRATION_FROM_BQLOG.md # 从 BqLog 迁移指南
│
├── scripts/                    # 辅助脚本
│   ├── build.sh                # 构建脚本
│   ├── test.sh                 # 测试脚本
│   ├── format_code.sh          # 代码格式化
│   ├── run_sanitizers.sh       # 运行消毒器 (ASAN/TSAN/UBSAN)
│   └── setup_ci.sh             # CI/CD 设置
│
├── .clang-format               # clang-format 配置
├── .github/                    # GitHub Actions CI
│   └── workflows/
│       ├── build.yml           # 构建工作流
│       ├── test.yml            # 测试工作流
│       └── sanitizers.yml      # 消毒器工作流
│
├── .gitignore                  # Git 忽略规则
└── LICENSE                     # Apache 2.0 许可证

```

---

## 目录说明

| 目录 | 用途 | Milestone |
|------|------|-----------|
| `include/qlog/` | 公有 API | - |
| `src/qlog/primitives/` | 无锁原语 | M0 |
| `src/qlog/buffer/` | SPSC/MPSC 缓冲区 + 调度 | M1-M3 |
| `src/qlog/entry/` | 二进制序列化 | M4 |
| `src/qlog/layout/` | 格式化引擎 | M5 |
| `src/qlog/appender/` | Appender 体系 | M6 |
| `src/qlog/worker/` | 异步 Worker | M7 |
| `src/qlog/log/` | 核心对象管理 | M8 |
| `src/qlog/recovery/` | 崩溃恢复 | M9 |
| `test/` | 单元 + 集成测试 | - |
| `benchmark/` | 性能基准 | M12 |
| `demo/` | 示例程序 | - |
| `tools/` | 代码生成 + 工具 | M11 |
| `wrapper/` | 多语言绑定 | M10 |
| `docs/` | 设计文档 + 用户指南 | - |
| `scripts/` | 构建 + 测试脚本 | - |

---

## 关键命名约定

### 文件命名
- **头文件**: `.h` (例：`ring_buffer.h`)
- **实现**: `.cpp` (例：`ring_buffer.cpp`)
- **内联实现**: `.inl` (例：`ring_buffer.inl`)
- **平台特定**: `._platform.h` (例：`atomic._linux.h`)

### 类型命名
- **类/结构体**: `snake_case` (例：`siso_ring_buffer`)
- **类型别名**: `_t` 后缀 (例：`handle_t`)
- **常量**: `kCamelCase` 或 `ALL_CAPS` (例：`kDefaultBufferSize`)

### 命名空间
```cpp
namespace qlog                    // 公有 API
namespace qlog::internal          // 内部实现细节
namespace qlog::platform          // 平台特定代码
```

---

## 编译与构建

```bash
# 构建主库（Release）
mkdir build && cd build
cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release
cmake --build .

# 运行测试
cmake --build . --target test

# 运行基准测试
./bin/qlog_benchmark

# 启用消毒器（开发/调试）
cmake -S .. -B build_debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DQLOG_ENABLE_TSAN=ON
cmake --build build_debug

# 代码格式化
./scripts/format_code.sh
```

---

## 扩展指南

### 添加新 Appender
1. 在 `src/qlog/appender/` 创建 `appender_xxx.h` / `.cpp`
2. 继承 `appender_base`，实现虚函数
3. 在 `src/qlog/CMakeLists.txt` 添加编译目标
4. 在 `test/` 添加单元测试

### 添加新平台支持
1. 在 `src/qlog/primitives/` 创建 `atomic._newplatform.h`
2. 实现平台特定的原子操作
3. 在 `CMakeLists.txt` 添加平台检测逻辑

### 添加语言绑定
1. 在 `wrapper/lang/` 创建相应语言目录
2. 实现公有 C API 的包装
3. 编译并在 `CMakeLists.txt` 注册

