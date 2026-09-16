# QLog I1-B 从当前骨架开始的动手指南

> 当前后续入口（2026-09-13）：I1 已按 WSL2 开发范围收口，I2 设计冻结。后续执行 [I2 计划](./MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md) 和 [I2 动手指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)，本文旧任务/行号仅用于 I1 实现历史。

2026-09-10：本指南的 hash 正确性基线已完成，见 [2026-09-10 修复与验证报告](./I1_HASH_ENCODER_VALIDATION_20260910_CHS.md)。正文保留最初教学起点，当前继续 I1CD 的 decoder。

日期：2026-09-08。承接 I1 规范第 2.1 节；这是第 1 轮指导的改写，不增加教学轮次。
维护者写生产实现。本文件的可编译骨架用于解释类型连接，其余算法用精确步骤交给维护者完成。

2026-09-09 衔接更新：后续第 2、3 轮已合并为
[I1-C 编解码与 I1 收口动手指南](./MILESTONE2_I1CD_HANDS_ON_GUIDE_CHS.md)。最新 hash 修改的只读复查与
剩余补齐清单见新指南 A 章；本文下面的 45 行草稿/空文件等起点描述是 2026-09-08 历史快照。
本文所有创建 tests 文件、测试工厂、测试 CMake 接线、运行命令与性能验证步骤，均由 Codex 在生产交接后负责；
维护者只完成生产实现及生产构建接线，不需要照本文编写测试。本次仅更新文档，未执行这些验证。

## 0. 2026-09-08 历史起点与完成顺序

当前 `include/qlog/detail/format_hash.hpp` 有 45 行接口草稿；`src/format_hash_reference.hpp`
只有字面量函数声明；`src/format_hash_core.hpp` 和三个 `.cpp` 为空。现有 measure 不修改。

顺序固定为：A 修到头文件能编译 -> B reference 和固定向量 -> C software hash/copy ->
D software dispatch 和 checked 接口 -> E x86 hardware -> F 完整测试与 codegen。
字母是同一轮内的工作顺序，不要求每步回来确认。

首先修复当前五个问题：

1. `RawCopyHashFn` 必须有 source、可写 destination、size 三个参数。
2. `HashResult` 先定义，再声明以它为返回值的 checked 函数。
3. `std::optional<>` 改为 `std::optional<std::uint64_t>`。
4. `HashResult` 类末尾补分号。
5. private 默认构造不能建立有效对象；result 用成功/失败构造，dispatch 用完整参数构造和工厂。

### 文件位置索引（行号基于本次读取，修改后以类名/函数名为准）

QLog 根目录为 `/home/qq344/QLog`。以下行号指修改前的当前代码：

| 文件与位置 | 操作 | 原因 |
|---|---|---|
| `include/qlog/detail/format_hash.hpp:10` | 修改RawCopyHashFn为source、可写destination、size | copy必须同时取得两个范围的起点 |
| 同文件`:12-19` FormatHashDispatch | 移到完整HashResult定义后；替换private默认构造，加入工厂/访问器/backend | 只有完整入口组合才能构造可调用dispatch |
| 同文件`:22-30` checked声明 | 移到两个完整类之后，保持签名 | 解决HashResult使用前未声明；代码阅读按类型到操作排序 |
| 同文件`:32-36` HashError | 移到函数指针别名后 | result保存此类型，必须先定义 |
| 同文件`:38-45` HashResult | 按A补完整，末尾`;`，optional明确uint64 | 建立有值/错误互斥的逻辑状态和合法构造通路 |
| `src/format_hash_reference.hpp:7-8` | 整个文件移到include/qlog/detail；原模板声明最终替换为带函数体的定义 | 常量求值需要调用处可见函数体，不应从installed/detail头反向include src |
| `src/format_hash_core.hpp` 空文件 | includes -> namespace hash_impl -> raw声明 -> consume_word -> hash_core | 非模板raw入口在外层；编译期Ops/Copy的共用实现留在头内 |
| `src/format_hash_software.cpp` 空文件 | includes -> namespace hash_impl -> 匿名namespace内表生成/表/SoftwareOps -> 两raw定义 | 表和Ops只供本TU使用；raw定义在命名namespace，供dispatch链接 |
| `src/format_hash.cpp` 空文件 | includes/条件cpuid include -> hash_impl能力检测 -> detail中的automatic -> 两checked定义 | 冷路径选择与checked入口集中；不把平台检测放进算法循环 |
| `src/format_hash_x86_crc32c.cpp` 空文件 | includes -> namespace hash_impl -> 匿名namespace内HardwareOps -> 两raw定义 | ISA相关代码全部留在硬件TU，保持其余目标可安全回退 |
| 根`CMakeLists.txt`的现有add_library(qlog STATIC ...)块之后 | 加F给出的sources与局部ISA配置 | qlog target已存在，target_sources才有合法目标 |
| `tests/CMakeLists.txt`现有include(GoogleTest)之前 | 加F的测试target定义 | 与已有argument_model_test同样组织 |
| 同文件最后一个gtest_discover_tests(...)之后 | 加新测试发现块 | 在GoogleTest函数已经加载后调用 |

