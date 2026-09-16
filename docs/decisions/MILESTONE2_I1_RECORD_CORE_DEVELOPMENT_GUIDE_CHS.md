# QLog 里程碑二 I1 Record Core 企业级开发规范

> 当前后续入口（2026-09-13）：I1 已按 WSL2 开发范围收口，I2 设计冻结。后续执行 [I2 计划](./MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md) 和 [I2 动手指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)，本文旧任务/行号仅用于 I1 实现历史。

> 2026-09-13 当前状态：Record Core（I1）已按 WSL2 开发范围完成阶段收口。六配置、覆盖率、连续一小时 decoder fuzz、完整性能基线已通过；经用户确认，原生 Linux 发布前复核暂缓，不阻塞后续模块开发。详见 [验收及收口报告](./I1D_ACCEPTANCE_20260913_CHS.md)。下文旧日期进度为历史记录。

- 状态：已接受，可进入实现
- 日期：2026-09-05
- 实现指南更新：2026-09-09；合并第 2、3 轮，仅文档交付，生产代码由维护者实现
- 适用任务：I1 独立 Record Core
- 实现责任：项目维护者编写生产代码；独立评审/测试方补充测试、执行门禁并记录结果
- 规范性质：本文定义接口语义、模块边界、错误合同和验收标准，不提供生产实现代码
- 上位决策：[ADR-007](./ADR-007-self-contained-record-header.md)、
  [ADR-008](./ADR-008-realtime-coarse-admission-timestamp.md)、
  [ADR-009](./ADR-009-v1-packed-tagged-arguments.md)、
  [ADR-010](./ADR-010-v1-backend-c20-format.md)
- 总体计划：[里程碑二 Record 实现指南](./MILESTONE2_RECORD_IMPLEMENTATION_GUIDE_CHS.md)

## 1. 文档权威与变更规则

I1 的目标是交付一个可独立测试、无 Ring 依赖、无格式化依赖的 Record Core。规范优先级固定为：

```text
ADR-007..010
  > 本文
  > MILESTONE2_RECORD_IMPLEMENTATION_GUIDE_CHS.md
  > 其他历史指南和源码注释
```

若实现过程中发现本文与 ADR 冲突，应停止相关代码并先修订 ADR/本文；不得用实现细节反向修改 golden bytes、
hash vectors 或已冻结 wire 值。影响以下内容的修改必须留下新 ADR 或明确的 superseding decision：

- `RecordHeader` 字段、大小、对齐或偏移；
- `ArgumentTag` 数值或 packed payload；
- `crc32c4x64_v1` 原始算法或 stored normalization；
- 字符串/null/Pointer64 语义；
- little-endian、参数数量或 format 上限。

内部错误枚举、文件拆分和非 ABI 类型可以在不改变可观察合同的前提下重构，但必须保持测试和文档同步。

本文中的“必须/禁止”是合入条件，“应”表示除非有记录在案的理由不得偏离，“可以”表示不改变合同的
实现选择。任何为了通过测试而降低边界检查、修改 golden/vector expected 或扩大 I1 职责的做法都不属于
允许的实现选择。

## 2. I1 交付结果

I1 完成后必须具备以下能力：

1. 显式 `qlog::ptr` / `qlog::cstr` 参数包装器；
2. C++ 参数到冻结 `ArgumentTag` 的编译期准入和规范化；
3. 无溢出的 Record 精确计长和一次性准备结果；
4. `crc32c4x64_v1` reference/software/hardware/dispatch 与 copy-and-hash；
5. 从准备结果编码一个完整、自包含的 V1 Record payload；
6. 从不可信的裸 byte 指针和显式长度解码 V1 Record，并返回裸指针加长度的借用型视图；
7. hand-written golden、known-vector、round-trip、corruption、property/fuzz 和 compile-fail 测试；
8. 独立的 hash/measure/encode/decode 性能基线。

I1 只有一个总体验收点。内部压缩为四个密集工作包，不升级成新里程碑：

| 工作包 | 内容 |
|---|---|
| I1-A | wrappers、常量、traits/normalization、checked measure |
| I1-B | CRC32C reference、SW/HW、dispatch、copy-and-hash |
| I1-C | 独立 raw-pointer + explicit-length encoder/decoder 与结果类型 |
| I1-D | 单测、compile-fail、property/fuzz、sanitizer、benchmark 和双编译器门禁 |

工作包可以按依赖顺序提交到同一 I1 分支，但只有全部满足第 17 节 Definition of Done 才算 I1 完成。

### 2.1 性能范围与集中指导（2026-09-09，第 2、3 轮合并）

第 1 轮的具体文件位置、接口骨架和逐函数实现步骤见 [I1-B 动手指南](./MILESTONE2_I1B_HANDS_ON_GUIDE_CHS.md)。

保留当前 wire 和已完成的 I1-A measure：32B Header、packed tagged arguments、
`crc32c4x64_v1`、递归参数规范化和一次 `strlen` 缓存长度均不重做。当前
`PreparedRecord<N>` / `NormalizedArgument` 表示作为第一版基线，不仅因对象 `sizeof` 而重构。

Producer 参数编码优先采用可内联实现，模板定义放在职责对应的头文件。不强制采用“薄模板入口加完全
非模板编码核心”；先检查真实 measure -> encode 组合中固定 tag/宽度、描述符临时写入和分派能否消除。
Decoder 仍按运行时 tag 解码，可以保留非模板实现。可内联不等于已经内联，也不构成性能结论。

允许按同机测量调整 prepared 的内部表示以及 dispatch 的稳定分支/不可变函数指针选择。只有观察到
组合路径存在实质成本才引入 typed prepared 对照；保留成功构造约束、标量快照、字符串借用期、长度缓存、
确定性错误和强失败保证。采用变体时同步修改本规范对应物理表示条款；不改变 wire，不同时维护两套
公共 prepared API。Reference 保持独立易核对；SW/HW 优化实现可共享编译期参数化分块核心。

本次性能探索只围绕三项：hash/copy-and-hash 与预计算、prepared 到可内联参数编码的连接、
measure -> encode 与独立 decode 的组合基线。默认不扩展 ARM、Callsite registry、新 wire、第三种 hash
或大量 SIMD 变体。checked arithmetic、Release 解码校验、目标不变的编码失败保证继续保留。

原三轮计划中，第 1 轮继续使用 I1-B 指南；维护者于 2026-09-08 确认将第 2、3 轮合并，
2026-09-09 根据最新 hash 修改复查更新入口。不逐函数追加教学轮次：

| 轮次 | 范围 | 集中交付 |
|---|---|---|
| 1 | I1-B | 接口/文件职责、CRC 与分块推导、reference/SW/HW、copy-and-hash、dispatch、测试顺序 |
| 2+3 | I1-C、I1-D 与收口 | 第一轮必要补齐、metadata/policy/result、可内联 encoder、独立 decoder、评审/验证/组合基准、优化取舍、文档同步 |

合并轮具体实施入口为 [I1-C 编解码与 I1 收口动手指南](./MILESTONE2_I1CD_HANDS_ON_GUIDE_CHS.md)。
其 A 章保留 2026-09-09 历史诊断；最新完成状态以第 2.2 节为准，不重复已修复的 hash/types/encoder。

本次明确的内部实现方案：`record_types.hpp` 公共类型与共享 metadata 校验；`record_encoder.hpp`
头内编码；`record_decoder.hpp` 声明与 `src/record_decoder.cpp` 解码。policy 使用四个 u64 level mask
与 fallback bool；新的 DecodedArg 采用 16B tag/union/length 表示，现有 producer prepared 不改。
这些内部细化不改变 Record wire 或错误优先级。

项目维护者编写生产实现；指导提供准确签名、关键伪代码、原因、步骤、反例和验收点。
集中指导是教学组织约束，不降低第 17 节 DoD，不把未执行的 native Linux、长时 fuzz 或 coverage 门禁写成完成。
测试文件、测试构建接线、benchmark/验证支持、门禁执行与报告由 Codex 在生产交接后承担；维护者不负责测试。
默认教学分工是维护者完成生产实现；2026-09-10 本轮按用户明确授权由 Codex 完成生产修复与测试，见第 2.2 节。

### 2.2 当前进度快照（2026-09-10，生产修复与验证）

本轮用户授权 Codex 直接完善修复和测试，覆盖 I1-B、公共类型与现有 encoder。
I1-A 保留并随全量回归；I1-B 可调用与正确性基线，以及 I1-C types/encoder 已通过本轮验证。
GCC/Clang Debug 各 88 项通过，Release 各 87 通过/1 个原有 Debug-only skip；
Clang ASan+UBSan 88 项通过；禁用硬件的 GCC Release 82 通过/6 个预期 skip，均无失败。
`record_core` 38 项由 arguments 13、format_hash 16、codec 9 组成；codec 当前仅测 types/encoder。
完整证据与边界见 [2026-09-10 修复与验证报告](./I1_HASH_ENCODER_VALIDATION_20260910_CHS.md)。

下一步是 I1CD D 章 decoder。完整 compile-fail/property/fuzz/coverage、性能与其余 I1-D 门禁仍待完成，
本轮不关闭整个 I1，也不将 WSL 开发结果替代额外 native runner 证据。

## 3. 严格非目标

I1 禁止实现或依赖：

