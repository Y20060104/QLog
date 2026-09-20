# QLog 里程碑二 Record 实现指南：V1 冻结版

> 2026-09-17 format覆盖：[ADR-016](./ADR-016-v1-bqlog-worker-format.md)优先于本文旧的严格花括号、参数数目匹配、默认文本表示和解析缓存合同。Producer原样copy/hash；worker按BQLog当前UTF-8顺序扫描。当前起点与剩余实施见[剩余V1指南](./V1_REMAINING_IMPLEMENTATION_GUIDE_CHS.md)。本文未被覆盖的wire/参数/长度规则继续有效，历史验收记录不改写为当前实现状态。

> 多 Appender 最新合同：[ADR-012](./ADR-012-v1-multi-appender.md)。一个 Logger 可分发多个目标，配置 reset 支持增删/替换；处理时过滤，各 Text 目标可独立时区。旧文中的单 Sink 流程须按该合同扩展。
> 2026-09-13 当前状态：I1 已按 WSL2 开发范围收口；I2 设计已冻结，生产骨架已开始，尚未验收。合同见 [ADR-011](./ADR-011-v1-producer-channel.md)，执行见 [I2 计划](./MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md) 与 [I2 动手指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)。原生 Linux 发布复核与自动 CI 后续补齐。

- 状态：D1～D6、H1～H4 已冻结；ABI 声明与测试已完成
- 日期：2026-09-05
- Record 基线：[ADR-007：32B 自包含 RecordHeader 与统一格式记录](./ADR-007-self-contained-record-header.md)
- 时间基线：[ADR-008：Unix Epoch 纳秒与 admission timestamp](./ADR-008-realtime-coarse-admission-timestamp.md)
- 参数基线：[ADR-009：V1 参数类型、字符串与 packed tagged arguments](./ADR-009-v1-packed-tagged-arguments.md)
- 格式化基线：[ADR-010：V1 format hash、c20_format、解析缓存与工作量边界](./ADR-010-v1-backend-c20-format.md)
- I1 执行规范：[I1 Record Core 企业级开发规范](./MILESTONE2_I1_RECORD_CORE_DEVELOPMENT_GUIDE_CHS.md)
- 当前动手入口：[I2 Producer/Channel 动手指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)
- 前置交付：[里程碑一完成报告](./MILESTONE1_COMPLETION_REPORT_CHS.md)
- 目标：把 V1 Record、hash、formatter、cache 和工作量合同转换为紧凑的实现与验收任务

## 1. 使用边界

本指南规划以下 V1 链路：

```text
业务线程
  -> ProducerHandle（长期缓存 Channel*）
  -> 每（线程, AsyncLogger）独立固定容量 SPSC
  -> 32B RecordHeader + inline UTF-8 format + packed tagged arguments
  -> 单 BackendWorker 公平扫描
  -> NullSink / 后台 c20_format / TextFileSink
```

剩余 G0 门禁已经由 ADR-010 的 2026-09-05 修订一次性关闭：BQLog 式四路 CRC32C 派生的
`crc32c4x64_v1`、自动索引的有界 `c20_format` 子集、固定 256-entry 解析缓存和 64KiB 完整文本行
均已冻结。输入、字段、spec、width、precision 与输出上限共同限制最坏工作量，不另设重复的 work-unit
计数。I1 Record Core 已完成 WSL2 阶段收口；当前按 ADR-011 进入 I2，不重新实现 codec。

V1 不实现 Compress Sink、二进制文件、离线解析、源码位置、UTF-16/UTF-32、用户 formatter、
CallsiteId、tagless static record、MPSC 或跨线程严格全序。

## 2. 已有 Ring 契约

里程碑二不得改变里程碑一已经冻结的边界：

```cpp
WriteHandle try_reserve(std::size_t exact_payload_bytes) noexcept;
void commit(const WriteHandle& handle) noexcept;
void abort(const WriteHandle& handle) noexcept;

ReadHandle try_read() noexcept;
void release(const ReadHandle& handle) noexcept;
void abandon(const ReadHandle& handle) noexcept;
```

关键不变量：

- Handle 是 16B 被动令牌，不拥有 Ring，也不在析构函数中推进游标；
- Producer/Consumer 各自最多一个 pending handle；
- reserve 成功只借出连续 payload，commit 才以 release 发布；
- acquire 以 acquire 观察已发布 Frame；release 后 view 立即失效；
- `FrameHeader::payload_bytes` 是 Decoder 可访问范围的唯一权威；
- `frame_bytes` 的外层取整不等于 Record 的逻辑 `payload_bytes`；
- 队满默认 `drop_new`，Producer 不等待 Consumer。

## 3. V1 对象与所有权

### 3.1 AsyncLogger

`AsyncLogger` 冷路径拥有或管理：

- Logger identity 和过滤配置；
- Channel 注册/注销与稳定地址容器；
- 单 BackendWorker 及其唤醒/关停状态；
- category 名称表；
- Sink 链；
- 聚合统计和诊断。

V1 不在每条 Record 中保存 Logger ID。一个 Channel 永久绑定一个 `AsyncLogger`。

### 3.2 Channel

一个 Channel 对应：

```text
一个生产线程 + 一个 AsyncLogger + 一个固定容量 SPSC Ring
```

