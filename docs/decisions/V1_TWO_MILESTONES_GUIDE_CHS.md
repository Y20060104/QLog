# QLog V1 后续两里程碑实现指南

- 状态：唯一生效的总体执行计划
- 日期：2026-08-28
- 最后修订：2026-09-05
- 适用范围：从 SPSC RingBuffer 到可演示、可公平基准的异步日志 V1
- 分工：你实现生产代码；Codex 负责测试、构建门禁、benchmark 执行与结果分析

旧文档中的阶段 A～D、里程碑 0～7 和 M2～M7 仅保留为历史设计证据。
项目仍只有两个顶层里程碑；里程碑内部的小步骤不再升级成新里程碑。

## 共同原则

- Linux x86-64，纯 C++20。
- 每个稳定 `(生产线程, AsyncLogger)` 独占一个 SPSC Channel，一个后台线程消费多个 Channel。
- Producer 稳态热路径不解析/格式化、不分配、不加锁、不阻塞，不执行共享原子 RMW。
- 固定容量，满时 `drop_new`；失败不得推进任何游标。
- 自研 `c20_format` 只在后台线程执行。
- BQLog 是主要源码参照，spdlog、Quill 和 NanoLog 用于补充对照；不逐行移植。
- 每次只改变一个性能变量；结论必须来自同机、同负载、同计时边界的测试。

## 里程碑一：可用且高性能的 SPSC RingBuffer

### 当前状态

Geometry、固定 Storage、四缓存行热状态、单条 reserve/read、连续回绕 payload、
批量发布读游标、Debug/Release 分层校验、对称被动 Handle API 和隔离 benchmark
已经实现。

ADR-005 将读写 Handle 统一为 16B 被动令牌：

```text
WriteHandle = payload pointer + packed frame/status + payload bytes
ReadHandle  = payload pointer + packed frame/status + payload bytes
```

二者均不保存 Ring owner 或 next cursor，没有自定义析构，不通过 RAII 改变 Ring。

### 最终接口

```cpp
[[nodiscard]] WriteHandle try_reserve(std::size_t payload_bytes) noexcept;
void commit(const WriteHandle& handle) noexcept;
void abort(const WriteHandle& handle) noexcept;

[[nodiscard]] ReadHandle try_read() noexcept;
void release(const ReadHandle& handle) noexcept;
void abandon(const ReadHandle& handle) noexcept;

void publish_reclaimed() noexcept;
```

正常路径每侧只使用两个 Ring 操作：

```text
try_reserve -> 写 payload -> commit
try_read    -> 读 payload -> release
```

`abort/abandon` 只服务冷路径。`return` 是 C++ 关键字，因此读侧采用 `release`。

### 不可破坏的不变量

- Header 留在物理尾端，回绕 payload 连续放在物理头部；tail waste 计入当前 Frame。
- reserve 使用精确 payload 大小；commit 不改变大小、不复制 Header 或 payload。
- 每侧最多一个成功且未终结的操作。
- 发布游标是提交标志；commit 前的字节对 Consumer 不可见。
- Producer release-store 与 Consumer acquire-load 建立 payload 可见性。
- Consumer 读完后才推进本地读游标；回收游标按既定阈值批量发布。
- Handle 物理上可复制，逻辑上只能终结一次；Release 保留每侧单次 pending 契约，
  不承担跨 Ring、重复终结或完整损坏 Frame 校验。
- 不使用 CAS、`fetch_add`、锁、逐 Block 原子、版本号或 Ring 内部等待。

### 里程碑一状态

2026-09-02，SPSC RingBuffer 标记为“本地开发完成”：

1. Geometry、Storage、Frame 回绕、被动 Handle、读写游标与批量回收已经实现；
2. Debug 47/47、Release 46 项通过且仅 1 项预期跳过、ASan/UBSan 47/47 通过；
3. R1 读侧 pending 实验未达到 3% 性能门槛，已经回退；R2 不再执行；
4. Release 汇编与最终 pending/校验分层契约一致；
5. quick benchmark 的 10 个样本全部有效，数量统计无异常。