```text
SpscRingBuffer / FrameHeader / WriteHandle / ReadHandle
try_reserve / commit / abort / release
Channel / AsyncLogger / ProducerHandle / TLS 注册
过滤、admission clock、Producer/Backend 统计
BackendWorker、公平扫描、唤醒和 shutdown
c20_format / FormatPlan / parse cache
NullSink / TextFileSink / write / fdatasync
CallsiteId / schema / tagless Record
Compress Sink / BinaryFileHeader / 离线解析
UTF-8 校验、Unicode 显示宽度、字符串截断
源码位置、MPSC、跨线程排序
```

特别禁止让任何 I1 头文件 include `spsc_ring_buffer.hpp`、未来 Logger/Channel/Backend/Sink 头或 formatter。
I1 只处理 Ring payload 内的 Record bytes，不计算外层 `frame_bytes`、tail waste 或 Frame padding。
I1 生产接口和内部持久字段同样禁止使用 `std::span`；连续 byte 范围统一表示为裸指针和独立长度。

## 4. 平台与编译合同

### 4.1 Tier 1

I1 V1 的正式 Tier 1 平台为 native Linux x86-64：

```text
C++20 without language extensions
CHAR_BIT == 8
sizeof(uintptr_t) == 8
native endian == little
sizeof(float) == 4 and IEC 559 binary32
sizeof(double) == 8 and IEC 559 binary64
std::atomic<uint64_t> platform assumption does not enter I1
```

不满足上述 ABI 条件时应在配置或编译期明确失败，而不是生成另一套 wire。Windows/WSL 可做开发验证，
但不作为正式 Linux 性能、TSan 或发布证据。

### 4.2 编译器和 ISA

- GCC 13+ 与 Clang 18+ 分别执行 Debug/Release；
- `qlog` 与全部 I1 test target 都必须设置 `CXX_EXTENSIONS OFF`，不得让测试退回 `gnu++20`；
- x86 CRC32C 只在独立 translation unit 或可靠 function target 中启用 SSE4.2；
- 禁止给整个 `qlog` target 添加 `-msse4.2`、`-march=native` 或等价全局 ISA 选项；
- CPU capability 未确认前不得执行硬件 CRC 指令；不支持的机器必须安全选择 software；
- AArch64 不是当前 Tier 1。若加入 ARM CRC path，在 native AArch64 runner 完成 SW/HW/vector/canary
  验证前不得宣称支持或默认 dispatch 到 hardware。

编译 warning 沿用仓库的 `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`；CI/验收可以把 warning
升级为 error，但不得把 `-Werror` 作为 PUBLIC usage requirement 传播给下游项目。

## 5. 模块与依赖边界

### 5.1 建议文件边界

```text
include/qlog/arguments.hpp
include/qlog/detail/checked_size.hpp
include/qlog/detail/argument_traits.hpp
include/qlog/detail/record_measure.hpp
include/qlog/detail/format_hash.hpp
include/qlog/detail/record_types.hpp
include/qlog/detail/record_encoder.hpp
include/qlog/detail/record_decoder.hpp

src/format_hash.cpp
src/format_hash_software.cpp
src/format_hash_x86_crc32c.cpp
src/format_hash_aarch64_crc32c.cpp      // 仅在实际支持该目标时加入
src/record_decoder.cpp
```

已有且不得改变 wire 的文件：

```text
include/qlog/detail/record_header.hpp
include/qlog/detail/argument_tag.hpp
```

I1 模板定义直接放在职责对应的头文件；`record_measure.hpp` 同时包含声明和模板定义，不创建或包含
`record_measure.inl`。只有不依赖模板参数且值得隐藏实现的 helper 才放 `.cpp`。不得把所有职责堆入一个
`common.hpp`。I1 继续使用现有单一 `qlog` static library，无需为了逻辑分层创建新的公共 library target。

I1-A 的具体类型归属固定为：

- `argument_tag.hpp` 只定义冻结的 wire `ArgumentTag`；`0x00` 保持命名为 `Invalid`，不得改成
  compile-time `Unsupported`，也不在该文件放 matcher 或引入字符串类型；
- `argument_traits.hpp` 定义内部 `ArgumentKind`、`match_argument_kind<T>()`、`ArgumentTraits<T>`、
  `SupportedArgument` 和 `NormalizedArgument`；
- `record_measure.hpp` 定义 `FormatInput`、measure 错误/结果、`PreparedRecord<N>`、measure 声明及全部
  依赖参数 pack 的模板实现；不再拆分 `record_measure.inl`；
- `record_types.hpp` 定义 metadata、policy、共享校验、编码/解码错误、DecodedArg、view/result 和内部构造工厂；
- `record_encoder.hpp` 消费 successful prepared result，包含可内联编码 helper/模板，不重新定义计长规则；
- `record_decoder.hpp` 只声明解码入口并依赖公共类型；实现位于 `src/record_decoder.cpp`，不 include encoder；
- 旧建议 `record_codec.hpp/.cpp` 由上述明确边界取代，不增加无实际消费者的聚合头。

### 5.2 允许的依赖方向

```text
arguments.hpp
  -> argument_traits / normalization
  -> checked measure
  -> record encoder

record_header + argument_tag + checked_size
  -> record encoder / decoder

format_hash
  -> record encoder
```

Decoder 不依赖 public wrappers、argument traits、hash、formatter 或 Ring。Hash 模块不依赖 RecordHeader、
argument codec 或 Logger。依赖必须单向，不允许为方便测试建立循环 include。
`record_types.hpp` 仅依赖基础标准库、record_header、argument_tag 和 record_limits；不反向 include
measure/hash/encoder。这样 decoder 的实际 include 链也满足上述边界，而非仅在运行时不用这些模块。

### 5.3 公共与内部稳定性

- `include/qlog/arguments.hpp` 是 I1 唯一新增 public API，只公开 `ptr/cstr`；
- 其余 I1 类型位于 `qlog::detail`，不承诺下游源码稳定，但其 wire 输出受 ADR 约束；
- argument traits 不是用户扩展点，禁止外部特化来添加自定义 wire 类型；
- hardware 强制路径和 raw-hash 入口只对内部测试可见，不进入 public API。

### 5.4 裸指针与显式长度合同

I1 以及后续直接复用 I1 类型的 Producer/Backend 生产接口统一禁止 `std::span`。物理接口固定使用：

```text
只读 bytes       = const std::byte* data + std::size_t size
可写 bytes       = std::byte* data + std::size_t size
参数槽位          = DecodedArg* data + std::size_t count
prepared format  = const std::byte* data + uint32_t format_bytes
decoded string   = const std::byte* data + uint32_t byte_count
```

要求：

- `size/count > 0` 时相应指针必须非空；长度为 0 时允许空指针，且不得做解引用或无必要的指针算术；
- null 检查不能验证任意非空地址。调用方仍必须保证 source 指向真实可读范围、destination 指向真实可写
  范围，typed workspace 指向已经开始生命周期且正确对齐的对象；悬空、伪造或权限错误地址属于调用方违约；
- 指针不表达所有权；prepared 借用期到 encode 返回，或 reserve/其他前置阶段失败后本次日志操作放弃并销毁
  prepared result，以较早者为准；decoded 借用期只到当前 Frame release；
- `PreparedRecord<N>` 使用 `std::array<NormalizedArgument, N>` 保存编译期固定槽位，但不返回 span；
- 访问器使用 `data()/size()` 或职责更明确的 `format_data()/format_size()`、`arguments_data()/argument_count()`；
- Decoder 仍必须在任何派生指针之前用 subtraction-first 验证长度。取消 `std::span` 不取消边界校验；
- 面向调用方的 checked measure/hash/encode/decode 入口把 `null + nonzero` 作为可恢复 metadata 错误；
- 只有 hash dispatch 内部的 raw backend 使用已验证指针/容量前置条件；standalone 调用方必须走 checked
  入口，Encoder 在完成同等 preflight 后可直接调用 raw backend；
- 该决定只冻结物理 C++ API，不改变 Record wire、packed arguments、hash 或生命周期合同。

## 6. 统一 Record 准备模型

I1 不建立“静态 Record”和“动态 Record”。所有 format 最终产生完全相同的 payload：

```text
[32B RecordHeader][format bytes][packed tagged arguments]
```

为了避免 `qlog::cstr` 二次扫描，主流程不能是“只返回长度，encode 再重新读取原始 metadata”。逻辑上应有
一个调用栈内、非拥有的 prepared result，至少携带：

- 借用的 format 裸指针与显式字节数；
- 可选预计算 stored hash；值 0 表示 encode 阶段执行 copy-and-hash；
- 每个参数规范化后的 tag、标量值或借用 byte 指针与显式字节数；
- 已通过一次 `strlen` 取得的 cstr 长度；
- `format_bytes`、`args_bytes`、`payload_bytes`、`arg_count`；
- 本次测量使用的 `max_payload_bytes`。

本文不强制把 32 个参数物理存成一个大型运行时数组；实现可以利用 parameter pack 保持常见两参数日志的
栈开销较小。但以下语义必须成立：

- measure 成功后不得再次扫描 cstr 或重新求任一字符串长度；
- prepared result 不拥有字符串；允许在同一次日志操作的调用链中从 measure 立即传给 reserve/encode，
  但不能缓存到下一条事件、跨线程保存或人为延长借用期；
