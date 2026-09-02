# QLog 里程碑二设计讨论指南：异步日志 V1

- 状态：等待商讨，尚未冻结 Record ABI
- 前置条件：里程碑一已本地开发完成
- 当前规则：先完成设计决策和 ADR，再编写 codec
- 目标场景：Linux C++ 实时游戏服务器 / 实时服务

## 1. 已冻结的产品边界

V1 面向 1～128 个长期稳定的业务线程：

```text
业务线程
  -> ThreadLogger
  -> 每线程固定容量 SPSC Channel
  -> 单后台线程公平扫描
  -> Record 解码与 fmt
  -> 可复用输出缓冲区
  -> NullSink / 批量 FileSink
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
- Channel 本身可以携带稳定的线程、logger 或时钟校准元数据，避免每条记录重复保存。

## 3. 第一轮必须按顺序讨论的问题

### D1：Callsite 元数据与每条 Record 如何拆分

候选静态元数据包括：format string、源文件、函数、行号、参数类型 schema、logger/category。
候选动态字段包括：时间戳、CallsiteId、日志级别、参数值和少量 flags。

需要决定：

- CallsiteId 是进程内运行时注册，还是构建期稳定 ID；
- 静态元数据由首次使用注册，还是显式预注册；
- 动态 format string 是否进入 V1；若进入，是否只能走明确的慢路径；
- 插件/动态库卸载前如何保证已接受记录排空。

### D2：RecordHeader 的职责、大小和对齐

先比较，不立即冻结：

- 16B 最小 Header：例如 64 位时间值、32 位 CallsiteId、32 位 packed metadata；
- 24B 扩展 Header：显式加入线程 ID、额外长度或序列字段，但增加每条日志带宽。

讨论原则：

- 每线程 Channel 已隐含线程身份，优先考虑把 thread ID 放在 Channel 元数据而非每条记录；
- Ring FrameHeader 已有 payload 长度，不默认在 RecordHeader 重复；
- level、参数数量、版本与 flags 可评估是否打包；
- 必须用 `static_assert(sizeof/alignof)` 和十六进制 golden bytes 固定 ABI。

### D3：时间戳模型

需要比较：

- 每条调用 `system_clock`；
- 每条调用 `steady_clock`，后台结合校准点转换墙上时间；
- 读取 TSC 并后台校准。

V1 不因理论速度直接选择 TSC。必须同时考虑跨核一致性、校准、休眠/频率变化、可移植性
以及 Producer P99。时间值是否记录纳秒、时钟 tick 或 delta 也需要冻结。

### D4：V1 参数类型集合

建议从可明确编码的最小集合开始讨论：

- `bool`、有符号/无符号整数；
- `float`、`double`；
- 字符、枚举、指针值；
- UTF-8 字符串；
- 是否支持二进制 blob。

数组、容器、嵌套对象和任意自定义类型默认不进入第一版。需要为不支持类型设计清晰的
编译期报错或显式慢路径。

### D5：字符串所有权

- 任意运行时字符串默认必须在 Producer 侧复制进 Ring，不能跨异步边界保存裸
  `string_view`；
- format string、文件名等具有静态生命周期的内容可以保存在 Callsite 元数据；
- 是否提供显式 `static_string`/interned string 优化，必须单独命名，不能从普通
  `string_view` 猜测生命周期。

### D6：参数编码方式

需要比较两条路线：

- 自描述 tagged encoding：每个参数携带类型，动态灵活但增加字节和分支；
- Callsite schema encoding：类型保存在静态元数据，Record 只写值，速度和密度更好，
  但依赖注册表与模板实例化。

可以讨论“静态 fast path + 明确动态 slow path”的双路径，但必须分别 benchmark，不能把
两者的结果混成一个吞吐数字。

## 4. 第一轮讨论必须产出的结果

第一轮结束时只要求形成一份 Record ABI ADR，至少包含：

1. RecordHeader 字段表、偏移、大小、对齐和字节序；
2. Callsite 元数据表和注册/生命周期规则；
3. V1 参数类型与每类编码长度；
4. 字符串所有权与最大长度规则；
5. 版本兼容和未知类型处理；
6. payload 精确长度公式；
7. 正常路径与失败路径伪代码。

在这些项目书面冻结前，不创建 codec 生产实现。

## 5. 冻结后的实现顺序

1. 只实现 `encoded_size()`、`encode()`、`decode()` 与 golden/round-trip 测试；
2. 接入一次 `try_reserve()` 和 Ring 内直接编码，验证失败不污染 Ring；
3. 实现稳定地址 Channel、ThreadLogger 与冷路径注册；
4. 实现单 Backend 公平扫描、NullSink、shutdown 排空和统计守恒；
5. 接入后台 fmt 与可复用输出缓冲区；
6. 实现 Linux 批量 FileSink，正确处理 partial write 与 `EINTR`；
7. 完成游戏服务器 demo 和分层端到端 benchmark。

这些是里程碑二内部步骤，不再拆成新的顶层里程碑。

## 6. 里程碑二最终验收

- 每个 Channel 保持 FIFO；
- `attempted == accepted + dropped`，shutdown 后 `accepted == processed`；
- Producer 热路径无 fmt、稳态分配、锁、阻塞或共享 RMW；
- Sink 不持有已 release 的 Ring view；
- FileSink 正确处理短写和 `EINTR`；普通 flush 与 durable flush 分开；
- 分别报告 Producer 延迟、NullSink、后台格式化、普通写与 durable flush；
- 报告吞吐、P50/P99/P99.9、CPU、RSS、accepted/dropped/processed；
- Ring 微基准只对比 QLog SPSC 与 BQLog SISO；
- 系统级对比 QLog、spdlog async 与 BQLog Text；BQLog Compress 单列。

## 7. 新窗口建议浏览顺序

1. 本文；
2. [里程碑一完成报告](./MILESTONE1_COMPLETION_REPORT_CHS.md)；
3. [V1 决策日志](./V1_DECISION_LOG.md)；
4. `include/qlog/detail/spsc_ring_buffer.hpp` 与 `src/spsc_ring_buffer.cpp`；
5. ADR-005（Handle）与 ADR-006（benchmark 分层）；
6. 再对照 BQLog、NanoLog/fmtlog 与 spdlog 的 Record、参数所有权和异步边界实现。

新窗口的第一句话可以直接使用：

> 阅读 `docs/decisions/MILESTONE2_DESIGN_GUIDE_CHS.md` 和里程碑一完成报告，继续与我商讨
> D1 Callsite 元数据拆分和 D2 RecordHeader，不要开始写 codec。