Channel 冷且不可变的元数据至少描述：

```text
magic
ring_frame_abi_version
record_abi_version = 1
record_header_bytes = 32
thread_id / thread_name
logger identity
clock descriptor
hash algorithm / seed
max_payload_bytes
```

同一个业务线程向两个 Logger 写日志时创建两个 Channel。ProducerHandle 稳态直接缓存 `Channel*`，
不得每条日志查询 `(thread, logger)` map。

### 3.3 生命周期

Channel 地址一旦发布，在 Backend 退出且相关 Ring 排空前不得改变。关闭顺序为：

```text
停止新日志调用
  -> 禁止新 Channel 注册
  -> 请求 Backend 排空
  -> 队列排空（验收语义 accepted == processed，不依赖 Release 内置计数）
  -> Backend 与 Sink 退出
  -> 销毁 Channel/Ring/Logger 元数据
```

Record 不含 format/Callsite/thunk/用户对象指针；动态库停止调用 QLog API 后，已发布 Record 可以在
模块卸载后继续处理。

## 4. D1/D2：Record ABI

### 4.1 32B Header

| 偏移 | 大小 | 字段 | 语义 |
|---:|---:|---|---|
| 0 | 8 | `time_value` | Unix Epoch 纳秒 admission timestamp |
| 8 | 8 | `format_hash` | 候选 hash；0 表示 Backend 需要延迟计算 |
| 16 | 4 | `format_bytes` | UTF-8 format 字节数，不含结尾 NUL |
| 20 | 4 | `args_bytes` | packed tagged arguments 精确长度 |
| 24 | 4 | `category_id` | Logger 内动态 category；0 为默认 |
| 28 | 2 | `arg_count` | 参数数量；V1 准入上限 32 |
| 30 | 1 | `level` | 动态 level |
| 31 | 1 | `flags` | 低 2 位 timestamp status，其余位为 0 |

用于 layout test 的说明类型：

```cpp
struct alignas(8) RecordHeader {
    std::uint64_t time_value;
    std::uint64_t format_hash;
    std::uint32_t format_bytes;
    std::uint32_t args_bytes;
    std::uint32_t category_id;
    std::uint16_t arg_count;
    std::uint8_t level;
    std::uint8_t flags;
};
```

必须静态验证：

```text
sizeof(RecordHeader) == 32
alignof(RecordHeader) == 8
offsetof 每个字段与表一致
standard-layout
trivially-copyable
native endian == little
```

该结构仅用于声明布局和构造已对齐局部值。禁止把 Ring 地址直接转换成 `RecordHeader*` 后访问。

### 4.2 逻辑 payload

```text
[32B RecordHeader]
[format_bytes 个 UTF-8 bytes]
[args_bytes 个 packed tagged arguments bytes]
```

V1 内部没有 format-to-args padding、参数 padding 或逻辑 payload 尾部 padding：

```text
args_alignment = 1
format_begin = 32
args_begin = checked_add(32, format_bytes)
payload_bytes = checked_add(args_begin, args_bytes)
```

外层 Ring 仍可为 Frame 回绕/对齐计算更大的 `frame_bytes`；多出的字节不计入 `payload_bytes`。

### 4.3 自包含 format

所有静态和运行时 format 都逐 Record 复制进 Ring。两类 API 的差异只存在于前端检查：

```cpp
QLOG_INFO("player={} hp={}", id, hp);
QLOG_INFO(runtime_format, id, hp);
```

两者使用同一 API 语义和同一 Record。Producer 不解析 format；Backend `c20_format` 统一处理语法、
占位符与参数类型错误。

两者进入 Ring 后结构完全相同。V1 不设置 `dynamic_format` flag，ADR-008 的 `flags` bit 2～7 继续为 0。

静态字面量：

- 编译期取得 `format_bytes = N - 1`；
- 使用与 runtime raw 算法逐位一致的 constexpr 软件参考实现计算 stored hash；
- reserve 成功后稳态只复制 format bytes。

运行时 format：

- 使用显式长度，不隐式 `strlen`；
- reserve 成功并取得 admission timestamp 后，在一次四路 CRC32C copy-and-hash 遍历中同时复制和计算 hash；
- 不得先 hash 再进行第二次完整 memcpy。

空 format 合法；底层接口只在 `format_bytes > 0 && data == nullptr` 时返回非法 metadata，
`format_bytes == 0` 时 data 可以为空。format 不使用 `NullUtf8` 参数 tag 表达。

## 5. D3：时间戳

`time_value` 固定为 Unix Epoch 纳秒形式的 admission timestamp：

```text
primary  = clock_gettime(CLOCK_REALTIME_COARSE)
fallback = clock_gettime(CLOCK_REALTIME)
```

采样点：

```text
try_reserve(exact_payload_bytes) 成功之后
写 Record payload 之前
```

`flags & 0x03`：

| 值 | 名称 | 规则 |
|---:|---|---|
| 0 | `primary_valid` | primary 取得的 epoch ns |
| 1 | `fallback_valid` | fallback 取得的 epoch ns |
| 2 | `time_unavailable` | `time_value` 必须为 0；Record 仍处理 |
| 3 | `reserved` | V1 Producer 不生成；不承担 Record invalid 语义 |