- format/字符串/cstr 源从 measure 首次读取开始，直到 encode 返回或 reserve/其他前置阶段失败后本次日志
  操作放弃并销毁 prepared result（以较早者为准），都必须有效且不被并发修改；
- `PreparedRecord<N>` 对象本身也必须存活到上述终点；跨语句保存一个借用了临时 owning string 的 prepared
  result 会悬空，属于调用方违约；
- prepared result 只能由成功的 normalization/measure 创建，调用方不能伪造其长度字段。

### 6.1 `PreparedRecord<N>` 的职责与构造约束

`PreparedRecord<N>` 定义在 `record_measure.hpp`，其中 `N` 是编译期参数数量且必须不大于 32。它不是最终
Record bytes，而是 measure 与 encode 之间的栈内、非拥有、已验证计划：I2 先读取其精确
`payload_size()` 完成一次 reserve，随后 Encoder 复用同一批规范化结果。它至少提供只读 accessor：

```text
format_data() / format_size()
precomputed_stored_hash()
arguments_data() / argument_count()
args_size() / payload_size() / max_payload_size()
```

其参数槽使用 `std::array<NormalizedArgument, N>`；`N == 0` 合法。它不保存 Ring/Channel/Logger、
timestamp、category/level/flags、拥有型字符串、formatter、回调或 span。timestamp 必须在 I2 reserve 成功后
另行取得，不能进入 measure/prepared 阶段。

`PreparedRecord<N>` 必须显式声明 private 构造函数；仅把数据字段写在 `private:` 后面并不能代替该声明，
否则编译器仍可能提供可用的隐式默认构造。构造函数一次接收并初始化全部已经检查、已经安全收窄的字段，
由 `measure_record()` 的 friend 或内部 `RecordMeasureAccess` 调用。不得在构造函数中把未经检查的
`size_t` 直接 cast 成 `uint32_t`。若数组按值传入并以 `std::move(arguments)` 初始化成员，头文件必须直接
包含 `<utility>`；`std::move` 只表达允许移动的值类别转换，不单独构成性能证明。

normalization 成功后不得在 `PreparedRecord<N>` 中同时保存可相互矛盾的 `ArgumentKind` 和
`ArgumentTag`；只保存最终 wire `ArgumentTag`。复制一个仍处于有效借用期的 successful prepared result
不改变语义，但不得缓存到下一次日志操作、跨线程或以其他方式延长借用期。

### 6.2 `MeasureResult<N>` 的 success/failure 表示

`PreparedRecord<N>` 没有公共默认构造，因此 `MeasureResult<N>` 必须是真正的 tagged result，不能通过
公开一个“默认空 PreparedRecord”来凑出失败状态。推荐在 result 内使用不分配内存的
`std::optional<PreparedRecord<N>>` 加固定大小 `MeasureFailure`。独立保存的 failure 成员在所有状态下都必须
value-initialize：success 时 optional engaged，failure 仅在逻辑上不可读；failure 时 optional disengaged 且
failure 有效。这样复制/移动 success result 不会接触未初始化标量。若不愿保存这个始终已初始化的备用对象，
也可使用真正只激活一侧的 tagged union/storage，但不得同时读取非活动成员。公开 accessor 返回指针或先返回
状态，不在正常错误路径调用可能抛 `bad_optional_access` 的 `value()`。

`RecordMeasureAccess` 只负责把已经验证并安全收窄的字段一次性交给 private 构造函数；它不是 public API，
也不能成为任意 detail 调用者绕过 measure 的通用工厂。复制/移动一个已经成功构造的 PreparedRecord 可以
保持默认语义，因为它只复制同一借用计划，不会凭空制造未经验证的长度。

## 7. Public wrappers 与参数准入

### 7.1 `qlog::ptr`

`qlog::ptr` 是显式“把地址当数值记录”的包装器。工厂调用时立即保存 `uintptr_t`/`uint64_t` 数值，
不得把原始对象指针留给 Backend。

| 输入 | 结果 |
|---|---|
| object pointer，包括 `void*` 和显式包装的 `char*` | `Pointer64` |
| null object pointer / 显式 nullptr overload | `Pointer64(0)` |
| function pointer | 编译期拒绝 |
| member pointer | 编译期拒绝 |

Backend 永远不把 `Pointer64` 转回指针或解引用。wrapper 工厂必须 `noexcept`、无分配、无隐式字符串语义。

### 7.2 `qlog::cstr`

V1 `qlog::cstr` 只服务 `char` C 字符串参数，不是 runtime format 入口。runtime format 必须使用显式长度
`std::string_view`/`std::u8string_view`。wrapper 构造时只保存 `const char*`，不执行扫描。

```text
ptr == nullptr
  -> NullUtf8，不调用 strlen

ptr != nullptr
  -> 调用方保证 ptr 指向可读且以 NUL 终止的 char 字符串
  -> measure 恰好调用一次 strlen(ptr)
  -> 首个 NUL 下标成为 byte_length，并缓存 {pointer, byte_length}
```

非空指针无效、不可读或没有 NUL 终止符属于调用方违约，行为与直接调用 `strlen` 相同，不映射成
`MeasureError`。`strlen` 返回值超过 UInt32 时返回参数级 `argument_length_out_of_range`。全部参数完成
规范化后再执行 checked aggregate 和最终 quota 检查，因此 `payload_too_large` 使用非参数下标 `0xFF`。
`max_payload_bytes` 不限制 `strlen` 的读取量；这是换取单一 cstr API、递归/单-fold 单阶段遍历和更少
scratch 的明确取舍。Encoder 必须复用缓存长度，不得再次调用 `strlen`。

### 7.3 traits 匹配顺序

匹配前必须保留字符数组 extent，不能先 decay 成裸指针。准入顺序固定为：

```text
bool
-> plain char
-> signed/unsigned char
-> other integral
-> enum
-> float/double
-> nullptr
-> Ptr wrapper
-> UTF-8 array/string/view
-> CStr wrapper
-> reject
```

额外冻结：

- `volatile` 修饰的参数、指针目标或字符串 view 在 V1 编译期拒绝；普通 `const`/reference 不改变 wire 身份；
- enum underlying `bool` 拒绝；
- enum underlying plain `char` 按平台实际 signedness 归一化为 `Int8/UInt8`，不映射 `Char`；
- `std::byte`、`char8_t/char16_t/char32_t/wchar_t` 不得被通用 integral/enum 分支误接纳；
- 用户隐式转换、`format_as`、formatter 或 ADL 回调不能绕过 allowlist；
- `char/char8_t` 字符串字面量/数组保留 `N - 1` bytes，但一般数组的末元素内容不保证是常量表达式。

类型不支持和静态 33 参数属于 compile-time rejection。受支持 `char/char8_t` 数组若末元素非 NUL，应在
reserve 前返回运行时 `invalid_string_metadata`；不能把所有此类数组误写成 compile-fail 测试。

### 7.4 matcher、类型归一化与 kind/tag 分层

matcher 的签名固定为：

```cpp
template <class T>
[[nodiscard]] consteval ArgumentKind match_argument_kind() noexcept;
```

它没有运行时参数，只返回内部分类。`ArgumentKind` 至少包含 `Unsupported` 和 `CStr`；二者都不是可直接
写入 Record 的 tag。`ArgumentTag` 则只表示最终 wire 类型：`Invalid` 是 `0x00` 的保留非法 tag，
`NullUtf8` 只由运行时确认的 `qlog::cstr(nullptr)` 产生。被拒绝的 `std::byte`、标量
`char8_t/char16_t/char32_t/wchar_t` 必须返回 `ArgumentKind::Unsupported`，绝不能返回 `NullUtf8`。

类型处理顺序必须保留 reference、array extent 和 volatile 信息：

```text
Raw = remove_reference_t<T>

若 Raw 是数组：
  Element = remove_extent_t<Raw>       // 只在确认 array 后移除一维
  先拒绝 volatile Element
  Character = remove_const_t<Element>
  只接受 const char[N] / const char8_t[N]

若 Raw 不是数组：
  先拒绝 top-level volatile Raw
  U = remove_const_t<Raw>
  再执行标量、wrapper、string/view 匹配
```

不得在入口写 `remove_extent_t<T>`：它不会移除引用，且会在直接收到数组类型时丢失 extent。plain char
使用 `is_same_v<U, char>` 检查，并必须位于通用 integral 分支之前；`signed char/unsigned char` 分别映射
`Int8/UInt8`。其他标准整数按 `sizeof(U)` 和 signedness 映射 1/2/4/8B，不能只枚举固定宽度 typedef。
enum 使用 underlying type；underlying `bool` 拒绝，underlying plain char 按平台 signedness 映射
`Int8/UInt8`。

通用 `is_pointer_v<U>` 分支禁止存在，因为它会误接纳裸 object/char/function pointer。只有
`std::nullptr_t` 和已经由 `qlog::ptr()` 验证并数值化的 `PointerArgument` 映射 `Pointer64`；
`CStrArgument` 映射 `ArgumentKind::CStr`。显式长度字符串还必须覆盖 `string/string_view` 与
`u8string/u8string_view`。

`consteval` 在这里用于强制“matcher 没有运行时语义”的接口约束，而不是声称比 `constexpr` 更快。如果
matcher 永远只初始化 `static constexpr` traits，`constexpr` 通常生成相同机器码；选择 `consteval` 是为了
阻止未来把它误用为运行时分类器。matcher 中的 `if constexpr` 条件来自模板类型，因而在实例化时已知。

