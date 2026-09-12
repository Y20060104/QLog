# ADR-009：V1 参数类型、字符串与 packed tagged arguments

- 状态：已接受（冻结 D4～D6；I1 Record Core 可按企业级开发规范实现）
- 日期：2026-09-03
- 前置决策：[ADR-007：32B 自包含 RecordHeader 与统一格式记录](./ADR-007-self-contained-record-header.md)
- 时间决策：[ADR-008：Unix Epoch 纳秒与 admission timestamp](./ADR-008-realtime-coarse-admission-timestamp.md)
- 格式化决策：[ADR-010：V1 format hash、c20_format、解析缓存与工作量边界](./ADR-010-v1-backend-c20-format.md)
- 影响范围：Producer 参数准入、字符串所有权、Ring Record payload、Backend 解码与格式化参数视图
- 不改变：32B RecordHeader、逐 Record UTF-8 format、动态 category/level、每线程每 Logger SPSC、Null/Text Sink

## 背景

ADR-007 已冻结 BQLog 式自包含 Record：静态格式字面量和运行时格式串都复制进每条 Ring Record，
参数类型也随 Record 保存。V1 不使用 `CallsiteId`、Callsite 注册表、模块内 format 指针或格式化 thunk。

BQLog 的常规路径同样在 Producer reserve 后把 format 字节复制进 Ring，并在复制过程中计算 hash；
Compress Sink 的模板去重发生在 Backend/文件层。QLog 因此保留自包含路线，同时通过编译期取得静态
format 长度/hash、运行时 copy-and-hash 和紧凑参数布局减少热路径成本。

对应 BQLog 源码锚点为：`include/bq_log/misc/bq_log_impl.h:259-283` 计算长度、调用 write-begin、
编码参数并 commit；`src/bq_log/api/bq_log_api.cpp:216-264` 把 format 长度计入 Record 并执行
`bq_memcpy_with_hash()`；`src/bq_log/log/appender/appender_file_compressed.cpp:377-396` 在 Backend
读取 Record 内 format/hash 并查询模板缓存。这些锚点用于说明复制边界，不直接规定 QLog ABI。

D4～D6 需要冻结三件事：哪些 C++ 参数可进入异步边界、字符串如何取得长度并深拷贝，以及每个参数
在 Ring 中的精确字节协议。

## 决策摘要

```text
D4 = 固定基本类型集合；拒绝隐式用户转换和 Producer 侧 formatter
D5 = 字符串调用期间借用，commit 前深拷贝；裸 const char* 默认拒绝
D6 = [u8 tag][紧随 payload] 的 packed little-endian 协议

kMaxArgCount = 32
args_alignment = 1
```

V1 只有一条 Producer Record 写入路径。无论 format 来自字面量还是运行时 view，都使用完全相同的
Record ABI：

```text
[32B RecordHeader][format_bytes][packed tagged arguments]
```

所有调用都逐 Record 复制 format 并携带 per-argument tag。format 来源只影响取得长度/hash 的方式，
不形成两套 API 语义、编码或解码路径。V1 不实现 tagless payload。

## D4：V1 参数类型集合

### 1. 直接支持的值类型

| C++ 输入 | Wire 类型 | 规则 |
|---|---|---|
| `bool` | `Bool` | 只编码 0 或 1 |
| 普通 `char` | `Char` | 保留 1B 字节值；语义不同于 Int8/UInt8 |
| `signed char` / `std::int8_t` | `Int8` | 有符号 8 位 |
| `unsigned char` / `std::uint8_t` | `UInt8` | 无符号 8 位 |
| 其他有符号整数 | `Int16/Int32/Int64` | 按实际宽度映射 |
| 其他无符号整数 | `UInt16/UInt32/UInt64` | 按实际宽度映射 |
| `float` | `F32` | IEEE-754 binary32 bit pattern |
| `double` | `F64` | IEEE-754 binary64 bit pattern |
| enum | 对应整数 tag | Producer 立即转换为 underlying type |
| `std::nullptr_t` | `Pointer64(0)` | 裸 `nullptr` 不解释为字符串 |
| `qlog::ptr(object_pointer)` | `Pointer64` | 仅保存数值，不跨线程解引用 |
| UTF-8 字符串输入 | `Utf8String` 或 `NullUtf8` | 规则见 D5 |