新文件每个函数具体位置按如下顺序固定，避免先写调用者再猜被调用者：

```text
include/qlog/detail/format_hash_reference.hpp
  includes
  namespace qlog::detail::hash_reference
    1. crc_byte_ref
    2. byte_value<Byte>
    3. crc_bytes_ref<Byte>
    4. hash_raw_ref<Byte>
  关闭hash_reference
  namespace qlog::detail
    5. hash_literal_stored<CharT,N>

src/format_hash_core.hpp -- namespace qlog::detail::hash_impl
  1. 四个raw backend声明和x86_crc32c_available声明
  2. consume_word<Width,Copy,Ops>定义
  3. hash_core<Copy,Ops>定义

src/format_hash_software.cpp -- namespace qlog::detail::hash_impl
  匿名namespace:
    1. make_crc_table() constexpr返回std::array<uint32_t,256>
    2. inline constexpr auto kCrcTable = make_crc_table()
    3. struct SoftwareOps { static u8; static u16; static u32; static u64; }
  关闭匿名namespace:
    4. software_hash_raw
    5. software_copy_raw
```

helper与表放匿名namespace是为了限制链接可见性，不是自动提速。两个raw wrapper不能也放匿名namespace，
否则其他.cpp里的同名声明找不到可链接定义。模板放头文件是为了实例化时看见函数体，也不保证一定内联。

## A. 第一次动手：只整理 format_hash.hpp

下面是完整的接口骨架，可以独立通过 syntax-only，但尚不能链接调用尚未定义的函数。
先理解每个成员，再在自己的文件中完成同等结构；不需要改 record_measure.hpp。

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

namespace qlog::detail {

using RawHashFn = std::uint64_t (*)(const std::byte*, std::size_t) noexcept;
using RawCopyHashFn = std::uint64_t (*)(
    const std::byte*, std::byte*, std::size_t) noexcept;

enum class HashError : std::uint8_t {
    invalid_source_metadata,
    invalid_destination_metadata,
    destination_too_small,
};

enum class HashBackend : std::uint8_t { software, x86_crc32c };

[[nodiscard]] constexpr std::uint64_t stored_hash_from_raw(std::uint64_t raw) noexcept {
    return raw == 0U ? 1U : raw;
}

class FormatHashDispatch;

class HashResult final {
public:
    [[nodiscard]] bool succeeded() const noexcept { return stored_hash_.has_value(); }
    [[nodiscard]] const std::uint64_t* stored_hash() const noexcept {
        return stored_hash_ ? &*stored_hash_ : nullptr;
    }
    [[nodiscard]] const HashError* failure() const noexcept {
        return stored_hash_ ? nullptr : &error_;
    }
private:
    // Only checked friends call this, after raw -> stored normalization.
    explicit HashResult(std::uint64_t stored) noexcept : stored_hash_(stored), error_{} {}
    explicit HashResult(HashError error) noexcept : stored_hash_(std::nullopt), error_(error) {}

