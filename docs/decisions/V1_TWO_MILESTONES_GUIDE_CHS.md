# QLog V1 后续两里程碑实现指南

- 状态：唯一生效的后续执行计划
- 日期：2026-08-28
- 最后修订：2026-08-29
- 适用范围：从当前 SPSC RingBuffer 骨架到可演示、可公平基准的异步日志 V1
- 分工：你实现生产代码；Codex 负责测试、构建门禁、评审与结果记录

旧文档中的阶段 A～D、里程碑 0～7 和 M3～M7 仅作为历史设计记录，
不再表示后续任务数量。后续只执行下面两个里程碑，里程碑内的编号只是实现顺序。

## 共同原则

- 目标平台为 Linux x86-64，语言标准为纯 C++20。
- 每个稳定生产线程独占一个 SPSC Channel，一个后台线程消费多个 Channel。
- Producer 稳态热路径不格式化、不分配、不加锁、不阻塞，不执行 CAS 或
  `fetch_add` 等共享原子 RMW。
- 固定容量，满时 `drop_new`；失败不得推进任何游标。
- fmt 只在后台线程执行。
- 先证明正确，再检查 Release 汇编和 benchmark；一次只优化一个变量。
- BQLog、spdlog、Quill 和 NanoLog 只用于学习与对照，不逐行移植实现。

## 里程碑一：完成可用且高性能的 SPSC RingBuffer

### 完成结果

在现有 Geometry、固定 Storage、私有读写状态和 `CursorSet` 基础上，打通：

```text
try_reserve -> 写 payload -> commit/abort
try_peek    -> 读 payload -> consume/abandon
```

完成后，Ring 可以由一个 Producer 和一个 Consumer 真正并发使用，不依赖 Logger、
fmt、文件 Sink 或后台调度器。

### 最小接口

短期 `WriteHandle` 提供：

```text
operator bool / status / data / size / commit / abort
```

短期 `ReadHandle` 提供：

```text
operator bool / status / data / size / consume / abandon
```

查询接口按以下类型实现为类内内联函数：

```cpp
// WriteHandle
[[nodiscard]] explicit operator bool() const noexcept;
[[nodiscard]] ReserveStatus status() const noexcept;
[[nodiscard]] std::byte* data() noexcept;
[[nodiscard]] std::uint32_t size() const noexcept;

// ReadHandle
[[nodiscard]] explicit operator bool() const noexcept;
[[nodiscard]] ReadStatus status() const noexcept;
[[nodiscard]] const std::byte* data() const noexcept;
[[nodiscard]] std::uint32_t size() const noexcept;
```

`operator bool()` 以 `ring_ != nullptr` 判断 Handle 是否 active；`status()` 返回本次结果；
`data()` / `size()` 只读取 private 的 `payload_` / `payload_bytes_`。Ring 基础接口不构造
`std::span`，也不公开可修改的状态字段。`deactivate()` 必须放在 private，调用者不能绕过
`commit/abort` 或 `consume/abandon` 清除 Handle。

Handle 失效状态统一为 `operator bool() == false`、`data() == nullptr`、`size() == 0`；
`status()` 仍保留本次尝试的原始结果。该规则同时适用于失败、moved-from、commit、abort、
consume 和 abandon。成功的零长度记录仍为 active，因此是 `bool == true`、`size() == 0`。
`try_reserve()` 的失败优先级固定为先检查本侧 `reservation_pending_`，再检查 payload 上限和
可用空间，确保已有 active Handle 时始终返回 `reservation_pending`。

`SpscRingBuffer` 直接提供 `try_reserve()`、`try_peek()` 和
`publish_reclaimed()`。单次 Handle 只可移动、hot API 全部 `noexcept`，目标大小不超过 32B，
但大小不是持久 ABI，最终由 Release 汇编和 benchmark 决定。

payload 写入协议固定为：

```text
try_reserve(exact_payload_bytes)
    -> Ring 写入尚未发布的 FrameHeader 并返回 data()/size()
    -> 调用者直接编码，或把现有连续数据一次 memcpy 到 data()
    -> commit() 只推进游标并 release-store 发布，不复制 payload
```

Ring 私有 `FrameHeader` 不属于上层日志格式。未来日志层的 `RecordHeader` 和编码参数都位于
payload 中，由编码层在 commit 前写入。WriteHandle 的地址只在 `commit/abort` 前有效；
ReadHandle 的地址只在 `consume/abandon` 前有效。

### 实现顺序

1. 完成当前骨架清理，并为 WriteHandle/ReadHandle 增加最小字段、查询接口和移动后
   inactive 语义。owner 指针为空表示 inactive，不再增加独立 active 标志。