kind 到固定 tag 的函数使用普通 `switch`：

```cpp
[[nodiscard]] constexpr ArgumentTag wire_tag_for(ArgumentKind kind) noexcept;
```

普通函数参数 `kind` 不是 `if constexpr` 所要求的模板实例化常量，因此不能写
`if constexpr (kind == ...)`。`constexpr` 函数中的普通 `switch/if` 在传入常量时仍可整体常量求值。
`Unsupported/CStr` 可返回 `ArgumentTag::Invalid` 表示“无静态 wire 映射”，但 normalization/Encoder
不得把该 Invalid 当成可编码参数；CStr 必须先根据值产生 `NullUtf8` 或 `Utf8String`。

## 8. Checked measure 合同

### 8.1 输入与输出

Measure 输入是受支持的 format、参数 pack 和本 Channel 的 `max_payload_bytes` 数值，不接触 Channel 对象。
成功结果满足：

```text
args_bytes    = sum(frozen_encoded_size(normalized_arg))
payload_bytes = 32 + format_bytes + args_bytes
arg_count     = number of normalized arguments
```

精确 `encoded_size` 必须根据 `NormalizedArgument::tag`，而不是先前的 `ArgumentKind` 计算，因为同一个
`ArgumentKind::CStr` 可在运行时产生两个不同结果：null cstr 为 `NullUtf8` 且只占 1B，非空 cstr 为
`Utf8String` 且占 `1 + 4 + byte_count`。建议采用与 checked arithmetic 一致的接口：

```cpp
[[nodiscard]] constexpr bool encoded_size(
    const NormalizedArgument& argument,
    std::size_t& result) noexcept;
```

固定结果为 `Bool/Char/Int8/UInt8 = 2`、`Int16/UInt16 = 3`、`Int32/UInt32/F32 = 5`、
`Int64/UInt64/F64/Pointer64 = 9`、`Utf8String = 5 + byte_count`、`NullUtf8 = 1`。
`ArgumentTag::Invalid` 必须返回 false，且所有失败路径不得修改 `result`；不能把 Invalid 静默解释为正常的
零长度参数。

必须同时满足：

```text
format_bytes <= 8192
arg_count <= 32
format_bytes and args_bytes representable by uint32_t
arg_count representable by uint16_t
32 <= max_payload_bytes <= UINT32_MAX
payload_bytes representable by size_t and uint32_t
payload_bytes <= max_payload_bytes
```

`8192` 是 format 自身上限，不表示默认 8KiB Channel 能容纳 8192B format 加 Header。空 format 和零参数
Record 合法，只要总 payload 可接纳。`max_payload_bytes < 32` 或 `max_payload_bytes > UINT32_MAX` 返回
`invalid_limits`；实际 Channel 还必须服从 Ring 更严格的容量上限，但 I1 不 include 或复制 Ring 配置规则。

### 8.2 确定性错误与优先级

I1 对调用方可观察的运行时 `MeasureError` 集合固定为：

```text
invalid_limits
invalid_format_metadata
format_too_large
invalid_string_metadata
argument_length_out_of_range
args_length_out_of_range
size_overflow
record_length_out_of_range
payload_too_large
```

错误含义固定为：

| 错误 | 含义 |
|---|---|
| `argument_length_out_of_range` | 单个显式长度字符串无法用 wire u32 length 表示 |
| `args_length_out_of_range` | checked aggregate `args_bytes` 成功，但结果大于 `UINT32_MAX` |
| `size_overflow` | 任一中间 checked add/multiply 无法用 `size_t` 表示 |
| `record_length_out_of_range` | total checked sum 成功，但 Record payload 大于 `UINT32_MAX` |
| `payload_too_large` | Record 可由 wire 表示，但超过本次合法 `max_payload_bytes` |

不支持类型与编译期可知的参数超限不进入运行时枚举。错误结果附带固定大小的 `argument_index`/byte count，
不得构造诊断字符串或记录日志。非参数错误的 `argument_index` 固定为 `0xFF`；参数错误使用原 parameter
pack 的从零开始下标。`byte_count` 使用触发判断的可观察量：format/string 错误使用其声明长度，
args/Record/quota 错误使用已成功计算的对应总长，checked overflow 使用导致失败的右操作数。

检查优先级固定为：

```text
compile-time type/count admission
-> invalid limits
-> format metadata
-> format limit
-> each argument from left to right: metadata/strlen normalization, then per-argument representability
-> aggregate args checked sum, then u32 representability
-> total checked sum, then u32 representability
-> final max_payload_bytes quota; this payload_too_large uses argument_index 0xFF
```

极大长度/overflow 单测通过内部 checked-size seam 注入，不构造违反标准库前置条件的伪造 string_view。

### 8.3 热路径限制

- 不解析 `{}` 或校验 UTF-8；
- 不读取 clock，不 reserve，不更新统计；
- 无分配、无锁、无 syscall、无 shared atomic RMW；
- 固定宽度参数的 tag/size 由类型决定；
- `std::string[_view]` 使用一次 `size()`；非空 cstr 调用一次 `strlen`；
- 所有加法/乘法在转换到窄整数前完成范围检查。

### 8.4 单阶段递归 parameter-pack 遍历

当前实现练习选择递归模板展开，而不是 fold expression。递归层只负责“处理第一个参数，再处理剩余参数”，
所有实际规范化逻辑放在只依赖当前参数类型的 `normalize_one<T>` 中：

```cpp
template <std::size_t Index>
[[nodiscard]] bool normalize_pack(State&) noexcept {
    return true;
}

template <std::size_t Index, class First, class... Rest>
[[nodiscard]] bool normalize_pack(State& state, const First& first,
                                  const Rest&... rest) noexcept {
    if (!normalize_one(state, Index, first)) {
        return false;
    }
    return normalize_pack<Index + 1U>(state, rest...);
}
```

递归不变量是：进入 `normalize_pack<Index>` 时，`[0, Index)` 已经保存最终
`NormalizedArgument`，当前只处理原参数包下标 `Index`。空参数包命中基础重载并返回 true；首个参数错误
立即返回 false，因此运行时错误顺序天然从左到右。`normalize_one` 接收普通 `std::size_t index`，不要把
`Index` 继续作为其模板参数，否则相同 `T` 出现在不同位置会产生不必要的重复实例。

入口保留 `Args&&...` 用于正确推导类型并保留数组 extent，但递归 helper 直接读取命名形参并接收
`const T&`；不得 `std::forward`/`std::move`，不得构造会复制字符串并 decay 数组的 tuple。非空 cstr 在
对应的 `normalize_one` 中调用一次 `strlen`，之后与 string/view 一样只保存 `{pointer, byte_length}`。

异构参数规范化后，最终 exact `args_size` 通过同构 `NormalizedArgument` 数组的普通 checked loop 聚合；
这是第二次访问小型描述符数组，不是再次展开参数包，也不会再次调用 `size()` 或 `strlen`。复杂度为
`O(N + strlen 实际读取字节数)`，且不得引入分配、虚调用、锁、系统调用或 shared atomic RMW。递归与
fold 的运行性能必须以 Release 汇编和基准判断；递归实现的主要工程代价是更多模板实例与更深诊断链。

开始实现 `measure_record()` 前，`argument_traits.hpp` 必须先通过 self-contained 编译与准入矩阵测试，
`encoded_size()` 必须证明每个成功 tag 写入精确结果、`Invalid`/兜底失败保持输出参数不变；否则后续模板
诊断和总长度结果都不可信。

### 8.5 与 BQLog/fmt 的对照及 `strlen` 决策（2026-09-07）

BQLog 的 Producer 也采用“先计长、再精确申请、最后编码”的两阶段结构。参考 checkout 中，
`bq_log_wrapper_tools.h` 的 `make_size_seq<true>` 第一次遍历参数并只为动态长度参数保存 `size_t`，
`bq_log_impl.h` 的 `_do_log_args_fill` 第二次遍历并复用该长度；因此 parameter pack 被处理两次，
但 C 字符串内容只扫描一次。BQLog 对真正的 `char*` 使用 `__builtin_strlen/strlen`，数组使用 extent，
string/view 类使用 `size()`。可借鉴的是“固定尺寸编译期化、动态长度缓存、精确 reserve、encode 不再求长”；
不可照搬的是它的 4B 参数对齐、较宽松的 pointer/POD/custom-type 准入，以及部分未经 checked arithmetic
保护的 `size_t -> uint32_t` 窄化。

fmt 的参数存储同样把编译期类型 descriptor 与运行时 value/string-view 分离；已知长度字符串保存
`{pointer, size}`，`const char*` 则保存为 cstring 并在同步格式化时通过 `basic_string_view(s)` 调用
`__builtin_strlen`。fmt 的 `formatted_size` 会完整运行一次格式解析和 formatter，并不是 QLog 这种只计算
Record wire 长度的 Producer preflight。fmt 的借用参数存储也不是稳定 wire ABI，不能整体复制进 Ring。