标准整数类型可以是 `short/int/long/long long`、`size_t`、`ptrdiff_t` 或固定宽度别名；wire 类型只由
宽度和有符号性决定，不保存 C++ typedef 身份。实现必须在编译期拒绝宽度不是 1/2/4/8B 的整数。
enum underlying `bool` 明确拒绝；underlying plain `char` 按目标平台实际 signedness 映射
`Int8/UInt8`，不映射为 `Char`。

V1 目标是 64 位 little-endian Linux。`Pointer64` 要求 `sizeof(std::uintptr_t) == 8`；对象指针先转换为
`uintptr_t`，再按 UInt64 字节表示编码。Backend 只格式化该数值，不得解引用。函数指针和成员指针不支持。

浮点编码必须以 `std::bit_cast` 等价语义保留 bit pattern，不执行十进制转换；实现需要静态验证
`sizeof(float) == 4`、`sizeof(double) == 8` 和目标平台的 IEC 559 条件。

### 2. UTF-8 输入

V1 接受：

- `const char (&)[N]` 字符串字面量/数组约定，编码 `N - 1` 个字节且要求最后元素为 `\0`；
- `std::string`、`std::string_view`；
- `const char8_t (&)[N]`、`std::u8string`、`std::u8string_view`，按 UTF-8 原始字节复制；
- 显式 `qlog::cstr(ptr)` 兼容包装器。

标量 `char8_t` 不作为字符参数支持。显式长度字符串可以包含内嵌 `\0`，长度字段始终是字节数。
Producer 不验证 UTF-8 合法性，也不做转码；调用方负责输入为 UTF-8。该选择避免热路径二次遍历。

`const char/char8_t (&)[N]` 路径按固定 extent 编码 `N - 1` 字节，不搜索第一个 NUL；除字符串字面量外的
一般字符数组的末元素不总是常量表达式。因此末元素非 NUL 必须在 reserve 前返回运行时
`invalid_string_metadata`，不能普遍建模成 compile-fail。若调用方需要数组内更短的运行时有效长度，必须
显式构造 `std::string_view`。

### 3. 明确不支持

V1 编译期拒绝：

- 裸 `const char*` / `char*`；
- 未包装的对象指针、函数指针和成员指针；
- `long double`、`__int128`、`unsigned __int128`；
- `wchar_t`、`char16_t`、`char32_t` 及其字符串；
- `std::byte`、二进制 blob；
- 任意 `volatile` 参数、指针目标或字符串 view；
- named arguments、容器、chrono、路径和任意用户自定义对象；
- 依赖 `formatter<T>`、`format_as`、序列化回调或隐式转换才能格式化的类型。

用户类型必须在 Producer 调用日志 API 之前显式转换为上述基本类型或 UTF-8 字符串。Ring 中不得保存
对象地址、虚函数地址、回调地址或动态库内代码地址。

### 4. 编译期与运行期计长边界

参数 tag、参数数量和固定宽度值的单项编码大小由 C++ 类型在编译期确定；这不代表整个 `args_bytes`
总能在编译期确定：

- 固定宽度值、`Pointer64` 和 `NullUtf8` 的编码大小是编译期常量；
- `const char/char8_t (&)[N]` 的 extent 是编译期常量，按本 ADR 的数组约定编码 `N - 1` 字节；
- `std::string[_view]`、`std::u8string[_view]` 的 byte length 在运行期通过 `size()` 取得；
- 非空 `qlog::cstr` 的 byte length 在运行期通过一次 `strlen` 取得并缓存；
- 只要存在运行期长度字符串，最终 `args_bytes` 就是“编译期固定贡献 + 运行期长度贡献”的 checked sum。

