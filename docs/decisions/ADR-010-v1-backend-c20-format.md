# ADR-010：V1 format hash、c20_format、解析缓存与工作量边界

- 状态：已接受（2026-09-05 修订，冻结 G0/H1～H4）
- 日期：2026-09-05
- 取代：本 ADR 早期的 FNV-1a 64-bit 决定，以及 V1 使用外部 `{fmt}` 的决定
- 前置决策：[ADR-007](./ADR-007-self-contained-record-header.md)、
  [ADR-008](./ADR-008-realtime-coarse-admission-timestamp.md)、
  [ADR-009](./ADR-009-v1-packed-tagged-arguments.md)

## 1. 决策摘要

V1 只有一条自包含 Record 协议：Producer 根据 C++ 参数类型计长，把每条日志的 UTF-8 format 深拷贝进 Ring，
并写入 tagged arguments；Backend 解码后由 QLog 自研 `c20_format` 生成文本。所谓“字面量路径”和“运行时路径”
只是长度/hash 获取方式的实现优化，不产生两种 Record kind，也不改变 Ring 布局。

本 ADR 冻结：

```text
raw hash                    = crc32c4x64_v1
stored hash                 = raw_hash == 0 ? 1 : raw_hash
stored hash 0               = reserved for "not computed"
replacement fields          = {} and {:spec}, automatic indexing only
custom fill                 = one printable ASCII byte, excluding '{' and '}'
integer types               = d, x/X, o, b/B
floating types              = f/F, e/E, g/G
pointer type                = p only when an explicit type is present
dynamic width/precision     = unsupported
string precision            = unsupported
width unit                  = produced bytes
kMaxFormatBytes             = 8192
kMaxFormatSpecBytes         = 32
kMaxArgCount/field count    = 32
kMaxWidth                   = 4096
kMaxFloatPrecision          = 64
kMaxTextLineBytes           = 65536, metadata prefix and trailing '\n' included
parse cache                 = 256 entries, 4-way set associative
kMaxCachedFormatBytes       = 256
```

这些是 V1 实现常量，不进入每条 Record。hash 算法标识属于 Channel 元数据；修改算法或 Record 的解释必须
提升相应版本，不能静默改变。`kMaxFormatBytes` 是 format 自身的语法上限，不保证它能放入任意 Channel：

```text
sizeof(RecordHeader) + format_bytes + args_bytes
    <= channel.max_payload_bytes
```

仍是 Producer reserve 的必要条件。默认 `max_payload_bytes == 8 KiB` 时，一条 8192B format 加 Header 不可接纳；
若产品要求此类 Record 可写入，必须显式配置更大的 Channel payload 上限。

## 2. H1：四路 CRC32C 派生的 64 位 format hash

### 2.1 名称和用途

QLog 将算法命名为 `crc32c4x64_v1`。它借鉴 BQLog 当前的 four-way interleaved CRC32C copy-and-hash：
四个 32 位 CRC32C 状态并行更新，再折叠为一个 64 位私有 hash。它不是标准单路 CRC32C，不是 CRC64，
也不是安全散列。

`format_hash` 只用于定位解析缓存或未来模板表的候选项。任何命中都必须继续比较：

```text
format_hash + format_bytes + complete format bytes
```

未来 Compress Sink 还必须把 encoding、category 和 level 纳入模板身份；hash 永远不能单独证明相等。

### 2.2 CRC primitive

反射多项式固定为 CRC32C Castagnoli `0x82F63B78`，四个 lane 的初值均为 `0xFFFFFFFF`，无最终取反，
不加入随机 seed。软件参考 primitive 为：

```text
crc8(crc, byte) = (crc >> 8) xor table[(crc xor byte) & 0xFF]
```

`crc16/crc32/crc64` 按内存地址递增顺序，把 little-endian 值的低字节到高字节依次送入 `crc8`。
QLog V1 线协议已经固定 little-endian；实现仍必须通过固定宽度 `memcpy` 到局部值或显式 little-endian load，
不得用未对齐的 typed pointer 解引用。

### 2.3 精确分块算法

令 `h1 = h2 = h3 = h4 = 0xFFFFFFFF`，输入长度为 `len`：