```text
timestamp_status_mask = 0x03
known_flags_mask = 0x03
flags bits 2..7 = 0
```

`time_value` 不用于耗时计算，不保证递增，也不承诺跨线程全局时间顺序。每个 Channel 的 SPSC FIFO
才是线程内顺序权威。

## 6. D4：前端参数准入

### 6.1 支持集合

| 类别 | 输入 | Wire tag |
|---|---|---|
| 布尔 | `bool` | `Bool` |
| 字符 | 普通 `char` | `Char` |
| 整数 | 1/2/4/8B signed/unsigned integral | 对应 `I*/U*` |
| 枚举 | enum underlying type | 对应 `I*/U*` |
| 浮点 | `float` / `double` | `F32/F64` |
| 空指针 | 裸 `nullptr` | `Pointer64(0)` |
| 对象指针 | `qlog::ptr(p)` | `Pointer64` |
| UTF-8 | char/char8_t array、string/view、`qlog::cstr` | `Utf8String/NullUtf8` |

`char`、`signed char`、`unsigned char` 分别映射 `Char/Int8/UInt8`。enum 立即转 underlying type，不设置
独立 enum tag。Pointer64 只保存值，不允许 Backend 解引用。

### 6.2 拒绝集合

编译期拒绝：

- 裸 `char*` / `const char*`；
- 未包装对象指针、函数/成员指针；
- `long double`、128 位整数；
- scalar `char8_t`、宽字符和宽字符串；
- blob、named args、容器、chrono 和用户自定义对象；
- 任何依赖隐式转换、用户 formatter、`format_as` 或回调的类型。

`kMaxArgCount = 32`。静态调用超过上限应编译失败；运行时适配入口必须在 reserve 前拒绝。

## 7. D5：字符串准入与深拷贝

### 7.1 支持输入

```text
const char (&)[N] / const char8_t (&)[N] -> N - 1 bytes
std::string / std::u8string              -> size() bytes
std::string_view / std::u8string_view    -> explicit size bytes
qlog::cstr(ptr)                          -> caller-guaranteed NUL-terminated compatibility path
```

所有输入只借用到本次日志调用结束；commit 前必须完成深拷贝。Producer 不验证 UTF-8、不转码、不保存
结尾 NUL。显式长度字符串允许内嵌 NUL。

字符数组路径要求最后元素是 NUL，并固定编码 `N - 1` 字节；运行时字符数组若要使用首个 NUL 之前的
有效长度，调用方应显式传 `string_view` 或 `qlog::cstr`。

### 7.2 空与 null

```text
"" / std::string_view{}     -> Utf8String(length=0)
qlog::cstr(nullptr)          -> NullUtf8
裸 nullptr                   -> Pointer64(0)
qlog::ptr(nullptr)           -> Pointer64(0)
```

`NullUtf8` 只占一个 tag。Text Sink 把它显示为 `<null>`，但 Ring 不保存这六个显示字节。

`string_view.size() == 0` 时不读取 `data()`；`size() > 0 && data() == nullptr` 是非法 metadata。

### 7.3 qlog::cstr

```text
ptr == nullptr -> NullUtf8，不扫描
ptr != nullptr -> 调用方保证从 ptr 开始可读且存在 NUL
                -> measure 恰好调用一次 strlen
                -> 缓存 byte_length，encode 直接复用
```

`qlog::cstr` 是显式兼容入口。非空指针若不可读或没有可达 NUL，属于调用方违反前置条件，不是
`measure_record` 的可恢复错误。`strlen` 的结果必须先检查能否表示为 u32，再参与 checked aggregate 和
Channel payload 上限判断。V1 不截断字符串；同一次调用只求长一次。

## 8. D6：packed wire protocol

### 8.1 固定 tag

| 值 | Tag | Payload bytes |
|---:|---|---|
| `0x00` | Invalid/reserved | 0；Producer 不生成 |
| `0x01` | Bool | 1 |
| `0x02` | Char | 1 |
| `0x03` | Int8 | 1 |
| `0x04` | UInt8 | 1 |
| `0x05` | Int16 | 2 LE |
| `0x06` | UInt16 | 2 LE |
| `0x07` | Int32 | 4 LE |
| `0x08` | UInt32 | 4 LE |
| `0x09` | Int64 | 8 LE |
| `0x0A` | UInt64 | 8 LE |
| `0x0B` | F32 | 4 LE bit pattern |
| `0x0C` | F64 | 8 LE bit pattern |
| `0x0D` | Pointer64 | 8 LE |
| `0x0E` | Utf8String | `u32_le length + bytes` |
| `0x0F` | NullUtf8 | 0 |
| `0x10..0xFF` | reserved/unknown | V1 Producer 不生成 |

固定值布局：

```text
[u8 tag][1/2/4/8B value]
```

字符串布局：

```text
[0x0E][u32_le byte_length][bytes]
```

### 8.2 encoded size

```text
Bool/Char/Int8/UInt8 = 2
Int16/UInt16         = 3
Int32/UInt32/F32     = 5
Int64/UInt64/F64/Pointer64 = 9
Utf8String(n)   = 5 + n
NullUtf8        = 1
```