当前决定与 BQLog/fmt 的 cstring 前置条件对齐：wrapper 为 `qlog::cstr(ptr)`；非空 `ptr` 由调用方保证
可读且 NUL 终止，measure 恰好调用一次 `strlen` 并缓存长度。此前的 `max_scan`、`invalid_cstr`、共享 cstr
内容预算和双阶段 fold 合同由本决定取代。代价是 quota 不再限制 `strlen` 的读取量，非法或未终止指针属于
调用方违约；收益是 cstr API、错误模型、scratch 与遍历控制流更小。普通热路径仍优先使用字面量、
`std::string[_view]` 或 `std::u8string[_view]`，它们分别从 extent 或 `size()` 取得长度，不扫描内容。

当前学习实现选择第 8.4 节的递归模板 driver。driver 只依赖 `Index/First/Rest...` 并调用单参数
`normalize_one<T>`；`strlen`、UTF-8 metadata 检查和 checked aggregate 等逻辑应保持在单参数或非模板 helper
中，避免把完整业务逻辑沿递归层重复实例化。
`RecordMeasureAccess` 的 success/failure factory 保持 private，并只精确 friend 受约束的
`measure_record` 模板，防止其他 detail 调用者绕过校验伪造 successful `PreparedRecord`。

递归版本完成后必须重新执行 Release codegen sanity check；此前 fold 版本的汇编观察不能作为递归版本证据。
该检查只用于发现明显抽象开销，不是吞吐/延迟结论，最终性能仍由第 15 节基准决定。

## 9. Hash 子系统合同

### 9.1 API 分层

命名必须区分 raw 与 stored，禁止以一个含糊的 `hash()` 同时表示两者：

| 层 | 语义 |
|---|---|
| constexpr/reference raw | ADR-010 的精确四 lane 算法 |
| software raw hash-only | 不复制，只计算 raw |
| software raw copy-and-hash | 精确复制并计算 raw |
| hardware raw hash-only | 可用 ISA 下与 reference 逐位一致 |
| hardware raw copy-and-hash | 可用 ISA 下与 reference 逐位一致 |
| production stored wrapper | `raw == 0 ? 1 : raw` |

正常 Encoder 对外只产生 stored 语义；内部调用 dispatch raw backend 后恰好执行一次
`raw == 0 ? 1 : raw`。standalone raw 入口只服务算法实现与测试；Header 的 0 是“未计算”sentinel，不是
raw 算法的空输入结果。

### 9.2 immutable dispatch

I1 定义一个窄、不可变的 `FormatHashDispatch` 逻辑对象，同时持有 hash-only 与 copy-and-hash 入口：

- 在 Core/Logger 发布给 Producer 之前由冷路径工厂创建；对象不能默认构造或聚合伪造；
- 创建后只读，可被多个线程并发调用；
- 不在第一次日志调用中执行 lazy initialization、`call_once`、锁或共享 RMW；
- automatic 构造始终可退回 software；hardware 只有“已编译且 CPU 支持”时可选；
- test-only forced hardware 在能力不足时由工厂返回 `backend_unavailable`，不产生 dispatch，也不得执行
  非法指令；成功构造后的 dispatch 在每次 hash 调用中不再检查 backend 可用性；
- dispatch 方式是直接分支还是不可变函数入口可以用基准选择，但不能改变结果。

I1 不包含 Channel；I2 只保存/引用已构造的 dispatch 和 Channel 级 hash algorithm descriptor。

### 9.3 copy-and-hash memory contract

- checked hash-only 接收 `source + source_size`；checked copy-and-hash 接收
  `source + source_size + destination + destination_capacity`。运行时检查顺序固定为 source metadata、
  destination metadata、destination capacity；前者只返回 `invalid_source_metadata`，后者还可返回
  `invalid_destination_metadata/destination_too_small`。`backend_unavailable` 只属于 dispatch 工厂；
- `FormatHashDispatch` 中的函数指针只采用已经验证后的 raw 签名
  `(source, size)` / `(source, destination, size)`；`size > 0` 时两指针非空、目标容量已足够且范围不重叠
  是 raw backend 前置条件；
- production automatic dispatch 必须始终可退回 software；copy destination 容量不足返回
  `destination_too_small`；
- source/destination 的 `[data, data + size)` 范围不重叠；
- destination 容量不足时返回错误且目标完全不变；
- 成功只改写前 `source_size` bytes，多余尾部和两侧 canary 不变；
- destination 前 `source_size` bytes 必须逐字节等于 source；
- size 0 不解引用任一 data，也不对 null pointer 做算术；
- 固定宽度读取使用局部值加 `memcpy`/显式 LE load，禁止未对齐 typed-pointer；
- runtime format 不得出现“完整 hash 扫描 + 第二次 memcpy”；重叠尾窗口属于算法本身，不算第二次全扫描。

### 9.4 known vectors

以下 raw 值是永久测试输入，expected 不得由生产 hash 代码生成：

| 输入 | raw hash | stored hash |
|---|---:|---:|
| empty | `0x0000000000000000` | `0x0000000000000001` |
| `a` | `0x33bbc03300000000` | 同 raw |
| `{}` | `0x000000000e3ee044` | 同 raw |
| `abc` | `0x33bbc033d64581af` | 同 raw |
| `123456789` | `0xb4ee537e6087809a` | 同 raw |
| 32 个 `0x00` | `0x187cd3cfe93daadb` | 同 raw |
| bytes `0x00..0x20` | `0xe166cd00cec7b109` | 同 raw |
| bytes `0x00..0x3f` | `0x977cb6d90d3cc2e4` | 同 raw |

实现必须以 ADR-010 的 polynomial、lane seed、长度分段、重叠尾窗和 rotate/fold 为规范，不以 BQLog
头文件中省略 rotate 的简化注释为依据。

### 9.5 短输入、字面量与 raw/stored 实现约束

`len = 1..3` 时维护从 0 开始的 byte offset：`len & 2` 处理 offset 0 的 2B 后把 offset 增加 2；
`len & 1` 再处理当前 offset 的 1B，即原输入的 `len - 1`。copy-and-hash 使用相同 source/destination
offset，不能把 3B 情况的最后一 byte 再从 offset 0 读取。

C++20 字面量 constexpr 路径通过数组下标取得 `char/char8_t` 的无符号 8-bit 值，不依赖常量求值中不允许的
`reinterpret_cast<const std::byte*>`。运行时 raw backend 才接收 byte pointer。dispatch 函数指针始终返回
raw hash；stored normalization 只在 stored wrapper 或 Encoder 中执行一次。非零 precomputed stored hash
必须由同一算法针对同一 format bytes 得到，Encoder 不重新 hash 验证。

## 10. Record metadata validation policy

当前 ADR 尚未冻结 public LogLevel 的具体名称和 wire 数值；I1 不得自行照搬 BQLog 或假设 `0..5`。
同时，`fallback_valid` 是否允许取决于 Channel clock descriptor，而 I1 又禁止依赖 Channel。

解决方式是由 I1 定义窄、不可变、纯值语义的 `RecordValidationPolicy`，逻辑内容仅包括：

```text
valid_level_values          // 对全部 uint8_t 值给出合法/非法结论的固定 mask/table
fallback_timestamp_allowed // Channel 是否声明 fallback source
```

要求：

- policy 不包含 Channel 指针、回调、虚函数或动态容器；
- I2 从已冻结的 LogLevel/API 和 Channel clock descriptor 构造；
- Encoder/Decoder 接受只读 policy，并返回 `invalid_level` 或 `fallback_timestamp_not_configured`；
- policy 的物理表示是内部实现细节，但查询必须 O(1)、`noexcept`、无分配；
- `record_abi_version` 与 `hash_algorithm` 由调用方在 Channel 注册/dispatch 前校验，I1 的 `decode_v1`
  不 include Channel，也不逐 Record 分支选择版本。

这样 I1 可以完整测试结构和 policy 行为，同时不会由实现指南偷偷冻结尚未讨论的 public level 数值。

2026-09-08 确认、2026-09-09 具体化的第一版表示为 `std::array<uint64_t,4>` 与 fallback bool，
完整参数构造、无 setter，查询按 `level >> 6` 定位 word、按 `level & 63` 定位 bit。全零 mask 合法，
含义是全部拒绝；四个 mask 表示所有 uint8 值，不能只接受小于 64 的 level。
入口按 `const RecordValidationPolicy&` 传递，不逐 Record 复制整个位图。

共用的 `RecordMetadata` 只保存 time_value/category_id/level/flags。
头内 `record_metadata_impl::validate_record_metadata` 返回 optional<MetadataError>，
严格按 level、unknown flags、reserved status、time/status、fallback policy 检查；无错误为 nullopt。
encoder/decoder 分别映射为自身错误（decoder 额外生成固定 offset/index），不用枚举数值强转映射。
primary/fallback time_value=0 合法；只有 time_unavailable 配非零时间返回 invalid_time_value。

## 11. Encoder 合同

### 11.1 输入

Encoder 的逻辑输入是：

```text
std::byte* destination + std::size_t destination_size
+ successful prepared measure
+ caller metadata to validate: time_value/category_id/level/flags
+ immutable RecordValidationPolicy
+ immutable FormatHashDispatch
```

Header 中的 format/args 长度和 arg_count 必须由 Core 从 prepared result 生成，不允许调用方覆盖。
Encoder 不写外层 FrameHeader 或 Ring padding。

### 11.2 强失败保证

I1 对调用方可观察的 `EncodeError` 集合固定为：