| `len` | 更新规则 |
|---:|---|
| `>= 32` | 从头处理所有完整 32B 块；每块的四个 8B 依次更新 `h1..h4`。若最后不足 32B，再处理一次以输入末尾结尾的完整 32B 窗口。 |
| `16..31` | 前两个 8B 更新 `h1/h2`；末尾两个 8B 分别以 `h3 xor len`、`h4 xor len` 为初值更新 `h3/h4`。 |
| `8..15` | 前 8B 更新 `h1`；末尾 8B 以 `h2 xor len` 为初值更新 `h2`。 |
| `4..7` | 前 4B 更新 `h1`；末尾 4B 以 `h2 xor len` 为初值更新 `h2`。 |
| `1..3` | 若 `len & 2`，前 2B 以 `h1 xor len` 为初值更新 `h1`；若 `len & 1`，剩余 1B 以 `h2 xor len` 为初值更新 `h2`。 |
| `0` | 四个 lane 保持初值。 |

`len >= 32` 的末尾窗口允许与前一个块重叠，这是算法身份的一部分，不得换成普通 scalar tail。例如 33B
依次处理 `[0, 32)` 和 `[1, 33)`，63B 处理 `[0, 32)` 和 `[31, 63)`，64B 只处理两个完整块。

最终折叠固定为：

```text
low  = h1 xor rotl32(h3, 17)
high = h2 xor rotl32(h4, 19)
raw_hash = (uint64(high) << 32) | low
```

### 2.4 0 值与 Record 表示

原始算法对长度为 0 的输入范围结果是 0，其他输入也不能从数学上排除结果为 0。QLog 为保留现有 Header 中的缺失 sentinel，
在算法边界统一规范化：

```text
stored_hash = raw_hash == 0 ? 1 : raw_hash
```

因此 QLog 的存储值在 raw hash 为 0 时有意不同于 BQLog 原始返回值；除此之外，raw 算法逐位一致。
`format_hash == 0` 只表示 Producer 没有提供可用 hash。Backend 遇到 0 时对 Record 内的 format 执行 hash-only，
再应用同一规范化，不回写 Ring。正常 V1 Producer 路径必须写入非零 hash。

实现落地前用独立软件参考实现复核下列 raw vectors；规范化只改变第一项：

```text
""             -> 0x0000000000000000  (stored 0x0000000000000001)
"a"            -> 0x33bbc03300000000
"{}"           -> 0x000000000e3ee044
"abc"          -> 0x33bbc033d64581af
"123456789"    -> 0xb4ee537e6087809a
32 bytes 0x00   -> 0x187cd3cfe93daadb
bytes 0x00..20  -> 0xe166cd00cec7b109
bytes 0x00..3f  -> 0x977cb6d90d3cc2e4
```

### 2.5 copy-and-hash、hash-only 与 Producer 时序

必须提供相同核心语义的四条可测试路径：

```text
software hash-only
software copy-and-hash
hardware hash-only
hardware copy-and-hash
```

支持硬件 CRC32 指令的平台可在 Core 冷路径检测能力并选择实现；`FormatHashDispatch` 只能由工厂成功构造，
不能默认构造或聚合伪造。automatic 模式始终可退回 software；test-only forced-hardware 不可用时由工厂返回
`backend_unavailable`，且不产生 dispatch。dispatch 状态初始化后不可变，Producer 稳态不得为此执行共享
原子 RMW。x86-64 可用 CPUID SSE4.2；通用 AArch64 binary 只有在独立 CRC-enabled TU
或可靠的 function target 与 HWCAP 检测同时成立时才启用硬件路径，否则使用软件路径。硬件能力和所选实现不进入
Record ABI，所有可用路径必须逐位一致。

copy-and-hash 合同与 `memcpy` 相同：源、目标范围不重叠；完成后目标逐字节等于源。I1/V1 生产接口
固定为裸指针加显式长度，不得使用 `std::span`。checked 边界与底层 raw backend 必须分层：

```text
checked hash-only = source + source_size
checked copy-and-hash = source + source_size + destination + destination_capacity
raw hash backend = source + size
raw copy-and-hash backend = source + destination + size
```