所有加法使用 checked arithmetic。先在 `size_t` 中计算，再检查 `format_bytes/args_bytes` 可表示为
`uint32_t`，最后检查 `payload_bytes <= Channel.max_payload_bytes`。

### 8.3 访问规则

Header 和 arguments 都禁止未对齐 typed-pointer 解引用。实现只能：

- 把完整初始化的局部 Header `memcpy` 到 Ring，或按固定偏移 store；
- 用固定宽度 `memcpy`/`load_le` 在 byte cursor 与对齐局部值之间传递；
- float/double 通过 bit pattern 编码；
- 以 subtraction-first 边界检查阻止加法溢出。

packed 仅影响 Record 内部。Ring 原子游标继续独立缓存行对齐，Frame 起点/回绕规则保持不变。

## 9. Producer 实施流程

### 9.1 统一前置阶段

```text
calls++
  -> Logger/Category/Level filter
       filtered++，返回
  -> 编译期类型准入 + 运行时 metadata 检查
  -> 取得 format_bytes 和字符串长度
  -> checked 计算 args_bytes/payload_bytes
  -> 参数数量和 max_payload_bytes 检查
  -> attempted++
  -> try_reserve(payload_bytes) 一次
       full -> dropped_full++，返回
  -> admission timestamp
  -> 直接写 Ring payload
  -> commit
  -> accepted++
```

上述计数为 Debug 条件诊断；Release 由外部测试观测。metadata 非法和 too-large 属于 rejected_pre_admission，
编译期不支持不形成运行调用；所有这些拒绝路径都不读取时钟或 reserve。精确映射见 I2 动手指南 E/F。

参数数量、tag 和固定宽度值的单项大小可在编译期确定；字符串并非都能编译期计长：数组 extent 可为
编译期常量，`std::string[_view]`/`std::u8string[_view]` 从运行期 `size()` 取长度，`qlog::cstr` 则在调用方
保证 NUL 的前提下运行期调用一次 `strlen`。实现应把固定贡献与运行期长度做 checked sum，并复用结果。

### 9.2 唯一 Record 写入路径

V1 不设置“静态 Record”和“动态 Record”两条路径。所有受支持的 format 输入完成长度/metadata 检查
后，都进入以下唯一流程：

```text
measure format/arguments
  -> checked payload_bytes
  -> try_reserve 一次
  -> admission timestamp
  -> 写同一个 32B Header
  -> 复制完整 UTF-8 format
  -> 编码相同的 tagged arguments
  -> commit
```

唯一差异只是 format metadata 从哪里取得：

- 字面量/固定 extent：`format_bytes` 可在编译期取得，hash policy 冻结后可预计算 hash；reserve 成功后
  仍逐 Record `memcpy` format；
- 运行时 view/string：从显式长度取得 `format_bytes`，reserve 成功后在复制的同一次遍历中计算 hash。

这不改变 RecordHeader、flags、arguments ABI 或 Backend 解码。Producer 不解析 `{}`、不匹配占位符，
也不根据 format 决定 tag；tag 和 encoded size 只由参数类型与参数值决定。不注册 Callsite，不读取共享
Callsite 状态，不省略 format 或 argument tags。

所有借用的 format bytes 必须在日志调用期间保持有效。Backend 捕获 format syntax/type mismatch，计入
`format_error`，继续处理下一条 Record。

### 9.3 reserve 后失败

正常 codec 在 reserve 前已完成所有可恢复检查，reserve 后路径必须 `noexcept` 且不分配。若内部不变量
仍导致无法完成写入，必须 `abort(handle)`；不得提交半条 Record。非法用户地址导致的进程级 fault 不在
QLog 可恢复保证内，调用方必须满足借用契约。

## 10. Decoder 实施流程

I1 Decoder 的输入是调用方已经界定的 Record payload 裸指针和显式长度，而不是 Ring 对象或
`FrameHeader`。I3 先以可信的 Frame 几何取得这对值，再调用 Decoder：

```text
const std::byte* record_data + std::size_t record_size
DecodedArg* workspace + std::size_t workspace_count（必须为 32）
immutable RecordValidationPolicy
```

这是 checked Decoder 边界：`record_size > 0 && record_data == nullptr` 返回
`invalid_payload_metadata`；`workspace == nullptr` 或 `workspace_count != 32` 返回 `invalid_workspace`，
两者都必须发生在解析或写入槽位之前。

调用方仍必须保证 record range 真实可读，workspace 指向 32 个已开始生命周期、正确对齐且可写的
`DecodedArg`，并且两范围不重叠；checked Decoder 不能验证悬空或伪造的非空地址。成功 view 同时借用
record bytes 与 workspace，并在 Frame release 或 workspace 下次复用这两个时点中较早者失效。

`RecordValidationPolicy` 只提供 O(1) 的合法 level 集合与 `fallback_timestamp_allowed`；不含 Channel 指针、
回调或动态容器。ADR-011 已冻结 public LogLevel 为 verbose/debug/info/warning/error/fatal=0…5；policy 允许六个合法值，
不能由动态过滤 bitmap 构造。I1 本身仍接受调用方传入的通用 policy。

验证顺序：