Producer 不需要、也不得为了计算 tag 或参数大小而解析 format 占位符。tag 来自参数的 C++ 类型，长度来自
参数值自身；占位符匹配属于 Backend 格式化阶段。BQLog 采用相同分层：Producer 的 `make_size_seq<true>`/
`_type_copy<true>` 负责参数计长和 type info，Backend `layout` 才扫描 format。

### 5. format API 与统一写入语义

V1 只有一个接受受支持 UTF-8 format 输入的日志 API 语义、一条 Record 编码路径和一套 Backend
格式化路径。字面量、`std::string[_view]` 和 `std::u8string[_view]` 都进入同一个
measure/reserve/write/commit 流程，并产生相同 Record；前端只利用类型上已有的 extent/`size()` 取得
长度，不解析占位符，也不执行最终文本格式化。空 format 合法；底层裸指针接口仅在
`format_bytes > 0 && data == nullptr` 时视为非法 metadata，`format_bytes == 0` 时 data 可以为空。format 语法和参数
匹配统一由 Backend 自研 `c20_format` 处理，错误计为当前 Record 的 `format_error`，不得终止 Backend。

`LOG_INFO("{}", dynamic_text)` 把 `dynamic_text` 当普通字符串，不会递归解释其中的占位符。format
本身无论来自字面量还是运行时字符串，均只在 Backend 解释一次。具体位置参数/数字索引规则由
`c20_format` 语法 ADR 冻结；V1 不支持 named arguments。

## D5：字符串所有权与长度

### 1. 所有权边界

所有 format 和字符串参数只在当前日志操作期间借用；Producer 必须在 `commit()` 前把其字节深拷贝进
当前 Ring Record。借用期到 encode 返回，或 reserve/其他前置阶段失败后本次操作放弃 prepared result，
以较早者为准。已发布 Record 不得引用调用方字符串、静态只读区或动态库地址。

调用方必须保证借用范围从 measure 首次读取到上述借用终点保持有效且不被并发修改。QLog 不尝试从裸地址
判断生命周期，也不把普通 `string_view` 自动视为 interned/static 字符串。

### 2. 字符串编码

普通 UTF-8 字符串参数编码为：

```text
[Utf8String tag: u8][byte_length: u32 little-endian][byte_length 个字节]
```

规则：

- `byte_length` 不含结尾 `\0`，Record 不保存结尾 NUL；
- 空字符串合法，编码为 `Utf8String + u32(0)`；
- 显式长度字符串中的内嵌 NUL 原样保存；
- 长度必须可表示为 `uint32_t`，并同时受 Record/Channel 最大 payload 限制；
- V1 不截断，超过上限的事件在 reserve 和取时前拒绝。

`std::string_view{}` 即使 `data() == nullptr` 也表示合法空字符串；只有 `size() > 0 && data() == nullptr`
才是非法 metadata。

### 3. 显式 C 字符串兼容路径

裸 `const char*` 默认拒绝。需要兼容 C API 时必须显式写：

```cpp
qlog::cstr(ptr)
```

精确定义：

```text
ptr == nullptr
  -> NullUtf8，不调用 strlen

ptr != nullptr
  -> 调用方保证 ptr 指向可读且以 NUL 终止的 char 字符串
  -> measure 恰好调用一次 strlen(ptr)
  -> 返回值是 byte_length，并缓存 {pointer, byte_length}
  -> byte_length 超过 UInt32：argument_length_out_of_range
```

`qlog::cstr` 不拥有字符串。非空 `ptr` 无效、不可读或没有 NUL 终止符属于调用方违约，行为与直接调用
`strlen` 相同，不建模为可恢复的 `MeasureError`。`max_payload_bytes` 不限制 `strlen` 的读取量；measure 在求得
完整长度后执行 checked aggregate，再以非参数下标 `0xFF` 返回最终 `payload_too_large`。Encoder 必须复用
缓存长度，不能再次调用 `strlen`。该取舍与 BQLog/fmt 的 C 字符串前置条件对齐，以更小的 API、scratch 和
模板控制流换取由调用方承担 NUL 终止保证。

### 4. null 字符串与空指针

`NullUtf8` 仅由显式字符串包装器产生：