standalone checked 入口先验证 `null + nonzero` metadata 和目标容量，失败时不写目标；backend 可用性已经由
冷路径 dispatch 工厂解决，不在每次 hash 调用中重新判断。dispatch 中的函数指针只指向 raw backend。
raw backend 以“`size > 0` 时指针非空、copy 两范围不重叠且目标容量已足够”为前置条件。
Encoder 在首次写入前完成等价 preflight 后可以直接调用 dispatch raw backend，这不会破坏强失败保证。
`size == 0` 时不解引用任一指针。hash-only 核心不得为了复用 copy 宏而对空目标指针做算术。

C++20 字面量常量求值路径通过数组下标逐字节取得无符号 8-bit 值，不依赖把 `char*`/`char8_t*`
`reinterpret_cast` 成 `std::byte*`；该转换留给运行时 raw backend。非零 precomputed stored hash 必须由同一
`crc32c4x64_v1` 算法针对同一组 format bytes 得到，Encoder 不执行第二遍 hash 来验证它。

Producer 固定时序：

```text
filter
  -> checked measure format/arguments/frame
  -> try_reserve
  -> reserve 成功后采 admission timestamp
  -> 在栈上构造/填充局部 Header 的其余字段
  -> 复制 format 并得到 stored hash
  -> 编码 tagged arguments
  -> 最后一次性复制完整 Header
  -> commit
```

reserve 失败不复制 format、不计算运行时 hash、不读时钟。运行时 `std::string`/`string_view`/`u8string_view`
使用显式长度，在 reserve 成功后以一次 fused copy-and-hash 完成复制与 hash，不允许先完整扫描 hash 再 `memcpy`。

字符串字面量可由逐位一致的 constexpr 软件参考实现对 `N - 1` bytes 预先得到 stored hash；reserve 成功后仅
`memcpy` format 并写入常量 hash。该优化不建立“静态 Record”协议；若 constexpr 未生效，仍可走相同的运行时
copy-and-hash。裸 `char*`/`const char*` 不是隐式运行时 format API，必须改用显式长度的 view。

## 3. H2：c20_format 严格语法合同

### 3.1 文法与索引

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

- 只支持自动顺序索引；每个 replacement field 消费下一个参数；
- `{}` 与 `{:}` 等价；字段数必须恰好等于 `arg_count`，参数不足或多余都失败；
- `{{` 输出 `{`，`}}` 输出 `}`，均不消费参数；孤立或未闭合 brace 失败；
- `{0}` 明确报 `explicit_index_not_supported`，`{name}` 报 `named_index_not_supported`；
- `{:{}}` 报 `dynamic_width_not_supported`，`{:.{}f}` 报 `dynamic_precision_not_supported`；
- 不支持 locale、chrono、debug presentation、用户 formatter、嵌套 replacement field 或宽松恢复；
- format 由裸 `data` 指针和显式 byte size 表示，可包含 NUL；parser 不依赖结尾 NUL，也不验证或规范化 UTF-8。

用户要求的最小兼容面固定如下：

| 输入 | 结果 |
|---|---|
| `{}` | 合法，按当前 ArgumentTag 的默认表示输出 |
| `{:<16}` | 合法，左对齐到至少 16 bytes |
| `{:>16}` | 合法，右对齐到至少 16 bytes |
| `{:^16}` | 合法，居中到至少 16 bytes |
| `{:+08d}` | 合法数字格式；`42` 输出 `+0000042` |
| `{:#x}` | 合法整数格式；`42` 输出 `0x2a` |
| `{:.6f}` | 合法浮点格式；`3.5` 输出 `3.500000` |
| `{:12.4g}` | 合法浮点格式；4 位有效数字、最小字段宽度 12 bytes |
| `{{literal}}` | 合法，输出 `{literal}`，不消费参数 |

### 3.2 fill、align、width 与 precision

自定义 fill 只允许一个 printable ASCII byte `0x20..0x7E`，且不能是 `{` 或 `}`；仅当它后面紧跟
`<`、`>` 或 `^` 时才识别为 fill。默认 fill 为空格。解析必须先识别 `[fill]align`，所以：

```text
{:0>8}   -> fill='0', align='>', width=8
{:08d}   -> zero flag, width=8, type=d
{:*>8}   -> fill='*', align='>', width=8
```