```text
1. payload pointer/size metadata
2. workspace pointer/count
3. payload size >= 32
4. memcpy/load Header 到对齐局部值
5. 通过 policy 校验 level
6. 校验 unknown flags、timestamp status/time_value/policy 组合
7. format_bytes <= 8192
8. format_bytes <= payload_remaining_after_header
9. args_bytes == remaining_after_format
10. arg_count <= 32
11. 以 args_end 和 arg_count 双重限制遍历
12. decoded_count == arg_count && cursor == args_end
```

未知 tag 令当前 Record 解码失败。packed item 没有通用 item length，因此不得猜测并跳到下一个参数。

错误必须至少分类为：

- `invalid_payload_metadata`；
- `invalid_workspace`；
- `record_too_small`；
- `invalid_level`；
- `unknown_flags`；
- `reserved_timestamp_status`；
- `invalid_time_value`；
- `fallback_timestamp_not_configured`；
- `format_too_large`；
- `invalid_format_length`；
- `invalid_args_length`；
- `arg_count_exceeded`；
- `invalid_or_unknown_tag`；
- `invalid_bool`；
- `truncated_value` / `truncated_string_length` / `truncated_string`；
- `decoded_count_mismatch` / `trailing_args_bytes`。

这些 Record 边界检查在 Debug/Release 永久开启，不受 `QLOG_ENABLE_RING_VALIDATION` 控制。Decoder 接受
包括 0 在内的任意 `format_hash`，不校验 hash 身份；hash 0 延迟计算和碰撞后的完整字节比较属于 I4。

任何错误都只结束当前 Record。Backend 依靠可信 Frame release 空间并继续扫描。
`format_error` 属于结构解码成功后的 Backend 格式化错误，不是 Decoder 的 wire 错误。

## 11. Backend c20_format 与 Sink

### 11.1 固定工作区与处理顺序

每个 BackendWorker 启动时预分配：

- 固定 32 槽 `DecodedArg` 工作区；
- 恰好 65536B 的私有 Text scratch；
- 256-entry、4-way 私有 `FormatPlan` cache；
- 每 Channel 公平扫描游标和私有诊断计数。

数字解码进局部槽；字符串只在当前 Frame 尚未 release 时借用 Ring bytes。Text 路径固定为：

```text
验证 FrameHeader / RecordHeader 并建立全部安全的指针-长度范围
  -> 获取或解析 FormatPlan
  -> 恰好解码 arg_count 个 tagged arguments，并验证 plan/type
  -> 在私有 64KiB scratch 生成 metadata prefix + message + '\n'
  -> 全部成功后把完整行交给 Sink 自有内存 batch
  -> 所有成功/失败路径 release Frame
  -> 只有在持帧区间外才允许 write/fdatasync 等阻塞 I/O
```

Sink 的“接收”只表示复制/移动到自己的内存 batch，不等于完成系统调用。batch 必须能接纳一条最大行；
容量整理或 flush 在取得下一条 Frame 前进行。Sink 接收返回后不得借用 scratch，formatter/Sink 都不得保留
已 release 的 Ring view。NullSink 可在必要 Header/Frame 验证后跳过 plan、参数解码与格式化。

只有完整 scratch 行才能提交。语法、类型或容量错误丢弃 scratch、分类计数并 release；不截断、不写替代行、
不让异常穿出 worker。该策略保证 Sink batch 不收到半行，但文件 `write()` 发生永久错误时仍可能留下部分文件尾。
TextFileSink 必须处理 short write 和 `EINTR`；普通 `flush/drain` 与 `fdatasync/fsync` durability 分开命名测试。

### 11.2 精确语法

```text
format            := (literal-byte | "{{" | "}}" | replacement-field)*
replacement-field := "{}" | "{:" format-spec "}"
format-spec        := [[fill]align][sign]["#"]["0"][width]["." precision][type]
literal-byte       := any byte except '{' and '}'
align              := "<" | ">" | "^"
sign               := "+" | "-" | " "
width              := [1-9][0-9]*
precision          := [0-9]+
```

只支持自动索引，字段数必须恰好等于 `arg_count`。`{{`/`}}` 是唯一 brace 转义；孤立 brace 失败。
明确接受：

```text
{}
{:<16}
{:>16}
{:^16}
{:+08d}
{:#x}
{:.6f}
{:12.4g}
{{literal}}
```

明确拒绝并保留独立错误原因：

```text
{0}       -> explicit_index_not_supported
{name}    -> named_index_not_supported
{:{}}     -> dynamic_width_not_supported
{:.{}f}   -> dynamic_precision_not_supported
```

V1 自定义 fill 是一个 printable ASCII byte `0x20..0x7E`，不能为 brace，并且必须紧跟 `<`/`>`/`^`。
默认 fill 为空格。width 范围 `1..4096`，是最小输出 byte 数而非 Unicode code point/显示列；居中的奇数
padding 左少右多。文本默认左对齐，数字和 pointer 默认右对齐。

`#` 只允许整数 `b/B/o/x/X`，前缀分别为 `0b/0B/0/0x/0X`；sign 位于 prefix 前。
`0` 只允许整数、浮点和 Bool-`d`，必须有 width，不能与显式 align/custom fill 共存，并在 sign/prefix 后补零。
例如 `"{:+08d}", 42 -> "+0000042"`，`"{:#08x}", 42 -> "0x00002a"`。

### 11.3 tag/type 映射

