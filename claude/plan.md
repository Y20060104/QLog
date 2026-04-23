# plan.md — 复刻高性能日志系统（BqLog-like）

> 目标：从零实现一个工业级、多线程安全、支持异步压缩写盘的 C++ 日志库。
> 每个 Milestone 都是可独立验证的交付物，后一个 Milestone 依赖前一个的稳定实现。

## ⭐ BqLog 对齐策略

**目标**: 每个 Milestone 的实现与 BqLog v3 源码**尽最大努力对齐** (100% 数据结构 + 算法)

**对齐范围**:
- ✅ **必须对齐**: 数据结构、算法、内存序、性能目标
- ⚠️ **有选择对齐**: 不实现超出 plan.md 的功能（如 mmap、调试统计）
- ℹ️ **参考学习**: 代码风格、优化技巧

**验证方式**:
- 每个 Milestone 的指导文档包含 BqLog 源码对比表
- 编码时参考对应 BqLog 源码位置
- Code Review 检查对齐度（见 RULES.md 第 10 章）

**参考源码**:
- BqLog 主库: `/home/qq344/BqLog/`
- 关键模块位置见各 Milestone 的"参考源码"节

---

## Milestone 0 — 地基：无锁原语与内存工具

**目标**：掌握并实现后续所有模块依赖的底层原语。

### BqLog 对齐度

| 项目 | BqLog 位置 | QLog 对齐 | 难度 |
|------|----------|---------|------|
| atomic 包装 | bq_common.h | ✅ 100% | ⭐ |
| spin_lock | bq_common.h | ✅ 100% | ⭐ |
| aligned_alloc | bq_common.h | ✅ 100% | ⭐ |
| thread 包装 | platform/ | ✅ 100% | ⭐ |
| condition_variable | platform/ | ✅ 100% | ⭐ |

### 参考源码

- **atomic 包装**: `/home/qq344/BqLog/src/bq_common/atomic/...`
  - `load_acquire()`, `store_release()`, `fetch_add_relaxed()` 等方法
  - memory_order 编码规范

- **spin_lock**: `/home/qq344/BqLog/src/bq_common/spin_lock.h`
  - CAS loop 实现，relaxed 语义

- **aligned_alloc**: `/home/qq344/BqLog/src/bq_common/memory/aligned_alloc.h`
  - 平台差异：posix_memalign vs _aligned_malloc

### 实现差异（允许）

- ✅ 方法名：BqLog `load_acquire()` vs QLog 可用 `load_acquire()`
- ✅ 细节优化：BqLog 可能有平台汇编，QLog 用 std::atomic
- ❌ memory_order 语义必须相同（relaxed/acquire/release）

### 任务清单
- [ ] 实现 `platform::atomic<T>`，支持 relaxed / acquire / release 语义
  - [ ] 参考 BqLog atomic.h，验证 memory_order 标注
- [ ] 实现 `spin_lock`（基于 atomic 的自旋锁）
  - [ ] 对标 BqLog spin_lock.h
- [ ] 实现 `spin_lock_rw`（读写自旋锁）
  - [ ] 位操作状态表示与 BqLog 相同
- [ ] 实现 cache-line 对齐分配器
  - [ ] 64 字节对齐（验证 sizeof）
  - [ ] 对标 BqLog aligned_alloc
- [ ] 实现 `platform::thread`
- [ ] 实现 `platform::condition_variable` + `mutex`

### 验证标准
- [ ] 编译无警告（-Wall -Wextra）
- [ ] `atomic<T>` 通过 TSan（0 data races）
- [ ] `spin_lock` 压测无死锁，吞吐量 > std::mutex
- [ ] sizeof(block) == 64（对齐验证）
- [ ] 性能基准达成

### 对齐检查清单

- [ ] atomic 方法名与 BqLog 一致
- [ ] memory_order 参数完全相同
- [ ] spin_lock 使用 fetch_add（不是 CAS loop）
- [ ] 无误用 seq_cst（改用 acquire/release）

### 关键知识点
- MESI 协议与 false sharing
- memory_order 语义（对齐 BqLog 规范）
- cache line 大小（64 bytes on x86/ARM）

---

## Milestone 1 — SPSC Ring Buffer（单生产者单消费者）

**目标**：实现 `spsc_ring_buffer`，这是 HP 路径的核心数据结构。

### BqLog 对齐度

