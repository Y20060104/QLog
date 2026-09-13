# QLog I1-C 编解码与 I1 收口动手指南（合并第 2、3 轮）

> 2026-09-13 当前状态：Record Core（I1）已按 WSL2 开发范围完成阶段收口。六配置、覆盖率、连续一小时 decoder fuzz、完整性能基线已通过；经用户确认，原生 Linux 发布前复核暂缓，不阻塞后续模块开发。详见 [验收及收口报告](./I1D_ACCEPTANCE_20260913_CHS.md)。下文旧日期进度为历史记录。

方案确认：2026-09-08。最新进度：2026-09-10，hash/types/encoder 基线已修复并验证，decoder 与完整 I1-D 待完成。

本文承接 [第一轮 I1-B 动手指南](./MILESTONE2_I1B_HANDS_ON_GUIDE_CHS.md)，将原第 2 轮 I1-C 与
第 3 轮 I1-D/性能收口合并。你编写生产实现；Codex 后续编写测试、执行验证并报告问题。
上述分工为原教学安排。2026-09-10 用户明确授权 Codex 完善修复与测试；本轮已完成生产修复、测试接线及实际验证。
当前状态和准确验收范围见 [2026-09-10 修复与验证报告](./I1_HASH_ENCODER_VALIDATION_20260910_CHS.md)；下文 2026-09-09 的源码行号与缺陷描述仅供历史回顾。

权威顺序仍为 ADR-007～010 > [I1 主规范](./MILESTONE2_I1_RECORD_CORE_DEVELOPMENT_GUIDE_CHS.md) >
本动手指南。本文把已确认的内部文件边界、policy 与紧凑 `DecodedArg` 具体化；不改变 wire。