非 ASCII 或多字节 UTF-8 fill 拒绝。文本默认左对齐，数字和 Pointer64 默认右对齐。width 是最小字段宽度，
不会截断内容，范围 `1..4096`，不得以 0 开头；一律按最终输出 bytes 计量，不按 Unicode code point 或终端显示列。
居中出现奇数个 padding 时，左侧取 `floor(n / 2)`，多出的一个放右侧。

precision 只允许浮点 presentation，语法范围 `0..64`。字符串 precision 明确拒绝：字符串总是完整输出，
避免按 byte 截断破坏 UTF-8，也不在 V1 引入 code point/display-width 算法。完整行上限负责限制 Text 输出。

### 3.3 `#` 与 `0`

`#` 只允许整数的 `b/B/o/x/X`：

```text
b -> 0b    B -> 0B    o -> 0    x -> 0x    X -> 0X
```

八进制值 0 的 alternate form 仍只输出一个 `0`；符号位于 prefix 前。`#` 与 `d`、Bool、Char、float、
Pointer64、Utf8String、NullUtf8 都不兼容。V1 不支持浮点 alternate form。

`0` 是 sign/prefix-aware 的数字补零标志：

- 只允许整数、浮点，以及 `d` presentation 的 Bool；
- 必须同时存在 width；
- 不得与显式 align 或自定义 fill 同时出现；
- padding 位于 sign 和进制 prefix 之后。

因此 `"{:#08x}", 42` 输出 `0x00002a`。`{:0d}`、`{:00d}`、`{:<08d}`、`{:08s}` 和 `{:016p}`
都必须拒绝。需要用字符 `0` 填充字符串或 pointer 时，显式写 `{:0>16s}` 或 `{:0>16p}`。

### 3.4 ArgumentTag 与 presentation type

| ArgumentTag | 允许的 type | 默认输出与约束 |
|---|---|---|
| `Bool` | 省略、`s`、`d` | 省略/`s` 输出 `true`/`false`；`d` 输出 `1`/`0` |
| `Char` | 省略、`c` | 原样输出一个 byte |
| `Int8/16/32/64` | 省略、`d`、`b/B`、`o`、`x/X` | 省略为十进制；非十进制为 sign 加 magnitude |
| `UInt8/16/32/64` | 省略、`d`、`b/B`、`o`、`x/X` | 省略为十进制 |
| `F32/F64` | 省略、`f/F`、`e/E`、`g/G` | 见 3.5 |
| `Pointer64` | 省略、`p` | 省略等价于 `p`；显式 type 只允许小写 `p` |
| `Utf8String` | 省略、`s` | 原样输出全部 bytes；不允许 precision |
| `NullUtf8` | 省略、`s` | 输出固定 ASCII 文本 `<null>` |
| `Invalid`/未知 tag | 无 | Decoder 在 formatter 前拒绝 |

sign 允许 signed/unsigned integer、floating-point 和 Bool 的 `d` 形式；`+` 或空格作用于非负值，`-` 为默认行为。
Bool 文本形式、Char、Pointer64、Utf8String、NullUtf8 不接受 sign、`#`、`0` 或 precision。Bool `d` 可接受
sign、width、align 和 `0`，但不接受 `#`。不兼容组合为 `type_mismatch`，不得静默回退。

Pointer64 固定输出 `0x` 加无前导零的小写十六进制，0 输出 `0x0`；拒绝 `P/x/X/d`。裸 `nullptr` 和
`qlog::ptr(nullptr)` 编码为 `Pointer64(0)`。裸 `char*`/`const char*` 无论是否为空都在 Producer API 拒绝：

```text
qlog::cstr(nullptr) -> NullUtf8 -> <null>
qlog::cstr("")      -> Utf8String(length=0) -> empty output
```

空字符串配 width 时只输出 padding；`nullptr` 不等于空字符串。

### 3.5 浮点

- `f/F` 为 fixed，`e/E` 为 scientific，`g/G` 为 general；
- `f/F`、`e/E` 的 precision 是小数点后位数；`g/G` 的 precision 是有效十进制位数；
- `g/G` 的 precision 语法值 0 按 effective precision 1 处理；
- 省略 type 但给出 precision 时按 `g`；显式 `f/F/e/E/g/G` 未给 precision 时使用 6；
- type 和 precision 都省略时，按原始 `F32` 或 `F64` 类型输出 shortest round-trip general；
- 数值转换基线为 C++20 `std::to_chars`；自研范围是 parser、类型分派、padding、边界、缓存和错误策略；
- `F/E/G` 把指数标记及 `inf/nan` 转为大写；不输出 NaN payload；
- 保留负零符号；浮点 `#` 不进入 V1。