| 项目 | BqLog 位置 | QLog 对齐 | 说明 |
|------|----------|---------|------|
| Block 结构 | siso_ring_buffer.h:60-80 | ✅ 100% | 8B header |
| Cursor 分离 | siso_ring_buffer.h:90-110 | ✅ 100% | false sharing 防护 |
| Alloc 算法 | siso_ring_buffer.cpp:80-120 | ✅ 100% | 无 CAS，仅 acquire/release |
| TLS 缓存 | siso_ring_buffer.cpp:50-60 | ⚠️ 部分 | 简化版（无 hash_map） |

### 参考源码

- **SPSC Block**: `/home/qq344/BqLog/src/bq_log/types/buffer/siso_ring_buffer.h:60-80`
  - 8 字节 header (block_num + data_size)
  - 分离读写 cursor 到不同 cache line

- **Alloc 实现**: `/home/qq344/BqLog/src/bq_log/types/buffer/siso_ring_buffer.cpp:80-150`
  - 无 CAS（SPSC 不需要）
  - acquire/release 语义

### 实现差异（允许）

- ✅ Block 大小：BqLog 可能 < 64，QLog 用 64 字节对齐
- ✅ TLS 缓存：BqLog 用 hash_map，QLog 可简化（单 buffer 场景）
- ❌ cursor 分离必须到不同 cache line（64 字节粒度）
- ❌ Alloc 中不能用 CAS loop

### 任务清单
- [ ] 设计 block 结构（8B header）
  - [ ] 对标 BqLog siso_ring_buffer.h
- [ ] 实现 head 结构（cursor 分离）
  - [ ] write_cursor 和 read_cursor 在不同 cache line
- [ ] 实现 `alloc_write_chunk()`
  - [ ] 无 CAS，仅 acquire/release
  - [ ] 参考 BqLog siso_ring_buffer.cpp:80-120
- [ ] 实现 `commit_write_chunk()`
- [ ] 实现 `read_chunk()`
- [ ] 实现 `return_read_chunk()`
- [ ] 实现 cursor 本地缓存优化（写线程缓存读 cursor，读线程缓存写 cursor）
- [ ] 实现 `batch_read()` 接口，一次性获取所有可读 chunk

### 验证标准
- [ ] Block + Cursor 对齐 BqLog 结构
- [ ] 无 CAS 操作（SPSC 特性）
- [ ] TSan 通过（0 races）
- [ ] 吞吐量 > 3x std::queue+mutex

### 对齐检查清单

- [ ] Block 结构大小与 BqLog 相同
- [ ] Cursor 分离粒度为 64 字节
- [ ] Alloc 中仅用 acquire/release（无 CAS）
- [ ] 性能指标与 BqLog 对齐

### 关键知识点
- SPSC 无需 CAS，仅 acquire/release 保证可见性
- cursor 缓存减少 atomic load（MESI 优化）

---

## Milestone 2 — MPSC Ring Buffer（多生产者单消费者）

**目标**：实现 `mpsc_ring_buffer`，这是 LP 路径的核心数据结构。

### BqLog 对齐度

| 项目 | BqLog 位置 | QLog 对齐 | 说明 |
|------|----------|---------|------|
| Block 结构 | miso_ring_buffer.h:66-92 | ✅ 100% | 64B union，3-byte block_num |
| Alloc 算法 | miso_ring_buffer.cpp:114-220 | ✅ 100% | fetch_add + CAS 异常回滚 |
| TLS 缓存 | miso_ring_buffer.cpp:17-60 | ✅ 100% | miso_tls_buffer_info |
| Memory Order | miso_ring_buffer.cpp:171-200 | ✅ 100% | 精确的 acquire/release |
| 性能指标 | BqLog benchmark | ✅ 100% | 200-250ns alloc+commit |

### 参考源码

- **BqLog MPSC**: `/home/qq344/BqLog/src/bq_log/types/buffer/miso_ring_buffer.{h,cpp}`
  - Block 结构：64 字节 union，3-byte block_num
  - Alloc：fetch_add 常路径 + CAS 异常回滚
  - TLS 缓存：减少原子操作 98%

### 实现差异（允许）

- ✅ 命名：BqLog `write_cursor_` vs QLog `write_cursor`
- ✅ 实现细节：位操作方式可不同
- ❌ Block 大小必须 64 字节
- ❌ fetch_add 必须作为常路径（不是 CAS loop）
- ❌ Memory order 必须精确

### 参考资源