快速定位：[A：hash 复查](#hash-preflight) · [B：公共类型](#record-types) ·
[C：encoder](#record-encoder) · [D：decoder](#record-decoder) ·
[E：生产交接](#implementation-handoff) · [F：验证与性能](#verification-performance)。

## 0. 从哪里开始，按什么顺序完成

实际项目根目录是 `/home/qq344/QLog`；`E:\VisualStudioProject\BqLog` 是 BQLog 参考仓库。
本指南的生产文件名均相对于 QLog 根目录。现有 I1-A 的 measure、递归参数规范化和一次 `strlen`
缓存长度继续复用。不要重新引入旧的 `cstr(max_scan)` 或 shared scan budget 方案。

2026-09-10：A 的 hash 可调用与正确性基线、B 公共类型、C encoder 已通过本轮测试。
当前直接进入 D 独立 decoder，随后完成其生产接线与 F 的剩余门禁。A 的旧缺陷不再作为当前待办。

完整依赖顺序如下（当前已推进至 C，下一步 D）：

```text
A 补齐 hash 的可调用基线
  -> B 公共类型、policy、metadata 校验与结果构造
  -> C 头内可内联 encoder
  -> D 独立 decoder
  -> E 生产构建接线与交付
  -> F Codex 验证、给出问题/优化证据，维护者完成生产修订
```

每章中的“完成条件”是实现交接条件，不要求你编写或执行测试。A 的依赖验证与最终验证由 Codex 承担。
可以先编写 B 中不依赖 hash 的类型；C 的联调需要 A 已经具备完整 raw/checked/dispatch 入口。

### 0.1 本轮只需要新增的生产文件

| 文件 | 从上到下的内容 | 谁包含它 |
|---|---|---|
| `include/qlog/detail/record_types.hpp` | metadata、policy、共享校验、错误、DecodedArg、view/result、内部构造工厂 | encoder 和 decoder |
| `include/qlog/detail/record_encoder.hpp` | includes、定宽 store、单参数写入、固定槽位展开、metadata 错误映射、`encode_v1<N>` | 需要编码的内部调用方 |
| `include/qlog/detail/record_decoder.hpp` | types include、`decode_v1` 声明 | decoder 使用者及 decoder `.cpp` |
| `src/record_decoder.cpp` | includes、私有 load/错误映射 helper、`decode_v1` 定义 | 加入现有 `qlog` 静态库 |

不再创建旧计划中的 `record_codec.hpp/.cpp`；这是建议文件边界的细化。无需额外聚合头、`.inl`、
第二个公共 library 或 `common.hpp`。生产 `record_types.hpp` 不 include measure/hash/traits/wrappers。

```text
record_header.hpp + argument_tag.hpp
                    |
                    v
              record_types.hpp
               /            \
              v              v
record_encoder.hpp      record_decoder.hpp -> record_decoder.cpp
    ^       ^
    |       |
 measure   hash
```

共同类型放在 `qlog::detail`。校验 helper 放 `qlog::detail::record_metadata_impl`，编码 helper 放
`qlog::detail::record_encoder_impl`；decoder `.cpp` 私有 helper 放匿名 namespace。
模板与需要让调用方看见的短 helper 在相应头中定义。非模板头内函数需要 `inline`（或本身为 `constexpr`）。

### 0.2 不变量速查

- Wire：`[32B Header][format bytes][packed tagged args]`，无 Record 内部 padding。
- 上限：format 8192B、参数 32 个，使用现有 `record_limits.hpp`；不建立第二套同值常量。
- 输入：裸指针与独立长度，不使用 `std::span`。
- Encoder：destination 必须恰好等于 `prepared.payload_size()`；失败写入 0B 且目标所有字节不变。
- Decoder：workspace 非空且恰好 32 槽；Release 永久保留所有内容/边界检查。
- `PreparedRecord<N>` 借用源到本次操作结束；decoded view 借用 payload 与 workspace 的共同有效期。
- I1 只处理 Record bytes，仍不调用 Ring、时钟、formatter、统计或 Sink。

下面 C++ 块是接口/布局骨架与局部语义示例。带声明的函数由你补函数体；不要把“声明存在”当作已经可链接。

<a id="hash-preflight"></a>

## A. 第一轮 2026-09-09 历史复查与 hash 算法说明

2026-09-10 本章对应修复与基线验证已完成，见 [2026-09-10 修复与验证报告](./I1_HASH_ENCODER_VALIDATION_20260910_CHS.md)。以下保留当时诊断与教学说明，不再表示最新源码仍有这些缺陷。

本节只收口 I1-B 的必要实现，不重写已完成的 measure，也不要求调整现有 `format_hash.hpp` 中两个类的顺序。
2026-09-09 已重新只读检查用户修改后的 WSL 文件。相较前次，以下结构已经补上，应保留现有改动：

- `consume_word` 的每种宽度已经把局部 value、读取和条件复制放在同一个 `if constexpr` 分支内，作用域问题已解决。
- `SoftwareOps` 已成为 struct，四个方法已有 static/noexcept 和返回类型；不再要求重建这个类壳。
- `byte_value` 的合法分支已增加 return；CRC 的多项式 XOR 已移回八轮循环内。
- hash-only checked 归一化与 copy checked 的三个前置检查顺序保持正确；接口类与结果类型可保留。

这些是静态阅读确认的进展。剩余类型/语法、CRC 状态传递、短输入分支及链接入口尚未收口，哈希源也未进入 `qlog` target。
本次未运行编译或测试，因此不把以上改动描述为已通过完整正确性或性能验收。

以下代码路径均相对 `/home/qq344/QLog`，行号为 2026-09-09 复查快照，修改后按函数名定位。
实现顺序是 A1 reference → A2 共用 core → A3 software → A4 checked/dispatch → A5 hardware → A6 生产接线。
这些生产代码由维护者实现；助手后续负责测试文件、测试构建接线、执行验证及 codegen 检查。

| 位置快照 | 本节要补齐的内容 |
|---|---|
| `include/qlog/detail/format_hash_reference.hpp:10`、`:24`、`:39`、`:53`、`:72` | 字面量模板、CRC 每轮次序、类型判断、for 语法、全部短输入分支 |
| `src/format_hash_core.hpp:17`、`:23`、`:50` | Width 拼写、接住 Ops 返回值、补 hash_core 定义 |
| `src/format_hash_software.cpp:6`、`:8`、`:13` | constexpr 表、按值 state 接口、逐 byte 赋回状态及位移缩窄、raw wrappers |
| `src/format_hash.cpp:28`；`include/qlog/detail/format_hash.hpp:30` | copy stored 归一化、automatic 工厂 |
| `src/format_hash_x86_crc32c.cpp:3`；`src/format_hash_core.hpp:11` | 正确包含 core 头、硬件实现、统一能力检测名称 |
| `CMakeLists.txt:36` | 把生产哈希源加入已有 target，并限制硬件 ISA 选项的作用范围 |

### A1. reference：先修一字节，再写长度分支，最后接字面量

完整背景见[第一轮动手指南](MILESTONE2_I1B_HANDS_ON_GUIDE_CHS.md)的“B. reference：从一个字节开始，完成独立正确性依据”。
本文重新核对了 ADR-010“2.2 CRC primitive”“2.3 精确分块算法”“2.4 0 值与 Record 表示”，下列数值保持冻结身份。
`format_hash_reference.hpp` 直接包含 `<bit>`、`<cstddef>`、`<cstdint>`、`<type_traits>`。
helper 按以下顺序放入 `qlog::detail::hash_reference`，字面量入口最后放在 `qlog::detail`。

**1. `crc_byte_ref` 的输入输出都是值。**

```cpp
constexpr std::uint32_t crc_byte_ref(
    std::uint32_t state, std::uint8_t byte) noexcept;
```

保留现有 byte XOR 和八轮循环，把函数名称统一为 `crc_byte_ref`，使定义与已有两条断言一致。
每轮必须先保存旧的最低位 `(state & 1U) != 0U`，再无条件执行 `state >>= 1U`，
若刚才保存的最低位为 1，再执行 `state ^= 0x82F63B78U`。
当前第 24–28 行仍仅在低位为 1 时移位，并用移位后的低位决定 XOR；这两处都要改。XOR 已在循环内，保留该位置，不做最终取反。
冻结校对值为 `crc_byte_ref(0U, 0U) == 0U`、`crc_byte_ref(0U, 1U) == 0xF26B8303U`。

**2. `byte_value` 根据类型返回一个无符号 byte。**

```cpp
template<class Byte>
constexpr std::uint8_t byte_value(Byte value) noexcept;
```

`if constexpr` 的判断比较类型 `Byte`，不能把参数值 `value` 放进 `std::is_same_v`。
若 Byte 是 `std::byte`，返回 `std::to_integer<std::uint8_t>(value)`；
若是 `char` 或 `char8_t`，先 `static_cast<unsigned char>(value)`，再转为 `std::uint8_t` 返回。
其他类型用依赖 Byte 的 `static_assert` 拒绝；普通整数不能调用 `std::to_integer`。
保留现有分支内直接 return 的结构，只修正类型判断与转换表达式。

**3. `crc_bytes_ref` 只表达一个已证明有效的小范围。**

```cpp
template<class Byte>
constexpr std::uint32_t crc_bytes_ref(std::uint32_t state,
    const Byte* data, std::size_t offset, std::size_t count) noexcept;
```

第 53 行现有 `i < count++ i` 仍不合法，改为 `i = 0; i < count; ++i`；每次执行
`state = crc_byte_ref(state, byte_value(data[offset + i]))`，最后返回 state。
reference 保留独立的字节循环，不借用生产 core、宽读取或查表实现。

**4. `hash_raw_ref` 按完整分支表更新四个 lane。**

```cpp
template<class Byte>
constexpr std::uint64_t hash_raw_ref(const Byte* data, std::size_t size) noexcept;
```

先处理 `size == 0`：立即返回 raw 0，不解引用 data，也不做空指针算术。
现有 raw 空输入返回与四个 `0xFFFFFFFFU` lane 初值都可保留；目前缺少下表四组短输入分支。
下面记 `n = size`，`C(h,o,w) = crc_bytes_ref(h,data,o,w)`；所有更新都是把返回值赋回对应 lane。
只有 `n < 32` 的分支需要 `uint32_t len = static_cast<uint32_t>(n)`，供长度 XOR 使用。

| 输入长度 n | 依次执行的更新 |
|---|---|
| 0 | 已在入口返回 raw 0，不进入下面分支 |
| 1..3 | offset=0；若 `n & 2U`，`h1=C(h1 xor len,0,2)`、offset=2；若 `n & 1U`，`h2=C(h2 xor len,offset,1)` |
| 4..7 | `h1=C(h1,0,4)`；`h2=C(h2 xor len,n-4,4)` |
| 8..15 | `h1=C(h1,0,8)`；`h2=C(h2 xor len,n-8,8)` |
| 16..31 | `h1=C(h1,0,8)`；`h2=C(h2,8,8)`；`h3=C(h3 xor len,n-16,8)`；`h4=C(h4 xor len,n-8,8)` |
| >=32 | 按下面的完整块循环和重叠尾窗更新；不混入长度 |

`>=32` 分支内先设 offset=0。只要 `offset <= size - 32U`，就分别在
offset、offset+8、offset+16、offset+24 读取 8B，依次更新 h1、h2、h3、h4，然后 offset+=32。
循环后若 `offset != size`，令 tail=size-32，再按 tail、tail+8、tail+16、tail+24 各读 8B 更新四个 lane。
`size - 32U` 只能出现在已经证明 `size >= 32U` 的分支内，避免短输入无符号下溢。
33B 的窗口为 `[0,32)`、`[1,33)`；63B 为 `[0,32)`、`[31,63)`；64B 为两个完整块，不能再重复尾窗。
3B 情况最后那个 1B 的 offset 是 2；1B 情况则是 0。不得把两种情况都固定读 offset 0。

第 88–90 行的 fold 已符合冻结规则，直接保留；所有新增短输入分支也汇合到它：

```text
uint32 low  = h1 xor std::rotl(h3, 17)
uint32 high = h2 xor std::rotl(h4, 19)
raw = (uint64(high) << 32) | uint64(low)
```

先将 high 扩宽再左移 32 位；raw 层保持 0，不在这里执行 stored 归一化。

**5. 字面量只 hash 一次 `N-1`，不扫描 NUL。**

```cpp
template<class CharT, std::size_t N>
[[nodiscard]] constexpr std::uint64_t hash_literal_stored(
    const CharT (&text)[N]) noexcept;
```

形参必须使用 CharT，才能从普通字面量和 `u8` 字面量推导；只接受 char/char8_t。
这是内部已知 NUL 结尾数组入口，末元素为 NUL 是前置条件；不能用字符与 `nullptr` 比较。
先调用一次 `hash_reference::hash_raw_ref(text, N - 1U)`，再返回 `raw == 0U ? 1U : raw`。
不要用 N 次循环重复 hash，不把末尾 NUL 算入，也不用 `strlen` 或 constexpr `reinterpret_cast`。
完成后由 `format_hash.hpp` include reference 头；reference 自己保留这条归一化表达式，避免循环包含。
冻结样例：空字面量 stored 为 1；`"a"` 为 `0x33bbc03300000000`；`"abc"` 与 `u8"abc"` 均为 `0x33bbc033d64581af`。

### A2. 共用 core：保留分支局部值，补正确的返回通路和分块

参照第一轮“C. software：宽读取、融合复制、查表 primitive”。`src/format_hash_core.hpp` 直接包含
`<bit>`、`<cstddef>`、`<cstdint>`、`<cstring>`；保留现有分支结构，无需新增类型选择工具。
所有声明和模板放在 `qlog::detail::hash_impl`；将能力检测声明统一为 `x86_crc32c_available()`。

```cpp
template<std::size_t Width, bool Copy, class Ops>
std::uint32_t consume_word(std::uint32_t state, const std::byte* source,
    std::byte* destination, std::size_t offset) noexcept;

template<bool Copy, class Ops>
std::uint64_t hash_core(const std::byte* source,
    std::byte* destination, std::size_t size) noexcept;
```

现有 static_assert 和四个分支内的局部 uint8/16/32/64 value 可保留，不再要求换成 Word 类型别名。
先把第 17 行 `WIdth` 修为 `Width`，直接包含 `<cstring>`，并统一调用 `std::memcpy`。
保留每个分支只读取一次 value、在 `if constexpr (Copy)` 中复制同一 value 的结构。
第 23/30/37/45 行必须直接 `return Ops::u8/u16/u32/u64(state,value)`；四条分支都返回后，删除底部返回旧 state 的通路。
当前只调用 Ops 而丢弃返回值，最后 `return state` 会把没有更新的 state 交给上层。

四种 Ops 方法统一接受按值 state，返回新 `uint32_t` state；软件与硬件必须遵守相同签名。
`hash_core` 第 50–51 行仍只有声明，需要在此头内定义：逐项实现 A1 分支表，将每个 C 替换为对应 Width 的 `consume_word` 并赋回 lane。
reference 仍独立；两种生产 backend 只替换 Ops，不另写两套分块、尾窗和 fold。
copy 开关是模板参数，hash-only 不计算 destination+offset；空输入在任何指针操作前返回 raw 0。
V1 的值按 little-endian 解释；当前 x86-64 的固定宽度 memcpy 读取与此一致，不能用未对齐整数指针解引用替代。
固定宽度 memcpy 的指令生成由助手后续检查；写成模板或 inline 本身不构成性能结论。

### A3. software：constexpr 表、静态 Ops 和两个可链接入口

`src/format_hash_software.cpp` include core 头与 `<array>`；把现有 SoftwareOps struct 放到 `hash_impl` 的匿名 namespace 中，
与新增 `make_crc_table()`、constexpr 表放在一起。保留已经写出的 struct/static/noexcept，不需要重新搭类壳。
`make_crc_table` 返回 `std::array<std::uint32_t,256>`；每项以该索引为 state，执行 A1 的八轮移位/XOR 规则。
生成器自身实现这八轮，不调用 reference helper；结果为不可变编译期对象，替换当前仅被零初始化的可变数组。

四个方法当前首参仍是 `uint32_t&`，改成按值传入，统一采用下面的签名，避免“返回新值”和“修改引用”混用：

```cpp
struct SoftwareOps {
    static std::uint32_t u8(std::uint32_t state, std::uint8_t value) noexcept;
    static std::uint32_t u16(std::uint32_t state, std::uint16_t value) noexcept;
    static std::uint32_t u32(std::uint32_t state, std::uint32_t value) noexcept;
    static std::uint32_t u64(std::uint32_t state, std::uint64_t value) noexcept;
};
```

u8 返回 `(state >> 8U) ^ table[(state ^ value) & 0xFFU]`。
当前 u16/u32/u64 忽略每次 u8 的返回值，最终直接返回原 state。改为各依次处理 2/4/8 个 byte：对 j=0..宽度-1，取
`static_cast<std::uint8_t>(value >> (8U * j))`，执行 `state = u8(state, byte)`，最后返回 state。
先右移再缩窄；仅 mask 高位后直接转 uint8_t 会把高位 byte 截成 0。循环上界不能包含宽度本身。
匿名 namespace 关闭后，在 `hash_impl` 定义两个非模板 raw 入口，保证其他 TU 能链接到它们：

```text
software_hash_raw(source,size)      -> return hash_core<false,SoftwareOps>(source,nullptr,size)
software_copy_raw(source,dest,size) -> return hash_core<true, SoftwareOps>(source,dest,size)
```

两者均返回 `uint64_t` 并声明 noexcept，签名与 core 头一致。不要在每个 byte 上通过运行时函数指针调用 Ops。

### A4. checked 与 automatic：先接 software，再接硬件选择

参照第一轮“D. 先跑通 software 的 dispatch 与 checked 接口”。`src/format_hash.cpp` 同时 include 公共 detail 头和 core 头。
先定义 `FormatHashDispatch::automatic()`，绑定两个 software raw 入口及 `HashBackend::software`，得到完整可调用对象。
当前 `hash_format_stored` 的检查与归一化结构可保留；copy wrapper 的 raw 结果必须调用 `stored_hash_from_raw(raw)`，
不能用 `static_cast<std::uint64_t>(raw)` 代替，因此空输入在两个 checked 入口中都成功返回 stored 1。

copy checked 的次序固定为 source metadata → destination metadata → capacity → raw copy → stored 归一化 → 成功。
source_size 非零而 source 为空时报 source 错误；destination_capacity 非零而 destination 为空时报 destination 错误；
capacity 小于 source_size 时报告容量不足。前三步不写目标，调用 raw 之后不再返回可恢复错误。
零容量的空目标可以表达空范围；真实有效内存以及 source/destination 不重叠仍是调用方前置条件。
生产 raw wrapper 始终返回 raw，stored wrapper 或 Encoder 在自己的边界归一化一次。

### A5. x86：保持 core 不变，把能力检测留在冷路径

参照第一轮“E. 最后接 x86 硬件，不重新实现另一套分块”。硬件文件 include `<nmmintrin.h>` 与 `format_hash_core.hpp`，
不能 include 不存在的 `format_hash_core.cpp`。在 `hash_impl` 内匿名 namespace 定义 HardwareOps，签名与 SoftwareOps 完全一致。
u8/u16/u32 分别返回 `_mm_crc32_u8/u16/u32(state,value)`；u64 返回
`static_cast<std::uint32_t>(_mm_crc32_u64(state,value))`。再提供与 software 对应的 x86 两个 raw wrappers。

`x86_crc32c_available()` 在 `format_hash.cpp` 的 `hash_impl` 中定义；构建宏为 0 时直接返回 false，不引用硬件符号。
宏为 1 时包含 `<cpuid.h>`，调用 `__get_cpuid(1,&eax,&ebx,&ecx,&edx)`；失败返回 false，
成功时返回 `(ecx & bit_SSE4_2) != 0U`。hardware include 与函数地址引用都受同一个构建宏保护。
automatic 随后改为：已编入且 CPU 支持时绑定两个 x86 入口，否则始终绑定两个 software 入口。
dispatch 构造后只读使用，实际 hash/copy 调用不再运行 CPUID；不要在每条记录上重新构造 dispatch。

### A6. 生产接线完成后，交由助手验收再进入 encode

维护者在根 `CMakeLists.txt` 的 `add_library(qlog STATIC ...)` 之后，把 `src/format_hash.cpp` 和
`src/format_hash_software.cpp` 加入已有 qlog target；保持同一个公共库，software 始终编入。
完整条件参照第一轮“F. 构建接线与验收”的生产 CMake 块：仅 Linux x86-64、GCC/Clang 时加入 x86 源，
仅该源设置 `-msse4.2`；给 qlog 设置唯一的 `QLOG_HAS_X86_CRC32C=0/1` 定义，不全局打开 SSE4.2。
头文件定义、raw wrappers 和工厂都补齐以后，助手负责验证 known vectors、reference/SW/HW 一致性、copy 范围及失败不变性，
并检查软件回退、硬件隔离和融合复制的 codegen。维护者无需编写测试文件、测试专用 friend 工厂或测试 CMake。
本节的完成条件是实现交付后由助手确认 hash 基线可用；不把当前静态草稿描述为已经通过验收。

<a id="record-types"></a>

## B. 公共类型：先把构造和语义连通

全部放 `include/qlog/detail/record_types.hpp`。直接 include `<array>`、`<bit>`、`<cstddef>`、
`<cstdint>`、`<optional>`、`<type_traits>`，以及 `record_header.hpp`、`argument_tag.hpp`、`record_limits.hpp`。
实现中使用 `assert` 再直接 include `<cassert>`；不要依赖另一个头间接提供声明。

定义顺序固定为：

```text
RecordMetadata -> RecordValidationPolicy -> MetadataError/共享校验
-> EncodeError -> DecodeError -> DecodeFailure
-> DecodedArg -> 前置声明 RecordCodecAccess
-> DecodedRecordView -> EncodeResult -> DecodeResult
-> RecordCodecAccess 的工厂定义
```

### B1. RecordMetadata：只允许调用方提供四项信息

```cpp
struct RecordMetadata final {
    std::uint64_t time_value{};
    std::uint32_t category_id{};
    std::uint8_t level{};
    std::uint8_t flags{};
};
```

它是局部输入值，不是新的 wire Header。format/hash/args 长度和参数数量不在这里，必须由成功 prepared
生成。`category_id` 的全部 u32 值在 I1 都是结构上合法的；类别注册和过滤属于 I2。

`time_value` 是调用方提供的 Unix Epoch 纳秒值；I1 不取时、不转换 `timespec`。`primary_valid` 和
`fallback_valid` 的时间可以为 0；只有 `time_unavailable` 必须为 0，不能擅自加上“有效时间必须非零”。

### B2. RecordValidationPolicy：四个 64 位 mask

```cpp
class RecordValidationPolicy final {
public:
    explicit constexpr RecordValidationPolicy(
        std::array<std::uint64_t, 4> valid_levels,
        bool fallback_timestamp_allowed) noexcept;

    [[nodiscard]] constexpr bool allows_level(std::uint8_t level) const noexcept;
    [[nodiscard]] constexpr bool fallback_timestamp_allowed() const noexcept;

private:
    std::array<std::uint64_t, 4> valid_levels_;
    bool fallback_timestamp_allowed_;
};
```

按顺序实现：

1. 完整参数构造直接初始化两个成员；不提供默认构造、setter、Channel 指针或能力检测。
2. `allows_level` 将 level 转为 `size_t` 计算槽位 `level >> 6U`，其范围必定是 0～3。
3. 位号是 `level & 63U`，查询 `(valid_levels_[slot] >> bit) & std::uint64_t{1}` 是否非零。
4. fallback accessor 直接返回成员。所有查询只读、`noexcept`、无分配。

四个 mask 的全部 bit pattern 都合法，包含全零（拒绝全部 level）。全零 policy 不是构造错误。
例如 mask 0 的 bit 2 表示允许 wire level 2，这只是位索引示例，不定义公共 `LogLevel` 数值。
构造 mask 时使用 `std::uint64_t{1} << bit`，不要写 `1 << bit`；后者可能在 32 位 int 上发生无效移位。

policy 在调用阶段按 `const&` 传递。这里的 immutable 指构造完成后无成员修改接口、发布后只读使用，
不要求把每个字段声明为 `const`，也不引入逐 Record 拷贝 32B 位图的参数传递。

### B3. 共用 metadata 校验：只做一次，保留顺序

```cpp
enum class MetadataError : std::uint8_t {
    invalid_level,
    unknown_flags,
    reserved_timestamp_status,
    invalid_time_value,
    fallback_timestamp_not_configured,
};

namespace record_metadata_impl {
[[nodiscard]] inline std::optional<MetadataError> validate_record_metadata(
    const RecordMetadata& metadata,
    const RecordValidationPolicy& policy) noexcept;
}
```

实现中按下表从上到下返回第一个错误；全通过返回 `std::nullopt`。`optional<enum>` 不分配内存。

| 顺序 | 判断 | 返回 |
|---|---|---|
| 1 | `!policy.allows_level(metadata.level)` | `invalid_level` |
| 2 | flags 的 bits 2～7 任一非零，即 `flags & 0xFCU` 非零 | `unknown_flags` |
| 3 | `status = flags & 0x03U`，status 为 3 | `reserved_timestamp_status` |
| 4 | status 为 2（time_unavailable），但 time_value 非零 | `invalid_time_value` |
| 5 | status 为 1（fallback_valid），但 policy 不允许 fallback | `fallback_timestamp_not_configured` |

status 0 是 `primary_valid`，1 是 `fallback_valid`，2 是 `time_unavailable`。flags=7 同时有
unknown bits 和 reserved status，必须首先报告 `unknown_flags`。category 不参加这个校验。
表中 0x03/0xFC 展示位语义；实现复用现有 `kTimestampStatusMask` 与 `kKnownFlagMask`，
通过 unsigned 位运算取补集判断 unknown bits，不在新头重复定义同义 mask 常量。

调用边界：encoder 在 destination metadata/exact-size 检查之后调用；decoder 在局部 Header 读取之后调用。
共享 helper 不认识 destination、payload 或 error offset，也不返回 `EncodeError`/`DecodeError`。
两端各用显式 switch 做错误映射，不用 `static_cast` 假定不同错误枚举的编号相同。

### B4. 错误类型：完整定义，避免写到一半补状态

```cpp
enum class EncodeError : std::uint8_t {
    invalid_destination_metadata,
    destination_size_mismatch,
    invalid_level,
    unknown_flags,
    reserved_timestamp_status,
    invalid_time_value,
    fallback_timestamp_not_configured,
};

enum class DecodeError : std::uint8_t {
    invalid_payload_metadata,
    invalid_workspace,
    record_too_small,
    invalid_level,
    unknown_flags,
    reserved_timestamp_status,
    invalid_time_value,
    fallback_timestamp_not_configured,
    format_too_large,
    invalid_format_length,
    invalid_args_length,
    arg_count_exceeded,
    invalid_or_unknown_tag,
    invalid_bool,
    truncated_value,
    truncated_string_length,
    truncated_string,
    decoded_count_mismatch,
    trailing_args_bytes,
};

struct DecodeFailure final {
    DecodeError error;
    std::size_t error_offset;
    std::uint8_t argument_index;  // 0xFF = non-argument error.
};
```

这些是内部 C++ 枚举，不写入 Record。错误结果不拥有文本、不抛异常、不写 stderr，不递归记录日志。
`error_offset` 使用 `size_t`：外部提供的 payload_size 尚未验证，不能先把它或失败位置缩窄为 u32。

### B5. DecodedArg：16B 工作槽，与 wire 分开

```cpp
struct DecodedArg final {
    union Value {
        std::uint64_t bits;
        const std::byte* bytes;
    };

    Value value{.bits = 0U};
    std::uint32_t byte_count{};
    ArgumentTag tag{ArgumentTag::Invalid};
};

static_assert(sizeof(DecodedArg) == 16U);
static_assert(alignof(DecodedArg) == 8U);
static_assert(std::is_standard_layout_v<DecodedArg>);
static_assert(std::is_trivially_copyable_v<DecodedArg>);
```

在当前 Tier 1 的 64 位 ABI 中，Value 占 8B，byte_count 占 4B，tag 占 1B，尾部对齐补齐到 16B。
32 槽的对象尺寸是 512B；这只是工作区布局，不是吞吐收益或 wire 长度。不要将 struct 整体写入 Record，
也不要比较/序列化它的 padding。

成员使用规则固定如下：

| tag | 有效 union 成员 | byte_count |
|---|---|---|
| 固定标量/Pointer64 | `value.bits`：有效宽度内的 unsigned wire bit pattern，零扩展至 u64 | 0 |
| `Utf8String` | `value.bytes`：当前 payload 中的借用指针，空字符串也用这个成员 | 已验证的 u32 长度 |
| `NullUtf8` | `value.bits = 0` | 0 |
| 默认 `Invalid` | `value.bits = 0` | 0；不是成功解码值 |

不要把 `{pointer, length}` 整体塞入 union 再加 tag；那会增大 union 到 16B，使整个槽位通常回到 24B。
当前 producer 的 `NormalizedArgument` 继续使用原 bits/pointer/count/tag 多字段表示，不跟随本节重构。

**union 的有效成员必须按 tag 使用。** 解字符串时直接向内建指针成员赋值，如
`slot.value.bytes = payload_data + string_offset`；解数字时向 `slot.value.bits` 赋值。这里的成员都是
内建的隐式生命周期类型，按这种直接赋值可以切换有效成员。先准备值/长度，再设置最终 tag。
不能写 bytes 后从 bits 读地址，也不能把 bits 所在内存当 float 引用。按 tag 读取才有定义。

workspace 指向已开始生命周期的对象，例如调用方拥有的 `std::array<DecodedArg, 32>`。
不接受随意把一段 byte buffer 强转为 `DecodedArg*`。可以复用同一数组；每条记录只填实际解出的槽位，
不在 decoder 入口清零整个数组、不构造第二份 32 槽暂存结果。

### B6. 数字如何从 bits 恢复

Decoder 按 wire 宽度读取 unsigned 局部值，再零扩展到 bits。对不同类型的解释在使用该参数时进行：

| tag 示例 | 正确恢复语义 | 常见错误 |
|---|---|---|
| `Int8`，bits=255 | 先缩窄 `uint8_t`，`bit_cast<int8_t>`，再扩宽为 int64，得到 -1 | 直接把 u64(255) 转 int64 得到 255 |
| `Int16/Int32` | 分别先缩窄为 u16/u32，再等宽 bit_cast 为 i16/i32 | 把 8B bits 直接 bit_cast 为 2B/4B 类型 |
| `Int64` | `bit_cast<int64_t>(bits)` | 使用不同类型 union 成员解释同一存储 |
| `UInt8/16/32/64` | 按 tag 解释零扩展的 unsigned 数值 | 对 unsigned 进行符号扩展 |
| `F32` | `bit_cast<float>(uint32_t(bits))` | `static_cast<float>(bits)` 做数值转换 |
| `F64` | `bit_cast<double>(bits)` | 十进制往返改变 NaN 或 signed-zero 位模式 |
| `Char` | 取低 8 位字符字节，与 Int8 的数值语义分开 | 当作 signed integer 格式化 |
| `Pointer64` | 使用 u64 数值 | 恢复地址并解引用 |

这些恢复式说明协议含义；本轮不实现 formatter，也无需为了示例创建另一套对外标量访问 API。
NaN 的原始 bit pattern 一直保存在 bits 中；在 I1 内不执行浮点运算或通过浮点寄存器数值往返来验证它。

### B7. 成功视图：Header 按值，format/参数槽位借用

```cpp
struct RecordCodecAccess;

class DecodedRecordView final {
public:
    [[nodiscard]] const RecordHeader& header() const noexcept;
    [[nodiscard]] const std::byte* format_data() const noexcept;
    [[nodiscard]] std::uint32_t format_size() const noexcept;
    [[nodiscard]] const DecodedArg* arguments_data() const noexcept;
    [[nodiscard]] std::uint16_t argument_count() const noexcept;
private:
    DecodedRecordView(RecordHeader header, const std::byte* format_data,
                      const DecodedArg* arguments) noexcept;
    RecordHeader header_;
    const std::byte* format_data_;
    const DecodedArg* arguments_;
    friend struct RecordCodecAccess;
};
```

完整构造初始化全部字段。`format_size()` 和 `argument_count()` 从 header_ 的对应字段取得，不再保存
第二套长度；参数指针只暴露 const。`header()` 返回 view 内部的 Header 引用，随 view 对象销毁失效。

成功 view 可以按值复制，但不会延长 payload/workspace 的寿命。即使保存了 DecodeResult 的副本，
下一次 decode 复用了同一 workspace，旧 view 的参数也不得继续读取。I3 以后负责调用和释放 Frame，
本轮不在 view 析构中做 release，也不加入 Ring owner。

### B8. EncodeResult 与 DecodeResult：失败对象也完整初始化

```cpp
class EncodeResult final {
public:
    [[nodiscard]] bool succeeded() const noexcept;
    [[nodiscard]] std::uint32_t bytes_written() const noexcept;
    [[nodiscard]] const EncodeError* failure() const noexcept;
private:
    explicit EncodeResult(std::uint32_t bytes_written) noexcept;
    explicit EncodeResult(EncodeError error) noexcept;
    std::uint32_t bytes_written_;
    std::optional<EncodeError> error_;
    friend struct RecordCodecAccess;
};

class DecodeResult final {
public:
    [[nodiscard]] bool succeeded() const noexcept;
    [[nodiscard]] const DecodedRecordView* record() const noexcept;
    [[nodiscard]] const DecodeFailure* failure() const noexcept;
private:
    explicit DecodeResult(DecodedRecordView record) noexcept;
    explicit DecodeResult(DecodeFailure failure) noexcept;
    std::optional<DecodedRecordView> record_;
    DecodeFailure failure_;
    friend struct RecordCodecAccess;
};
```

实现顺序与状态：

1. EncodeResult 成功构造设 bytes_written_ 为已写精确值，error_ 为 nullopt；失败构造设 bytes_written_=0、
   error_ 为错误。`succeeded()` 只看 error_ 是否为空；`failure()` 成功返回 nullptr，失败返回内部 enum 指针。
2. DecodeResult 成功构造用完整 record 初始化 optional，并把备用 failure_ 值初始化为 `{}`；失败构造设
   record_ 为 nullopt 并保存 failure。成功时 failure_ 的初始化不表示发生过那个枚举错误。
3. `record()` 成功返回 optional 中对象地址，失败 nullptr；`failure()` 正好相反；`succeeded()` 看 optional。
4. 访问器返回的是结果对象成员指针，不能从临时 DecodeResult/EncodeResult 取指针后跨越临时对象寿命保存。

不加公共默认构造来“先造对象稍后补状态”；不要让不完整状态进入返回路径。optional 本身无动态分配，
它的物理尺寸由编译器确定，不能把“用了 optional”当作存在堆分配或性能不足的证据。

### B9. 内部工厂：在完整类型之后定义

```cpp
struct RecordCodecAccess final {
    [[nodiscard]] static EncodeResult encode_success(std::uint32_t bytes) noexcept;
    [[nodiscard]] static EncodeResult encode_failure(EncodeError error) noexcept;
    [[nodiscard]] static DecodeResult decode_success(
        RecordHeader header, const std::byte* format_data,
        const DecodedArg* arguments) noexcept;
    [[nodiscard]] static DecodeResult decode_failure(DecodeFailure failure) noexcept;
};
```

四个方法由你放在 struct 内定义（隐式 inline），或在后面显式 inline 定义。它们是 detail 内的受控构造
通路，不是用户扩展点，也不构造/修改 PreparedRecord。调用方只有 encoder/decoder 的已确认结果路径。

前两个直接调用对应私有 EncodeResult 构造；decode_failure 直接调用失败构造；decode_success 先在这个
友元上下文中构造完整 `DecodedRecordView{header, format_data, arguments}`，再交给 DecodeResult。
不要对 optional 调用 `emplace(header, format_data, arguments)` 来试图进入 view 的私有构造：
标准库模板并不继承 RecordCodecAccess 的友元权限。

本节完成条件：新头包含的类型顺序无循环，所有构造初始化全部逻辑字段；policy 能表达全部 u8 level；
DecodedArg 的有效成员约定清楚；成功/失败访问器互斥。接下来开始 C，不需要你创建 self-contained 测试文件。

<a id="record-encoder"></a>

## C. Encoder：把成功的 PreparedRecord 写成精确的 Record

本节只指导你实现 `include/qlog/detail/record_encoder.hpp`。生产算法、接口定义由你填写；
助手接手行为测试、构建检查、sanitizer、汇编和性能验证。这里的接口骨架与字节演算是设计说明，
不能据此声称 codec 已编译通过或已获得性能收益。

### C.1 文件位置、include 方向和定义顺序

在 QLog 的 `include/qlog/detail/record_encoder.hpp` 新建以下结构，不新增 `record_codec.hpp`：

```cpp
#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <type_traits>
#include <utility>

#include "argument_tag.hpp"
#include "format_hash.hpp"
#include "record_header.hpp"
#include "record_limits.hpp"
#include "record_measure.hpp"
#include "record_types.hpp"

namespace qlog::detail::record_encoder_impl {
[[nodiscard]] inline EncodeError encode_error_from_metadata(MetadataError error) noexcept;
template <std::size_t Width>
inline void store_low_bits_le(std::byte* destination, std::uint64_t bits) noexcept;
[[nodiscard]] inline std::byte* encode_argument_unchecked(
    std::byte* cursor, const NormalizedArgument& argument) noexcept;
template <std::size_t N, std::size_t... I>
[[nodiscard]] inline std::byte* encode_arguments_unchecked(
    std::byte* cursor, const PreparedRecord<N>& prepared,
    std::index_sequence<I...>) noexcept;
}  // namespace qlog::detail::record_encoder_impl

namespace qlog::detail {
template <std::size_t N>
[[nodiscard]] EncodeResult encode_v1(
    std::byte* destination, std::size_t destination_size,
    const PreparedRecord<N>& prepared, const RecordMetadata& metadata,
    const RecordValidationPolicy& policy,
    const FormatHashDispatch& hash_dispatch) noexcept;
}  // namespace qlog::detail
```

以上是声明骨架；实际文件必须按这个顺序补上函数定义。模板定义留在头内；非模板 helper 同样在头内
定义并标记 `inline`，避免多个翻译单元产生重复定义，同时让调用方看见参数写入逻辑。
`inline` 不保证优化器一定内联。这个布局保留消除 prepared 临时对象和常量 tag 分派的机会。
`record_types.hpp` 提供结果、metadata、policy 和共享校验，不反向 include encoder。
encoder 不 include decoder、Ring、Channel、formatter，也不采样时间或创建 dispatch。

### C.2 第一个 helper：把共享 metadata 错误映射到 EncodeError

实现 `encode_error_from_metadata(MetadataError)`，用完整 `switch` 逐项映射同名枚举：

| MetadataError | EncodeError |
|---|---|
| `invalid_level` | `invalid_level` |
| `unknown_flags` | `unknown_flags` |
| `reserved_timestamp_status` | `reserved_timestamp_status` |
| `invalid_time_value` | `invalid_time_value` |
| `fallback_timestamp_not_configured` | `fallback_timestamp_not_configured` |

不要 `static_cast<EncodeError>(error)`：两套枚举前面有不同错误项，数值不能互相假定。
本 helper 不重新执行 policy 查询；`record_metadata_impl::validate_record_metadata(metadata, policy)` 已按规定顺序选择了错误。
枚举穷尽后的防御路径采用 `assert(false)` 加 `std::terminate()`；错误值被内部破坏属于程序不变量失效，
不能随意选一个公开错误冒充真实原因，也不能只放一个在 Release 消失的 assert 后掉出非 void 函数。

### C.3 固定宽度写入：store_low_bits_le<Width>

此 helper 只接收已经证明容量足够的可写位置，不接收 `remaining`，不返回 recoverable error。
步骤固定为：

1. `static_assert` 限定 `Width` 为 1、2、4、8；其他宽度是编译期错误。
2. 按 Width 选择 `std::uint8_t/uint16_t/uint32_t/uint64_t` 局部类型。可使用嵌套
   `std::conditional_t`，或分别写 `if constexpr` 分支；不要引入运行时宽度参数。
3. 把 `bits` 显式转换为选定的无符号局部值，高位自然截断，保留恰好 Width bytes 的位模式。
4. `std::memcpy(destination, &local, Width)`，由常量 Width 表达准确写入量。

现有 `record_header.hpp` 已静态要求宿主为 little-endian，所以局部整数的对象表示就是本次所需 LE bytes。
函数名强调 wire 语义；当前支持边界仍是 little-endian 宿主。今后支持 big-endian 时需改 helper 和 Header
序列化，不能仅删除现有端序断言。此处不新增平台扩展任务。

这避免在任意 byte offset 使用 `reinterpret_cast<std::uint32_t*>(cursor)` 后解引用：后者可能未对齐，
也混淆对象生命周期和别名规则。`memcpy` 的常量宽度能否被合适地降低为机器 store，由助手查实际 codegen。
本函数不推进指针，由上层显式 `cursor += Width`；这样每个 tag 的总宽度可以就地核对。

### C.4 单参数写入：encode_argument_unchecked

输入是 measure 已完成规范化的 `NormalizedArgument`，输出是当前参数之后的第一个位置。
不再调用 traits matcher、`normalize_one`、`encoded_size` 或 `strlen`；不接收原始 C++ 参数。
`argument.tag` 已是最终 wire tag，`CStr` 已在 measure 时成为 `Utf8String` 或 `NullUtf8`。

所有合法 tag 先写恰好一个 tag byte，再按下表写 payload。每条分支显式返回推进后的 cursor。
可以在每个合法分支调用一个简单的 tag-byte 写入语句；不要为了共享 tag store 让未知 tag 也被当成合法值。

| Tag / wire 值 | payload 宽度 | 写入数据与游标总增量 |
|---|---:|---|
| `Bool / 0x01` | 1 | `bits` 的最低 1B，规范值已是 0 或 1；共 2B |
| `Char / 0x02` | 1 | 最低 1B，按 byte 保留；共 2B |
| `Int8 / 0x03`、`UInt8 / 0x04` | 1 | `store_low_bits_le<1>`；共 2B |
| `Int16 / 0x05`、`UInt16 / 0x06` | 2 | `store_low_bits_le<2>`；共 3B |
| `Int32 / 0x07`、`UInt32 / 0x08` | 4 | `store_low_bits_le<4>`；共 5B |
| `Int64 / 0x09`、`UInt64 / 0x0A` | 8 | `store_low_bits_le<8>`；共 9B |
| `F32 / 0x0B` | 4 | `bits` 中缓存的 float 位模式；共 5B |
| `F64 / 0x0C` | 8 | `bits` 中缓存的 double 位模式；共 9B |
| `Pointer64 / 0x0D` | 8 | 缓存的 uint64 值，不解引用、不转回宿主指针；共 9B |
| `Utf8String / 0x0E` | 4 + byte_count | u32 LE 长度，再复制原始 bytes；共 5 + byte_count B |
| `NullUtf8 / 0x0F` | 0 | 只写 tag，不读 bytes 或 bits；共 1B |
| `Invalid / 0x00`、其他值 | 不适用 | 内部不变量失效，assert 后 terminate |

同宽度的 scalar case 可以合并分支，但必须保留输入 tag。例如 `Int32` 与 `F32` 共用 4B store，
不能因此把两者都输出为 `UInt32`。Bool 的范围已由 normalization 保证，可以 Debug assert，不新增编码错误。

有符号整数在 measure 中先转到同宽度无符号表示。例如 `int16_t(-2)` 的低 16 bits 是 `0xFFFE`，
这里直接写 `FE FF`，不需要转回有符号数。F32/F64 同样只运输已缓存的 bits，不进行浮点数值转换，
因此编码步骤不会主动规范化 NaN 或把 `-0.0` 改成 `+0.0`。

`Utf8String` 分支按顺序完成：

1. 写 `0x0E` 并推进 1B。
2. 用 `store_low_bits_le<4>(cursor, argument.byte_count)` 写长度，推进 4B。
3. 仅当 `byte_count != 0` 时执行 `memcpy(cursor, argument.bytes, argument.byte_count)`。
4. 将 cursor 推进 `byte_count`；长度为零时不访问源，也不对空源指针做算术。

不追加 NUL，不做 UTF-8 检查，不做 4B/8B 对齐。`"A\0B"` 配合显式长度 3 必须复制全部三个 byte。
空字符串输出 `0E 00 00 00 00`；null cstr 输出 `0F`。这两种 wire 值不可合并。

对于 `Invalid` 或未知 tag，使用 `assert(false)` 后 `std::terminate()`，不使用裸 `__builtin_unreachable()`。
正常 private-constructed prepared 不会到达此分支；如果内部数据已被破坏，不能在已经写入 destination 后
返回 `EncodeError` 并宣称仍具强失败保证。这个终止路径也不是对不可信解码输入的处理方式。

### C.5 参数展开：encode_arguments_unchecked<N, I...>

这个 helper 的目的，是在调用处保留 N 个槽位的可见访问与写入顺序。加入 `static_assert(sizeof...(I) == N)`；
唯一调用点传 `std::make_index_sequence<N>{}`，不要允许业务调用方自行组织或遗漏索引。
关键伪代码只有以下折叠和返回，不需要递归：

```cpp
((cursor = encode_argument_unchecked(cursor, prepared.arguments_data()[I])), ...);
return cursor;
```

逗号 fold 从左向右完成每次写入，并让下一个参数使用刚更新的 cursor。不要用算术 fold 累加返回指针，
也不要先批量计算所有参数指针；不同宽度必须按前一个参数的结束位置连接。
N 为 0 时，这个逗号 fold 是合法空展开，不调用单参数 helper，也不读取 `arguments_data()[0]`。
直接返回进入函数时的 cursor。`std::array<T, 0>::data()` 可以为空，不能先对它做 `+ 0` 或解引用。

这保留当前通用 `PreparedRecord<N>` 和零参数入口。无需新建 `encode_zero_args`、专门 result 或不同 wire 格式。
运行时 `NormalizedArgument` 仍带 tag；是否能消除 switch，取决于 measure 与 encode 的实际调用可见性。
不要把单参数 encoder 的实现移到不透明 `.cpp` 后再假定优化效果相同，也不要据此改造现有 producer 表示。

### C.6 encode_v1 的第一次写入之前：只做这一组 preflight

先执行以下顺序，任何失败立即通过 `RecordCodecAccess::encode_failure(...)` 构造结果：

1. `destination_size > 0 && destination == nullptr`：`invalid_destination_metadata`。
2. `destination_size != prepared.payload_size()`：`destination_size_mismatch`。
3. 调用一次 `record_metadata_impl::validate_record_metadata(metadata, policy)`；如果返回 optional error，
   用 `record_encoder_impl::encode_error_from_metadata(*error)` 映射后立即返回。

共享 metadata 校验内部顺序固定为 level、unknown flags、reserved timestamp status、time/status 值、fallback
policy。没有错误才进入写入阶段。不要在这组检查之前创建 `destination + 32`、写零 Header 或写任何 tag。
`encode_v1` 位于 `qlog::detail`；调用 C.1 中其他编码 helper 时同样写 `record_encoder_impl::` 前缀。
不能假定旁边嵌套 namespace 的函数会被自动找到。
`destination == nullptr && destination_size == 0` 先通过第 1 项，再在第 2 项失败，因为成功 prepared 至少 32B。
目标即使比 payload 更大也失败；本接口的 size 是本次完整 Record 长度，不是“至少能容纳”的 capacity。

prepared 已证明源 metadata、长度、参数数目、总尺寸与 quota。无需重复 checked measure，不再逐参数检查
字符串地址，也不建立第二个可失败写入计划。源与目标范围不重叠、源存活且不被并发修改，仍是调用方前置条件；
裸指针接口不能可移植地证明任意地址可访问，不新增 `source_overlap` 等未冻结错误。

### C.7 encode_v1 的成功阶段：format、arguments、Header

在所有 preflight 通过后，按下列步骤写入；此后不存在可恢复失败返回：

1. 创建 `RecordHeader header{}` 并填写 `time_value/category_id/level/flags`。
   `format_bytes = prepared.format_size()`，`args_bytes = prepared.args_size()`；
   `arg_count = static_cast<std::uint16_t>(N)`，其安全性来自 PreparedRecord 的 `N <= 32` 约束。
2. 令 `cursor = destination + kRecordHeaderBytes`。从 prepared 取得 format 指针、长度和预计算 stored hash。
3. 若 `precomputed_stored_hash() != 0`，直接设置 Header hash；只有 format 长度非零才 `memcpy` format bytes。
   不重新 hash 验证，不再归一化：这已经是同算法、同 bytes 的 stored hash，由创建 FormatInput 的入口保证。
4. 否则走 runtime 路径。format 长度非零时调用
   `hash_dispatch.copy_raw_unchecked(prepared.format_data(), cursor, prepared.format_size())`。
   它一遍完成复制与 raw hash；将 raw 交给 `stored_hash_from_raw` 后赋给 `header.format_hash`。
   format 长度为零时可直接取 raw 0，再统一归一化为 stored 1，避免无意义的间接调用和任何空指针操作。
5. 将 cursor 推进 format 长度；调用参数展开 helper，接收并保存返回的最终 cursor。
6. Debug assert 最终 cursor 与 `destination + prepared.payload_size()` 相等；可以额外核对参数段增量。
   这些是内部一致性断言，不能改成首次写入后的 `destination_size_mismatch` 或其他失败返回。
7. 最后 `std::memcpy(destination, &header, sizeof header)`，一次复制全部 32B Header。
8. 通过 `RecordCodecAccess::encode_success(prepared.payload_size())` 返回精确 `bytes_written`。

局部 Header 的对齐由其类型保证；destination 不必满足 `alignof(RecordHeader)`，所以仍用 memcpy。
Header 最后写入有利于审计成功路径，但它不是原子发布，也不代替将来 Ring 的 release commit。
`FormatHashDispatch::automatic()` 由调用方在初始化阶段取得，本函数不得逐条进行 CPUID、初始化可变全局状态
或持锁。raw dispatch 返回 uint64，成功 prepared 加 preflight 已满足调用前提，不需要再次调用 checked hash wrapper。

### C.8 手算一次 byte offset，再对照自己的实现

取 format bytes 为 `{}`，参数依次为 `int16_t(-2)`、显式长度 3 的 `"A\0B"`、null cstr、`true`。
假定 caller metadata 满足所传 policy。measure 应得到 format 2B、args 14B、payload 48B：

| payload offset | bytes / 内容 | 解释 |
|---|---|---|
| 0..31 | 完整 RecordHeader，最后写入 | format_bytes=2、args_bytes=14、arg_count=4 |
| 32..33 | `7B 7D` | format `{}`，没有 terminator |
| 34..36 | `05 FE FF` | Int16 tag + -2 的 2B 位模式 |
| 37..41 | `0E 03 00 00 00` | Utf8String tag + u32 LE 长度 |
| 42..44 | `41 00 42` | 字符串包含中间 NUL |
| 45 | `0F` | NullUtf8，没有长度前缀 |
| 46..47 | `01 01` | Bool tag + true |
| 48 | one-past-end，不写入 | 最终 cursor |

format `{}` 的 frozen stored hash 是 `0x000000000e3ee044`，Header offset 8..15 应为
`44 E0 3E 0E 00 00 00 00`。空 format 且无参数则恰好 32B，runtime hash 必须写 stored 1。
这两个例子是实现自查的字节推导；最终 golden 的独立期望与执行由助手负责。

### C.9 需要避开的实现偏差与交接条件

不要先 memcpy 再调用完整 hash；不要把 u32 长度写成宿主 `size_t`；不要把 padding 或 NUL 写入 wire；
不要为了统一所有字符串再调用 strlen；不要让 runtime hash 的 raw 0 直接进入正常 Encoder Header。
不要在写入后发现偏移不一致再返回失败，更不要把未知内部 tag 当成“跳过一个参数”继续写 Header。

BQLog 当前参考提交 `60ef4d3` 的 `include/bq_log/misc/bq_log_impl.h`，`log::do_log` 模板在约 251 行起
先调用 `make_size_seq<true>(args...)` 保存尺寸，再交给 `_do_log_args_fill` 写参数；本章借鉴尺寸复用与
调用点可见的编码组织。BQLog 的 `align_4`、参数格式和目标访问方式并非 QLog wire 合同，不能随之照搬。

交接时提供完成的 `record_encoder.hpp` 及你遇到的具体编译诊断或设计疑点。正文实现应能清楚指出：
唯一 recoverable-return 区域、每类 tag 的精确宽度、空展开路径、raw→stored 的唯一位置、最终 Header 写入点。
助手负责构建与测试接线、每个 EncodeError 的目标不变检查、golden/round-trip、边界和 codegen/性能验证；
你不需要编写或执行测试。助手发现生产实现问题后，反馈文件、函数、原因和修改步骤，由你继续实现。

<a id="record-decoder"></a>

## D. 实现独立 Decoder：先界定范围，再解释 tag

本节对应 I1-C 的 Decoder。你实现 `record_decoder.hpp` 和 `src/record_decoder.cpp`；
我在实现交接后编写与运行 golden、corruption、property、fuzz 和 sanitizer 验证。
本节不接 Ring，不实现 formatter；输入是调用方已界定的完整 Record payload。

Decoder 的工作是把不可信 bytes 还原成受边界约束的借用视图。不要把 Encoder 的
“prepared 已经证明正确”沿用到这里：所有 payload 校验在 Debug 和 Release 中永久执行。

### D1. 文件位置、include 与定义顺序

先创建 `include/qlog/detail/record_decoder.hpp`。它只声明下面这个入口：

```cpp
#pragma once
#include <cstddef>
#include "qlog/detail/record_types.hpp"

namespace qlog::detail {
[[nodiscard]] DecodeResult decode_v1(
    const std::byte* payload_data, std::size_t payload_size,
    DecodedArg* workspace, std::size_t workspace_count,
    const RecordValidationPolicy& policy) noexcept;
}  // namespace qlog::detail
```

这个头不 include `record_measure.hpp`、`record_encoder.hpp`、`format_hash.hpp` 或 wrappers。
共同类型及共享 metadata 校验由前文的 `record_types.hpp` 提供，不在 Decoder 内重新定义。
本轮不新增 `record_codec.hpp` umbrella；需要 Encoder 的调用者明确 include Encoder 头。

`src/record_decoder.cpp` 按以下顺序组织：

```text
1. include qlog/detail/record_decoder.hpp，先检验自己的声明头
2. include cstdint、cstring、type_traits、cassert、exception
3. include qlog/detail/argument_tag.hpp、record_header.hpp、record_limits.hpp
4. namespace qlog::detail
5. 匿名 namespace：load_le<UInt> 的模板定义、decode_failure_from_metadata 的定义
6. 关闭匿名 namespace
7. decode_v1 的非模板定义
8. 关闭 qlog::detail
```

`load_le` 仅供这个 `.cpp` 使用；不需要为了一个 Decoder 的局部 helper 建立公共工具头。
`decode_v1` 必须位于具名 namespace，不能放进匿名 namespace，否则无法与头文件声明链接。

### D2. 先写固定宽度读取 helper

准确签名如下；定义放在 `decode_v1` 前面：

```cpp
template <class UInt>
[[nodiscard]] UInt load_le(const std::byte* source) noexcept;
```

函数内部只做三件事：

1. `static_assert` 限制 `UInt` 为 `uint8_t/uint16_t/uint32_t/uint64_t` 之一。
2. 建立 `UInt value{}`，用一次 `std::memcpy(&value, source, sizeof value)` 复制固定宽度。
3. 返回 `value`，不读取额外 byte，不在 helper 中再次解析 tag。

`RecordHeader` 已要求 native little-endian；因此本目标上复制到等宽 unsigned 值就是 LE load。
若以后支持其他字节序，需要另立平台方案；本节不加入未经使用的通用 byteswap 分支。
不要仅用 `is_unsigned_v` 限制模板，因为它不能完整表达这里的四种精确存储类型。

helper 的前置条件是 `source` 指向至少 `sizeof(UInt)` 个真实可读 byte。
**边界检查由调用者在形成这个 source pointer 前完成。** helper 不拥有长度，也不返回截断错误。
这样每个 tag 分支只检查一次范围，再执行编译期宽度明确的复制；是否消除外部 memcpy 调用由我检查汇编。

反例：`*reinterpret_cast<const uint64_t*>(payload_data + cursor)` 既可能未对齐，
也把对象访问规则和 payload 存储混在一起；即使 x86 经常容忍未对齐指令，也不能采用这种写法。

### D3. 给 decode_v1 建立整数 offset 状态

先只建立下列局部状态，不急着写参数 switch：

```text
header            : RecordHeader 的对齐局部值副本
format_begin      : size_t，固定为 sizeof(RecordHeader)，也就是 32
args_begin        : size_t，格式范围检查通过后才计算
args_end          : size_t，args exact-range 检查通过后等于 payload_size
cursor            : size_t，初始化为 args_begin
argument_index    : size_t，从 0 开始；通过 arg_count <= 32 保证之后可安全收窄
```

offset 始终相对 `payload_data`，不要混用“相对参数区”的位置与“相对 Record”的位置。
整数状态不意味着可以随意相加：只有先证明长度不超过剩余量，才推进 cursor 或计算 args_begin。
进入参数循环后一直保持 `args_begin <= cursor <= args_end <= payload_size`。

用下面的模式处理一个固定宽度读取；这是控制流伪代码，不是让你增加一套错误模型：

```text
remaining = args_end - cursor
if width > remaining:
    return 对应截断错误，offset = args_end，index = 当前参数
value = load_le<等宽 unsigned>(payload_data + cursor)
cursor += width
```

反例：不要先计算 `value_end = payload_data + cursor + width` 再与 end 比较。
也不要先做 `cursor + width > args_end`，这会把防溢出责任交给未经检查的加法。

错误统一交给前文提供的内部工厂：

```cpp
RecordCodecAccess::decode_failure(DecodeFailure{
    error, error_offset, argument_index
});
```

实际写入 `DecodeFailure::argument_index` 前，参数下标先利用已证明的 `<= 32` 范围显式转为 `uint8_t`；
非参数错误直接使用 `0xFF`。不要为了生成诊断字符串引入分配、日志或 stderr 输出。

### D4. 先完成 Header 和区域校验，再触碰 workspace

`decode_v1` 的入口检查严格按以下顺序写，不能把便于实现的检查提前：

1. `payload_size != 0 && payload_data == nullptr`：返回 `invalid_payload_metadata`，offset 0。
2. `workspace == nullptr || workspace_count != 32`：返回 `invalid_workspace`，offset 0。
3. `payload_size < sizeof(RecordHeader)`：返回 `record_too_small`，offset 为 `payload_size`。
4. 现在才 `memcpy` 32B 到局部 `RecordHeader header{}`，不把 payload 转为 `RecordHeader*`。
5. 从 header 构造 `RecordMetadata{time_value, category_id, level, flags}`，调用共享的
   `record_metadata_impl::validate_record_metadata(metadata, policy)`；若有错误，按下表映射并立即返回。
6. 先检查 `header.format_bytes > 8192`，失败为 `format_too_large`，offset 16。
7. 计算 `after_header = payload_size - sizeof(RecordHeader)`；若 format 长度大于它，
   返回 `invalid_format_length`，offset 16。此时才允许计算 `args_begin = 32 + format_bytes`。
8. 计算 `available_args = payload_size - args_begin`；要求它**恰好等于** `header.args_bytes`，
   不相等返回 `invalid_args_length`，offset 20。不要只检查“小于等于”。
9. 检查 `header.arg_count <= 32`；否则返回 `arg_count_exceeded`，offset 28。
10. 设置 `args_end = payload_size`、`cursor = args_begin`，再进入参数循环。

步骤 1～9 都不需要清零 workspace。输入范围合法性由调用方保证；这些检查只能验证可观察 metadata，
不能证明任意非空地址真实有效、workspace 对齐或它与 payload 不重叠。
workspace 的对象生命周期、对齐，以及 workspace 与 payload 不重叠，均为调用方前置条件。

共享校验已经按 `level -> unknown flags -> status -> time -> fallback policy` 排好顺序。
Decoder 只做 `MetadataError` 到本层 `DecodeFailure` 的显式映射，不重写另一份判断：

| MetadataError | DecodeError | error_offset | argument_index |
|---|---|---:|---:|
| invalid_level | invalid_level | 30 | 0xFF |
| unknown_flags | unknown_flags | 31 | 0xFF |
| reserved_timestamp_status | reserved_timestamp_status | 31 | 0xFF |
| invalid_time_value | invalid_time_value | 0 | 0xFF |
| fallback_timestamp_not_configured | fallback_timestamp_not_configured | 31 | 0xFF |

映射 helper 的准确签名为 `DecodeFailure decode_failure_from_metadata(MetadataError error) noexcept`，
定义在 `.cpp` 的匿名 namespace、`decode_v1` 之前。它按上表完整 switch 返回三个已初始化字段；
不再次校验 metadata，也不依赖 `EncodeError`。穷尽后的内部非法枚举防御路径用 assert 后 terminate，
不能掉出非 void 函数；这不用于处理任何不可信 wire tag。
入口遇到 optional error 时，将 `decode_failure_from_metadata(*error)` 交给
`RecordCodecAccess::decode_failure(...)`。后面 payload/tag 错误仍直接构造自己的 DecodeFailure。

这里没有 category 合法值检查，也没有 hash 校验。`format_hash == 0` 必须保留在成功 header 副本中。
低两位状态固定为 primary_valid=0、fallback_valid=1、time_unavailable=2、reserved=3。
primary/fallback 的有效时间可以为 0；只有 time_unavailable 配非零 time_value 才是 invalid_time_value。

### D5. 按声明参数数目循环，每次先读 tag

循环条件采用 `argument_index < header.arg_count`，不要仅用 `cursor < args_end` 驱动解析。
每轮按以下步骤写：

1. 若 `cursor == args_end`，说明下一期待参数连 tag 都没有：返回 `decoded_count_mismatch`。
2. 保存 `tag_offset = cursor`；读取一个 byte，用 `std::to_integer<uint8_t>` 取得值，再转换为 `ArgumentTag`。
3. `++cursor`。步骤 1 已证明一个 tag byte 可读，因此现在推进不会越过 args_end。
4. 建立局部 `DecodedArg decoded{}`，其默认 active member 是 `value.bits`，值为 0。
5. 在运行时 tag switch 中完成该参数全部校验与读取。只有这个参数完全成功后，才赋给 `workspace[index]`。
6. 然后推进参数下标，继续下一轮；未知 tag 直接失败，不能猜宽度或在后续 bytes 中搜索同步点。

局部 `DecodedArg` 的 tag 使用本次读到且分支已确认合法的 tag；不要把当前输入 tag 写进上一槽位。
只在参数成功后提交槽位，可以避免暴露半个参数；这不意味着整体失败时要回滚先前槽位。

固定宽度分支按下表分组。每组先比较 `width > args_end - cursor`，失败为 `truncated_value`；
成功后 load 等宽 unsigned，零扩展到 `decoded.value.bits`，设置 `byte_count = 0` 并推进 cursor。

| tag 分组 | width | load 类型 | 额外动作 |
|---|---:|---|---|
| Bool | 1 | uint8_t | 只允许 0/1，否则 invalid_bool，offset 为这个 value byte |
| Char、Int8、UInt8 | 1 | uint8_t | 保留低 8 bit，Char 不做符号扩展 |
| Int16、UInt16 | 2 | uint16_t | 保留低 16 bit |
| Int32、UInt32、F32 | 4 | uint32_t | F32 保留原始 bit pattern，不做数值转换 |
| Int64、UInt64、F64、Pointer64 | 8 | uint64_t | Pointer64 只保存数值，不重建可解引用指针 |

Bool 分支需保存读取前的 `value_offset = cursor`；`invalid_bool` 指向它，不能指向 tag 或读取后的 cursor。
其余定宽分支没有内容合法性检查；例如 NaN、负零、最高位为 1 的整数 bit pattern 都应保留。

`Invalid` 和 switch 的 default 都返回 `invalid_or_unknown_tag`，offset 为 `tag_offset`。
不要为 `Invalid` 分配空 payload 成功分支，它是明确禁止出现在成功 wire record 中的 tag。

### D6. Utf8String 与 NullUtf8 各写一个明确分支

`Utf8String` 是 `[u32 byte_count][bytes]`；按下面顺序完成一个参数：

1. 若 `4 > args_end - cursor`，返回 `truncated_string_length`，offset 为 args_end。
2. `load_le<uint32_t>(payload_data + cursor)` 取得 `byte_count`，再推进 cursor 4B。
3. 把 byte_count 转成 size_t 参与比较；若大于 `args_end - cursor`，返回 `truncated_string`。
4. 现在才形成 `payload_data + cursor`，赋给 `decoded.value.bytes`，激活 pointer 成员。
5. 保存原始 uint32 长度；tag 设置为 `Utf8String`；按已验证长度推进 cursor。

这里不复制字符串内容，不追加 terminator，不调用 strlen，也不验证 UTF-8。
长度为 0 仍然是 Utf8String，仍然激活 `value.bytes`。如果 cursor 恰好在 payload 尾部，
`payload_data + cursor` 是合法 one-past pointer，可以保存，但不能解引用。

`NullUtf8` 不消费任何 value byte：保留/设置 `value.bits = 0`、`byte_count = 0` 和相应 tag。
它与空字符串通过 tag 区分；不要用空指针或 length==0 把两种 wire 含义合并。

前文的 16B DecodedArg 使用 `union { uint64_t bits; const byte* bytes; }`，长度放在 union 外。
仅 Utf8String 使用 pointer 成员；全部其他 tag 使用 bits。字段赋值和后续访问都必须遵守这个约定。
禁止先写 `value.bits` 再读 `value.bytes`，也禁止通过错误 union 成员把 F32/F64 直接当 float/double 读取。

将来使用 signed 参数时，先取等宽 unsigned，再 `bit_cast` 到等宽 signed，然后按需要符号扩展：
Int8 的 wire `FF` 在这里保存 bits=255；使用时先恢复 int8_t 的 -1，再扩展为 int64_t 的 -1。
F32 则先从 bits 取 uint32_t，再 `bit_cast<float>`；F64 对 uint64_t 做 `bit_cast<double>`。
这些说明约定值如何解释，本节不实现 formatter，也不把浮点数转换成整数数值。

### D7. 完成计数、尾部校验与成功视图

离开按 arg_count 驱动的循环时，已经成功解出且只解出声明数量的参数。
可以用 Debug assert 核对计数，但不要删掉读取下一 tag 前的运行时缺失判断。
随后检查 `cursor != args_end`；若还有 bytes，返回 `trailing_args_bytes`，
offset 为 cursor，argument_index 为 header.arg_count（已证明不超过 32，可以转为 uint8_t）。

全部条件满足后，用内部工厂一次生成结果：

```cpp
RecordCodecAccess::decode_success(
    header, payload_data + sizeof(RecordHeader), workspace
);
```

工厂返回的 view 保存 Header 值副本、format 借用范围以及只读参数槽视图，不保存局部 decoded 的地址。
format_size()/argument_count() 直接从 Header 副本派生，工厂不重复接收或保存另一份长度与数量。
format 长度为 0 时同样允许保存已验证范围中的 pointer；32B 空 Record 的 format pointer 就是 one-past。
对 0 参数 Record，workspace 仍须非空且声明恰好 32 槽，但成功路径无需写任何槽位。

成功 view 的有效期是 payload 与 workspace 有效期的交集；Frame release 或 workspace 下一次复用，
任意一件事发生就结束借用。复制一个 view 不会延长这两个对象的生命周期。
失败只返回 DecodeFailure；workspace 已写的槽位均不是可用逻辑结果，不清零、不回滚、不公开部分 view。

### D8. 对照这张完整错误定位表收口

表中的 `i` 是当前期待的参数编号，`args_end` 是整个 Record 内参数区末尾的整数 offset。

| DecodeError | 触发位置/条件 | error_offset | argument_index |
|---|---|---|---|
| invalid_payload_metadata | null + nonzero payload | 0 | 0xFF |
| invalid_workspace | workspace 为 null 或 count 不是 32 | 0 | 0xFF |
| record_too_small | Header 不足 32B | payload_size | 0xFF |
| invalid_level | level policy 拒绝 | 30 | 0xFF |
| unknown_flags | flags 高位非零 | 31 | 0xFF |
| reserved_timestamp_status | 低两位为 3 | 31 | 0xFF |
| invalid_time_value | status=2 且 time_value 非零；status=0/1 允许 time 为 0 | 0 | 0xFF |
| fallback_timestamp_not_configured | status=1 且 fallback policy 拒绝 | 31 | 0xFF |
| format_too_large | format_bytes > 8192 | 16 | 0xFF |
| invalid_format_length | format 超过 Header 后剩余 bytes | 16 | 0xFF |
| invalid_args_length | args_bytes 不等于格式后的剩余 bytes | 20 | 0xFF |
| arg_count_exceeded | arg_count > 32 | 28 | 0xFF |
| invalid_or_unknown_tag | tag 为 Invalid 或未知值 | tag_offset | i |
| invalid_bool | Bool value 不为 0/1 | value_offset | i |
| truncated_value | 固定值剩余宽度不足 | args_end | i |
| truncated_string_length | u32 length prefix 不完整 | args_end | i |
| truncated_string | 字符串 bytes 不完整 | args_end | i |
| decoded_count_mismatch | 下一个期待参数连 tag 都没有 | args_end | i |
| trailing_args_bytes | 声明参数均已解完仍有 bytes | cursor | header.arg_count |

三个具体例子帮助你检查控制流是否写对，均假定 Header 与区域检查已通过：

- `arg_count=0` 且 args 有一个 byte：不进入 tag switch，直接 trailing_args_bytes，index=0。
- `arg_count=1`、参数区只有 Int32 tag：不是 count mismatch，而是 truncated_value，offset=args_end。
- `arg_count=1`、Utf8String tag 后有完整的零长度 prefix：成功空字符串；若 prefix 只有 3B，
  则 truncated_string_length，即使这 3B 恰好都是零也不能推测缺失的第 4B。

交接给我时，Decoder 应已具备上述入口、全部 tag 分支、完整错误映射与成功借用视图。
我负责验证每个错误及其优先级、精确 offset/index、非对齐输入、所有截断位置、hash=0、空范围与工作区复用。
你不需要编写测试代码或执行测试命令；我会将验证发现对应回具体实现位置，生产修改仍由你完成。

<a id="implementation-handoff"></a>

## E. 生产构建接线与交付：由你完成什么

### E1. 只把真实的非模板生产实现加入 qlog

A 章完成后，根 `CMakeLists.txt` 的现有 `qlog` target 应已经包含 software/common hash，
以及按平台条件加入的 x86 hardware 源。C/D 完成后，再将下面这一项加到同一个 target：

```cmake
target_sources(qlog PRIVATE src/record_decoder.cpp)
```

插入点是 `add_library(qlog STATIC ...)` 已经创建目标之后，与 hash 的生产接线放在一起即可。
不要把以上行添加到 `tests/CMakeLists.txt`，也不要创建 `record_encoder.cpp` 来存放模板定义。
`record_types.hpp` 中 header-only 定义、`record_encoder.hpp` 的模板、`record_decoder.hpp` 声明
各有明确归属；不能为了让链接暂时成功而让某个 `.cpp` include 另一个 `.cpp`。

生产构建继续保持 C++20、`CXX_EXTENSIONS OFF` 和既有 warning；SSE4.2 只给硬件 hash TU。
不要为 codec 加 `-march=native` 或全局 `-msse4.2`。如果 Debug 下慢，不在这一步加满 `always_inline`，
先让 Codex 分别检查 Release 的实际编译结果和调用路径。

根 CMake 的生产 source/ISA 接线由你实现；测试 target、测试 include path、GTest 发现、测试专用宏、
benchmark target 与脚本接线由 Codex 后续实现。本次文档交付并未写入这段 CMake。

### E2. 一次交接所需的文件清单

| 交付组 | 维护者提交的生产内容 | Codex 接手后检查 |
|---|---|---|
| Hash | 两个 format_hash 头、共用 core、software/common/x86 `.cpp`、生产接线 | 编译、向量、SW/HW/reference 一致、copy 失败不变 |
| Common | `record_types.hpp` | self-contained、16B 布局、policy、结果构造和访问状态 |
| Encoder | `record_encoder.hpp` | 全 tag wire、所有 preflight 失败目标不变、精确字节数 |
| Decoder | `record_decoder.hpp`、`src/record_decoder.cpp`、生产接线 | 全部错误与定位、边界、借用工作区、不依赖 encoder/hash |

你不需要提供测试代码、测试报告或自写 benchmark。完成后只需说明修改了哪些生产文件，以及仍有的
编译诊断或实现疑点。可以先交接 hash 依赖，再继续 B/C/D；这不形成额外教学轮次。

不需要为了交接改动 Ring、`RecordHeader`、`ArgumentTag` 或已完成的 measure。
若生产编译提示问题，记录完整首个错误及其文件位置，不以放开准入/边界检查来消除诊断。

<a id="verification-performance"></a>

## F. 合并后的第三轮：验证与性能收口如何承接

本节说明 Codex 在生产实现交接后承担的工作，以及你何时需要继续改生产代码。
它是未来的执行安排；本次仅文档更新，不把任何一项标记为已经通过。

### F1. 验证顺序和结果归属

1. **先封住构建与依赖边界。** Codex 补 self-contained/compile-fail 等测试与独立 target，检查 codec 的
   header include 链、错误接口、raw backend 可链接性、C++20 和局部 ISA 配置。
2. **先验证 hash，再验证 codec。** 手写 known vectors 与独立 reference 验证算法身份；SW/HW 的共用
   分块核心不能同时充当独立 expected。具备合法能力检测后才运行 hardware 路径。
3. **分别验证 encoder 与 decoder。** Encoder 用独立 golden 和每个错误下整段 destination 的不变性；
   Decoder 用手写合法/损坏 bytes、所有截断点、完整错误优先级和定位。round-trip 是补充，不能证明
   两端没有共同偏差；canary 也不能单独证明没有越界读。
4. **补 I1-D 剩余门禁。** GCC/Clang Debug/Release、ASan+UBSan、property/fuzz/corpus replay、coverage
   及既有回归范围遵守主规范第 14～17 节。capability skip 与环境未具备的门禁单独记录。
5. **基线正确后才测性能。** Codex 建立 Record Core 独立 benchmark/codegen 证据，不用已完成的 Ring
   benchmark 数字代替 hash/measure/encode/decode 结果。

验证失败时，Codex 给出“文件/函数、触发输入、应有行为、实际表现、根因与修改步骤”。维护者完成生产修订，
Codex 重跑受影响验证。失败后不擅自改 frozen expected、删除检查或代写你的生产算法。

### F2. 第一版先保留什么

生产基线已经由本指南确定：

- 当前 `PreparedRecord<N>` 与 `NormalizedArgument`，标量按值快照、字符串借用和长度复用。
- Encoder helper 与模板定义在头中；Decoder runtime tag switch 在 `.cpp`。
- 新 Decoder 槽位采用 16B tag/union/length 表示；按实际 count 填充，复用 caller workspace。
- immutable 函数指针 hash dispatch 在初始化时选择；literal 预计算，runtime 融合 copy/hash。
- checked arithmetic、encoder 强失败保证、decoder Release 校验与确定性错误继续保持。

不要仅因 `sizeof(PreparedRecord<32>)` 较大就先改成 typed tuple。源码中存在数组、optional 或 switch，
不能证明它们在实际组合路径仍付出同样的运行时成本；反过来，“可内联”也不能证明优化器已将它们消除。

### F3. 组合路径需要看什么，证据出现后如何改

| Codex 的观察对象 | 只有什么证据出现才提出生产变更 | 给维护者的实现方向 |
|---|---|---|
| `measure -> encode` | 固定 tag/宽度的分派、描述符栈写读仍存在，且对应实际成本 | 先检查函数定义可见性与 helper 边界；必要时才对照 typed prepared |
| hash dispatch | 短 runtime format 的间接调用成本在组合场景可稳定复现 | 同一算法上比较稳定 backend 分支与不可变函数指针，保留冷路径检测 |
| 定宽 load/store | 1/2/4/8B 操作产生意外的外部 helper call 或冗余搬运 | 核对常量宽度、局部类型和 include/内联边界，不采用未对齐 typed 解引用 |
| Decoder 工作区 | 出现每条记录清零全部 32 槽或重复保存已在 Header 的长度 | 移除无用全量初始化/冗余成员，仍保证 active member 与成功视图不变量 |

typed prepared 若确需采用，仍要保证标量在 measure 时快照、CStr 只求长一次、字符串借用期、错误顺序和
失败目标不变；不能为减少 scratch 改成 encode 再读取原始业务对象。最终只保留一套内部 prepared 主接口，
并同步其物理表示条款。变体的生产代码仍由维护者编写，不在本次指南交付中预先实现第二条路径。

### F4. 性能比较的边界与保留标准

先报告 hash-only/copy-and-hash、measure→encode、独立 decode 三组，注明 format/args/Record bytes。
输入包括空记录、2 个整数、混合参数、短长字符串、32 参数；hash 长度边界按主规范矩阵覆盖，区分
预计算与 runtime、可用 backend、热数据复用与轮转工作集。

Codex 在计时外完成 setup、分配、输入生成和输出消费；吞吐批次与延迟模式分开。
不同实现同机同编译器、相同工作量、before/after 交替多轮，报告 median/MAD；不以单条汇编的长短判胜。
沿用主规范门槛：回归超过 3% 且超过合并 MAD 时必须解释并阻断。第一次基线不设虚构的绝对吞吐目标。
优化必须具有可重复的组合收益、正确性门禁全通过，并说明代码体积/编译成本等代价；证据不足保留基线。

WSL 是开发验证环境；native Linux、可靠 PMU、长时 fuzz/coverage 等尚未实际完成时分别标注待补，
不能将 WSL Ring 历史数字、README 宣传数据或某个 hash microbenchmark 当作完整 logger 性能。
I1 本轮仍不扩展 ARM、Callsite、新 wire、额外 hash 算法或 formatter。

### F5. I1 最后如何关闭

Codex 后续根据主规范 DoD 汇总实际执行的编译器/CPU/flags、测试通过与跳过数量、sanitizer、fuzz/coverage、
组合 benchmark median/MAD、汇编依据、限制和文档同步情况。维护者的实现已写完与 I1 已完成验收分开记录。
只有全部 DoD 满足，才将 I1 标记完成并进入 I2 的 Producer/Channel 链路；本文件的交付本身不关闭 I1。

## G. 对照源码时具体借鉴什么

参考版本固定：BQLog 本地 commit `60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`；fmt 使用官方 `12.0.0`
tag。它们用于判断实现组织和取舍，QLog 的线格式仍由自己的 ADR 冻结。

| 参考位置 | 已读取的实现方式 | 在本指南中的采用边界 |
|---|---|---|
| [BQLog do_log 与参数填充](https://github.com/Tencent/BqLog/blob/60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9/include/bq_log/misc/bq_log_impl.h#L251) | 先建立尺寸序列，后续参数复制复用它 | 复用当前 measure 的长度；编码 helper 对调用处可见 |
| [BQLog API 复制 format](https://github.com/Tencent/BqLog/blob/60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9/src/bq_log/api/bq_log_api.cpp#L260) | 复制 format 时取得 hash | runtime format 走融合 raw copy/hash |
| [fmt 参数 value](https://github.com/fmtlib/fmt/blob/12.0.0/include/fmt/base.h#L1995) | 同一内部参数值通过 union 保存不同类型 | QLog 按闭合 tag 选择有效成员，单独设计 16B 解码槽 |
| [fmt basic_format_arg::visit](https://github.com/fmtlib/fmt/blob/12.0.0/include/fmt/base.h#L2327) | 根据运行时类型 switch 分派 | Decoder 保留 runtime switch；producer 是否消除分派看组合 codegen |
| [fmt reserve/write 组织](https://github.com/fmtlib/fmt/blob/12.0.0/include/fmt/format.h#L446) | 为连续输出取得空间，再进入写入 | I1 利用已测精确容量直接写 bytes，保留自身更严格的 preflight 合同 |

这里没有声称 fmt/BQLog 具有与 QLog 相同的错误顺序、强失败保证、工作区容量或 packed wire。
不要照搬 BQLog 的 `align_4`，也不要引入 fmt 的 formatter/custom-value 扩展或动态容器。

## H. 2026-09-09 文档交付历史记录

最新状态以 [2026-09-10 修复与验证报告](./I1_HASH_ENCODER_VALIDATION_20260910_CHS.md) 和本文第 0 节为准。

- 已完成：2026-09-09 hash 修改的只读复查、合并指南、文件边界/policy/DecodedArg 方案具体化、计划与决策文档同步。
- 待维护者完成：A 章剩余 hash 修补，以及 B/C/D/E 的生产类型、算法和生产构建接线。
- 待 Codex 在生产交接后执行：测试与验证支持代码、代码门禁、组合基准/汇编、评审报告。
- 当前没有新增生产实现，没有运行代码测试，也没有新增性能通过声明。

你的下一动作：先按 A 章复查清单补齐 hash 的剩余问题，再按 B 的类型定义顺序开始
`record_types.hpp`；之后接 C/D。每一步都在这份指南内，不需要为每个 helper 再商讨一轮。