2. 先实现 `abort()` / `abandon()` 与析构语义：WriteHandle 默认 abort，
   ReadHandle 默认 abandon；二者都不能隐式发布或丢弃记录。
3. 实现单线程 `try_reserve()`：检查 pending 和 payload 上限，调用 Geometry，
   用本地缓存判断空间；缓存不足时只 acquire-load 一次共享读游标。
4. `try_reserve()` 在确认空间后用 `memcpy` 写入尚未发布的 `FrameHeader`，
   调用者再直接写 Ring payload；`commit()` 只推进本地写游标并 release-store
   发布写游标。
5. 实现 `try_peek()`：本地快照耗尽时才 acquire-load 写游标；通过 `memcpy`
   读取 Header，重新计算布局，返回连续只读 payload。
6. 实现 `consume()` 与 `publish_reclaimed()`：consume 只推进本地读游标；累计
   32 条、4 KiB、观察到 empty、切换 Channel 或 shutdown 时发布回收游标。
7. 最后进行双线程压力、随机模型、Release 汇编检查和 Ring 微基准；根据证据
   优化，不提前加入 branch hint、prefetch、SIMD 或平台汇编。

### 固定 Storage 的写端使用顺序

`try_reserve()` 是唯一负责把 Geometry offset 转换成真实地址的对象。实现顺序
冻结为：

```text
检查 reservation_pending
-> 检查 exact_payload_bytes <= max_payload_bytes
-> 计算 FrameLayout
-> 用逻辑游标距离检查 layout.frame_bytes 是否可用
-> storage_.get() + header_offset 得到 Header 地址
-> storage_.get() + payload_offset 得到 payload 地址
-> memcpy 写入尚未发布的 FrameHeader
-> 填充 WriteHandle 并设置 pending
-> 返回给调用者直接写 payload
```

空间按逻辑距离计算：

```cpp
const std::uint64_t used =
    writer_state_.current_write_cursor_ -
    writer_state_.cached_read_cursor_;
const std::size_t free =
    capacity - static_cast<std::size_t>(used);
```

若 `free < layout.frame_bytes`，只 acquire-load 一次 `cursors_.read_cursor_`，更新
`cached_read_cursor_` 后重新计算；仍不足才返回 `ReserveStatus::full`。不能比较
掩码后的地址大小，也不能使用 `current + frame >= cached_read` 判断 Ring 是否满。

成功 Handle 保存 `ring_`、连续 payload 地址、`next_write_cursor_`、有效 payload
长度和 status；不保存 `FrameHeader` 或 `FrameLayout`。Header 已经位于 Ring，
Layout 在 `try_reserve()` 返回前结束生命周期。

小型 hot 函数放入 `.hpp` 或单独 `.inl` 以便内联；构造、配置校验和 Storage
分配继续放在 `.cpp`。

### 不可破坏的不变量

- 尾端继续采用“Header 留在尾端，连续 payload 放到物理头部”；tail waste
  计入该帧的 `frame_bytes`，不增加 padding/invalid 帧。
- reserve 使用最终精确 payload 大小；commit 不允许修改大小。
- Ring 的每一侧同时最多一个未终结的短期 Handle。
- 发布游标才是提交标志；commit 前的字节对 Consumer 不可见。
- Producer commit：payload/Header 写入 happens-before Consumer acquire。
- Consumer reclaim：payload 读取 happens-before Producer 覆盖已回收空间。
- peer cursor 缓存只能导致保守的少写或少读，不能扩大可访问范围。
- 不使用 CAS、`fetch_add`、锁、逐 Block 原子、版本号或 Ring 内部等待。
- Debug 做完整 Header/Geometry 检查；Release 只保留避免越界和未定义行为的
  最小检查，绝不使用不可信的 `frame_bytes` 寻找下一条记录。

### 验收标准

- 空 payload、最大 payload、贴尾、回绕、满、drop、回收后复用全部正确。
- abort 不可见；abandon 后再次 peek 得到同一记录。
- 固定随机种子的参考队列模型长时间一致。
- 真实 1P1C 传递数百万条带序号和校验模式的记录，无丢失、重复、乱序或死锁。
- 覆盖逻辑游标跨 `UINT64_MAX` 回绕。
- Debug、Release、ASan/UBSan、TSan 全部通过。
- `WriterState` / `ReaderState` 仍各占独立 64B 热块，`CursorSet` 仍为两个独立
  64B 原子缓存行。
- Release 热路径无分配、锁、CAS、`fetch_add` 和意外 `seq_cst`。
- 建立与 BQLog SISO 的同机、同容量、同 payload、同满队列策略微基准；
  先以达到其 90% 吞吐为优化目标，低于目标先分析汇编和 cache miss。

## 里程碑二：完成可演示、可公平基准的异步日志 V1

### 完成结果

