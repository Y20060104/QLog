# ADR-010：V1 format hash与Record复制合同

> 2026-09-17：[ADR-016](./ADR-016-v1-bqlog-worker-format.md)取代本ADR旧H2严格语法、H3解析缓存及H4中的字段数量/语法错误合同。这里保留已实现的H1 hash与wire边界，不能再以旧严格parser实施V1。

- 状态：H1及下述Record不变量有效；format解释以ADR-016为准。
- 历史：2026-09-05以crc32c4x64_v1替代FNV-1a；2026-09-16 ADR-014确定数组入口运行时copy/hash；2026-09-17按用户要求优先对齐BQLog UTF-8 worker。
- 关联：[ADR-007](./ADR-007-self-contained-record-header.md)、[ADR-009](./ADR-009-v1-packed-tagged-arguments.md)、[ADR-014](./ADR-014-v1-bqlog-style-literal-format.md)。

## 1. 保留合同

每条Record是32B Header、深拷贝format、packed tagged arguments。Producer不解析花括号、不渲染参数。format<=8192B、arg_count<=32且整个payload满足Channel配额；例如8192B format加Header不能放进8192B payload配额。

raw hash为crc32c4x64_v1，stored为raw==0 ? 1 : raw；0保留为未计算。正常Producer在reserve成功后fused copy/hash。BackendWorker按ADR-016直接扫描format，无FormatPlan/FormatCache必做项。

## 2. H1：四路 CRC32C 派生的 64 位 format hash

### 2.1 名称和用途

QLog 将算法命名为 `crc32c4x64_v1`。它借鉴 BQLog 当前的 four-way interleaved CRC32C copy-and-hash：
四个 32 位 CRC32C 状态并行更新，再折叠为一个 64 位私有 hash。它不是标准单路 CRC32C，不是 CRC64，
也不是安全散列。

`format_hash`只可作为未来缓存或模板表的候选定位值；ADR-016的V1 text scanner不使用它。将来任何命中都必须继续比较：

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
`format_hash == 0` 只表示 Producer 没有提供可用 hash。未来基于hash的功能需要时才对Record format执行hash-only并规范化，不回写Ring；ADR-016的V1 text scanner不查缓存，因此不因hash0补算。正常 V1 Producer 路径必须写入非零 hash。

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

公共数组/runtime入口都按ADR-014在reserve后执行copy-and-hash，不在此开启constexpr literal入口；现有constexpr参考算法及其等价性测试继续有效。非零precomputed hash是I1内部能力，不是普通调用方可任意填写的公共API。裸char指针必须包装为显式长度view。

## 3. worker输出和所有权

严格语法、默认值渲染、字段/参数数量、spec窗口以及安全数值扩展统一见[ADR-016](./ADR-016-v1-bqlog-worker-format.md)。旧32fields/32B spec/width4096/precision64不再作为V1 formatter规则。

保留正文与完整行各65536B固定工作区，完整行包含元数据前缀和换行。只把完整成功行交给Appender自有batch；恰好release一次Frame并publish回收后，才能执行write/open/reopen/sync/close或等待。所有范围使用pointer+length，禁止未对齐typed load，不保存跨release视图。

## 4. 验证边界

I1已有hash测试仍须保留：known vectors、raw0规范化、各长度尾块、非对齐输入、HW/SW hash-only与copy/hash等价、constexpr参考等价、目标canary与失败不写。源码参照是BQLog util.cpp的实际rotate/fold，而非简化注释。

formatter不再测试旧严格错误枚举或缓存命中；改按ADR-016验收UTF-8状态机和类型输出、容量/安全分支。公共数组入口不能因保留constexpr参考测试而提前hash。当前实施与状态见[剩余V1指南](./V1_REMAINING_IMPLEMENTATION_GUIDE_CHS.md)。本轮仅文档修订，无生产变更与运行测试。