## 4. H3：Backend 解析计划缓存

每个 Text BackendWorker 私有一个启动时预分配的固定缓存；Producer、NullSink 和不同 worker 不共享：

```text
entries                    = 256
sets                       = 64
ways per set               = 4
replacement                = per-set round-robin
maximum cached format      = 256 bytes
maximum cached fields      = 32
```

缓存 entry 保存 stored hash、format length、完整 format 副本、只含 offset/长度/spec 的固定大小 `FormatPlan`，
以及确定性的语法解析失败状态。plan 不保存 Ring 指针，也不缓存某条 Record 的 ArgumentTag；类型匹配逐 Record 执行。

查找规则：

1. Header hash 为 0 时先按 H1 对当前 format 延迟计算并规范化；
2. 用 `hash xor (hash >> 32)` 的低位选择 set；
3. 比较 hash、length 和完整 format bytes；任一不同就是 miss；
4. `format_bytes > 256` 时有界解析但不插入缓存；
5. category 和 level 不影响语法计划，不进入 cache key；未来 Compress Sink 的模板表是另一层。

缓存不做 LRU、不在稳态分配、不因动态 format churn 扩容。至少统计
`hit/miss/insert/eviction/uncacheable/hash_collision`；统计只由 BackendWorker 写，不给 Producer 热路径增加共享 RMW。

## 5. H4：有界工作、失败原子性与 Frame 生命周期

### 5.1 固定上限

```text
format bytes             <= 8192
replacement fields       <= 32
arg_count                <= 32
format-spec bytes/field  <= 32
width                    <= 4096
floating precision       <= 64
complete Text line       <= 65536 bytes
```

format-spec 长度只计算 `:` 和结束 `}` 之间的 bytes。完整 Text line 包含元数据前缀、格式化消息与 Text Sink
添加的换行。所有长度、十进制解析、padding 与总大小计算都使用 checked arithmetic；写任何 literal、参数文本或
padding 前先检查剩余容量。format/string 中的 NUL 是普通输出 byte，也计入长度。scratch 和 Sink batch 均以
裸 `{data, size}` 对传递，不要求也不额外预留结尾 NUL，不包装成 `std::span`。

这些输入和输出上限，加上无递归、无嵌套字段、无动态 width/precision、无回溯和无完整 format 重扫，共同构成
V1 的 CPU 工作量边界；不再另设容易重复计数的 `kMaxFormatWorkUnits`。实现不能通过逐 byte padding 循环绕开
输出容量检查，应先计算目标长度，再进行有界填充。

### 5.2 Backend 顺序

Text 路径固定为：

```text
validate FrameHeader / RecordHeader / all pointer-length ranges
  -> obtain or parse FormatPlan
  -> decode exactly arg_count tagged arguments and validate plan/type compatibility
  -> format metadata prefix + message + newline into worker-private 64KiB scratch
  -> after complete success, append the complete line to Sink-owned memory batch
  -> release Frame on every success/failure path
  -> only outside the held-Frame interval may write/fdatasync perform blocking I/O
```

“交给 Sink”在此只表示把完整行复制/移动进 Sink 自有的内存 batch，不表示已经完成 `write()`。Sink batch 必须
为一条最大行保证空间，容量不足时的 flush 或 buffer rotation 在持有下一条 Frame 之前完成。不得让慢磁盘 I/O
延迟 `read_cursor` 回收并放大 `drop_new`。

NullSink 在验证可信 Frame 边界和必要 Header 字段后可跳过 plan、参数解码与格式化；它仍必须正确 release Frame。

### 5.3 失败原子性

只有 64KiB scratch 中已经完整生成的行才可提交给 Sink batch。任何结构、语法、类型或容量错误都丢弃当前
scratch 内容，计入对应 failure，依据可信 `frame_bytes` release，然后继续下一条。不得截断、部分提交、写伪造的
用户日志行、让异常穿出 worker 或终止 Backend。