```text
qlog::cstr(nullptr)      -> NullUtf8
"" / std::string_view{} -> Utf8String，长度 0
裸 nullptr              -> Pointer64(0)
qlog::ptr(nullptr)      -> Pointer64(0)
```

`NullUtf8` 只占一个 tag，不带长度或 payload。Text Sink 的 `c20_format` 把它格式化为 `<null>`；不得把
六个显示字节预先复制进 Ring，也不得把它改写成 Pointer64。因为 V1 没有持久 Callsite schema，
Backend 直接依据每条 Record 的实际 tag 解码。

## D6：packed tagged arguments ABI

### 1. Tag 表

| Tag | 名称 | Payload |
|---:|---|---|
| `0x00` | `Invalid` | reserved；Producer 永不生成 |
| `0x01` | `Bool` | 1B，只允许 `00` 或 `01` |
| `0x02` | `Char` | 1B |
| `0x03` | `Int8` | 1B two's-complement bit pattern |
| `0x04` | `UInt8` | 1B |
| `0x05` | `Int16` | 2B little-endian |
| `0x06` | `UInt16` | 2B little-endian |
| `0x07` | `Int32` | 4B little-endian |
| `0x08` | `UInt32` | 4B little-endian |
| `0x09` | `Int64` | 8B little-endian |
| `0x0A` | `UInt64` | 8B little-endian |
| `0x0B` | `F32` | 4B IEEE-754 bit pattern，little-endian |
| `0x0C` | `F64` | 8B IEEE-754 bit pattern，little-endian |
| `0x0D` | `Pointer64` | 8B little-endian |
| `0x0E` | `Utf8String` | `u32_le byte_length + bytes` |
| `0x0F` | `NullUtf8` | 无 payload |
| `0x10..0xFF` | Reserved/unknown | V1 Producer 不得生成 |

Tag 数值一旦进入 `record_abi_version = 1` 就不得改变含义。未来增加可跳过的新类型需要新的 Record ABI
或为扩展项定义独立长度；当前 packed 协议遇到未知 tag 无法安全跳过单个参数。

### 2. 单参数布局

固定宽度值：

```text
[u8 tag][紧随其后的 1/2/4/8B value]
```

字符串值：

```text
[u8 Utf8String][u32_le byte_length][bytes]
```

null 字符串：

```text
[u8 NullUtf8]
```

没有 per-argument alignment、format-to-args padding 或 Record 内部尾部 padding：

```text
args_alignment = 1
```

“packed”只描述 Record 内部字节协议。Ring Frame 起点、Frame 尾部取整、读写游标和原子对象仍遵守
各自的 8B/缓存行对齐约束。

### 3. 精确长度公式

```text
fixed_size(width) = checked_add(1, width)
utf8_size(n)      = checked_add(checked_add(1, 4), n)
null_utf8_size    = 1

args_bytes = checked_sum(encoded_size(arg[i]))

format_begin = 32
args_begin   = checked_add(format_begin, format_bytes)
payload_bytes = checked_add(args_begin, args_bytes)
```

外层 Ring 的 `frame_bytes` 可以继续按 Ring 契约取整，但：

```text
FrameHeader::payload_bytes == payload_bytes
RecordHeader::args_bytes   == args_bytes
```

所有计算在 `size_t` 中使用 checked add/sum，验证可表示范围后才收窄到 Header 的 `uint32_t`。
Producer 在调用 `try_reserve()` 前必须已经得到唯一的精确 `payload_bytes`。

### 4. 参数数量

```text
kMaxArgCount = 32
```

Header 保留 `uint16_t arg_count`，但 V1 Producer 编译期/准入时拒绝超过 32 个参数；Decoder 在遍历前
拒绝 `arg_count > 32`。`arg_count == 0` 时必须有 `args_bytes == 0`。

32 的运行时上限允许每个 BackendWorker 使用固定大小参数槽位，无需逐 Record 扩容。提高该上限属于
实现策略/ABI 兼容评审项，不能只修改前端常量。

### 5. 对齐安全与字节序

禁止：