原生 Linux TSan 和 clean commit 的完整重复性能矩阵仍作为正式发布门禁，后续通过 CI
补齐；它们不阻塞里程碑二的协议设计，但在对外发布 Ring V1 性能结论前必须完成。

完整交付、最终不变量、门禁证据与未完成发布项见
[里程碑一完成报告](./MILESTONE1_COMPLETION_REPORT_CHS.md)。

里程碑一历史实验细节见：
[下一步实现指南](./NEXT_IMPLEMENTATION_GUIDE_CHS.md)。

## 里程碑二：可演示、可公平基准的异步日志 V1

### 目标链路

```text
业务线程
  -> ProducerHandle
  -> 每个（线程, AsyncLogger）SPSC Channel
  -> 单后台线程公平扫描
  -> Record 解码与 c20_format 到 BackendWorker 私有 64KiB scratch
  -> NullSink 计数 / TextFileSink 接收完整行到内存 batch
  -> release Frame
  -> TextFileSink 执行可能阻塞的 write/fdatasync
```

### 实现顺序

1. I0 已完成：ADR-007～ADR-010、RecordHeader/ArgumentTag、静态断言和 ABI tests；
2. I1 按 [企业级开发规范](./MILESTONE2_I1_RECORD_CORE_DEVELOPMENT_GUIDE_CHS.md) 一次完成 hash、
   包装器/traits、checked measure、独立的裸指针加显式长度 codec 和完整测试；
3. I2 一次完成 Channel/ProducerHandle/AsyncLogger、过滤、一次 reserve、时间戳、Ring 直写和统计；
4. I3 一次完成 Backend 公平扫描、固定解码槽、NullSink、错误隔离和关停排空；
5. I4 一次完成 `c20_format`、固定 cache、BackendWorker 私有 64KiB scratch、Sink 内存 batch、
   TextFileSink、demo 和端到端 benchmark。

### 已冻结的 Record/Backend 设计点

- [已冻结] `RecordHeader` 为 32B/8B 对齐，字段偏移和尾部 `arg_count/level/flags`；
- [已冻结] 不使用 Callsite；format 深拷贝，参数逐条携带 type tag；
- [已冻结] thread/Logger/版本放 Channel，category/level 放每条 Record；
- [已冻结] D3 使用 Unix Epoch 纳秒；`CLOCK_REALTIME_COARSE` 为主时钟，
  `CLOCK_REALTIME` 为 fallback；成功 reserve 后、写 Record 前采集 admission timestamp；
  `flags` 低两位为 primary/fallback/unavailable/reserved，值 3 只叫 reserved；不做校准或
  时间戳排序，`time_value` 不用于耗时计算，跨线程不承诺全局时间顺序；
- [已冻结] D4 只支持固定基本类型、显式 Pointer64/UTF-8，拒绝用户 formatter，参数上限 32；
- [已冻结] D5 所有字符串 commit 前深拷贝，裸 C 字符串拒绝；显式 `qlog::cstr(ptr)` 对非空指针
  调用一次 `strlen` 并缓存长度，调用方保证 NUL 终止，null wrapper 编码 `NullUtf8`；
- [已冻结] D6 使用 `[u8 tag][payload]` packed LE 协议，`args_alignment = 1`，禁止未对齐 typed-pointer 解引用；
- [已冻结] `crc32c4x64_v1` 使用 BQLog 式四路 CRC32C 原始折叠得到 64-bit hash，raw 0
  规范化为 1，Header 的 0 保留为 sentinel；字面量仅可把逐位一致的 constexpr hash 作为优化，
  运行时 format 在 reserve 成功后执行 fused copy-and-hash；
- [已冻结] 严格自动索引 `c20_format` 子集、256-entry/4-way BackendWorker 私有解析缓存、
  8KiB format、32 fields、32B spec、width 4096、precision 64，以及包含元数据前缀与换行的
  64KiB 完整文本行；禁止回溯、递归和完整 format 重扫，不设置独立 work-unit 上限；
