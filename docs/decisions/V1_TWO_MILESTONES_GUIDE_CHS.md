# QLog V1 后续两里程碑实现指南

- 状态：唯一生效的总体执行计划
- 日期：2026-08-28
- 最后修订：2026-09-02
- 适用范围：从 SPSC RingBuffer 到可演示、可公平基准的异步日志 V1
- 分工：你实现生产代码；Codex 负责测试、构建门禁、benchmark 执行与结果分析

旧文档中的阶段 A～D、里程碑 0～7 和 M2～M7 仅保留为历史设计证据。
项目仍只有两个顶层里程碑；里程碑内部的小步骤不再升级成新里程碑。

## 共同原则

- Linux x86-64，纯 C++20。
- 每个稳定生产线程独占一个 SPSC Channel，一个后台线程消费多个 Channel。
- Producer 稳态热路径不 fmt、不分配、不加锁、不阻塞，不执行共享原子 RMW。
- 固定容量，满时 `drop_new`；失败不得推进任何游标。
- fmt 只在后台线程执行。
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

当前可执行细节见：
[下一步实现指南](./NEXT_IMPLEMENTATION_GUIDE_CHS.md)。

## 里程碑二：可演示、可公平基准的异步日志 V1

### 目标链路

```text
业务线程
  -> ThreadLogger
  -> 每线程 SPSC Channel
  -> 单后台线程公平扫描
  -> Record 解码与 fmt
  -> 可复用输出缓冲区
  -> NullSink / 批量 FileSink
```

### 实现顺序

1. 商讨并冻结 `RecordHeader`、参数编码、字符串所有权和静态 Callsite 元数据。
2. 实现 codec round-trip：精确计算长度，只 reserve 一次，直接编码到 Ring payload。
3. 实现稳定地址 Channel、`ThreadLogger`、`AsyncLogger` 和冷路径注册。
4. 用 NullSink 跑通多 Producer、单 Backend、公平扫描和关停排空。
5. 接入后台 fmt；输出使用可复用缓冲区，Sink 不得持有 Ring view。
6. 实现 Linux 批量 FileSink，正确处理 partial write 和 `EINTR`。
7. 实现 shutdown、统计守恒、游戏服务器 demo 和端到端 benchmark。

### 进入里程碑二前必须商讨的设计点

- `RecordHeader` 的字段、大小、对齐和版本方式；
- V1 支持的参数类型集合；
- 字符串是立即复制还是允许受约束的静态引用；
- 时间戳、线程标识、日志级别和 `CallsiteId` 放在哪一层；
- 静态 format string 与动态 format string 的边界；
- 参数编码是否固定宽度，以及是否允许嵌套/自定义类型。

这些决策会改变 payload ABI 和生产者成本。在书面冻结前，不开始实现 codec。

跨窗口继续讨论时，以
[里程碑二设计讨论指南](./MILESTONE2_DESIGN_GUIDE_CHS.md) 为入口；该文档只冻结讨论范围
和顺序，不代表 Record ABI 已经决定。

### 最终验收

- 每个 Channel 保持 FIFO；不承诺跨线程严格全序。
- `attempted == accepted + dropped`，shutdown 后 `accepted == processed`。
- Producer 热路径没有 fmt、稳态分配、锁、阻塞或共享 RMW。
- FileSink 正确处理短写和 `EINTR`；普通 flush 与 durable flush 分开定义。
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

进入里程碑二的设计阶段，先商讨并书面冻结 `RecordHeader` 与参数编码协议。第一轮只做
字段归属和成本分析，不实现 codec：确定 Callsite 元数据、时间戳、线程标识、日志级别、
参数类型集合、字符串所有权以及版本方式。协议冻结后，再实现精确长度计算、单次 reserve
和直接编码到 Ring payload 的 codec round-trip。

具体讨论问题、候选方案、产出物和新窗口浏览顺序见
[里程碑二设计讨论指南](./MILESTONE2_DESIGN_GUIDE_CHS.md)。

R1 的失败原因、回退和门禁证据继续保存在
[读侧 R1 实验记录](./NEXT_IMPLEMENTATION_GUIDE_CHS.md)，不再作为当前实现指南。
