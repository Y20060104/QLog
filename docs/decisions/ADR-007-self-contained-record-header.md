# ADR-007：32B 自包含 RecordHeader 与统一格式记录

- 状态：已接受（本文冻结 D1/D2；D3 由 ADR-008 冻结；D4～D6 由 ADR-009 冻结；Backend formatter 由 ADR-010 取代原 fmt 决定）
- 日期：2026-09-02
- 影响范围：QLog V1 Ring Record、Channel 元数据、Producer/Backend 边界
- 取代：V1 决策日志中关于静态参数 schema、进程级 `CallsiteId`、首次使用注册及静态 category 的方案
- 不改变：SPSC Ring 的 `FrameHeader`、连续 payload、`drop_new`、16B 被动 Handle 与 acquire/release 协议

## 背景

QLog V1 选择更接近 BQLog 的工程路线：Ring 中的每条日志都应在脱离调用模块后独立解码，
所有 format 来源使用同一种内存记录格式。V1 不再依赖进程级 Callsite 注册表、格式化 thunk
或动态库中的只读数据。

这一选择主动接受逐条复制格式串、保存参数类型标签和更大 RecordHeader 的成本，换取：

- 任意受支持的 UTF-8 format、动态 category 和动态 level 使用统一路径；
- Backend 不依赖 Callsite 生命周期；
- 已发布 Record 可以在调用模块卸载后继续处理；
- 后续 Compress Sink 可以仅在 Backend/文件层建立模板表。

## 决策

### 1. Channel 身份

一个 Channel 永久绑定：

```text
一个生产线程 + 一个 AsyncLogger
```

Channel 元数据保存线程身份、Logger 身份、Ring Frame ABI、Record ABI、RecordHeader 大小、
时钟域和哈希策略。这些稳定字段不逐 Record 重复保存，同一 Channel 生命周期内也不允许混用版本。

同一个线程向两个 Logger 写日志时，使用两个不同 Channel。主热路径通过长期
`ProducerHandle` 直接缓存 `Channel*`；每条日志不得查询 `(thread, logger)` map。

### 2. 不使用 Callsite 注册表

V1 不设置：

- 进程级 `CallsiteId`；
- 调用点原子 ID 缓存；
- Callsite 首次注册或显式预注册；
- Callsite 地址稳定要求；
- 参数 schema 注册表；
- 后台格式化 thunk；
- module token 与 Callsite 卸载屏障。

静态格式串与动态格式串都被复制进 Ring。参数类型信息随每条记录保存。

### 3. RecordHeader

V1 RecordHeader 固定为 32B、8B 对齐：

| 偏移 | 大小 | 字段 | 语义 |
|---:|---:|---|---|
| 0 | 8 | `time_value` | Unix Epoch 纳秒的 admission timestamp；见 ADR-008 |
| 8 | 8 | `format_hash` | `crc32c4x64_v1` 的存储值；Backend 缓存和未来模板表的候选键 |
| 16 | 4 | `format_bytes` | UTF-8 格式串字节数，不含结尾 `\0` |
| 20 | 4 | `args_bytes` | tagged arguments 区精确字节数 |
| 24 | 4 | `category_id` | Logger 内逐 Record 动态类别；0 表示默认类别 |
| 28 | 2 | `arg_count` | 参数数量，与实际解码数量必须一致 |
| 30 | 1 | `level` | 逐 Record 动态日志级别 |
| 31 | 1 | `flags` | 低 2 位为 timestamp status；其余位为 0；见 ADR-008 |

用于说明布局的类型草案为：

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

实现阶段必须固定：

```text
sizeof(RecordHeader) == 32
alignof(RecordHeader) == 8
offsetof(...) 与字段表一致
RecordHeader 为 standard-layout、trivially-copyable
```

这不改变 `WriteHandle` / `ReadHandle` 的 16B 被动令牌布局；Handle 只描述一次 Ring 借用，
RecordHeader 是写入 Ring payload 的日志数据。

### 4. Record Payload

逻辑布局固定为：

```text
[ 32B RecordHeader ]
[ format_bytes 个 UTF-8 字节 ]
[ args_bytes 个 tagged arguments 字节 ]
```

格式串不要求 `\0` 结尾。ADR-009 已冻结 `args_alignment = 1`：format 与 arguments 之间、参数之间
以及 Record 逻辑 payload 尾部都没有内部 padding。外层 Ring 仍可根据 Frame 契约对 `frame_bytes`
取整，但取整字节不属于 `RecordHeader::args_bytes` 或 Ring `FrameHeader::payload_bytes`。