```text
业务线程
  -> 长期 ThreadLogger
  -> 每线程 SPSC Channel
  -> 单后台线程公平扫描
  -> 参数解码与 fmt
  -> 可复用输出缓冲区
  -> NullSink / 批量 FileSink
```

推荐公共使用方式：

```cpp
auto thread_log = logger.attach_thread();  // 冷路径，可分配、可加锁
QLOG_INFO(thread_log, "玩家 {} 进入场景 {}", player_id, scene_id);
```

`attach_thread()` 返回长期生产端，适合游戏服务器的长寿命工作线程。自动 TLS
便利层可以以后增加，但不能让稳态日志调用每次查询 map。

### 实现顺序

1. 冻结 Record Header、参数编码规则和静态 Callsite 元数据。调用点冷路径注册
   `uint32_t CallsiteId`；热路径不查 map。V1 使用固定宽度整数编码，字符串使用
   长度加字节，暂不引入 VLQ 和动态 format string。
2. 实现编码/解码 round-trip。Producer 先精确计算记录长度，只 reserve 一次，
   然后直接写入 Ring，不保存可能失效的用户对象引用。
3. 实现 `AsyncLogger`、长期 `ThreadLogger`、稳定地址 Channel 与冷路径注册。
   Logger 拥有所有 Channel，业务线程退出后才能 shutdown。
4. 先接 `NullSink` 跑通多 Producer、单 Backend、公平轮询和关停排空，再接
   后台 fmt。每次扫描设置记录数/字节预算，避免高流量 Channel 饿死其他线程。
5. Backend 格式化到可复用输出缓冲区；完成复制后立即 consume/reclaim Ring，
   再执行可能较慢的 Sink I/O，Sink 不得保留 Ring 内的 view。
6. 实现批量 Linux FileSink，正确处理 partial write 和 `EINTR`。普通 `flush()`
   表示排空并执行 `write()`，不宣称断电持久；`fdatasync/fsync` 必须单独定义和测量。
7. 实现 shutdown：停止接受新日志，排空全部 accepted records，停止并 join
   Backend，最后释放 Channel。统计满足守恒关系。
8. 完成游戏服务器风格 demo、README、端到端压力测试和公平 benchmark。

### Producer 热路径要求

- 无 fmt、稳态堆分配、锁、阻塞和共享原子 RMW。
- Callsite ID 已缓存；日志级别过滤在 reserve 前完成。
- payload 直接编码到 Ring；过大日志整体丢弃，绝不截断。
- 每线程普通计数保存 attempted/accepted/drop 分类，不污染发布游标缓存行。
- 初版后台调度采用 adaptive poll 加短时定时等待；Ring 内不包含唤醒逻辑，
  Producer 初版不为每条日志主动唤醒后台线程。

### 验收标准

- 多线程日志保持各 Channel FIFO，不承诺跨线程严格全序。
- 数值、浮点、布尔、短字符串和长字符串能够正确编码并格式化。
- 队列满时 Producer 立即返回；过载时明确统计 `drop_new`。
- shutdown 后 `accepted == processed`；始终满足
  `attempted == accepted + dropped`。
- FileSink 正确处理短写和 `EINTR`，格式化异常不会终止 Backend。
- Debug、Release、ASan/UBSan、TSan 和长时间压力测试通过。
- 提供多线程游戏服务器 demo、真实文本日志和架构说明。
- benchmark 分开报告 Producer 延迟、NullSink、后台格式化、普通文件
  `write()` 与 durable flush；记录 P50/P99/P99.9、吞吐、CPU、RSS、accepted、
  dropped 和 bytes。
- 同机对比 spdlog async 与 BQLog Text；BQLog Compress 单列。初始性能目标为
  超过 spdlog async，并尽量达到或超过 BQLog Text；任何结论必须同时报告丢弃量
  和计时边界。

## V1 之后再考虑

以下内容不允许扩张这两个里程碑：MPSC、mmap 恢复、压缩、VLQ、字符串驻留、
跨线程全序、多 Backend 消费同一 Channel、审计级持久化、完整 Release 损坏隔离。
只有 V1 基准指出明确瓶颈后，才为其中某一项建立新的实验分支。

## 当前下一步

Handle 查询接口、规范 inactive 状态以及 payload 字节测试已经完成；普通、零长度、失败、移动、
终结、跨尾、相邻 Frame 和跨线程 release/acquire 均已覆盖，并通过 Debug、Release、ASan/UBSan
与 Clang TSan 门禁。里程碑一的功能正确性关卡完成，下一步进入 SPSC Ring 微基准。在编写 benchmark
代码前，先与用户冻结 workload、payload 分布、计时边界、吞吐/延迟指标和对照组；不再增加阶段 D
或新的中间里程碑。