    std::optional<std::uint64_t> stored_hash_;
    HashError error_{};

    friend HashResult hash_format_stored(
        const FormatHashDispatch&, const std::byte*, std::size_t) noexcept;
    friend HashResult copy_and_hash_format_stored(
        const FormatHashDispatch&, const std::byte*, std::size_t,
        std::byte*, std::size_t) noexcept;
};

struct FormatHashTestAccess;

class FormatHashDispatch final {
public:
    [[nodiscard]] static FormatHashDispatch automatic() noexcept;
    [[nodiscard]] HashBackend backend() const noexcept { return backend_; }

    // Internal unchecked operations: checked wrappers and a preflighted Encoder only.
    [[nodiscard]] std::uint64_t hash_raw_unchecked(
        const std::byte* source, std::size_t size) const noexcept {
        return raw_hash_(source, size);
    }
    [[nodiscard]] std::uint64_t copy_raw_unchecked(
        const std::byte* source, std::byte* destination, std::size_t size) const noexcept {
        return raw_copy_hash_(source, destination, size);
    }
private:
    FormatHashDispatch(RawHashFn hash, RawCopyHashFn copy, HashBackend backend) noexcept
        : raw_hash_(hash), raw_copy_hash_(copy), backend_(backend) {}

    RawHashFn raw_hash_;
    RawCopyHashFn raw_copy_hash_;
    HashBackend backend_;
    friend struct FormatHashTestAccess;
};

[[nodiscard]] HashResult hash_format_stored(
    const FormatHashDispatch&, const std::byte*, std::size_t) noexcept;
[[nodiscard]] HashResult copy_and_hash_format_stored(
    const FormatHashDispatch&, const std::byte*, std::size_t,
    std::byte*, std::size_t) noexcept;

} // namespace qlog::detail
```

解释：函数指针的参数名不参与类型；目标必须是 `std::byte*`。两种 private result 构造通过参数类型
区分成功/失败；success 的备用 error 仍初始化，避免复制未初始化标量。optional 不使用动态分配。
访问器返回成员指针，不能保存到 result 销毁之后。dispatch 构造一次收齐两个有效入口，无 setter，
发布后按只读对象使用；不需要把每个成员声明为 const。unchecked 方法在 detail 内，不能作为 standalone
安全入口；后续 Encoder 完成等价 preflight 后才可调用。

创建 `tests/format_hash_self_contained.cpp`：

```cpp
#include "qlog/detail/format_hash.hpp"
#include <type_traits>
static_assert(!std::is_default_constructible_v<qlog::detail::HashResult>);
static_assert(!std::is_default_constructible_v<qlog::detail::FormatHashDispatch>);
static_assert(!std::is_aggregate_v<qlog::detail::FormatHashDispatch>);
```

在 `/home/qq344/QLog` 下运行：

```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Iinclude -fsyntax-only tests/format_hash_self_contained.cpp
```

此时不要调用 automatic。只有声明即可完成编译检查；使用未定义函数会在链接时失败。

## B. reference：从一个字节开始，完成独立正确性依据

把现有 `src/format_hash_reference.hpp` 移到 `include/qlog/detail/format_hash_reference.hpp`，
不要保留两份定义。constexpr 模板定义必须对使用它的 TU 可见；未来字面量使用者不能依赖 src 私有路径。
暂时由 reference test 直接 include 这个头；写完后再由 format_hash.hpp include 同目录 reference 头。

头直接包含 `<bit>`、`<cstddef>`、`<cstdint>`、`<type_traits>`。内部 helper 放
`qlog::detail::hash_reference`。按下面顺序实现：

### B1. crc_byte_ref

签名：`constexpr uint32_t crc_byte_ref(uint32_t state, uint8_t byte) noexcept`。

1. 将 byte 扩宽为 uint32，然后 `state ^= byte`。
2. 循环恰好八次。先保存 `(state & 1U) != 0U`，再执行 `state >>= 1U`。
3. 若原最低位为 1，`state ^= 0x82F63B78U`。
4. 返回 state。这里不固定 seed，不做最终取反。

示例：state=0、byte=1；第一次低位为1，移位后为0，再异或多项式。
完成八次得到 `0xF26B8303`。把循环执行顺序写反会使用错误的最低位。

最先加入：

```cpp
static_assert(crc_byte_ref(0U, 0U) == 0U);
static_assert(crc_byte_ref(0U, 1U) == 0xF26B8303U);
```

### B2. 读取不同输入类型的一字节

`template<class Byte> constexpr uint8_t byte_value(Byte value) noexcept`：

- `std::byte` 用 `std::to_integer<uint8_t>(value)`。
- char/char8_t 转为 unsigned char，再转为 uint8_t。
- 对其他类型 static_assert 拒绝。使用 if constexpr，不能让 std::byte 编译字符转换分支。

### B3. 更新一个小范围

```cpp
template<class Byte>
constexpr std::uint32_t crc_bytes_ref(
    std::uint32_t state, const Byte* data,
    std::size_t offset, std::size_t count) noexcept;
