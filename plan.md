# plan.md — 复刻高性能日志系统（BqLog-like）

> 目标：从零实现一个工业级、多线程安全、支持异步压缩写盘的 C++ 日志库。
> 每个 Milestone 都是可独立验证的交付物，后一个 Milestone 依赖前一个的稳定实现。

---

## Milestone 0 — 地基：无锁原语与内存工具

**目标**：掌握并实现后续所有模块依赖的底层原语。

### 任务清单
- [ ] 实现 `platform::atomic<T>`，封装 `std::atomic` 或平台内联汇编，支持 relaxed / acquire / release 语义
- [ ] 实现 `spin_lock`（基于 `atomic_flag` 的自旋锁）
- [ ] 实现 `spin_lock_rw`（读写自旋锁，读多写少场景）
- [ ] 实现 cache-line 对齐分配器 `aligned_alloc<64>`
- [ ] 实现 `platform::thread`（封装 pthread / Win32 线程）
- [ ] 实现 `platform::condition_variable` + `mutex`

### 验证标准
- 用 `std::thread` 压测 `spin_lock`，无死锁，吞吐量优于 `std::mutex`
- `atomic<T>` 通过 TSan（ThreadSanitizer）检测

### 关键知识点
- MESI 协议与 false sharing
- memory_order 语义（relaxed 不保序，acquire/release 建立 happens-before）
- cache line 大小（x86/ARM 均为 64 bytes）

---

## Milestone 1 — SPSC Ring Buffer（单生产者单消费者）

**目标**：实现 `siso_ring_buffer`，这是 HP 路径的核心数据结构。

### 任务清单
- [ ] 设计 block 结构：`chunk_head_def { uint32_t block_num; uint32_t data_size; }`
- [ ] 实现 head 结构：写 cursor 和读 cursor 分离到不同 cache line（防 false sharing）
- [ ] 实现 `alloc_write_chunk(size)` → 返回写指针，不足时返回 null
- [ ] 实现 `commit_write_chunk(handle)` → 标记 chunk 可读
- [ ] 实现 `read_chunk()` → 返回下一个可读 chunk
- [ ] 实现 `return_read_chunk(handle)` → 标记 chunk 已消费
- [ ] 实现 cursor 本地缓存优化（写线程缓存读 cursor，读线程缓存写 cursor）
- [ ] 实现 `batch_read()` 接口，一次性获取所有可读 chunk

### 验证标准
- 单线程读写正确性测试（写 N 条，读出 N 条，内容一致）
- 双线程压测（1 producer + 1 consumer），无数据竞争（TSan 通过）
- 与 `std::queue<std::string>` + mutex 对比，吞吐量 > 3x

### 关键知识点
- SPSC 无需 CAS，只需 acquire/release 保证可见性
- cursor 缓存减少 atomic load 次数（MESI 优化）

---

## Milestone 2 — MPSC Ring Buffer（多生产者单消费者）

**目标**：实现 `miso_ring_buffer`，这是 LP 路径的核心数据结构。

### 任务清单
- [ ] 设计 block 结构：64 bytes（一个 cache line），含 `status` 字段（unused/used/invalid）
- [ ] 实现 `alloc_write_chunk(size)`：多线程 CAS 竞争 `unused → used`
- [ ] 实现 `commit_write_chunk(handle)`：写完数据后 release store 标记 used
- [ ] 实现 `read_chunk()`：消费者顺序扫描 used 状态的 block
- [ ] 实现 `return_read_chunk(handle)`：标记 block 为 unused（循环复用）
- [ ] 处理 wrap-around（cursor 回绕）和 ABA 问题
- [ ] 实现 `invalid` 状态（跳过损坏/超大 block）

### 验证标准
- N 个生产者线程 + 1 个消费者线程，无数据丢失，无竞争（TSan 通过）
- 与 `std::queue` + mutex 对比，高并发下吞吐量 > 2x

### 关键知识点
- MPSC 的 CAS 竞争点只在 alloc，commit 无竞争
- block 粒度 = cache line，避免多生产者写同一 cache line

---

## Milestone 3 — 双路 Buffer 调度器（log_buffer）

**目标**：实现 `log_buffer`，根据线程频率自动路由到 HP 或 LP buffer。

### 任务清单
- [ ] 设计 TLS 结构 `log_tls_buffer_info`：写侧和读侧分离到不同 cache line
- [ ] 实现线程频率检测（首次写入注册为 HP，超过阈值降级为 LP）
- [ ] HP 路径：为每个线程分配独立 `siso_ring_buffer` block
- [ ] LP 路径：所有低频线程共享一个 `miso_ring_buffer`
- [ ] 实现 `oversize_buffer`：超大 entry 的临时缓冲区，spin-lock RW 保护，1s 后回收
- [ ] 实现统一的 `alloc_write_chunk()` / `commit_write_chunk()` 接口（内部路由）
- [ ] 实现读侧遍历：`read_chunk()` 按序消费 HP + LP buffer

### 验证标准
- 混合场景测试：高频线程走 HP，低频线程走 LP，消费者能正确合并两路数据
- 内存占用：10 线程 × 200 万条日志，log_buffer 自身 < 2MB

---

## Milestone 4 — 二进制序列化（log entry 格式）

**目标**：定义并实现日志条目的二进制格式，这是热路径的核心操作。

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