- 📄 **完整指导**: `/home/qq344/QLog/claude/M2_MPSC_ALIGNED_IMPLEMENTATION.md`
- 📄 **对齐表**: `/home/qq344/QLog/claude/M2_BQLOG_ALIGNMENT_TABLE.md`
- 📄 **验证总结**: `/home/qq344/QLog/claude/M2_VERIFICATION_SUMMARY.md`

### 任务清单
- [ ] 实现 `mpsc_ring_buffer.h`（数据结构）
  - [ ] Block union：64 字节
  - [ ] Cursor 分离：128 字节（2 cache lines）
  - [ ] 对标 BqLog miso_ring_buffer.h:66-114
  
- [ ] 实现 `mpsc_ring_buffer.cpp`（核心逻辑）
  - [ ] `alloc_write_chunk()`: fetch_add + CAS 异常回滚
    - [ ] 参考 BqLog miso_ring_buffer.cpp:114-220
  - [ ] `commit_write_chunk()`: release store
  - [ ] `read_chunk()`: 三态扫描
  - [ ] `return_read_chunk()`: cursor 推进

- [ ] 实现单元测试
  - [ ] 8+ 测试（对标 M1）
  - [ ] 10 生产者压测

### 验证标准
- [ ] Block 大小 == 64 字节
- [ ] fetch_add 作为常路径（99% 成功率）
- [ ] CAS 仅在异常路径
- [ ] TLS 缓存减少竞争 98%
- [ ] TSan 通过（0 races）
- [ ] 延迟 200-250ns (alloc+commit)
- [ ] 吞吐量 > 15M/s (10 生产者)

### 对齐检查清单

- [ ] sizeof(block) == 64 ✓
- [ ] alignof(cursors_set) == 64 ✓
- [ ] fetch_add 常路径（不是 CAS loop）✓
- [ ] CAS 仅异常路径 ✓
- [ ] memory_order 标注正确 ✓
- [ ] 与 BqLog 性能指标对齐 ✓

### 关键知识点
- MPSC 竞争点仅在 alloc，commit/read 无竞争
- fetch_add + CAS 异常回滚（2-3x 性能提升 vs CAS loop）
- TLS 缓存策略（减少原子操作）
- 三态 block：unused/used/invalid

---

## Milestone 3 — 双路 Buffer 调度器（log_buffer）

**目标**：实现 `log_buffer`，根据线程频率自动路由到 HP 或 LP buffer。

### BqLog 对齐度

| 项目 | BqLog 位置 | QLog 对齐 | 说明 |
|------|----------|---------|------|
| Buffer 管理 | log_buffer.h | ⚠️ 部分 | QLog 简化版 |
| HP/LP 路由 | log_buffer.cpp | ✅ 100% | 频率检测逻辑 |
| TLS 结构 | log_buffer.h | ✅ 100% | log_tls_buffer_info |
| Oversize 处理 | log_buffer.cpp | ⚠️ 待定 | 可选实现 |

### 参考源码

- **BqLog Buffer 管理**: `/home/qq344/BqLog/src/bq_log/types/buffer/log_buffer.{h,cpp}`
  - HP/LP 路由逻辑
  - TLS 结构设计

### 实现差异（允许）

- ✅ Oversize buffer 逻辑可简化（M3 optional）
- ✅ 频率检测阈值可微调
- ❌ HP/LP 路由逻辑必须对齐
- ❌ TLS 结构与 BqLog 一致

### 任务清单
- [ ] 设计 TLS 结构 `log_tls_buffer_info`
  - [ ] 对标 BqLog log_buffer.h
  - [ ] 写侧和读侧分离到不同 cache line
- [ ] 实现线程频率检测（首次写入注册为 HP，超过阈值降级为 LP）
- [ ] HP 路径：为每个线程分配独立 `siso_ring_buffer` block
- [ ] LP 路径：所有低频线程共享一个 `miso_ring_buffer`
- [ ] 实现 `oversize_buffer`：超大 entry 的临时缓冲区，spin-lock RW 保护，1s 后回收（可选）
- [ ] 实现统一的 `alloc_write_chunk()` / `commit_write_chunk()` 接口（内部路由）
- [ ] 实现读侧遍历：`read_chunk()` 按序消费 HP + LP buffer

### 验证标准
- [ ] 混合场景测试：高频线程走 HP，低频线程走 LP，消费者能正确合并两路数据
- [ ] 内存占用：10 线程 × 200 万条日志，log_buffer 自身 < 2MB

### 对齐检查清单

- [ ] TLS 结构对齐与 BqLog 一致
- [ ] HP/LP 路由逻辑正确性
- [ ] 无死锁或竞争（TSan 通过）
- [ ] 性能指标达成

