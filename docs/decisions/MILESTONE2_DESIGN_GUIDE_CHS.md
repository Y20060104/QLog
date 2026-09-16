# QLog 里程碑二设计讨论指南：异步日志 V1

> 当前 I2 接口已由 [ADR-013](./ADR-013-v1-automatic-producer-context.md) 更新为 Logger::try_log + TLS 自动上下文；旧显式绑定/冷注册锁说明失效。后续以 [主指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md) 和 [计划](./MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md) 为准。

> 多 Appender 最新合同：[ADR-012](./ADR-012-v1-multi-appender.md)。一个 Logger 可分发多个目标，配置 reset 支持增删/替换；处理时过滤，各 Text 目标可独立时区。旧文中的单 Sink 流程须按该合同扩展。
> 2026-09-13 当前状态：I1 已按 WSL2 开发范围收口；I2 设计已冻结，生产骨架已开始，尚未验收。合同见 [ADR-011](./ADR-011-v1-producer-channel.md)，执行见 [I2 计划](./MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md) 与 [I2 动手指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)。原生 Linux 发布复核与自动 CI 后续补齐。

- 状态：D1～D6 与 H1～H4 已由 ADR-007～ADR-010 全部冻结；ABI 声明与测试完成
- 前置条件：里程碑一已本地开发完成
- 当前规则：不再拆分设计小轮次；当前实现任务为 I2 Producer/Channel
- 目标场景：Linux C++ 实时游戏服务器 / 实时服务

## 1. 已冻结的产品边界

V1 面向 1～128 个长期稳定的业务线程：

```text
业务线程
  -> ProducerHandle
  -> 每个（线程, AsyncLogger）固定容量 SPSC Channel
  -> 单后台线程公平扫描
  -> Record 解码与 c20_format 到 BackendWorker 私有 64KiB scratch
  -> ConsoleAppender 完整行缓冲/输出 / TextFileSink 接收完整行到内存 batch
  -> release Frame
  -> TextFileSink 执行可能阻塞的 write/fdatasync
```

Producer 热路径目标：

- 不格式化最终文本；
- 稳态不进行堆分配；
- 不使用锁、阻塞或共享 RMW；
- 精确计算 payload，一次 reserve，直接编码到 Ring；
- 队列满时默认 `drop_new`，不阻塞业务线程；
- 保证每个 Channel 内 FIFO，不承诺跨线程严格全序。

MPSC、mmap 恢复、压缩、VLQ、字符串驻留、跨线程全序、多 Backend 与审计级持久化
不进入 V1。

## 2. RingBuffer 已提供的边界

- Producer 获得一段精确长度、连续、可写的 payload；填充完成后 commit。
- Consumer 获得连续只读 payload；其生命周期只持续到 release/abandon。
- Ring 内部 FrameHeader 已保存 frame/payload 长度，Record 协议不应无理由重复字段。
- Channel 本身可以携带稳定的线程、logger 或时钟描述元数据，避免每条记录重复保存。

## 3. 已冻结的 D1～D6 与后续讨论顺序

### D1：字段归属（已冻结）

[ADR-007](./ADR-007-self-contained-record-header.md) 已选择 BQLog 式自包含 Ring Record：

- V1 不使用 `CallsiteId`、Callsite 注册表、静态参数 schema 或格式化 thunk；
- 所有受支持的 format 来源都深拷贝 UTF-8 bytes 进 Ring，并使用同一 Record ABI；
- 参数类型以 tagged arguments 随每条 Record 保存；
- `category_id` 与 `level` 都是逐 Record 动态字段，过滤发生在长度计算和复制之前；
- thread/Logger 身份、时钟域、Ring Frame/Record 版本和 hash policy 属于 Channel；
- 一个 Channel 永久绑定一个生产线程和一个 `AsyncLogger`，同线程写两个 Logger 使用两个 Channel；
- V1 不保存 file/function/line 源码位置。

因此，模块停止调用日志 API 后，已经发布的 Record 不依赖模块内指针或代码，可以继续消费；
Logger、Channel、category 名称表和 Backend 仍需存活到排空。

### D2：32B RecordHeader（已冻结）

| 偏移 | 大小 | 字段 |
|---:|---:|---|
| 0 | 8 | `time_value` |
| 8 | 8 | `format_hash` |
| 16 | 4 | `format_bytes` |
| 20 | 4 | `args_bytes` |
| 24 | 4 | `category_id` |
| 28 | 2 | `arg_count` |
| 30 | 1 | `level` |
| 31 | 1 | `flags` |