| ArgumentTag | 允许 type | 默认/特殊语义 |
|---|---|---|
| `Bool` | 省略、`s`、`d` | 省略/`s` 为 `true/false`；`d` 为 `1/0` |
| `Char` | 省略、`c` | 一个原始 byte |
| signed/unsigned integer | 省略、`d/x/X/o/b/B` | 省略为十进制；signed 非十进制使用 sign+magnitude |
| `F32/F64` | 省略、`f/F/e/E/g/G` | 见下述浮点规则 |
| `Pointer64` | 省略、`p` | 省略等价 `p`；拒绝 `P/x/X/d` |
| `Utf8String` | 省略、`s` | 输出全部 bytes；precision 拒绝 |
| `NullUtf8` | 省略、`s` | `<null>` |

Pointer64 始终输出小写 `0x` 加最短 hex，0 为 `0x0`。裸 nullptr 是 Pointer64(0)；
`qlog::cstr(nullptr)` 是 `NullUtf8`；`qlog::cstr("")` 是长度 0 的 Utf8String，三者不可混淆。
裸 `char*`/`const char*` 继续拒绝。

浮点 `f/e` precision 表示小数位，`g` 表示有效位，范围 `0..64`；`g` 的 0 按有效 precision 1。
省略 type 但存在 precision 时按 `g`；显式 float type 无 precision 时使用 6；type/precision 都省略时使用
原始 F32/F64 的 shortest round-trip general。C++20 `std::to_chars` 是数值转换基线；`F/E/G` 大写
指数标记和 inf/nan，保留负零，不支持浮点 `#`。

字符串 precision 明确不支持。format/string bytes 可含 NUL，V1 不验证 UTF-8；width 只按输出 bytes。

### 11.4 固定限额

```text
format bytes             <= 8192
replacement fields       <= 32
arg_count                <= 32
format-spec bytes/field  <= 32
width                    <= 4096
floating precision       <= 64
complete Text line       <= 65536 bytes
```

完整行上限包含 metadata prefix 和换行。scratch/Sink 用裸 `{data, size}` 对，不使用 `std::span`，也不需要结尾 NUL。
所有十进制与长度计算使用 checked arithmetic，写入前检查容量。
Parser 不递归、不回溯、不支持嵌套/dynamic spec，也不重新扫描完整 format；这些限制与输出上限共同形成
最坏工作量边界，不另设 `kMaxFormatWorkUnits`。

BQLog 同样在 Backend 扫描占位符；QLog 只对照其职责与性能，自研严格 parser，不复制其宽松 brace 恢复、
4B type cell、align4 参数布局或未对齐 typed-pointer 解引用。`c20_format` 是独立 Backend 层，不进入 codec。

## 12. format hash/cache 规则

V1 hash 固定为 `crc32c4x64_v1`。其 raw 算法使用四个初值为 `0xFFFFFFFF` 的 CRC32C lane，
反射 Castagnoli 多项式为 `0x82F63B78`；按 ADR-010 的长短输入分块规则更新，最后：

```text
low         = h1 xor rotl32(h3, 17)
high        = h2 xor rotl32(h4, 19)
raw_hash    = (uint64(high) << 32) | low
stored_hash = raw_hash == 0 ? 1 : raw_hash
```

- 输入是恰好 `format_bytes` 个 bytes，不包含 NUL，不加入 category/level/random seed；
- `>=32B` 的尾部不足整块时必须重读以输入末尾结尾的 32B 重叠窗口，这是算法身份；
- constexpr reference、SW/HW hash-only 与 SW/HW copy-and-hash 必须逐位一致；
- runtime copy-and-hash 在成功 reserve 和 admission timestamp 后一次完成复制/hash；
- copy 路径遵守 non-overlap 和目标容量前置条件；hash-only 不得对 null 目标指针做算术；
- V1 正常 Producer 写非零 stored 值；Header hash 为 0 时 Backend 延迟计算并规范化；
- hash 只能定位候选项，命中必须比较 hash、长度与完整 format bytes。

Core 冷路径工厂检测硬件 CRC 能力并只返回成功构造的 immutable dispatch；automatic 模式始终可回退
software，test-only forced-hardware 不可用时返回 `backend_unavailable` 且不产生 dispatch。Producer 稳态
不重复检测能力，也不执行共享原子 RMW。通用 AArch64 binary 只有在独立 CRC-enabled TU/function target
与 HWCAP 检测都可靠时启用硬件路径，否则固定软件路径。

Text formatter 的 parse cache 不包含 category/level，因为语法计划只依赖 format：

```text
256 entries
64 sets * 4 ways
per-set round-robin
maximum cached format = 256B
fixed plan <= 32 replacement fields
```

entry 保存完整 format 副本并在 hash 命中后比较；plan 只保存 offset/spec，不保存 Ring 指针或参数 tag。
超长 format 有界解析但不缓存。缓存由 BackendWorker 私有并在启动时预分配，不做 LRU 或动态扩容。
未来 Compress Sink 的模板表可把 category/level 纳入身份，但它不是本缓存。

## 13. 计划文件边界

建议后续实现文件；本次不创建：