```text
invalid_destination_metadata
destination_size_mismatch
invalid_level
unknown_flags
reserved_timestamp_status
invalid_time_value
fallback_timestamp_not_configured
```

合同固定为：

- `destination_size > 0 && destination == nullptr` 先返回 `invalid_destination_metadata`；
- destination 必须恰好等于 measured `payload_bytes`；
- 所有可恢复检查在第一次写入前完成；
- 任一返回错误时 `bytes_written == 0`，目标 `[destination, destination + destination_size)` 每个 byte 完全不变；
- 成功时恰好写满 destination，`bytes_written == measured payload_bytes`；
- 一旦首次写入开始，基于成功 prepared result 的剩余步骤在合同上不可失败；
- destination 的完整可写范围不得与 prepared 中任何非空 format/string 借用范围重叠；借用源无效、发生
  并发修改或与 destination 重叠属于调用方违约，不伪装成 EncodeError，因为裸指针接口无法可移植地证明
  任意两个地址范围的来源与有效性；
- 非零 `precomputed_stored_hash` 必须对应当前 format bytes 和同一 V1 hash 算法；该条件由创建
  `FormatInput` 的内部入口保证，Encoder 不重新扫描验证。

恢复性错误优先级固定为：

```text
destination pointer/size metadata
-> destination exact-size mismatch
-> level policy
-> unknown flag bits
-> reserved timestamp status
-> invalid time/status value
-> fallback timestamp policy
-> first destination write
```

成功构造的 `FormatHashDispatch` 永远包含可调用 backend，因此 Encoder 不需要 hash-backend EncodeError。
prepared 已经证明 format/string metadata 与精确长度；Encoder 在首次写入前完成上述等价 preflight 后，
调用 dispatch raw backend 不属于绕过检查。

### 11.3 成功结果

- RecordHeader 的全部字段完整且与 frozen offset 一致；
- format 从 offset 32 开始，不含 terminator 或 padding；
- runtime format 通过 active copy-and-hash 写入；literal 可以复制 bytes 并使用 constexpr stored hash；
- 正常 Encoder 始终写非零 stored hash；
- arguments 严格按 `[u8 tag][payload]` 连续写入；
- 字符串长度为 u32 little-endian，NullUtf8 无 payload；
- scalar/float/pointer 通过局部值与 byte copy 写入，不执行未对齐 typed store；
- 最终写入字节数必须与 measure 完全相等。

Encoder 不提供“主动写入 hash 0”的正常选项。测试 hash 0 兼容输入时应构造/修改 hand-written record bytes。

### 11.4 推荐写入顺序

先在栈上零初始化并完整赋值局部 `RecordHeader`，但暂不写 Header。destination 从 offset 32 开始：运行时
format 由 raw copy-and-hash 同时复制并取得 raw hash，预计算 format 只复制；随后编码全部 arguments。最终
offset 恰好等于 `payload_size` 必须已经由 private `PreparedRecord` 构造不变量保证；首字节写入后只允许用
Debug assert 和测试核对该相等性，不得再把它转换成可恢复错误。将 raw 规范化为 stored、补齐局部 Header 后，
最后一次性复制 32B Header。这个顺序不是 wire ABI 的额外字段规则，而是让“首次写入后无恢复性失败”和
“Header 只在成功路径完成”更容易审计。所有零长度 copy 都先按长度分支，不能依赖向 `memcpy` 传空指针。

## 12. Decoder 合同

### 12.1 边界

I1 Decoder 的输入是调用方已经界定的一个完整 Ring payload
`(const std::byte* payload_data, std::size_t payload_size)`、一个准确的 32 槽 caller-owned
`(DecodedArg* workspace, std::size_t workspace_count)` 和 `RecordValidationPolicy`；
`workspace_count` 必须恰好为 32 且 `workspace` 非空，否则返回 `invalid_workspace`。当
`payload_size > 0 && payload_data == nullptr` 时返回 `invalid_payload_metadata`。外层 FrameHeader/Geometry
的可信性由 Ring/I3 保证，不属于 I1。

这些恢复性检查只覆盖可观察 metadata。调用方仍须保证 `[payload_data, payload_data + payload_size)` 是
真实可读范围；workspace 指向至少 32 个已经开始生命周期、满足 `alignof(DecodedArg)` 且在调用期间可写的
对象，并且不与 payload 范围重叠。悬空或伪造的非空地址不是 Decoder 可恢复错误。“不可信 payload”指
bytes 内容不可信，不表示任意坏地址都能安全读取。

Record payload 始终视为不可信数据。以下检查在 Debug/Release 永久启用，不受
`QLOG_ENABLE_RING_VALIDATION` 控制：

```text
payload pointer/size metadata
-> workspace pointer/count
-> payload size >= 32
-> memcpy Header 到对齐局部值
-> level policy
-> unknown flags
-> timestamp status/time/policy
-> format limit, then format pointer-length range
-> args exact pointer-length range
-> arg_count <= 32
-> arguments left to right: tag/value/string bounds
-> decoded_count == arg_count
-> cursor == args_end
```

范围判断采用 subtraction-first；任何 `size_t -> uint32_t/uint16_t` 转换都先检查可表示性。

### 12.2 DecodeError

I1 对调用方可观察的 `DecodeError` 集合固定为：

```text
invalid_payload_metadata
invalid_workspace
record_too_small
invalid_level
unknown_flags
reserved_timestamp_status
invalid_time_value
fallback_timestamp_not_configured
format_too_large
invalid_format_length
invalid_args_length
arg_count_exceeded
invalid_or_unknown_tag
invalid_bool
truncated_value
truncated_string_length
truncated_string
decoded_count_mismatch
trailing_args_bytes
```

错误优先级就是 12.1 的顺序；参数内从左向右，先 tag、后固定值/长度、再 payload。错误结果携带固定大小的
payload-relative offset 和 argument index，不创建文本、不计数、不写 stderr、不递归记录日志。

其中先以 `format_bytes > 8192` 返回 `format_too_large`，再以剩余 payload 校验
`invalid_format_length`；这一优先级在两项同时异常时也不改变。

`flags & 0x03 == 3` 返回 `reserved_timestamp_status`，但不得把该 bit pattern 命名成“Record invalid flag”。
`time_unavailable` 要求 `time_value == 0`；`fallback_valid` 且 policy 不允许时返回
`fallback_timestamp_not_configured`。

error offset/index 固定为：

- 非参数错误的 `argument_index` 为 `0xFF`；参数错误使用当前从零开始的参数编号；
- `invalid_payload_metadata/invalid_workspace` 的 offset 为 0；`record_too_small` 的 offset 为当前
  `payload_size`，即第一个缺失 Header byte；
- Header 语义/长度错误指向导致失败的字段 offset：time 0、format length 16、args length 20、arg count 28、
  level 30、flags/status/policy 31；
- invalid/unknown tag 指向 tag byte，`invalid_bool` 指向 bool value byte；
- fixed value、string length prefix 或 string bytes 的截断错误指向第一个缺失 byte，也就是当前
  `args_end`；
- 在读取下一期待参数的 tag 前已经到达 `args_end` 时，返回 `decoded_count_mismatch`，offset 为
  `args_end`，index 为下一期待参数；
- 声明的 `arg_count` 已解完但仍有 bytes 时，返回 `trailing_args_bytes`，offset 为第一个 trailing byte，
  index 为 Header `arg_count`。

### 12.3 成功视图与借用

I1 定义并拥有以下逻辑类型的职责：

- `DecodedArg`：数字按值保存，string 保存借用 `const std::byte* + uint32_t byte_count`，Pointer64 只保存 uint64；
- `DecodedRecordView`：Header 值副本、format 的裸指针与长度、`DecodedArg* + argument_count`；
- `DecodeResult`：成功视图，或 `DecodeError`、`std::size_t error_offset`、
  `std::uint8_t argument_index`；后者使用 `0xFF` 表示非参数错误。

I3 不得重复定义这些类型；I3 只拥有每个 BackendWorker 的 workspace、调用 Decoder、统计结果并管理
Frame release。

成功必须满足：

- Header 是对齐值副本；
- format 和所有 string view 完全位于 input `[payload_data, payload_data + payload_size)`；
- decoded count 恰好等于 Header arg_count；
- cursor 恰好等于 args_end；
- `arguments_data()` 只公开 `const DecodedArg*`；view 的有效期是 input payload/Frame 与 caller workspace
  有效期的交集，并在 workspace 被下一次 decode 复用或覆盖时立即结束。

失败不返回可用 Record view；workspace 已写槽位全部视为未定义逻辑结果，调用方不得读取。

#### 12.3.1 已确认的内部表示与文件归属

`DecodedArg` 在 `record_types.hpp` 中采用 `union Value { uint64_t bits; const std::byte* bytes; }`，
union 之外依次保存 `uint32_t byte_count` 和 `ArgumentTag tag`。Tier 1 固定 sizeof=16、alignof=8，
并保持 standard-layout/trivially-copyable；32 槽对象共 512B。这不是 wire 结构，也不是性能通过声明。
默认 Invalid 激活 bits=0；仅 Utf8String 激活 bytes（空字符串亦如此）；其他 tag 用 bits，NullUtf8 bits=0。
固定值以等宽 unsigned bit pattern 零扩展保存，解释 signed 时先等宽 bit_cast 后符号扩展，float 同理保留位模式。
禁止读取未激活的 union 成员，不清零/序列化 padding，不将该布局回推到 producer 的 NormalizedArgument。