外层 Ring `FrameHeader::payload_bytes` 是唯一可访问边界权威。I3 先据此建立
`(const std::byte* record_data, std::size_t record_size)` 裸指针与显式长度范围，I1 Codec 只消费这对值，
不依赖 Ring/Frame/Channel 类型。I1 及后续 Record/Backend 生产接口不得使用 `std::span`；可写范围同理使用
`(std::byte* data, std::size_t size)`。Codec 必须验证：

```text
format_begin = sizeof(RecordHeader)
args_begin = checked_add(format_begin, format_bytes)
args_begin <= ring_payload_bytes
args_bytes == ring_payload_bytes - args_begin
decoded_arg_count == arg_count
```

所有加法和对齐计算必须先做溢出检查。Header 内长度只能在外层边界内使用。
Decoder 必须同时受 `args_bytes` 和 `arg_count` 限制。合法 level 集合和 Channel 是否允许 fallback 通过
窄、不可变、纯值的 `RecordValidationPolicy` 注入；I1 不包含 Channel 指针或自行假设 level wire 数值。
非法 level、未知 flags 高位、reserved
timestamp status、未知 tag、长度越界或计数不符都必须分类处理，依靠可信 Ring Frame release
空间后继续扫描；不得越界、无限循环或终止 Backend。timestamp status 值 `3` 只命名为
`reserved`，不承担 Record invalid 语义。

### 5. format_hash

`format_hash` 不代表格式身份，只用于定位候选项。Text formatter 的语法解析缓存命中后必须比较：

```text
format_hash
+ format_bytes
+ 完整 format 内容
```

category/level 不改变语法 plan，因此不进入 Text parse cache key。未来 Compress Sink 若把 format、category、
level 共同定义为模板身份，则在上述完整字节验证后额外比较 `category_id + level`；两张缓存不得混称。

V1 只有一条 Producer Record 写入路径。字面量来源的长度可由 extent 在编译期取得，hash 可由与运行时
逐位一致的软件参考实现 constexpr 计算；运行时 view 使用显式长度，并在复制进 Ring 的同一次四路 CRC32C
遍历中更新 hash，不得先完整扫描 format 再单独复制。两者只是 format metadata 的取得方式不同；每条
Record 都包含完整 format bytes。

`format_hash == 0` 表示 Producer 没有提供可用 hash；需要 hash 的 Backend 必须根据 Record 内的
format bytes 延迟计算。ADR-010 已固定 BQLog 式四路 CRC32C 派生算法 `crc32c4x64_v1`，并把原始
结果 0 规范化为 1，因此正常 V1 Producer 路径必须写非零 hash。hash 只定位候选项，Backend 始终继续
比较长度和完整字节。

### 6. format API 语义

字面量、`std::string[_view]` 与 `std::u8string[_view]` 只在取得长度/hash 的方式和借用契约上不同，
不构成两套 API、Producer 或 Backend 路径：

```cpp
LOG_INFO("player {} entered scene {}", player_id, scene_id);
LOG_INFO(runtime_format, player_id, scene_id);
```

两者最终进入同一条 measure/reserve/write/commit 流程，format 语法与参数匹配统一由 Backend
自研 `c20_format` 处理；Record 不设置 `dynamic_format` flag。Producer 可利用字面量 extent/hash，
但不解析占位符，也不执行最终文本格式化。

```cpp
LOG_INFO("{}", dynamic_text);
```

表示把 `dynamic_text` 作为普通字符串参数，不会再次解释其中的占位符。

### 7. category、level 与过滤

`category_id` 和 `level` 都是逐 Record 动态字段。它们在写入 Ring 前完成 O(1) 过滤：

```text
Logger/Category/Level 过滤
  -> 计算 format/arguments 精确长度
  -> try_reserve 一次
  -> 成功后采集 admission timestamp
  -> 直接编码到 Ring
  -> commit
```

被过滤事件不复制格式串、不计算动态参数编码长度，也不计入 attempted/accepted/dropped
守恒式；应单独计入 `filtered`（若启用该统计）。

### 8. 生命周期与动态库卸载

Ring Record 不保存格式串指针、Callsite 指针、格式化函数指针或用户对象引用。字符串参数
必须深拷贝；自定义对象必须在 Producer 侧转换为已支持的基本类型或字符串。