```text
include/qlog/detail/record_header.hpp
include/qlog/detail/argument_tag.hpp
include/qlog/detail/checked_size.hpp
include/qlog/detail/format_hash.hpp
include/qlog/detail/record_types.hpp
include/qlog/detail/record_encoder.hpp
include/qlog/detail/record_decoder.hpp
src/record_decoder.cpp

include/qlog/arguments.hpp          // ptr/cstr 显式包装器
include/qlog/async_logger.hpp
src/async_logger.cpp
src/channel.cpp
src/backend_worker.cpp
src/c20_format.cpp
src/format_plan_cache.cpp
src/sink_null.cpp
src/sink_text_file.cpp

tests/record_layout_test.cpp
tests/record_codec_golden_test.cpp
tests/record_codec_roundtrip_test.cpp
tests/record_codec_corruption_test.cpp
tests/format_hash_test.cpp
tests/c20_format_test.cpp
tests/format_plan_cache_test.cpp
tests/producer_integration_test.cpp
tests/backend_sink_test.cpp
benchmarks/record_codec_benchmark.cpp
benchmarks/async_logger_benchmark.cpp
```

依赖方向必须保持：

```text
public API -> argument normalization/size -> record codec -> SPSC bytes
Backend -> record decoder -> c20_format -> Sink
```

RingBuffer 不知道日志类型、format grammar、category 或 Sink。
I1 内部文件边界于 2026-09-09 细化：types 不依赖 measure/hash/traits；encoder 模板头消费 prepared/hash；
decoder 声明头与 `.cpp` 只依赖公共类型、Header/tag/limits。原 record_codec 文件建议由此取代。

## 14. 紧凑实现任务

不再为单个小 helper 建立一轮。里程碑二只保留以下五个可验收任务，其中 I0 已完成。

### I0：ABI 与设计基线（已完成）

交付 `RecordHeader`、`ArgumentTag`、静态断言、自包含编译测试、layout/wire-value 测试，以及
ADR-007～ADR-010。退出条件是 Debug/Release 全测试通过且 `git diff --check` 无错误。

### I1：独立 Record Core

企业级接口、错误、测试、工具链和退出合同以
[I1 Record Core 企业级开发规范](./MILESTONE2_I1_RECORD_CORE_DEVELOPMENT_GUIDE_CHS.md) 为唯一执行入口。

一次完成 format hash（reference/SW/HW、hash-only/copy-and-hash）、`ptr/cstr` 包装器、argument
traits/normalization、checked measure、普通裸指针加显式长度的
encode/decode，以及 known-vector、golden bytes、round-trip、全部截断点和 compile-fail 测试。此任务不接 Ring，
不实现 formatter。I1 定义 `DecodeResult`、`DecodedArg` 和 `DecodedRecordView`；它们不是 I3 的新协议。

I1-A 的实现归属和不变量以企业级规范第 5.1、6.1、7.4、8.1 节为准：冻结的 `ArgumentTag::Invalid`
不得改名为 compile-time `Unsupported`；`ArgumentKind/match_argument_kind<T>()` 位于
`argument_traits.hpp`；`PreparedRecord<N>` 位于 `record_measure.hpp` 并使用显式 private 构造；最终
`encoded_size` 根据 normalized `ArgumentTag` 计算。该分层是 compile-time admission、runtime
normalization 与 wire encoding 的边界，不得为减少类型数量而合并。

I1 已按 WSL2 开发范围收口，详见 [I1-D 验收](./I1D_ACCEPTANCE_20260913_CHS.md)；不重复启动旧 hash/decoder 待办。

### I2：Producer 与 Channel 链路

一次完成稳定地址 Channel、ProducerHandle/AsyncLogger 冷路径绑定、动态 category/level 过滤、精确计长、
一次 reserve、admission timestamp、Ring 内直接编码、commit/abort 和条件诊断统计；执行合同见 ADR-011 与 I2 动手指南。

退出条件：同线程双 Logger 使用独立 Channel；失败不污染 Ring；filtered/invalid/too-large/full 路径符合时钟与
计数合同；稳态无 map、锁、分配、阻塞或共享 RMW。

### I3：Backend 与 NullSink

一次完成多 Channel 公平扫描、每个 BackendWorker 拥有的固定 `DecodedArg[32]` workspace、调用 I1 Decoder、
NullSink、错误统计与隔离、Frame release、唤醒/排空、shutdown 和统计守恒。I3 不重复定义 I1 的 decode 类型。

退出条件：1～128 Channel 压测无饥饿；损坏 Record 只影响自身；shutdown 后 `accepted == processed`，
生命周期测试不保留已 release view。

### I4：Text 路径与正式验收

一次完成 `c20_format` parser、固定 parse cache、64KiB scratch、Sink 内存 batch、TextFileSink write loop、格式错误/容量
隔离、游戏服务器 demo 和分层 benchmark。

退出条件：ADR-010 全部 grammar/cache/boundary tests、短写/EINTR、Debug/Release/Sanitizer、Release 汇编、
tail latency、吞吐、CPU、RSS 与 dropped/processed 数据齐全；QLog Text 只与 BQLog Text 同口径比较。

## 15. 测试矩阵

### 15.1 D4/D5 正常输入