```cpp
auto* header = reinterpret_cast<RecordHeader*>(ring_bytes);
auto value = *reinterpret_cast<const std::uint64_t*>(cursor);
```

允许的实现方式是：

- 完整初始化一个已对齐的局部 `RecordHeader`，再以 `memcpy` 复制 32B；或按固定偏移 `store_le`；
- 多字节参数通过固定宽度 `memcpy`/`load_le` 读写到已对齐局部变量；
- signed integer 和 float 先取得对应 unsigned bit pattern；
- 不因 x86-64 支持未对齐访问就绕过 C++ 对齐与对象生命周期规则。

V1 只接受 little-endian host，但 wire helper 仍应以明确宽度命名，避免把协议语义隐藏在 typed-pointer
解引用里。固定 2/4/8B `memcpy` 是否内联为单次 load/store由 Release 汇编门禁验证。

### 6. Golden bytes

以下片段只描述 arguments 区：

```text
Int32(0x01020304) = 07 04 03 02 01
Utf8String("A") = 0E 01 00 00 00 41
NullUtf8         = 0F
Pointer64(0)     = 0D 00 00 00 00 00 00 00 00
```

至少需要覆盖每个 tag 的零值、边界值、负数、正负零、无穷、NaN bit pattern、空字符串、内嵌 NUL、
NullUtf8、32 参数和所有截断位置。

## Producer 顺序

规范顺序为：

```text
1. Logger/Category/Level O(1) 过滤
2. 编译期类型准入；运行时检查 view/cstr metadata
3. 取得 format_bytes 和每个字符串 byte_length
4. checked 计算 args_bytes/payload_bytes；执行上限检查
5. try_reserve(payload_bytes) 一次
6. reserve 成功后取得 admission timestamp
7. 在栈上构造完整 RecordHeader，复制 format、编码 arguments，成功路径最后复制 Header
8. commit 发布
```

实现可以在 commit 前回填 `format_hash`，但未发布 Record 的最终字节必须完整。同一条写入流程中，
字面量来源可使用编译期 extent，并尽量使用与运行时完全相同算法得到的编译期 hash；运行时 view 使用
显式长度，并在复制进 Ring 的同一次遍历中计算 hash，不得 `hash(format)` 后再次 `memcpy(format)`。
这些只是 format metadata 的取得方式不同，不是不同的 Record path。

ADR-010 已固定 BQLog 式四路 CRC32C 派生算法 `crc32c4x64_v1`，并把原始结果 0 规范化为 1。正常
V1 Producer 路径必须提供非零 stored hash；Backend 仍把 Header 的 0 当作“未计算”并根据 Record 内
完整 format 延迟计算，但它不是 Producer 的常规开关。

被过滤、metadata 非法、不支持、参数过多或过大的事件不 reserve、不读取 admission clock、不写 Ring。
Ring 满采用 `drop_new`，不等待 Consumer。

## Decoder 与 Backend 边界

I3 首先以可信的 Ring `FrameHeader::payload_bytes` 建立 `[record_begin, record_end)`；I1 Decoder 只接收
`(const std::byte* record_data, std::size_t record_size)`、固定 32 槽
`(DecodedArg* workspace, std::size_t workspace_count)` 与不可变 `RecordValidationPolicy`，不包含
Ring/Channel 类型，也不使用 `std::span`。
至少按以下顺序限制工作：

1. `payload_bytes >= 32`；
2. policy 判定 `level` 合法，`flags & ~0x03 == 0`，timestamp status/time/policy 合法；
3. `format_bytes <= 8192`；
4. `format_bytes <= remaining_after_header`；
5. `args_bytes == remaining_after_format`；
6. `arg_count <= 32`；
7. 最多解码 `arg_count` 个参数，同时绝不越过 `args_end`；
8. 最终要求 `decoded_count == arg_count && cursor == args_end`。

边界检查优先使用 subtraction-first 形式，例如先验证 `length <= end - cursor`，避免 `cursor + length`
溢出。以下情况只令当前 Record 失败并分类计数：