```

函数体只循环 `i=0..count-1`，执行
`state = crc_byte_ref(state, byte_value(data[offset+i]))`，返回 state。
范围由上一层证明，count 在算法里只取 1/2/4/8；count=0 不读取或形成派生指针。
reference 不需要先拼 uint64，也不需要 memcpy。这让它独立于生产的宽读取实现。

### B4. hash_raw_ref 的精确函数体安排

```cpp
template<class Byte>
constexpr std::uint64_t hash_raw_ref(const Byte* data, std::size_t size) noexcept;
```

这是内部 reference raw 入口：非零 size 对应真实可读范围，零长度允许空指针。

1. size==0 立即返回0，不做指针运算。
2. 声明 h1/h2/h3/h4，全部 uint32，初始 `0xFFFFFFFFU`。
3. 按下面分支表更新。记 `C(h,o,w)=crc_bytes_ref(h,data,o,w)`，n=size：

| n | 精确更新 |
|---|---|
| 1..3 | offset=0；若 n&2，h1=C(h1 xor n,0,2)，offset=2；若 n&1，h2=C(h2 xor n,offset,1) |
| 4..7 | h1=C(h1,0,4)；h2=C(h2 xor n,n-4,4) |
| 8..15 | h1=C(h1,0,8)；h2=C(h2 xor n,n-8,8) |
| 16..31 | h1=C(h1,0,8)；h2=C(h2,8,8)；h3=C(h3 xor n,n-16,8)；h4=C(h4 xor n,n-8,8) |
| >=32 | 见下面完整块循环 |

只有 n<32 的分支才将 n 安全转成 uint32 用于 xor；>=32 的路径不混入长度。

完整块循环写成以下逻辑（是伪代码，不是新增递归）：

```text
offset = 0
while offset <= size - 32:
    h1 = C(h1, offset,      8)
    h2 = C(h2, offset + 8,  8)
    h3 = C(h3, offset + 16, 8)
    h4 = C(h4, offset + 24, 8)
    offset += 32

if offset != size:
    tail = size - 32
    按 tail、tail+8、tail+16、tail+24 再更新四个 lane