`sizeof(RecordHeader) == 32`、`alignof(RecordHeader) == 8`，不使用 C++ bit-field，
不逐 Record 保存 ABI version。版本在 Channel 注册时校验一次；未来文件版本由独立
`BinaryFileHeader` 承担。外层 `FrameHeader::payload_bytes` 仍是可访问边界权威，Record 内的
`format_bytes`/`args_bytes` 只负责安全划分 payload，并必须受 Frame 边界约束。
V1 限定 little-endian host。Header/参数使用已对齐局部值配合固定宽度 `memcpy`/`load_le`/`store_le`，
禁止把 Ring 地址转换为 `RecordHeader*` 或未对齐 typed pointer 后解引用。

### D3：admission timestamp（已冻结）

[ADR-008](./ADR-008-realtime-coarse-admission-timestamp.md) 冻结：

- `time_value` 保存 Unix Epoch 纳秒；单位不代表默认时钟具有纳秒精度；
- primary 为 `CLOCK_REALTIME_COARSE`，fallback 为 `CLOCK_REALTIME`，两者同域同单位且无需校准；
- 在成功 `try_reserve()` 后、写 payload 前采样，正式名称为 admission timestamp；
- `flags & 0x03` 的值 0/1/2 分别表示 primary/fallback/unavailable，值 3 只叫 `reserved`；
- 墙钟回退时保留原值，不 clamp；Channel FIFO 才是线程内顺序权威；
- `time_value` 不用于耗时计算，也不承诺跨线程全局时间顺序；
- V1 不实现 TSC 或 monotonic-to-wall 校准。

### D4：V1 参数类型集合（已冻结）

[ADR-009](./ADR-009-v1-packed-tagged-arguments.md) 冻结最小基本类型集合：bool、普通 char、
1/2/4/8B signed/unsigned integer、float、double、enum underlying value、显式 `qlog::ptr()`/
裸 `nullptr` 的 Pointer64，以及 UTF-8 字符串。`kMaxArgCount = 32`。

V1 拒绝裸 C 字符串指针、未包装对象指针、函数/成员指针、long double、128 位整数、宽字符、
blob、named args、容器、chrono、用户 formatter 和隐式用户转换。

### D5：字符串所有权（已冻结）

- format 与字符串参数都只在调用期间借用，commit 前逐 Record 深拷贝进 Ring；
- 所有 format 来源走同一 Record 写入路径；字面量可预计算长度，并仅允许把 constexpr hash 作为
  实现优化，且结果必须与 `crc32c4x64_v1` 一致；运行时 view 在 reserve 成功后使用一次 fused
  copy-and-hash，但两者不形成不同的 Header、flags、参数 ABI 或 Decoder；
- `std::string[_view]`、`std::u8string[_view]` 和字符串数组使用显式/静态长度；
- 裸 `const char*` 默认拒绝；显式 `qlog::cstr(ptr)` 对非空指针调用一次 `strlen`，调用方保证可读且 NUL 终止；
- `qlog::cstr(nullptr)` 编码 `NullUtf8`，空字符串编码长度为 0 的 `Utf8String`；
- V1 不验证 UTF-8、不截断，也不从 `string_view` 猜测静态生命周期。

### D6：参数编码方式（已冻结）

参数使用 `[u8 tag][紧随 payload]` 的 packed little-endian 协议；字符串为
`[u8 Utf8String][u32_le length][bytes]`，`NullUtf8` 只有 1B tag。`args_alignment = 1`，
format/args 之间、参数之间和逻辑 Record 尾部都没有 padding。

Header 和参数都必须通过局部对象 + `memcpy` 或显式 `load_le/store_le` 访问，禁止把 Ring 地址转换为
`RecordHeader*` 或未对齐 typed pointer 后解引用。Decoder 同时受 `args_bytes`、`arg_count <= 32`
和 Frame 边界限制；未知 tag 只放弃当前 Record 并安全 release。

## 4. 已关闭的 G0 门禁

[ADR-010](./ADR-010-v1-backend-c20-format.md) 已一次性冻结：

- `crc32c4x64_v1`：BQLog 式四路 CRC32C 原始折叠为 64-bit，raw 结果 0 规范化为 1，
  Header 中的 0 保留为“未计算” sentinel；可选 constexpr、hash-only 与 fused copy-and-hash
  必须逐位一致；
- 仅 `{}`/`{:spec}` 自动索引的严格 `c20_format` 子集；
- 256-entry、4-way、BackendWorker 私有解析缓存，完整字节验证碰撞；
- 8KiB format、32B spec、32 fields、4096 width、64 precision，以及包含元数据前缀与换行的
  64KiB 完整文本行上限；
- parser/formatter 禁止回溯、递归和完整 format 重扫；以上输入、字段、spec、width、precision 与
  输出上限共同保证最坏工作量有界，不再设置独立的 128Ki work-unit 计数；
- 动态 width/precision、显式/命名索引、locale、chrono 与用户 formatter 全部不进入 V1。