DecodedRecordView 只保存 Header 值副本、format 指针和 const DecodedArg*；format_size/argument_count
从 Header 派生。EncodeResult/DecodeResult 使用完整的私有成功/失败构造及 detail 内 RecordCodecAccess 工厂；
所有备用逻辑字段初始化，访问器成功/失败互斥，不让 optional::emplace 越过私有构造权限。
decoder header 只 include 公共类型，模板 encoder 独立在 record_encoder.hpp；准确签名/定义顺序见合并指南 B～D。

### 12.4 Decoder 明确不做

- 不计算或校验 hash；任何 uint64 `format_hash`，包括 0，都可结构解码；
- 不比较 hash 与 format 内容；碰撞验证属于 I4 FormatPlan cache；
- 不解析 brace，不匹配参数和 format field；
- 不验证 UTF-8，不格式化，不做统计，不 release Frame；
- 不尝试根据未知 tag 猜测长度或在 args 内重新同步。

## 13. 异常、内存与线程安全

以下逻辑操作全部必须 `noexcept`：

- wrapper factories/accessors；
- traits normalization 与 checked measure；
- hash capability/dispatch creation 和执行；
- encode/decode 与 status/result accessors。

I1 生产路径禁止：

```text
new/delete and allocator-backed containers
std::string/vector/function/regex as owned working state
mutex/condition_variable/call_once
shared mutable global state or shared atomic RMW
thread_local state
locale/iostream/formatters/user callbacks
exceptions as ordinary error transport
```

CRC lookup table 和 dispatch 发布后必须不可变。Hash、measure、encode、decode 对不同输入可并发重入。
如果实现引入 lazy/global mutable dispatch，则必须先修改本规范，并增加 native Linux TSan 初始化竞态门禁；
默认方案不允许该设计。

## 14. 测试架构

### 14.1 targets 和 labels

2026-09-10 更新：hash 与 types/encoder 测试已新增；后文 2026-09-08 数量为历史记录，最新结果见 [2026-09-10 修复与验证报告](./I1_HASH_ENCODER_VALIDATION_20260910_CHS.md)。

I1 测试不塞回 `qlog_record_header_test`，必须新增并拆为：

| Target | 主要内容 | Labels |
|---|---|---|
| `qlog_format_hash_test` | vectors、SW/HW、copy、dispatch | `record_core;format_hash` |
| `qlog_argument_model_test` | wrappers、traits、measure | `record_core;arguments` |
| `qlog_record_codec_test` | golden、round-trip、corruption | `record_core;codec` |
| `qlog_record_property_test` | deterministic property/corpus replay | `record_core;property` |

截至 2026-09-08，项目维护者已完成递归版 `record_measure_impl` 与 `measure_record`：异构参数包按原始顺序
规范化一次，随后通过 `NormalizedArgument` 数组汇总精确大小；非空 `qlog::cstr` 恰好调用一次 `strlen`，
Encoder 后续必须复用缓存长度。`qlog_argument_model_test` 已在 `tests/CMakeLists.txt` 注册并使用
`record_core;arguments` labels，现有 13 项 wrappers/traits/measure 测试在标准 Debug 与 Release 构建中均
为 13/13 通过。同期全量测试结果为 Debug 63/63 通过；Release 62 项通过、1 项既有的 Debug-only
corrupt-frame 测试按预期跳过；`./scripts/format.sh --check` 与 `git diff --check` 通过。

以上是当前 WSL 开发证据，只关闭 I1-A 的现有实现与行为测试，不等于 I1 总体验收完成。其余
`qlog_format_hash_test`、`qlog_record_codec_test`、`qlog_record_property_test` 仍待对应工作包实现，I1-A 的
扩展边界、compile-fail、property/fuzz、sanitizer、双编译器与性能证据仍按后续条款补齐。因此不能把当前
`ctest -L record_core` 结果单独作为 I1 完成证据。
所有 label 过滤命令必须带 `--no-tests=error`，防止零测试假绿。

每个新增 public/detail header 必须有“自身作为首个 include”的 self-contained translation unit。

compile-fail 使用专用 CMake driver，同时验证：

1. 编译确实失败；
2. 输出命中稳定的 QLog constraint/diagnostic token；
3. 相邻 compile-pass control 成功。

不能仅设置 `WILL_FAIL`，否则缺文件或工具链错误也会被误判为通过。compile-fail source 不加入普通
`add_executable` 或默认 build；专用 CMake driver 在隔离 build directory 中分别验证 compile-pass 成功、
compile-fail 失败以及 stdout/stderr 命中 QLog 自有稳定 diagnostic token。`__int128` case 只在支持该语法的
GCC/Clang 平台注册。

### 14.2 Hash 必测矩阵

- 第 9.4 节全部 hand-written known vectors；
- `len = 0..128` 全覆盖；
- `255/256/257` 和 `8191/8192/8193`；
- source/destination offset `0..31`；
- 前后 canary、source 不变、目标只写 exact length；
- constexpr/reference/SW/HW × hash-only/copy-and-hash 逐位一致；
- forced software、automatic dispatch、可用机器上的 forced hardware；
- CPU 不支持 hardware 时只允许 capability skip，SW/fallback 仍必须运行；
- designated hardware runner 上不允许 HW case 被 skip；
- `(nullptr, 0)` 空输入不解引用或执行 pointer arithmetic。

### 14.3 Wrappers、traits 与 measure

正向测试至少覆盖：

- 所有冻结 tag、cv/ref 归一化、native integer alias；
- enum underlying width/signedness，以及 enum-bool rejection；
- Pointer64 null/non-null/char address；
- cstr null、空串、普通 NUL 终止串、首个 NUL 之前的 byte length；
- 多个 cstr 的缓存长度与 exact/one-byte-too-large payload；
- empty view、embedded NUL、char/char8 array/string/view；
- fixed encoded sizes、无 padding、0/1/31/32 arguments；
- 8192/8193 format，exact payload capacity 与 one-byte-too-large；
- checked add/multiply 的 exact max/overflow；
- invalid limits、单参数/args/Record u32 表示边界与 Channel quota 分类；
- measure 结果等于实际成功写入长度；
- cstr 的 `strlen` 结果复用，encode 不发生第二次扫描；
- MeasureError 优先级和 argument index。

compile-fail 至少覆盖：

```text
bare const/mutable char pointer
unwrapped object pointer
function/member pointer
volatile argument
long double
signed/unsigned int128
scalar char8_t and all wide characters/strings
std::byte
enum with bool underlying
container and chrono
user type with implicit conversion / format_as
33 arguments
```

一般 `char/char8_t` 数组末尾非 NUL 是运行时 metadata test，不是 compile-fail。

### 14.4 Golden 与 round-trip

hand-written golden 至少包含：

1. empty format + zero arguments；
2. Header 每个字段的固定 little-endian offset；
3. 所有 fixed-width tags；
4. empty/embedded-NUL string 与 NullUtf8；
5. signed/unsigned 边界；
6. F32/F64 正负零、infinity 和多个 NaN payload；
7. Pointer64 zero/nonzero；
8. mixed 32-argument Record；
9. literal-prehash 与 runtime-copyhash 生成的相同 Record。

golden expected bytes 不得调用生产 encoder、LE helper 或 hash 实现生成。Round-trip 是第二层验证，不能替代
hand-written golden。Float/NaN 按 bit pattern 比较，pointer 按 uint64，string 按 bytes。

### 14.5 Corruption 和所有截断点

必须覆盖：

- payload 0..31；
- 每个选定 canonical/golden seed Record 的 `0..N-1` 全部 prefix truncation；
- format/args length 越界和不等于剩余 bytes；
- format 8193；
- arg_count 33/65535；
- unknown flag bits、reserved timestamp status、time/context mismatch；
- invalid/unknown tag；
- Bool payload 2..255；
- 每个 fixed value 的全部内部截断点；
- string length prefix 的全部截断点、length 超剩余、bytes 截断；
- decoded count mismatch 与 trailing args bytes；
- format 中的 NUL、非法 UTF-8 和 brace bytes 仍可结构解码；
- hash 0 可解码，Decoder 不执行 identity validation。

要区分“原始 payload 缩短但 Header 不变”和“同步改小 args_bytes 后产生内部参数截断”两种测试。

每个错误路径必须证明：

- 对至少 `size` bytes 真实可读的 input 不写入，也不读取 `[data, data + size)` 之外；
- input canary 与 workspace 两侧 canary 不被写坏；canary 本身不能证明没有越界读，no-overread 由 exact-size
  buffer、ASan/UBSan、必要的 guard-page case 与 fuzz/property 共同验证；
- Decoder 失败后已写 workspace 槽位按合同不可读，除写前失败外不要求整个 workspace 内容保持不变；
- 不返回越界/悬空 view；
- error/offset/index 确定；
- Debug/Release 结果一致；
- hash/formatter callback 次数为 0。

Frame 恰好 release 一次属于 I3 集成测试，不放入 I1 codec test。

### 14.6 Property、fuzz 和 coverage

确定性 property tests 使用固定 seed；失败必须打印 seed、case index、输入长度和 offset：