```

只在已经证明 size>=32 的分支中计算 size-32。33B 的窗口是 [0,32)、[1,33)；
64B 只有 [0,32)、[32,64)，不能再重复尾窗。这里的重复尾字节属于算法身份。

4. 分支结束统一折叠：

```text
uint32 low  = h1 xor std::rotl(h3,17)
uint32 high = h2 xor std::rotl(h4,19)
return (uint64(high) << 32) | uint64(low)
```

一定先扩宽 high 再移32位。不要在32位数上直接移32位。

### B5. 现有 hash_literal_stored 声明改为定义

入口保留在 `qlog::detail`，只允许 char/char8_t。它是内部已知字面量入口，前置条件是
最后一个元素为 NUL；不是任意数组的 checked format 工厂。一般数组 metadata 检查仍由 format/measure 边界负责。
函数体取 `N-1` 个字节调用 hash_raw_ref，然后 `raw==0 ? 1 : raw`；不要使用 strlen，不要 reinterpret_cast。
为避免循环 include，reference 头自行写这一小表达式，不 include format_hash.hpp。

加入 `tests/format_hash_reference_self_contained.cpp`，reference 头作为首个 include，使用全限定名 static_assert：

```cpp
static_assert(qlog::detail::hash_literal_stored("") == 1ULL);
static_assert(qlog::detail::hash_literal_stored("a") == 0x33bbc03300000000ULL);
static_assert(qlog::detail::hash_literal_stored("abc") == 0x33bbc033d64581afULL);
static_assert(qlog::detail::hash_literal_stored(u8"abc") == 0x33bbc033d64581afULL);
```

再用同样的 syntax-only 命令检查这个文件。运行时测试还需覆盖规范中的全部8个known vectors，
包括非字符串 byte 数组；不要只测试短字符串。失败时先查对应长度分支，不准改变 expected。

## C. software：宽读取、融合复制、查表 primitive

reference 全部向量通过后，才进入现有的 src/format_hash_core.hpp。

在 `qlog::detail::hash_impl` 中声明四个非模板 raw backend（source 在前）：

```cpp
std::uint64_t software_hash_raw(const std::byte*, std::size_t) noexcept;
std::uint64_t software_copy_raw(const std::byte*, std::byte*, std::size_t) noexcept;
std::uint64_t x86_hash_raw(const std::byte*, std::size_t) noexcept;
std::uint64_t x86_copy_raw(const std::byte*, std::byte*, std::size_t) noexcept;
bool x86_crc32c_available() noexcept;
```

实现两个模板 helper（直接定义在 core 头，不依赖 reference）：

```cpp
template<std::size_t Width, bool Copy, class Ops>
std::uint32_t consume_word(std::uint32_t state, const std::byte* source,
                          std::byte* destination, std::size_t offset) noexcept;

template<bool Copy, class Ops>
std::uint64_t hash_core(const std::byte* source,
                       std::byte* destination, std::size_t size) noexcept;