模块卸载前仍必须停止模块对日志 API 的调用，但无需仅为了已发布 Record 中的 Callsite/thunk
等待排空。`AsyncLogger`、Channel、category 名称表和 Backend 自身仍必须存活到相关记录处理完毕。

### 9. 版本、字节序与持久化边界

Record 不逐条保存 ABI 版本。Channel 注册时一次性携带并校验：

```text
magic
ring_frame_abi_version
record_abi_version = 1
record_header_bytes = 32
clock_domain
clock_descriptor_version
hash_algorithm
```

`ring_frame_abi_version` 只描述 `FrameHeader` 与 Ring 借用协议，`record_abi_version` 只描述
Ring payload 内的 Record；二者不得复用成一个含义含混的版本号。Backend 在 Channel 注册时校验
一次，不逐 Record 分支。

V1 只支持 little-endian host，编译期使用 `std::endian::native == std::endian::little` 拒绝其他
平台。Producer 可把已对齐局部整数通过固定宽度 `memcpy`/`store_le` 写入 Ring，不增加逐字段
换字节开销。禁止把 Ring 字节地址直接转换为 `RecordHeader*` 或未对齐的整数/浮点指针后解引用；
golden bytes 必须保持稳定。未来二进制文件使用独立 `BinaryFileHeader` 冻结文件版本、字节序、时间源和哈希算法；
Ring ABI 与 File ABI 不是同一协议，也不能直接把 `RecordHeader` 结构体落盘当作文件协议。

### 10. V1 范围

V1 Sink 仅包含 NullSink 与 TextFileSink。V1 只支持 UTF-8，不支持：

- Compress Sink、二进制文件或离线解析；
- UTF-16/UTF-32 格式串；
- file/function/line 源码位置；
- 日志截断；
- 异步保存用户自定义对象；
- 跨线程严格全序。

`format_hash` 在 V1 用于 Backend 格式缓存观测，并为 V2 Compress Sink 留出稳定输入；
模板编号只属于未来文件/文件序列，不进入 Ring。

## 性能约束

采用本 ADR 后，Producer 稳态仍必须满足：

- 无锁、无阻塞、无共享原子 RMW；
- 无格式解析和稳态堆分配；
- 不做 Logger/Channel map 查询；
- 精确计算 payload，只调用一次成功的 `try_reserve()`；
- 直接写入 Ring，不构造完整中间 Record；
- 队满 `drop_new`，不等待 Consumer。

32B RecordHeader 与逐条格式串复制会增加 Ring 带宽和容量压力，这是本路线主动接受且必须
测量的代价。字面量来源可通过编译期长度/hash 把稳态工作压缩为一次 format `memcpy`；运行时 view
通过四路 CRC32C copy-and-hash 合并遍历。这是同一写入路径中的 metadata 优化。是否达标由 Producer
P50/P99/P99.9、后台吞吐、Ring 高水位和
dropped 数据决定，不能只凭字段布局宣称高性能。CallsiteId/tagless static record 仅作为 V2
独立实验，不进入 V1 ABI。

## 后果

### 正面

- 所有 format 来源使用统一 Record；
- Record 脱离调用模块后仍可独立解码；
- 没有 Callsite 注册和卸载生命周期耦合；
- 动态 category/level 更适合游戏服务运行时诊断；
- 后续 Compress Sink 可在 Backend 独立建立文件内模板。

### 代价

- 每条记录重复复制 format 和参数类型；
- 相比 16B Callsite Header，每条记录多写 16B Header；
- Backend 需要校验 tagged arguments 并处理动态格式错误；
- 不再提供 V1 源码位置；
- 需要严格控制 Channel 数量和固定 Ring 内存总量。

## 后续冻结状态

D4～D6 已由 [ADR-009：V1 参数类型、字符串与 packed tagged arguments](./ADR-009-v1-packed-tagged-arguments.md)
冻结；hash、Backend `c20_format`、解析缓存和工作量上限已由
[ADR-010](./ADR-010-v1-backend-c20-format.md) 冻结。Record 设计门禁已全部关闭。

I1 的模块边界、policy、错误合同与测试门禁见
[I1 Record Core 企业级开发规范](./MILESTONE2_I1_RECORD_CORE_DEVELOPMENT_GUIDE_CHS.md)。

D3 的完整时间语义、flags 状态和性能门禁见
[ADR-008：Unix Epoch 纳秒与 admission timestamp](./ADR-008-realtime-coarse-admission-timestamp.md)。