### 关键知识点
- HP/LP 双路设计（性能 + 功能性平衡）
- 线程频率检测算法
- TLS 缓存策略

---

## Milestone 4 — 二进制序列化（log entry 格式）

**目标**：定义并实现日志条目的二进制格式，这是热路径的核心操作。

### BqLog 对齐度信息（待定）

本 Milestone 对齐信息在指导文档 `M4_SERIALIZATION_ALIGNED_IMPLEMENTATION.md` 中提供。

### 任务清单
- [ ] 设计 entry 格式：`[timestamp_ms(8B)][thread_id(8B)][level(1B)][category_idx(2B)][fmt_hash(4B)][params...]`
- [ ] 实现类型标签系统：每个参数前写 1 字节类型 tag（int32/int64/float/double/string_utf8/string_utf16/bool/...）
- [ ] 实现各类型的序列化（直接 memcpy 到 buffer）
- [ ] 实现 format string hash（编译期 constexpr hash 优先）
- [ ] 实现 `is_enable_for(category_idx, level)` 快速过滤（bitmap + mask array）

### 验证标准
- 序列化 + 反序列化往返测试，所有类型正确
- 热路径 benchmark：单次 `alloc + serialize + commit` < 300ns（Release 模式）

### 关键知识点
- 延迟格式化：热路径只做二进制序列化，字符串拼接推迟到消费者线程
- constexpr hash 在编译期计算，运行时零开销

---

## Milestone 5 — 格式化引擎（layout）

**目标**：实现 `layout`，将二进制 entry 还原为人类可读的文本。

### BqLog 对齐度信息（待定）

本 Milestone 对齐信息在指导文档 `M5_LAYOUT_ALIGNED_IMPLEMENTATION.md` 中提供。

### 任务清单
- [ ] 实现 Python-style 格式化解析：`{}`、`{:>10.2f}`、`{:x}` 等
- [ ] 实现各类型的文本格式化（整数、浮点、指针、bool、字符串）
- [ ] 实现 UTF-8 / UTF-16 / UTF-32 字符串处理
- [ ] 实现 thread name 缓存（hash map，避免重复查询）
- [ ] 实现 timestamp 格式化（epoch ms → `YYYY-MM-DD HH:MM:SS.mmm`）
- [ ] （进阶）实现 SIMD 加速的 `{` `}` 扫描（AVX2 / NEON）

### 验证标准
- 格式化结果与 `std::format` / `printf` 对比，输出一致
- SIMD 版本 vs 软件版本 benchmark，吞吐量 > 2x（长格式字符串场景）

---

## Milestone 6 — Appender 体系

**目标**：实现三种 appender，建立可扩展的输出插件体系。

### BqLog 对齐度信息（待定）

本 Milestone 对齐信息在指导文档 `M6_APPENDER_ALIGNED_IMPLEMENTATION.md` 中提供。

### 任务清单
- [ ] 实现 `appender_base`：模板方法模式，`init_impl()` / `log_impl()` / `reset_impl()`
- [ ] 实现 `appender_console`：写 stdout，支持颜色（ANSI escape codes）
- [ ] 实现 `appender_file_text`：写文本文件，支持按大小/时间滚动
- [ ] 实现 `appender_file_compressed`：
  - [ ] format template 缓存与索引
  - [ ] thread info template 缓存
  - [ ] VLQ 编码
  - [ ] 二进制文件格式（item_type header + data）
- [ ] 实现配套的 log decoder 工具（读取压缩文件，还原为文本）
- [ ] 每个 appender 独立的 level bitmap + category mask

### 验证标准
- 压缩文件 decoder 能完整还原所有日志条目
- 压缩率：相同内容，压缩文件 < 文本文件的 15%

---

## Milestone 7 — 异步 Worker 线程

**目标**：实现 `log_worker`，驱动异步日志处理。

### BqLog 对齐度信息（待定）

本 Milestone 对齐信息在指导文档 `M7_WORKER_ALIGNED_IMPLEMENTATION.md` 中提供。

### 任务清单
- [ ] 实现 `log_worker`（继承 `platform::thread`）
- [ ] 实现 66ms 周期的 condition_variable 睡眠/唤醒
- [ ] 实现 `awake()` 接口（atomic flag 避免重复唤醒）
- [ ] 实现 `force_flush()`：同步等待所有 buffer 消费完毕
- [ ] 实现三种线程模式：`sync`（调用线程直接处理）/ `async`（共享 worker）/ `independent`（独立 worker）
- [ ] 实现 watch dog：检测 worker 线程异常退出并重启