```

`consume_word` 的函数体顺序：

1. static_assert Width 是1/2/4/8，按 Width 选择 uint8/16/32/64 局部 value。
2. `memcpy(&value, source + offset, Width)`，只读取该宽度。
3. `if constexpr(Copy)` 中执行 `memcpy(destination + offset, &value, Width)`。
4. 用 if constexpr 按 Width 调用 `Ops::u8/u16/u32/u64(state,value)`，返回新 CRC。

源只读取一次，局部 value 同时服务 copy 与 hash。固定宽度 memcpy 允许安全处理未对齐地址；
编译器是否生成直接指令由 codegen 检查。不要改为解引用 reinterpret_cast 的整数指针。

`hash_core` 使用 B4 相同算法规则，把 C 操作换成 consume_word。两个实现遵守同一规范，
但 reference 保持独立的字节循环，不调用 hash_core。所有 destination+offset 运算都在 Copy 分支内。
空输入先返回0。hash-only wrapper 传 nullptr 作为 destination 不会产生空指针算术。

在 software.cpp 内定义 SoftwareOps，静态 noexcept 方法 u8/u16/u32/u64。

- 用 constexpr 生成256项 uint32 表：每项从索引 i 开始，按 B1 的移位/多项式规则迭代8次。
- u8：`(state >> 8) ^ table[(state ^ value) & 0xFF]`。
- u16：调用两次 u8，分别处理 value 的低字节与高字节。
- u32/u64：同理按低字节到高字节循环4/8次；位移前后的整数宽度要明确。
- 表为不可变 constexpr 对象，不在第一次调用时动态生成。

两个 wrapper 的函数体各只需返回对应模板实例：

```text
software_hash_raw(source,size)       -> hash_core<false,SoftwareOps>(source,nullptr,size)
software_copy_raw(source,dest,size)  -> hash_core<true,SoftwareOps>(source,dest,size)
```

Ops 是编译期类型；不允许每个字节通过运行时函数指针选择 u8。dispatch 只发生在整次 hash 边界。

## D. 先跑通 software 的 dispatch 与 checked 接口

format_hash.cpp include detail/format_hash.hpp 和同目录 format_hash_core.hpp。
第一版 automatic() 先返回绑定 software_hash_raw/software_copy_raw 的有效 dispatch。
这是过程中的 software 基线，E完成后补硬件选择；不要先写一个空dispatch或让调用返回伪造成功。

hash_format_stored 的函数体四步：

1. source_size!=0 且 source为空，返回 HashResult{HashError::invalid_source_metadata}。
2. 调用 dispatch.hash_raw_unchecked(source,source_size)。
3. stored_hash_from_raw(raw)。
4. 返回 HashResult{stored}。两个checked函数是friend，可以调用private构造。

copy_and_hash_format_stored 的函数体六步：

1. 同样的 source 检查。
2. destination_capacity!=0 且 destination为空，返回 invalid_destination_metadata。
3. destination_capacity<source_size，返回 destination_too_small。
4. 调用 dispatch.copy_raw_unchecked(source,destination,source_size)。
5. raw转stored。
6. 返回成功结果。

步骤1..3完全不写目标；步骤4开始以后不再返回可恢复错误。size/capacity代表各自声明的范围，
因此(null,capacity=0)可以表达空目标；非空源配空目标会在容量检查失败。空源但目标null且capacity>0，
属于目标metadata错误。非空地址的真实有效性及源目标不重叠仍是调用方前置条件。

三项最小行为检查：空输入stored=1；3B copy后bytes==abc且尾部不变；3B源配2B容量失败且整个目标不变。

## E. 最后接 x86 硬件，不重新实现另一套分块

hardware.cpp include `<nmmintrin.h>` 和 core 头，定义 HardwareOps 的同名静态方法：

| 方法 | 内部操作 |
|---|---|
| u8 | _mm_crc32_u8(state,value) |
| u16 | _mm_crc32_u16(state,value) |
| u32 | _mm_crc32_u32(state,value) |
| u64 | uint32_t(_mm_crc32_u64(state,value)) |

仍然导出两个 wrapper，分别实例化 hash_core<false/true,HardwareOps>。
不要在软件TU实例化硬件Ops。硬件只换更新primitive，不改变seed/分块/tail/fold。

构建时用 QLOG_HAS_X86_CRC32C=0/1 表示硬件实现是否编入；只在当前Linux x86-64、GCC/Clang
配置满足时加入硬件源，并只给它设置 -msse4.2。其他源文件保持基线ISA，软件路径总是编入。

在 format_hash.cpp 定义 x86_crc32c_available：

1. 未编入硬件实现时直接false，且不引用硬件符号。
2. 编入时使用 `<cpuid.h>` 的 __get_cpuid(1,&eax,&ebx,&ecx,&edx)。
3. 查询失败false；否则 `(ecx & bit_SSE4_2) != 0U`。

automatic() 改为：编入且支持时绑定两个x86入口，否则绑定两个software入口。
只读dispatch执行时不再检测CPU。不要在每次hash里调用CPUID。

测试用 FormatHashTestAccess 定义在 `tests/format_hash_test_access.hpp`，利用已声明的friend构造
强制software和强制hardware对象。强制hardware必须先调用相同能力检查，失败返回
backend_unavailable且不产生dispatch；不能通过测试绕过CPUID直接执行硬件。

测试工厂可以沿用HashResult模式定义 HashDispatchResult：optional<FormatHashDispatch> 加已初始化的
HashDispatchError，成功/失败private构造、dispatch()/failure()返回指针；唯一失败值backend_unavailable。
friend FormatHashTestAccess。成功构造用先构造完整dispatch再交给optional，不能让optional::emplace
直接调用FormatHashDispatch的private构造。

## F. 构建接线与验收

software.cpp和format_hash.cpp实现完成后，加入现有qlog target；不要创建第二个公共library。
在根CMakeLists.txt的add_library(qlog ...)之后增加target_sources即可。
硬件条件分支使用源文件COMPILE_OPTIONS属性，仅硬件文件-msse4.2；基线分支将宏设为0。

具体插入到根CMakeLists.txt当前add_library(qlog STATIC ... )结束的`)`之后、
`add_library(QLog::qlog ALIAS qlog)`之前；到E完成才加入下面包含hardware的完整配置：

```cmake
target_sources(qlog PRIVATE src/format_hash.cpp src/format_hash_software.cpp)
set(QLOG_HASH_X86_VALUE 0)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$"
   AND CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    set(QLOG_HASH_X86_VALUE 1)
    target_sources(qlog PRIVATE src/format_hash_x86_crc32c.cpp)
    set_source_files_properties(src/format_hash_x86_crc32c.cpp
        PROPERTIES COMPILE_OPTIONS "-msse4.2")