- [已冻结] Text 路径先在私有 scratch 形成完整行，再交给 Sink 内存 batch；batch 接收完成后
  release Frame，可能阻塞的 `write`/`fdatasync` 位于 release 之后。精确合同见 ADR-010。

设计门禁已经关闭；实现必须按照 ADR-010 的 hash 兼容性、Backend 固定分配和最坏工作量边界执行。

跨窗口继续讨论时，以 [ADR-007](./ADR-007-self-contained-record-header.md)、
[ADR-008](./ADR-008-realtime-coarse-admission-timestamp.md)、
[ADR-009](./ADR-009-v1-packed-tagged-arguments.md) 和
[ADR-010](./ADR-010-v1-backend-c20-format.md) 以及
[里程碑二实现设计指南](./MILESTONE2_RECORD_IMPLEMENTATION_GUIDE_CHS.md) 为总体基线；I1 实施必须继续遵循
[I1 Record Core 企业级开发规范](./MILESTONE2_I1_RECORD_CORE_DEVELOPMENT_GUIDE_CHS.md)；
[里程碑二设计讨论指南](./MILESTONE2_DESIGN_GUIDE_CHS.md) 记录 D1～D6 的决策状态与顺序。

### 最终验收

- 每个 Channel 保持 FIFO；不承诺跨线程严格全序。
- `calls == filtered + attempted`、`attempted == accepted + dropped`，shutdown 后
  `accepted == processed`。
- Producer 热路径没有 format parser、稳态分配、锁、阻塞或共享 RMW。
- Sink 只接收完整日志行；内存 batch 接收后先 release Frame，再执行可能阻塞的文件 I/O，
  且不得保留 Ring 或 Backend scratch view。
- TextFileSink 正确处理短写和 `EINTR`；普通 flush 与 durable flush 分开定义。
- benchmark 分开报告 Producer 延迟、NullSink、后台格式化、普通 `write()` 和
  `fdatasync/fsync`，并记录吞吐、P50/P99/P99.9、CPU、RSS 和丢弃量。
- Ring 微基准只比较 QLog SPSC 与 BQLog SISO；不把 spdlog 的带锁 MPMC 队列标成 SPSC。
- 系统级同机对比 spdlog async 与 BQLog Text：spdlog 至少包含单 Producer/单 Backend 和
  多 Producer/单 Backend，QLog `drop_new` 只对齐 spdlog `discard_new`；BQLog Compress
  单列，不混合口径。具体分层见 ADR-006。

## V1 之后再考虑

MPSC、mmap 恢复、压缩、VLQ、字符串驻留、跨线程全序、多 Backend、审计级持久化
和完整 Release 损坏隔离均不进入 V1。只有基准证明具体瓶颈后，才为其中一项建立
独立实验。

## 当前下一步

D1～D6、H1～H4、I0 已完成，I1-A 保留既有实现。
2026-09-10 用户授权 Codex 修复生产代码并测试，I1-B hash 基线与 I1-C types/encoder 已通过本轮验证。
准确结果见 [2026-09-10 修复与验证报告](./I1_HASH_ENCODER_VALIDATION_20260910_CHS.md)。
下一步进入 [I1CD 指南 D 章](./MILESTONE2_I1CD_HANDS_ON_GUIDE_CHS.md#record-decoder)，实现独立 decoder，
再补齐 decoder 测试、其余 I1-D 和性能门禁。整个 I1 尚未完成，仍不接 Ring 或 c20_format。

总体协议见 [里程碑二实现设计指南](./MILESTONE2_RECORD_IMPLEMENTATION_GUIDE_CHS.md)；I1 的模块边界、
错误合同、测试矩阵、工具链和性能验收以
[I1 Record Core 企业级开发规范](./MILESTONE2_I1_RECORD_CORE_DEVELOPMENT_GUIDE_CHS.md) 为准。

R1 的失败原因、回退和门禁证据继续保存在
[读侧 R1 实验记录](./NEXT_IMPLEMENTATION_GUIDE_CHS.md)，不再作为当前实现指南。