scratch 保证的是“格式化成功前不调用 Sink”和“内存 batch 不收到半行”，不承诺文件系统写入原子性；一旦未来
`write()` 开始，永久 I/O 错误仍可能留下部分文件尾。short write、EINTR、永久错误和重试策略属于 Sink 合同。

错误至少区分：

```text
format_too_large
unmatched_open_brace
unmatched_close_brace
explicit_index_not_supported
named_index_not_supported
dynamic_width_not_supported
dynamic_precision_not_supported
format_spec_too_long
invalid_format_spec
width_out_of_range
precision_out_of_range
too_many_replacement_fields
argument_missing
argument_unused
type_mismatch
text_output_limit_exceeded
```

错误优先级固定为：Frame/Record 结构与 format 大小，随后从左到右解析 brace/field/spec，再检查 field/argument
数量与 tag 兼容性，最后检查输出上限。内部诊断可经独立且限频的通道报告，但不能递归调用 QLog。

## 6. 实现与测试门禁

`c20_format` 不得调用 `std::format`、`{fmt}` 或用户 formatter。Formatter 只消费 decoder 产生的有界
`DecodedArg* + arg_count`，不直接解引用 Ring 中的未对齐 typed value，也不保存跨 release 的 Ring view。

hash 测试必须覆盖：

- raw known vectors与 stored 0 规范化；
- `len = 0..128`，以及 `31/32/33`、`63/64/65`、`255/256/257`；
- source/destination 多种非对齐 offset 与前后 canary；
- 固定 seed 的随机长输入；
- software hash-only 等于 software copy-and-hash；
- 支持硬件的机器上强制比较 HW/SW 四条路径；不支持时明确 skip 硬件路径；
- constexpr literal reference 等于相同 `(data, size)` 输入的 runtime hash-only/copy-and-hash。

formatter 测试必须覆盖：

- 本 ADR 的全部合法/非法样例及精确错误分类；
- 每种 tag/type 组合，尤其 Bool、pointer、NullUtf8 与空 Utf8String；
- `#`、`0`、sign/prefix/padding 顺序和 center 奇数 padding；
- float shortest、0/1/6/64 precision、负零、inf/nan；
- 0/1/32/33 fields 与参数不足/多余；
- width 4095/4096/4097、spec 31/32/33、format 8191/8192/8193；
- 完整行 65535/65536/65537，且 prefix/message/newline 全部计入；
- cache hit/miss/collision/eviction/oversize bypass，命中后仍比较完整 bytes；
- format 每个截断位置以及固定 seed 随机裸 byte-range smoke；
- 所有成功与失败路径恰好 release 一次 Frame，慢 Sink I/O 不发生在持帧区间。

与 BQLog 的对照必须基于其实际源码职责：Producer reserve 成功后复制 format 并 hash，参数随后编码，最后 commit；
QLog 保持自己的 admission timestamp（reserve 成功后采样），不照搬 BQLog 的 reserve 前时间点。BQLog 仅作为
算法、功能和性能参照，不复制其宽松语法、4B type cell、未对齐 typed-pointer 行为或 hash-only 空目标指针算术。

本次源码核对基于 BQLog `60ef4d3ea526`：

- `src/bq_common/utils/util.cpp:65-153`：软件 CRC32C 与 x86/ARM hardware primitive；
- `src/bq_common/utils/util.cpp:156-281`：四 lane、重叠尾块和最终 rotate/fold；
- `src/bq_common/utils/util.cpp:284-318`：HW/SW × copy-and-hash/hash-only 四个入口；
- `src/bq_common/global/common_vars.cpp:80-132,176`：CPU capability 检测与冷路径初始化；
- `include/bq_log/misc/bq_log_impl.h:251-283`：过滤、计长、参数编码和 commit 调用顺序；
- `src/bq_log/api/bq_log_api.cpp:199-273`：Record 大小包含 format，reserve 后 `bq_memcpy_with_hash()`；
- `test/cpp/bq_common_test/test_utils.h:151-230`：copy/hash、长度和 guard 测试基线。

算法规范以 `util.cpp` 的实际 rotate/fold 为准；`util.h` 中省略 rotate 的简化注释不是 QLog 的规范依据。

G0/H1～H4 至此重新冻结。下一实现入口仍是独立 Record Core；本 ADR 不授权开始 codec 或 formatter。