- `Invalid`/未知 tag；
- Bool payload 不是 0/1；
- 固定值、字符串长度或字符串 bytes 截断；
- `arg_count/args_bytes` 不一致；
- 非法 level、unknown flags、reserved timestamp status；
- 后台 `c20_format` 类型/占位符错误。

失败后 Backend 必须依据可信 Ring Frame release 当前空间并继续扫描；不得越界、无限循环或终止线程。

`DecodedArg/DecodedRecordView/DecodeResult` 由 I1 定义，I3 不得重复建立解码协议。每个 BackendWorker
使用固定 32 槽的 `DecodedArg` 工作区。数字复制进槽位；字符串槽只借用当前尚未
release 的 Ring bytes。自研 `c20_format` 只消费该固定视图，不建立逐 Record 动态参数容器。成功时先把
完整 64KiB scratch 行交给 Sink 自有内存 batch，再 release；失败时丢弃 scratch 后 release。任何可能阻塞的
`write()`/`fdatasync()` 都必须发生在 release 之后或两条 Frame 之间。

## format_hash 与缓存碰撞

`format_hash` 只定位候选项，不能单独代表格式身份。Text 解析缓存命中后必须继续比较：

```text
format_hash
+ format_bytes
+ 完整 format bytes
```

Text 语法 plan 与 category/level 无关。未来 Compress 模板表若把 category/level 定义为模板身份，则在
完整 format bytes 比较通过后再比较二者；它不是 Text parse cache。

当 Header 中 hash 为 0 时，Backend 先从当前 Record 的 format bytes 计算并规范化 stored hash，再执行同样比较。
空 format 合法，不能把 `format_bytes == 0` 当成损坏或外部格式引用。

## 性能理由与必须验证的假设

packed 布局把 Int64 参数压缩为 9B，把 5B UTF-8 字符串压缩为 10B；相较使用 4B type cell 和 align4，
它减少 Ring 占用与复制字节。代价是多字节 payload 经常未对齐，因此只能通过固定宽度 `memcpy`/
load helper 访问。

以下结论必须用基准而不是字段大小证明：

- 字面量 constexpr hash 是否优于每次四路 CRC32C copy-and-hash；
- packed 对 x86-64 Producer、AArch64 Producer/Backend 的吞吐和 P99 影响；
- format 复制对 Ring 高水位与 `drop_new` 的影响；
- 固定 32 个 `DecodedArg` 槽是否覆盖真实游戏日志。

至少对比“完全 packed”和“Record 起点 align8、内部 packed”；不得采用未对齐 typed-pointer 解引用的
实现作为性能捷径。

## 后果

### 正面

- 所有 format 来源和全部参数使用一套自包含协议；
- 动态库卸载后，已发布 Record 仍能独立解码；
- 无 Callsite 注册、状态机、稳定表或外部指针验证；
- 裸 C 字符串不隐式扫描，null 字符串语义可保留；
- packed 参数减少 Ring 内存流量，Backend 可用固定工作区。

### 代价

- 每条 Record 复制 format 并携带参数 tag；
- 非空 `qlog::cstr` 兼容路径需要一次 `strlen`，调用方承担 NUL 终止前置条件；
- packed payload 要求严格的 byte-wise codec；
- 未知 tag 不能单项跳过，只能放弃当前 Record；
- V1 类型集合有意较小，调用方需要显式转换用户类型。

## 后续冻结状态

本 ADR 完成 D4～D6；`crc32c4x64_v1`、`c20_format` 精确语法、解析缓存和有界输出合同已经由
[ADR-010](./ADR-010-v1-backend-c20-format.md) 冻结。Producer/Backend 的 x86-64 与目标 AArch64
基准继续作为实现验收，不再是阻塞 Record Core 的设计问题。

总体实施任务见 [里程碑二 Record 实现指南](./MILESTONE2_RECORD_IMPLEMENTATION_GUIDE_CHS.md)；I1 的
完整执行合同和测试门禁见
[I1 Record Core 企业级开发规范](./MILESTONE2_I1_RECORD_CORE_DEVELOPMENT_GUIDE_CHS.md)。