完整常量、类型组合和错误合同见 ADR-010；总体实施任务见
[里程碑二实现设计指南](./MILESTONE2_RECORD_IMPLEMENTATION_GUIDE_CHS.md)，I1 的企业级执行合同见
[I1 Record Core 开发规范](./MILESTONE2_I1_RECORD_CORE_DEVELOPMENT_GUIDE_CHS.md)。

## 5. 紧凑实现顺序

1. **I0 已完成**：RecordHeader/ArgumentTag ABI、静态断言、layout 与 wire-value tests；
2. **I1 独立 Record Core**：按企业级开发规范一次完成 hash、包装器/traits、checked measure、裸指针加显式长度的
   codec 及完整测试；I1 拥有 decode 结果类型，且不依赖 Ring/Channel；
3. **I2 Producer/Channel**：冷路径绑定、过滤、一次 reserve、时间戳、Ring 直写与统计；
4. **I3 Backend/ConsoleAppender**：公平扫描、固定解码槽、错误隔离、排空和生命周期；
5. **I4 Text/验收**：`c20_format`、固定 cache、BackendWorker 私有 64KiB scratch、Sink 内存 batch、
   TextFileSink、demo 和正式 benchmark。

这些是里程碑二内部步骤，不再拆成新的顶层里程碑。

2026-09-13 当前实施入口：[I2 动手指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)，合同见 [ADR-011](./ADR-011-v1-producer-channel.md)。
I1-A 保留已完成实现；先按新指南 A 章补齐最新 hash 修改的剩余问题，再实现独立 codec。
文件细化为 record_types.hpp、record_encoder.hpp、record_decoder.hpp 与 src/record_decoder.cpp；
采用四个 u64 level mask policy、16B tag/union/length DecodedArg。维护者写生产代码和生产接线，
Codex 在交接后负责测试/benchmark 支持与验证。本次只更新文档，不关闭 I1。

## 6. 里程碑二最终验收

- 每个 Channel 保持 FIFO；
- `calls == filtered + rejected_pre_admission + attempted`、
  `attempted == accepted + dropped_full + failed_after_attempt`；Debug/诊断构建及 Release 外部测试验证，shutdown 后
  `accepted == processed`；
- Producer 热路径无 format parser、稳态分配、锁、阻塞或共享 RMW；
- Text 路径只把格式化成功的完整行交给 Sink 内存 batch；batch 接收完成后 release Frame，
  可能阻塞的 `write`/`fdatasync` 只能发生在 release 之后；Sink 不持有已 release 的 Ring view；
- FileSink 正确处理短写和 `EINTR`；普通 flush 与 durable flush 分开；
- 分别报告 Producer 延迟、无输出测试消费者基线（非生产 Appender）、后台格式化、普通写与 durable flush；
- 报告吞吐、P50/P99/P99.9、CPU、RSS、accepted/dropped/processed；
- Ring 微基准只对比 QLog SPSC 与 BQLog SISO；
- 系统级对比 QLog、spdlog async 与 BQLog Text；BQLog Compress 单列。

## 7. 新窗口建议浏览顺序

1. 本文；
2. [里程碑一完成报告](./MILESTONE1_COMPLETION_REPORT_CHS.md)；
3. [ADR-007](./ADR-007-self-contained-record-header.md)；
4. [ADR-008](./ADR-008-realtime-coarse-admission-timestamp.md)；
5. [ADR-009](./ADR-009-v1-packed-tagged-arguments.md)；
6. [ADR-010](./ADR-010-v1-backend-c20-format.md)；
7. [里程碑二实现设计指南](./MILESTONE2_RECORD_IMPLEMENTATION_GUIDE_CHS.md)；
8. [I1 Record Core 企业级开发规范](./MILESTONE2_I1_RECORD_CORE_DEVELOPMENT_GUIDE_CHS.md)；
9. [V1 决策日志](./V1_DECISION_LOG.md)；
10. `include/qlog/detail/spsc_ring_buffer.hpp` 与 `src/spsc_ring_buffer.cpp`；
11. ADR-005（Handle）与 ADR-006（benchmark 分层）。

开始动手时打开 [I2 动手指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)，按 0/A/B/C/D/E/F/G/H 推进。

新窗口先读 ADR-011、I2 执行计划、I2 动手指南及 AGENTS.md；确认实际目录为 /home/qq344/QLog。
当前不重新实现 I1，也不提前加入 Backend/c20_format/MPSC。


2026-09-15 当前覆盖决定：生产取消 NullAppender，空 Appender 配置默认 Console；I3 包含 Console 所需基础格式化，I4 扩展 TextFile。详见 [I2 主指南 N](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md#i2-current-next)，历史测试基线不代表生产输出类型。
