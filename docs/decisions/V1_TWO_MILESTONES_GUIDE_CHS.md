# QLog V1 后续两里程碑实现指南

> 2026-09-17 format覆盖：[ADR-016](./ADR-016-v1-bqlog-worker-format.md)优先于本文旧的严格花括号、参数数目匹配、默认文本表示和解析缓存合同。Producer原样copy/hash；worker按BQLog当前UTF-8顺序扫描。当前起点与剩余实施见[剩余V1指南](./V1_REMAINING_IMPLEMENTATION_GUIDE_CHS.md)。本文未被覆盖的wire/参数/长度规则继续有效，历史验收记录不改写为当前实现状态。

> 2026-09-16 R2：用户要求文件恢复、共享/独立后台、低空间唤醒按BQLog，并明确采用worker mutex/CV。低空间/full路径允许exchange与短暂等待锁，正常低占用及Ring/Context注册协议不改；最新实施见[V1收尾指南](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md)和[ADR-015](./ADR-015-v1-backend-control-and-output.md)。

> 2026-09-16 当前执行入口：[V1 一轮收尾指南](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md)，新决定 [ADR-015](./ADR-015-v1-backend-control-and-output.md)。两个顶层里程碑不变；剩余 I2/I3/I4 在一份指南中连续实施，测试暂后置，不能因此标记 V1 已验收。

> 当前 I2 接口已由 [ADR-013](./ADR-013-v1-automatic-producer-context.md) 更新为 Logger::try_log + TLS 自动上下文；旧显式绑定/冷注册锁说明失效。后续以 [主指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md) 和 [计划](./MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md) 为准。

> 多 Appender 最新合同：[ADR-012](./ADR-012-v1-multi-appender.md)。一个 Logger 可分发多个目标，配置 reset 支持增删/替换；处理时过滤，各 Text 目标可独立时区。旧文中的单 Sink 流程须按该合同扩展。
> 2026-09-13 当前状态：I1 已按 WSL2 开发范围收口；I2 设计已冻结，生产骨架已开始，尚未验收。合同见 [ADR-011](./ADR-011-v1-producer-channel.md)，执行见 [I2 计划](./MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md) 与 [I2 动手指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)。原生 Linux 发布复核与自动 CI 后续补齐。

- 状态：两里程碑总体结构；剩余实施以2026-09-17新指南为准
- 日期：2026-08-28
- 最后修订：2026-09-17
- 适用范围：从 SPSC RingBuffer 到可演示、可公平基准的异步日志 V1
- 分工：你实现生产代码；Codex 负责测试、构建门禁、benchmark 执行与结果分析

旧文档中的阶段 A～D、里程碑 0～7 和 M2～M7 仅保留为历史设计证据。
项目仍只有两个顶层里程碑；里程碑内部的小步骤不再升级成新里程碑。

## 共同原则

- Linux x86-64，纯 C++20。
- 每个稳定 `(生产线程, AsyncLogger)` 独占一个 SPSC Channel，一个后台线程消费多个 Channel。
- Producer稳态不解析/格式化、不分配；低占用路径不拿worker锁，低空间/full允许ADR-015的exchange及mutex/CV通知。
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
  -> AsyncLogger::try_log + TLS ProducerContext
  -> 每个（线程, AsyncLogger）SPSC Channel
  -> 所属共享或独立worker按Session/Channel公平扫描
  -> Record解码与BQ UTF-8 scanner到worker正文scratch；各目标组完整行（各64KiB）
  -> ConsoleAppender 完整行缓冲/输出 / TextFileSink 接收完整行到内存 batch
  -> release Frame + publish_reclaimed
  -> TextFileSink 执行可能阻塞的 write/fdatasync