- 所有整数宽度和符号；enum underlying 映射；
- bool、char、正负零、边界整数；
- F32/F64 普通值、正负零、无穷和 NaN bit pattern；
- Pointer64 零值和非零值；
- 空字符串、短字符串、内嵌 NUL、u8 输入；
- `qlog::cstr(nullptr)`、空 C 字符串、普通 NUL 结尾 C 字符串；
- 0、1、31、32 个参数。

### 15.2 Producer 拒绝

- 不支持类型的 compile-fail tests；
- 33 个参数；
- 最后元素不是 NUL 的受支持字符数组在 reserve 前返回运行时 `invalid_string_metadata`；
- 非零长度 null view；
- string/args/payload 长度溢出和超过 Channel 上限；
- null runtime format。

不可读指针或没有可达 NUL 的 `qlog::cstr` 输入违反调用方前置条件，不属于可恢复拒绝测试。

### 15.3 Decoder 损坏矩阵

- payload 0～31B；
- 每个 Header 长度边界和溢出形状；
- flags bit 2～7 任一置位；
- timestamp status 3 与 unavailable/nonzero time；
- arg_count 33 和 65535；
- tag 0、未知 tag、非法 bool；
- 每种 fixed value 的 0～width-1 截断；
- string length prefix 各截断点、超长、尾随 bytes；
- decoded count 少/多于 Header；
- format hash 为 0 或与 format bytes 不匹配时仍可结构解码，Decoder 不做 identity validation。

### 15.4 Hash 与 formatter

- hash raw known vectors以及 raw 0 到 stored 1；
- hash 长度 `0..128`、`31/32/33`、`63/64/65`、`255/256/257`；
- source/destination 多种非对齐 offset、前后 canary 与固定 seed 随机长输入；
- SW hash-only/copy-and-hash，支持机器上的 HW 两路径，以及 constexpr reference 全部一致；
- 所有明确接受/拒绝的 format，精确错误分类与字段/参数恰好消费；
- 每种 tag/type、sign、`#`、`0`、prefix/padding 顺序和 center 奇数 padding；
- float shortest、precision 0/1/6/64/65、负零、inf/nan；
- width 4095/4096/4097，spec 31/32/33，format 8191/8192/8193；
- 完整行 65535/65536/65537，metadata prefix/message/newline 全部计入；
- cache hit/miss/collision/eviction/oversize bypass，碰撞必须比较完整 bytes；
- 所有成功/失败路径恰好 release 一次，阻塞 I/O 不发生在持帧区间。

### 15.5 生命周期与并发

- 字符串源在 commit 返回后立即销毁/修改，Record 内容仍正确；
- release 前 string view 有效，release 后 Sink 不保存 view；
- 动态库停止调用后卸载，已发布 Record 仍可处理；
- 1/2/4/8/16/32/64/128 Channel 公平扫描；
- shutdown 与满队列、错误 Record、Sink failure 并发。

## 16. 统计守恒

ADR-011 冻结：Debug 启用对应诊断，普通 Release 编译移除字段与更新，不提供假零统计 API。
对有效 Handle、调用已终结且计数未溢出的验证区间：

```text
calls == filtered + rejected_pre_admission + attempted
attempted == accepted + dropped_full + failed_after_attempt
shutdown drain 后 accepted == processed
processed == sink_success + sink_failure + decode_failure + format_failure
```

最后两式由 I3/I4 的终结语义验收；I2 不声称已完成 Backend。
invalid_handle 无 Channel，由测试驱动单独观察；编译期拒绝不产生运行计数。
measure 阶段内部错误归 pre-admission；非 full reserve 错误和 encode_abort 归 failed_after_attempt。
Debug 停止后聚合；Release 使用调用结果、Record ID 与测试 consumer 外部核对。
shutdown 按无 Producer 且队列排空实现，不依赖已编译移除的 accepted/processed 计数。
诊断宏需在 public 模板/库之间一致传播，不改变 Ring/decoder 校验合同。

## 17. 性能门禁

Producer 基准至少覆盖：

- 静态短 format + 两个整数；
- 静态游戏日志：整数、float、短字符串混合；
- runtime format copy-and-hash；
- `qlog::cstr` 单次 `strlen` 兼容路径；
- 32 参数上限；
- Ring 接近满和持续 `drop_new`。

分别报告：

```text
attempted / accepted / dropped
payload bytes / format bytes / args bytes
P50 / P99 / P99.9 / max producer latency
Backend throughput
Ring high-water mark
CPU / RSS
```

对齐验证：

- x86-64 与目标 AArch64 分别测试 packed decode；
- 检查 Release 汇编不存在逐字段 helper call 或未预期分配；
- 不使用未对齐 typed-pointer 版本作为正式候选；
- format hash、c20_format、write、fdatasync 分层计时。

外部比较必须同工作量、同复制边界、同 Sink、同 durability、同 accepted/dropped 口径。QLog Text 只与
BQLog Text 比较；BQLog Compress 单列。

## 18. 下一实现入口

I1 已按 WSL2 开发范围收口，下一步按 [ADR-011](./ADR-011-v1-producer-channel.md) 和
[I2 执行计划](./MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md) 实现 Producer/Channel。
逐文件、逐函数步骤见 [I2 动手指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)。
维护者提供生产实现及生产接线，Codex 负责测试与验证；后续用户授权优先。
当前只有文档交付，I2 生产实现、测试矩阵与性能基线尚未完成。