```text
measure(model) == encode bytes written
decode(encode(model)) == normalized model
reference == SW == available HW
hash-only == copy-and-hash
arbitrary byte contents in a real readable `(data, size)` range never overread, crash or loop indefinitely
```

随机 format 可以含 NUL、brace 和非法 UTF-8。任意 byte mutation 只要求安全和确定，不要求全部 mutation 被拒绝，
因为突变后仍可能形成另一个合法 Record。

建议 fuzz targets：

```text
qlog_fuzz_record_decode
qlog_fuzz_record_roundtrip
qlog_fuzz_format_hash_equivalence
qlog_fuzz_cstr_length_cache
```

- PR：ASan+UBSan 全量测试与 corpus replay；
- nightly：每个 fuzz target 至少 10 分钟；
- release candidate：Decoder fuzz 至少 1 小时；
- 任何 crash input 必须进入永久 regression corpus；
- TSan 与 ASan 分开构建；I1 无 mutable global 时 TSan 不是核心合入门禁。

Coverage 只统计 I1 first-party 文件，排除 GTest/test helper/generated files：

```text
function coverage = 100%
line coverage >= 95%
branch coverage >= 90%
```

checked arithmetic、hash 长度分支、ArgumentTag switch 和 Decoder 校验顺序的逻辑分支必须 100%；
每个 MeasureError/EncodeError/DecodeError 至少一个直接命名测试。不可达防御分支必须书面说明。

## 15. 性能验收

I1 benchmark 只测 Record Core，不接 Ring、Logger、formatter 或 Sink。当前 benchmark build 只包含 SPSC
target，因此 I1 必须新增独立 `qlog_record_core_benchmark`，并让脚本可选择该 target，不能把现有 SPSC
benchmark 结果当作 Record Core 证据：

| 组件 | 场景 |
|---|---|
| hash-only/copy-and-hash | 0/1/3/4/7/8/15/16/31/32/33/64/128/256/1024/8192B；aligned/unaligned |
| checked measure | 无 cstr 的 2 integers/混合游戏参数；1/2/32 个 cstr；cstr 位于首/中/尾；短/长 string；exact quota 与 one-byte-too-large |
| encode/decode | empty、2 integers、mixed game record、short/long string、32 args |

setup、allocation、随机数据生成和结果校验不进入计时区，但 benchmark 必须在计时外消费输出，防止优化删除。
吞吐模式使用批处理计时；P50/P99 放在独立 latency mode，以 batch sample 统计，不能对极短 hash 每次调用
clock。报告 compiler/version、flags、CPU、active hash path、record/format/args bytes、ns/op、bytes/s；
只有通过 `perf stat` 或可靠 PMU 实测时才报告 hardware cycles/op，不能把现有 benchmark 的 workload
`cycle_count` 当作 CPU cycles。hash/copy-hash 还要记录 hot/reused 或 rotating working-set cache 状态。

I1 第一次建立基线，不设拍脑袋的绝对吞吐。后续在同机同 compiler 下 before/after 交替多轮，以 median + MAD
判断；回归超过 3% 且超过合并 MAD 时必须解释并阻断。不得以单次数字或 BQLog README 数字宣称更快。
以当前递归规范化、长度缓存和描述符 checked aggregate 为 measure 基线；不再要求恢复已被取代的
共享 cstr 预算/双阶段实现作为候选。优先比较真实 measure -> encode 与独立 decode，报告 Record bytes。
prepared 表示或 dispatch 变体在相同输入上交替多轮比较，遵守上面的 median + MAD 门禁；可靠 PMU
可用时补充 instructions/branch misses。单独返回长度的优化汇编不能替代真实编码路径或延迟/吞吐证据。

Release 汇编检查至少证明：

- literal stored hash 不在运行时重算；
- runtime format 没有完整 hash 扫描后再次 memcpy；
- 无分配、异常展开、锁、shared RMW、locale/formatter；
- fixed-width byte copy 没有意外逐字段外部 helper call；
- 未使用未对齐 typed load/store。

## 16. 构建与验证门禁

当前仓库已有的基础命令：

```text
./scripts/format.sh --check
./scripts/build_test.sh Debug
./scripts/build_test.sh Release
ctest --test-dir build/test/debug -L record_core --output-on-failure --no-tests=error
ctest --test-dir build/test/release -L record_core --output-on-failure --no-tests=error
git diff --check
```

企业级 I1 还必须建立可复现的独立构建目录，目录名至少包含 compiler/profile，避免 GCC/Clang 共用现有
CMake cache：

```text
gcc-debug
gcc-release
clang-debug
clang-release
clang-asan-ubsan
```

ASan+UBSan 至少使用 address、undefined、no-recover 和 frame-pointer 配置，零 sanitizer/leak 报告。
仓库当前没有 CI、CMakePresets、`.clang-tidy` 或 sanitizer 脚本；实现交付时必须如实写“新增/待补”，
不能在报告中伪称已有自动门禁。

所有 I1 测试在 Debug/Release 都必须运行；只有 hardware capability case 可以明确 skip，并必须输出原因。
禁止 flaky retry 掩盖失败。

## 17. Definition of Done

I1 只有在以下条件全部成立时完成：

- [ ] 只修改批准的 Record Core、test、benchmark 和必要 build-support 文件；
- [ ] I1 头文件不依赖 Ring/Channel/Logger/Backend/formatter/Sink；
- [ ] public wrapper 和所有 detail header 通过 self-contained compile；
- [ ] policy 覆盖全部 u8 level，DecodedArg 16B 布局/有效 union 成员、结果构造与借用工作区合同通过验证；
- [ ] `RecordHeader`/`ArgumentTag` static_assert 与旧测试零回归；
- [ ] wrappers/traits/measure/hash/codec 的全部合同测试通过；
- [ ] hand-written vectors/golden 不由 production code 生成；
- [ ] reference/SW/HW/hash-only/copy-and-hash 在可用路径逐位一致；
- [ ] encode 失败目标完全不变，成功 exact-write；
- [ ] Decoder 对任意内容但真实可读的 `(data, size)` byte range 不越界、不崩溃、不失控，并分别验证
  `(nullptr, 0)` 与 `(nullptr, nonzero)`；
- [ ] GCC/Clang Debug/Release、ASan+UBSan、compile-fail 和 corpus replay 全绿；
- [ ] coverage 满足第 14.6 节；
- [ ] `format.sh --check` 与 `git diff --check` 通过；
- [ ] performance baseline 和 Release assembly evidence 归档；
- [ ] 无非 capability 原因的 skip、无 flaky retry、无未解释 warning；
- [ ] 文档、错误枚举和测试命名与最终实现同步；
- [ ] 没有借 I1 提前加入 Ring、Logger、formatter、Sink 或新 Record kind。

完成报告必须列出：commit、compiler/CPU、执行命令、通过/跳过数量、sanitizer/fuzz/coverage、benchmark
median/MAD、已知限制和下一入口。WSL 结果必须标注为开发证据，不包装成 native Linux 发布结论。

## 18. 代码评审清单

### API 与所有权

- prepared/decoded view 的借用期是否清楚且没有跨日志操作或 workspace 复用点缓存？
- I1 生产头与实现是否完全没有 `std::span`，所有范围是否显式携带并校验长度？
- `qlog::ptr` 是否保存数值而非原始指针？
- 非空 cstr 是否只调用一次 `strlen` 并复用长度？
- public API 是否仅增加 `arguments.hpp` 所需内容？

### Wire 与边界

- Header/tag offset 和值是否只来自冻结常量？
- 所有宽度转换前是否检查可表示性？
- 所有读取是否采用 subtraction-first，并保留 Release 校验？
- packed scalar 是否只经局部值 + byte copy/bit_cast？
- unknown tag 是否立即结束当前 Record，而非猜测跳过？

### Hash 与 ISA

- raw/stored 命名是否不混淆？
- polynomial、seed、tail overlap、rotate/fold 是否与 ADR 一致？
- raw 0 是否只在 stored 边界规范化？
- ISA 是否局部、检测后执行、fallback 安全？
- hash-only 是否完全没有 null destination pointer arithmetic？

### 错误与可观测性

- Measure/Encode/DecodeError 优先级是否确定？
- encode 强失败保证是否被 canary 测试证明？
- Decoder 是否接受 hash 0 且不做格式身份校验？
- I1 是否只返回错误，不计数、不输出、不递归日志？

### 性能

- Producer 可复用路径是否零分配、无锁、无 shared RMW？
- literal hash 是否 constexpr，runtime 是否 fused copy-and-hash？
- string/cstr 是否没有重复 size/scan/copy？
- benchmark 是否隔离职责并记录 active implementation？

## 19. I1 完成后的唯一入口

I1 完成且 Definition of Done 全绿后才能进入 I2。I2 负责：

```text
LogLevel/public API 的实际数值映射
Channel 创建 RecordValidationPolicy
immutable FormatHashDispatch 生命周期
ProducerHandle/AsyncLogger 冷路径绑定
filter -> measure -> reserve -> admission timestamp -> encode -> commit
统计与 drop_new
```

I2 不得重新实现 I1 的 tag mapping、cstr scan、measure、hash 或 codec；I3 也不得重新定义
`DecodedArg/DecodedRecordView/DecodeResult`。任何 I1 合同变化必须先返回本文和 ADR 评审。