```

### 历史能力分组与当前执行顺序

I0～I4是保留的能力分组，不是剩余模块的先后顺序。当前先完成B0/B诊断，再C formatter→D Appender/batch→E邮箱→F worker→G/H公共API与示例，详见[剩余V1指南](./V1_REMAINING_IMPLEMENTATION_GUIDE_CHS.md)。

1. I0 已完成：ADR-007～ADR-010、RecordHeader/ArgumentTag、静态断言和 ABI tests；
2. I1 按 [企业级开发规范](./MILESTONE2_I1_RECORD_CORE_DEVELOPMENT_GUIDE_CHS.md) 一次完成 hash、
   包装器/traits、checked measure、独立的裸指针加显式长度 codec 和完整测试；
3. I2 一次完成 Channel/TLS ProducerContext/AsyncLogger、过滤、一次 reserve、时间戳、Ring 直写和 Debug 条件诊断（Release 外部验收）；
4. I3 一次完成 Backend 公平扫描、固定解码槽、ConsoleAppender、错误隔离和关停排空；
5. I4 一次完成 BQ UTF-8 scanner、BackendWorker正文/完整行各64KiB scratch、Sink 内存 batch、
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
  规范化为 1，Header 的 0 保留为 sentinel；公共数组与runtime入口均在reserve成功后fused copy-and-hash，
  constexpr只保留I1参考算法能力；
- [已冻结，ADR-016] Producer原样format复制；worker按BQLog当前UTF-8顺序扫描，零参数raw，宽松字段与顺序参数；无V1解析缓存。format<=8192、arg_count<=32、完整行<=65536B，安全适配见新ADR。
- [已冻结] Text 路径先在私有 scratch 形成完整行，再交给 Sink 内存 batch；batch 接收完成后
  release Frame并publish_reclaimed，可能阻塞的 `write`/`fdatasync` 位于发布回收之后。精确合同见ADR-016/015。

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
- `calls == filtered + rejected_pre_admission + attempted`、
  `attempted == accepted + dropped_full + failed_after_attempt`；Debug/诊断构建及Release外部测试验证；仅正常排空、无worker/Frame故障且计数未溢出的shutdown后要求
  `accepted == processed`，失败路径以backend_failed/drain_incomplete报告。
- Producer无format parser与稳态分配；低占用/压力唤醒分开验收，不把压力分支称为无锁。
- Sink 只接收完整日志行；内存 batch 接收后先 release Frame，再执行可能阻塞的文件 I/O，
  且不得保留 Ring 或 Backend scratch view。
- TextFileSink 正确处理短写和 `EINTR`；普通 flush 与 durable flush 分开定义。
- benchmark 分开报告 Producer 延迟、无输出测试消费者基线（非生产 Appender）、后台格式化、普通 `write()` 和
  `fdatasync/fsync`，并记录吞吐、P50/P99/P99.9、CPU、RSS 和丢弃量。
- Ring 微基准只比较 QLog SPSC 与 BQLog SISO；不把 spdlog 的带锁 MPMC 队列标成 SPSC。
- 系统级同机对比 spdlog async 与 BQLog Text：spdlog 至少包含单 Producer/单 Backend 和
  多 Producer/单 Backend，QLog `drop_new` 只对齐 spdlog `discard_new`；BQLog Compress
  单列，不混合口径。具体分层见 ADR-006。

## V1 之后再考虑

MPSC、mmap 恢复、压缩、VLQ、字符串驻留、跨线程全序、同一Logger多消费者Backend、审计级持久化
和完整 Release 损坏隔离均不进入 V1。只有基准证明具体瓶颈后，才为其中一项建立
独立实验。

## 当前下一步

配置A已实现，诊断B部分写入并有接线阻塞；先按[剩余V1指南](./V1_REMAINING_IMPLEMENTATION_GUIDE_CHS.md)完成B0/B，再做worker formatter、Appender/batch、邮箱、共享/独立worker与公共API闭环。现状来自源码审计，不代表本轮构建/测试通过。

I1保持独立wire/hash模块；ADR-013自动Context、ADR-014数组入口、ADR-015管理/恢复/唤醒和ADR-016格式共同构成当前合同。历史显式绑定、strict plan/cache与旧未完成状态不再作为执行步骤。