endif()
target_compile_definitions(qlog PRIVATE QLOG_HAS_X86_CRC32C=${QLOG_HASH_X86_VALUE})
```

D阶段先仅加入两个software/dispatch源并将宏定义为0；E完成后替换成上述完整配置。
不要在同一target上同时保留宏=0和宏=1两条定义。在format_hash.cpp的include区用
`#if QLOG_HAS_X86_CRC32C`保护`<cpuid.h>`；automatic函数中的硬件符号引用也用相同宏保护。
宏为0时，声明可以存在，但不能形成未链接硬件函数的地址。

新增测试target的接线模式：

```cmake
add_executable(qlog_format_hash_test
    format_hash_test.cpp
    format_hash_self_contained.cpp
    format_hash_reference_self_contained.cpp
)
target_link_libraries(qlog_format_hash_test PRIVATE QLog::qlog GTest::gtest_main)
target_include_directories(qlog_format_hash_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(qlog_format_hash_test PRIVATE QLOG_HAS_X86_CRC32C=${QLOG_HASH_X86_VALUE})
set_target_properties(qlog_format_hash_test PROPERTIES CXX_EXTENSIONS OFF)
qlog_enable_warnings(qlog_format_hash_test)
```

在已有include(GoogleTest)之后加：

```cmake
gtest_discover_tests(qlog_format_hash_test
    TEST_PREFIX "qlog."
    PROPERTIES LABELS "record_core;format_hash"
)
```

访问硬件raw符号的测试必须与编译宏一致：只给该test target同步PRIVATE编入状态宏，
或通过test access工厂取得有效dispatch后调用raw方法；不要引用未链接的硬件函数。

按先小后大的顺序测试：

1. 全部固定向量；constexpr char/char8_t与runtime等价。
2. software hash-only和copy-hash；字节内容、source不变、destination尾部不变。
3. 两checked函数全部错误与优先级；失败目标完全不变。
4. automatic、forced SW、可用HW；不可用只跳过HW capability case。
5. 长度0..128、255/256/257、8191/8192/8193，source/destination offset 0..31。
6. 前后canary、exact-size buffer与ASan/UBSan；canary不能单独证明没有越界读。

hash自身没有8192上限；上限由measure负责。reference/generated inputs与生产expected不能互相混淆：
固定向量expected手写，其余大矩阵可用独立reference做等价检查。

运行现有Debug/Release脚本；过滤执行带--no-tests=error。检查Release汇编：宽memcpy无意外外部调用、
Copy=false没有目标地址运算、runtime没有完整hash后再次memcpy、CRC检测只在工厂执行。
这一轮不根据汇编行数判断dispatch胜负；真实组合基准在合并第2、3轮的新指南中集中决定。

本轮完整交付目标：reference、四个raw backend、有效dispatch、两个checked接口，以及由 Codex 补充的测试。
当前下一步以新合并指南 A 章的 2026-09-09 复查清单为准，不再从旧 45 行头骨架重做；
生产实现完成和测试验收通过分别记录。