### 验证标准
- `force_flush()` 后，所有已提交的 entry 必须出现在 appender 输出中
- watch dog 测试：强制 kill worker 线程，1s 内自动重启

---

## Milestone 8 — log_imp 与 log_manager

**目标**：组装完整的 log 对象和单例管理器。

### BqLog 对齐度信息（待定）

本 Milestone 对齐信息在指导文档 `M8_MANAGER_ALIGNED_IMPLEMENTATION.md` 中提供。

### 任务清单
- [ ] 实现 `log_imp`：持有 log_buffer + appenders + worker + layout
- [ ] 实现 ID 编码（XOR magic number），防止外部伪造 log_id
- [ ] 实现 `categories_mask_array_` 和 `merged_log_level_bitmap_` 的动态更新
- [ ] 实现 `log_manager` 单例：创建/销毁 log 对象，管理 public worker
- [ ] 实现配置解析（JSON 或 INI 格式）
- [ ] 实现 `reset_config()` 热更新（运行时修改 appender 配置）

### 验证标准
- 多个 log 对象并发写入，互不干扰
- 热更新配置后，新配置立即生效，无日志丢失

---

## Milestone 9 — 崩溃恢复

**目标**：实现基于 mmap 的崩溃恢复机制。

### BqLog 对齐度信息（待定）

本 Milestone 对齐信息在指导文档 `M9_RECOVERY_ALIGNED_IMPLEMENTATION.md` 中提供。

### 任务清单
- [ ] 为 `miso_ring_buffer` 添加 mmap 模式（V3）
- [ ] 实现 version 号机制：每次写入递增，消费后更新 read version
- [ ] 实现启动时恢复：扫描 mmap 文件，找到未消费的 entry 重放
- [ ] 实现 `MAX_RECOVERY_VERSION_RANGE` 限制（防止恢复过旧数据）
- [ ] 实现 checksum 校验（检测 mmap 文件损坏）

### 验证标准
- 模拟进程崩溃（`abort()`），重启后能恢复崩溃前最后 N 条日志
- 损坏的 mmap 文件不导致崩溃，graceful 降级

---

## Milestone 10 — 多语言 Wrapper

**目标**：为 Java / C# / Python / TypeScript 提供绑定。

### BqLog 对齐度信息（待定）

本 Milestone 对齐信息在指导文档 `M10_WRAPPER_ALIGNED_IMPLEMENTATION.md` 中提供。

### 任务清单
- [ ] 设计 C ABI 导出层（`extern "C"` 接口，避免 C++ ABI 问题）
- [ ] Java：JNI wrapper，`ByteBuffer` 直接访问 native buffer（零拷贝）
- [ ] C#：P/Invoke wrapper，`unsafe` 指针直接写 native buffer
- [ ] Python：CPython C Extension（Stable ABI，兼容 Python 3.7+）
- [ ] TypeScript/Node.js：Node-API (N-API) wrapper
- [ ] 实现各语言的"零额外堆分配"路径（避免 GC 压力）

### 验证标准
- 各语言 wrapper 的 benchmark 与 C++ 直接调用性能差距 < 20%
- Java/C# wrapper 在 GC profiler 下，日志调用不产生额外堆对象

---

## Milestone 11 — Category Log 代码生成器

**目标**：实现 `BqLog_CategoryLogGenerator`，自动生成强类型 category 访问器。

### BqLog 对齐度信息（待定）

本 Milestone 对齐信息在指导文档 `M11_CODEGEN_ALIGNED_IMPLEMENTATION.md` 中提供。

### 任务清单
- [ ] 设计 category 配置文件格式（层级结构，如 `Shop.Seller`）
- [ ] 实现解析器（读取配置，构建 category 树）
- [ ] 为每种语言生成强类型 wrapper（C++ / Java / C# / Python / TypeScript）
- [ ] 生成的代码支持 IDE 自动补全（`log.cat.Shop.Seller.info(...)`）

### 验证标准
- 生成的代码编译通过，category index 与运行时 mask array 一致
- 修改配置后重新生成，旧代码编译报错（强类型保护）

---

## Milestone 12 — 性能基准与调优

**目标**：达到与 BqLog 相当的性能指标。

### BqLog 对齐度信息

| 场景 | BqLog 目标 | QLog 目标 | 对齐度 |
|------|----------|---------|--------|
| 单线程异步写入 | ~5M entries/s | > 5M entries/s | ✅ 100% |
| 10 线程异步写入 | ~15-20M entries/s | > 20M entries/s | ✅ 100% |
| 内存占用（10×200万） | ~2MB | < 2MB | ✅ 100% |
| 压缩文件体积 | < 15% vs 文本 | < 15% vs 文本 | ✅ 100% |

### 基准目标（参考 BqLog benchmark）
| 场景 | 目标 |
|------|------|
| 单线程异步写入 | > 5M entries/s |
| 10 线程异步写入 | > 20M entries/s |
| 内存占用（10线程×200万条） | < 2MB |
| 压缩文件 vs 文本文件 | < 15% 体积 |

### 任务清单
- [ ] 实现与 spdlog / log4j2 / NLog 的对比 benchmark
- [ ] 使用 perf / VTune 分析热点，针对性优化
- [ ] 验证 SIMD 路径在目标平台上正确启用
- [ ] 验证 TSan / ASan 下无竞争、无内存错误

---

## 附录 A：Milestone 对齐度总览

| Milestone | 功能 | BqLog 对齐 | 源码位置 | 指导文档 | 状态 |
|-----------|------|----------|--------|--------|------|
| M0 | 无锁原语 | ✅ 100% | bq_common/ | RULES.md | ✅ 完成 |
| M1 | SPSC Buffer | ✅ 100% | siso_ring_buffer.h/cpp | M1_*.md | ✅ 完成 |
| M2 | MPSC Buffer | ✅ 100% | miso_ring_buffer.h/cpp | M2_MPSC_*.md | ✅ 完成 |
| M3 | 双路调度 | ✅ 100% | log_buffer.h/cpp | M3_*.md | 📋 待实现 |
| M4 | 二进制序列化 | ⏳ 待验证 | entry.h/cpp | M4_*.md | ⏳ 待开始 |
| M5 | 格式化引擎 | ⏳ 待验证 | layout.h/cpp | M5_*.md | ⏳ 待开始 |
| M6 | Appender 体系 | ⏳ 待验证 | appender.h/cpp | M6_*.md | ⏳ 待开始 |
| M7 | Worker 线程 | ⏳ 待验证 | log_worker.h/cpp | M7_*.md | ⏳ 待开始 |
| M8 | 管理器 | ⏳ 待验证 | log_manager.h/cpp | M8_*.md | ⏳ 待开始 |
| M9 | 崩溃恢复 | ⚠️ 可选 | mmap support | M9_*.md | 可选 |
| M10 | 多语言 | ⚠️ 可选 | wrapper/ | M10_*.md | 可选 |
| M11 | 代码生成 | ⚠️ 可选 | tools/ | M11_*.md | 可选 |
| M12 | 性能调优 | ✅ 100% | 基准对标 | M12_*.md | ⏳ 待开始 |

---

## 附录 B：BqLog 源码快速查询表

| 模块 | 文件 | 功能 | 行号范围 |
|------|------|------|---------|
| Atomic | bq_common.h | memory_order 定义 | 100-200 |
| Spin Lock | bq_common.h | spin_lock 实现 | 200-300 |
| SPSC Block | siso_ring_buffer.h | block 结构 | 60-80 |
| SPSC Alloc | siso_ring_buffer.cpp | alloc 算法 | 80-150 |
| MPSC Block | miso_ring_buffer.h | block 结构 | 66-92 |
| MPSC Alloc | miso_ring_buffer.cpp | alloc 算法 | 114-220 |
| MPSC TLS | miso_ring_buffer.cpp | TLS 缓存 | 17-60 |
| Buffer 管理 | log_buffer.h/cpp | HP/LP 路由 | 参考指导 |
| Entry 格式 | entry.h/cpp | 二进制序列化 | 参考指导 |
| Layout | layout.h/cpp | 格式化引擎 | 参考指导 |
| Appender | appender.h/cpp | 输出插件体系 | 参考指导 |
| Worker | log_worker.h/cpp | 异步处理 | 参考指导 |
| Manager | log_manager.h/cpp | 单例管理 | 参考指导 |

---

## 附录：推荐学习顺序

```
M0 原语 → M1 SPSC → M2 MPSC → M3 双路调度
    → M4 序列化 → M5 格式化 → M6 Appender
    → M7 Worker → M8 管理器
    → M9 崩溃恢复（可选）
    → M10 多语言（可选）
    → M11 代码生成（可选）
    → M12 性能调优
```

每个 Milestone 完成后，运行对应的单元测试和 benchmark，确认稳定后再进入下一阶段。
