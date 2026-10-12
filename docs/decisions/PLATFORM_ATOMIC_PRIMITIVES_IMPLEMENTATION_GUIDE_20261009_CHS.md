# 原子模块：BQLog 声明与内部结构严格对齐指导

> **2026-10-10 当前实施决定：** 三个实际头已各有参考70方法，原第一组/CAS与CR保留。GNU已写add_fetch逻辑保留并修正多余结束括号；原参数、默认值、memory order、宏与断言不增改。GNU减法builtin、MSVC add(-val)及既有算术边界继续对齐。新增常驻算术回归；两平台两配置实际15/15通过，算术9858项与实际参考一致，详见[最新实施边界](../validation/ATOMIC_FIRST_GROUP_REVIEW_CAS_GUIDANCE_20261010_CHS.md)。后续接运算符、普通atomic及第七个trait；本文件不追加详细实施正文。

> **2026-10-10 当前决定：** 本次已授权修正第一组/CAS实际源码并落实原后端文件名，保留CR、原结构、默认参数、memory order 与原断言。GNU CAS 的 RELEASE/ACQUIRE 文档合同边界和 MSVC 二次读取规则继续有效。后续实现指导仅在对话中给出，按“算术/位运算；运算符＋普通 atomic＋第七个 trait；普通自旋锁”三组接续，不再向本文件追加详细实现正文。
>
> **算术对齐决定：** GNU 减法使用参考自身的 sub builtins；MSVC 减法按参考委托 add(-val)，包括 raw 和固定序变体。无符号取负的 C4146 警告及 C++ 有符号表达式溢出边界显式记录，不擅自增加防御逻辑、断言或生产 warning pragma。实际修正验证与外部算术候选验证分开计数，见 [当前补齐记录](../validation/ATOMIC_FIRST_GROUP_REVIEW_CAS_GUIDANCE_20261010_CHS.md)。下方“实际失败/未填写”属于授权修正前快照。

> **2026-10-10 实际审查更新（本段最新）：** 已有两个后端正文，但 GNU/MSVC 实际头在 Debug/Release 都未编译通过，公共 atomic_trivially_constructible 尚未填写。先按第10节修正并用第9节闭合第一组，再按第11节接续 CAS。用户已确认保留 GNU release/非relaxed 失败序的原映射并记录 GCC 合同边界；MSVC 二次 load 与 strong 委托 weak 保留。新修正/补齐/CAS候选只完成两平台两配置编译，没有运行或生产验收。下方2026-10-09各结果为原阶段历史。见 [本轮实际审查](../validation/ATOMIC_FIRST_GROUP_REVIEW_CAS_GUIDANCE_20261010_CHS.md)。

> **文件命名最新确认：** 用户明确“ 不用沿用，该怎么对齐就怎么对齐 ”。本组后端固定为 _inner_atomic_GNU_C.h 与 _inner_atomic_msvc.h，对应参考原名；公共入口使用此前确定的 include/qlog/platform/atomic.hpp。核对末尾新增的 atomic_gnu.hpp/atomic_msvc.hpp 空草稿不作为指导路径，助手未搬移或覆盖它们。

> **最新补充：第9节提供当前第一组的完整三文件正文。** Linux GCC/原生Windows MSVC Debug/Release临时候选各637检查与独立BQLog参考一致；实际生产草稿未填写，完整原子模块仍后续接续。

日期：2026-10-09。本次修订以用户最新要求为准：**BQLog 有的默认参数和断言才保留；内部结构和命名严格对齐；默认 memory order 与对应声明完全一致；不擅自增加 assert 或类。** 新生产函数及未完成声明仍由用户填写，代码格式沿用 QLog 当前要求。

本版替代上一版的 AtomicTriviallyConstructible、AtomicBase、atomic_backend、GnuAtomicType、MsvcAtomicOps 和单一 mutable 原生槽方案。它们不符合本次严格结构对齐要求，不再作为填写依据。上一版临时候选的 633 项检查及 20000 次受保护更新仅保留为旧候选历史，不能证明本版声明、union、原始存储或完整 atomic 接口通过验收。

## 1. 实际参考与修改边界

QLog：feat/spsc-ring-opt@f24f3afcd8e9418ea48ad5a6b05680c622008145，权威目录 /home/qq344/QLog。BQLog：78cbfbef4f87e7558335f0c04bf6dd6894b46451，参考目录 /home/qq344/BqLog。本次已重新检查两个工作树；都有既有修改。参考 atomic.h、_inner_atomic_GNU_C.h、_inner_atomic_msvc.h、platform/macros.h 和 types/type_traits.h 相对 HEAD 无差异，不能称 BQLog 整树干净。

QLog 已有 include/qlog/platform/atomic.hpp 的部分草稿，尚未完成类及后端。本次不填入新生产定义，不覆盖草稿或 CR，不更改 CMake。模板实现放头文件，不建立空 cpp；当前根库仍每平台 17 个 cpp。上一轮实际线程基础构建与调用方 CTest 各 12/12 的证据不因本次原子指导修订而撤销。

参考文件：

- [公共接口与两种原子类型](/home/qq344/BqLog/src/bq_common/platform/atomic/atomic.h:26)。
- [GNU 帮助函数、union 与后端](/home/qq344/BqLog/src/bq_common/platform/atomic/_inner_atomic_GNU_C.h:25)。
- [MSVC 宏及尺寸特化](/home/qq344/BqLog/src/bq_common/platform/atomic/_inner_atomic_msvc.h:26)。
- [编译器判断与强制内联宏](/home/qq344/BqLog/include/bq_common/platform/macros.h:69)。
- [参考 trait 的原始判断](/home/qq344/BqLog/include/bq_common/types/type_traits.h:374)。

## 2. 文件、命名和包含顺序

| QLog 目标 | BQLog 对应文件 | 职责 |
| --- | --- | --- |
| include/qlog/platform/atomic.hpp | src/bq_common/platform/atomic/atomic.h | memory_order、选择一个编译器后端、atomic_trivially_constructible、atomic、末尾参考静态断言 |
| include/qlog/platform/atomic/_inner_atomic_GNU_C.h | 同名参考后端 | 宽度映射、原始转换函数、_atomic_base<T, N> 的 GNU 实现 |
| include/qlog/platform/atomic/_inner_atomic_msvc.h | 同名参考后端 | 原始宏、Windows 原生类型映射、_atomic_base<T, N> 主模板及尺寸特化 |

保留 qlog::platform 命名空间；不增加 atomic_backend 或其他封装 namespace。类名必须是 atomic_trivially_constructible、atomic 和 _atomic_base；保留原来的两个模板参数 T、N、typedef 名称、成员名和 public/protected 归属。第一组不能另建仅三个方法的新包装类型作为最终接口。

必要适配仅是工程前缀、头文件路径以及现有标准库基础：bq::platform → qlog::platform；BQ_ → QLOG_；bq_forceinline → qlog_forceinline；bq::decay/is_pointer/enable_if_t 采用对应 std 基础，不为此新增另一套 trait 类。整数和 size_t 使用项目现有的 std:: 限定方式。原子的类、函数、参数、存储表示和分支不能借适配进行重新设计。std trait 适配并不将本接口扩展成任意对象版 std::atomic。

编译器探测沿 macros.h:69：先 __clang__，再 _MSC_VER，再 __GNUC__；分别得到 QLOG_CLANG/QLOG_MSVC/QLOG_GCC。atomic.hpp 随后沿 atomic.h:36 先测试 QLOG_MSVC，再测试 QLOG_CLANG || QLOG_GCC，选择一个后端。clang-cl 因前一步归入 Clang，不能仅凭 _MSC_VER 误选 MSVC 后端；也不能按 WIN32 选后端。

qlog_forceinline 的 GNU/Clang 定义沿参考的 __inline__ __attribute__((always_inline))，MSVC 定义为 __forceinline，其他编译器宏本身沿参考回退 inline；atomic 后端选择的 else 保留原来的 static_assert(false, ...) 未支持编译器诊断，不改为新 #error。当前草稿的 __always_inline 应按参考改为 always_inline，未完成草稿由用户继续填写。

先写宏基础和 memory_order，然后关闭 namespace，包含所选后端，再重新打开 qlog::platform 定义公共类型。后端自身打开同一 namespace。不能在已经打开的 namespace 内包含定义 namespace 的后端，造成重复嵌套。后端仅通过 atomic.hpp 使用。MSVC 宏名称沿参考，仅替换 BQ/QLOG 前缀；_INTRIN_BIT_1/2/4/8、_INTRIN_BIT_SUFFIX 和 SPECIALIZED_ATOMIC_BASE 原名保留。不额外加入参考没有的 unsupported-architecture 分支、默认回退或统一宏清理逻辑。

## 3. 类型结构与成员不变量

公共类型的继承关系严格保持：

```cpp
template<typename T>
class atomic_trivially_constructible:public _atomic_base<T,sizeof(T)>{
public:
    typedef _atomic_base<T,sizeof(T)> base_type;
    static_assert(base_type::supported,"atomic type is not supported on this platform!");
    typedef typename std::decay<T>::type value_type;
    // 以下依原公共接口顺序填写转发方法及运算符。
};

template<typename T>
class atomic:public atomic_trivially_constructible<T>{
public:
    typedef typename atomic_trivially_constructible<T>::base_type base_type;
    typedef typename atomic_trivially_constructible<T>::value_type value_type;
    typedef atomic_trivially_constructible<T> trivially_constructible_type;
    // 按参考填写构造、赋值和运算符，不能额外添加状态或包装类。
};
```

此处只展示成员归属；后面的声明清单才是完整接口依据，不能把注释骨架当作已完成实现。

GNU 后端沿 _inner_atomic_GNU_C.h:88 的实际结构：

```cpp
template<typename T,std::size_t N>
class alignas(8) _atomic_base{
public:
    static constexpr bool supported=true;
protected:
    static_assert(sizeof(T)==N,
        "please don't use qlog::platform::_atomic_base, use qlog::platform::atomic instead.");
    static_assert(N==1||N==2||N==4||N==8,
        "please don't use qlog::platform::_atomic_base, use qlog::platform::atomic instead.");
    typedef typename std::decay<T>::type value_type;
    typedef typename _atomic_gnu_c_standard_type<sizeof(T)>::ptr_type api_ptr_type;
    typedef typename _atomic_gnu_c_standard_type<sizeof(T)>::value_type api_type;
    union{
        alignas(8) value_type value_;
        alignas(8) api_type value_standard;
    };
    // explicit_copy 及所有后端方法依参考放在 protected。
};
```

两个 union 成员名分别是 value_ 和 value_standard，后者没有末尾下划线；不改成 value_standard_，不加 mutable、初值或构造函数。保留 api_ptr_type，即使第一组暂未直接用到它。

MSVC 后端保留 alignas(8) 的 _atomic_base<T, N> 主模板；supported=false 放 public，value_type、alignas(8) value_type value_ 和 explicit_copy 放 protected。主模板的 explicit_copy 是 value_=val。SPECIALIZED_ATOMIC_BASE(SIZE_BYTE) 在原位置生成四个 _atomic_base<T, SIZE_BYTE> 特化：supported=true 放 public，其余参考断言、value_type、api_type、alignas(8) value_type value_ 和方法均放 protected。参考特化的 class 行没有另写 alignas(8)，成员保证其对齐；不要把主模板与特化的具体声明混为一套。

atomic_trivially_constructible 不写用户提供的构造、析构、初始化赋值或成员初值，不删除原来的平凡 copy/move。普通 atomic 则沿参考以 explicit_copy 处理默认构造、值构造、复制构造，并以 store_seq_cst 处理赋值。不能给参考无 explicit/noexcept 的构造或运算符额外加修饰。

静态存储对象零初始化；无 {} 的自动对象不能假定初始值为零。raw 方法和隐式平凡复制不是并发原子操作，仅能在不存在竞争访问时使用。8 字节对齐不是缓存行隔离；不增加 padding 字段或 lock-free 断言。参考支持的尺寸判断不是任意类型合法性的完整证明。

## 4. 默认参数、参数名及 cv/noexcept 合同

| 接口族 | 公共声明 | GNU/MSVC 对应后端声明 |
| --- | --- | --- |
| load | load(memory_order order = memory_order::seq_cst) const noexcept | 两后端都保留同一默认值、const、noexcept |
| store | store(value_type value, memory_order order) noexcept | store(value_type val, memory_order order) noexcept；两层均没有默认 order |
| exchange | exchange(value_type value, memory_order order) noexcept | exchange(value_type val, memory_order order) noexcept；两层均没有默认 order |
| compare_exchange_weak/strong | expected 引用、desired 值，const memory_order success_order = seq_cst，const memory_order fail_order = seq_cst；noexcept | 两后端同公共声明，两个默认值都保留 |
| 十个算术/位运算入口 | value_type val, memory_order order；noexcept | 对应后端相同；没有默认 order |
| _raw 与固定序变体 | 原声明没有 order 参数 | 同样不新加 order 或默认参数 |

不能因为公共 load 已经有默认值就删后端的默认值；MSVC compare_exchange_weak 内部还调用 load(fail_order)，整个后端声明族也必须完整核对。不能按常见 std::atomic 用法给 store/exchange 补 seq_cst 默认值。

公共 store/exchange 的 value 与后端一般使用的 val 是 BQLog 原始命名，不应强行统一。特别地，GNU exchange_raw 的参数名是 value；MSVC exchange_raw 的参数名是 val。explicit_copy 两后端均是 const value_type& val。其余 raw、固定序、算术和运算符参数逐个照附录清单，不靠批量猜测替换。

load 固定序变体只有 raw/acquire/relaxed/acq_rel/seq_cst；没有 load_release。store 固定序变体只有 raw/release/relaxed/acq_rel/seq_cst；没有 store_acquire。exchange 与十个算术/位运算族具有 raw/acquire/release/relaxed/acq_rel/seq_cst。不为整齐补出参考不存在的方法。

## 5. 当前第一组的完整填写顺序

本组仍先闭合 load/store/exchange，但同时保留它们各自已有的 raw 和固定序变体。三层分别完成，不能把 GNU 和 MSVC 算法合成一个新后端；未实现 CAS/算术等不写空函数体或伪回退，也不宣称整个原子模块已经完成。

**先 GNU 基础与原语，再 MSVC 基础与原语，最后公共转发。** 以下每一步的帮助函数都是实际参考中存在的函数，依赖需先填写后调用。

GNU：按 :25-86 定义 _atomic_gnu_c_standard_type<N> 主模板和 1/2/4/8 四个特化，分别映射 uint8_t/uint16_t/uint32_t/uint64_t 以及它们的指针类型。接着定义 _atomic_gnu_is_reinterpret_cast_enable<T>，value 按参考使用 is_pointer<T>。按原名填写 get_atomic_value 的指针/非指针 enable_if 两个重载：前者 reinterpret_cast，后者 static_cast；get_atomic_ptr(const T& value) 将 const_cast<T*>(&value) 再转为原生指针；最后填写 get_value_type_from_atomic_standard_value<To, AtomicStandardType> 两个重载。不得将这些实际结构改成 GnuAtomicType 或 convert_atomic_value。

GNU：把上述 _atomic_base 的存储与断言写完，explicit_copy(const value_type& val) 沿 :104，用 __atomic_store_n(&value_standard, get_atomic_value(val), __ATOMIC_SEQ_CST)，不添加 noexcept。然后按 :109-228 填以下函数。

| GNU 方法族 | 实际步骤与返回 |
| --- | --- |
| load(order=seq_cst) const noexcept | relaxed → __ATOMIC_RELAXED；acquire → __ATOMIC_ACQUIRE；default → __ATOMIC_SEQ_CST；调用 __atomic_load_n(&value_standard, 常量序)，以原帮助函数转回 value_type |
| load_raw | 直接返回 value_ |
| load_acquire / load_relaxed | 直接用对应常量的 __atomic_load_n |
| load_acq_rel / load_seq_cst | 都用 __ATOMIC_SEQ_CST，不能将 acq_rel 直接传给纯 load |
| store(val,order) noexcept | relaxed → RELAXED；release → RELEASE；default → SEQ_CST；调用 __atomic_store_n，值经 get_atomic_value(val)，无返回值 |
| store_raw | value_=val |
| store_release / store_relaxed | 用对应常量；store_acq_rel/store_seq_cst 均用 SEQ_CST |
| exchange(val,order) noexcept | relaxed/acquire/release/acq_rel 四分支分别用对应常量；default 用 SEQ_CST；__atomic_exchange_n 的旧值转换后返回 |
| exchange_raw(value) noexcept | old=value_；value_=value；返回 old，非原子 |
| exchange 五个固定序方法 | 原生 __atomic_exchange_n 使用对应常量，返回转换后的旧值 |

这些分支沿参考保留；不要为了把未知枚举视为错误增加 assert，也不要把已存在的 default 删除。GNU 这里允许某些枚举进入更强的 seq_cst 分支，是其实际实现规则。

MSVC：按 :26-92 保留主模板和原宏。x86/x64（_M_IX86 || (_M_X64 && !_M_ARM64EC)）的 relaxed/acquire/release 宏映射同一个原始 intrinsic；ARM/ARM64/ARM64EC 分别选 _nf/_acq/_rel；acq_rel 与 seq_cst 用原始 intrinsic。_INTRIN_BIT_1/2/4/8 分别形成 8、16、无后缀、64。QLOG_ATOMIC_CHOOSE_INTRINSIC 仍是五个 enum case，无 default。不能添加默认顺序回退或另造 MsvcAtomicOps。

MSVC：在 :94 的 SPECIALIZED_ATOMIC_BASE(SIZE_BYTE) 宏中按原序填写特化成员及 :105-220 的 explicit_copy/load/store/exchange 各变体。在宏之后沿 :625-663 写 _atomic_standard_windows_type<N> 的四个特化：char/short/long/__int64 及相应指针。随后写原来的 get_atomic_value(const T& value) C 风格值转换和 get_atomic_ptr(const T* value) 的 const_cast/reinterpret_cast 指针转换。这两个 MSVC 帮助函数在参考中没有 bq_forceinline，不能为整齐额外加内联修饰。最后 :665-672 先 push 原两项 warning 抑制，再展开 SPECIALIZED_ATOMIC_BASE(1/2/4/8)，再 pop。宏定义可以先出现，但不能在帮助函数及映射声明可见之前展开特化。

| MSVC 方法族 | 实际步骤与返回 |
| --- | --- |
| explicit_copy(const value_type& val) | SEQ_CST 宏调用对应宽度 _InterlockedExchange，(void)result；不是主模板的普通赋值 |
| load(order=seq_cst) const noexcept | api_type result；选择宏调用对应宽度 _InterlockedExchangeAdd，增量为 get_atomic_value<value_type>((value_type)0)；返回 (value_type)result |
| load_raw | 直接返回 value_，不走 intrinsic |
| load 固定序变体 | 使用对应参考宏调用同一 ExchangeAdd(0)；不改成普通 load 指令 |
| store(val,order) noexcept | 选择宏调用对应宽度 _InterlockedExchange，(void)result 丢弃旧值；不重写为新 Ops 层调用 |
| store_raw | value_=val |
| store 固定序变体 | 原序宏调用 Exchange，(void)result |
| exchange(val,order) noexcept | 选择宏调用 Exchange，返回 (value_type)result 旧值 |
| exchange_raw(val) noexcept | old=value_；value_=val；返回 old |
| exchange 固定序变体 | 对应宏调用 Exchange，返回旧值 |

MSVC result 的类型、未初始化声明、五 case 无 default 和原 warning 6001/4701 区域均按参考。不能为了消除警告给 result 补初值或添加断言。通过强转构造无效 memory_order 的路径未被参考定义为安全回退。32 位 API 类型使用 long，不能仅凭 sizeof 相同替换成 int*；const load 实际执行 RMW，也不能据此把存储声明擅自改为 mutable。

公共层：沿 atomic.h:46-150 填 public typedef/支持断言和 load/store/exchange 全部转发。load 默认值保留；store/exchange 的参数名用 value，固定序转发到同名 base_type 方法，返回与 void 形式逐项按原文件。公共层不存数据，不重写后端 order 分派，不添加构造或保护机制。

## 6. 后续接口族与仅保留的断言

第一组完成后按 atomic.h 的原次序接续：weak/strong CAS → add_fetch/fetch_add/sub_fetch/fetch_sub → xor_fetch/fetch_xor/or_fetch/fetch_or/and_fetch/fetch_and → 平凡类型运算符 → 普通 atomic 构造/赋值/运算符 → 文件末尾七个 trait 断言 → 实际自旋锁。后续详细函数体仍需逐组检查参考，不因本附录有声明就假定实现已提供。

运算符不能改返回合同：前置 ++/-- 和 +=/-= 返回对应 atomic 类型引用；后置 ++/-- 和 +/- 返回对应 atomic 对象，不返回 value_type。后置哑参数保持 int32_t；原构造、运算符没有默认参数的地方不加。普通 atomic 的值构造没有 explicit；原复制支持也不删成 std::atomic 的不可复制合同。

这三个原子头的运行时 assert 数量为 0。本次只保留以下已有的 static_assert，条件和出现位置严格对应参考：

| 原始位置 | 条件 |
| --- | --- |
| atomic.h:41 | 仅未支持编译器分支的 static_assert(false, ...) |
| atomic.h:51 | base_type::supported |
| GNU :94-95 | sizeof(T)==N；N 为 1/2/4/8 |
| MSVC :101 | 特化宏内 sizeof(T)==SIZE_BYTE，随四次原特化展开 |
| atomic.h:670-676 | 以下七个 trait 断言，原顺序不变 |

```cpp
static_assert(std::is_trivially_constructible<atomic_trivially_constructible<std::int32_t>>::value,
    "atomic_trivially_constructible must be trivial type");
static_assert(std::is_trivially_copy_assignable<atomic_trivially_constructible<std::int32_t>>::value,
    "atomic_trivially_constructible must be trivial type");
static_assert(std::is_trivially_destructible<atomic_trivially_constructible<std::int32_t>>::value,
    "atomic_trivially_constructible must be trivial type");
static_assert(std::is_trivially_copy_constructible<atomic_trivially_constructible<std::int32_t>>::value,
    "atomic_trivially_constructible must be trivial type");
static_assert(std::is_trivially_move_assignable<atomic_trivially_constructible<std::int32_t>>::value,
    "atomic_trivially_constructible must be trivial type");
static_assert(std::is_trivially_move_constructible<atomic_trivially_constructible<std::int32_t>>::value,
    "atomic_trivially_constructible must be trivial type");
static_assert(!std::is_trivially_constructible<atomic<std::int32_t>>::value,
    "atomic must not be trivially constructible");
```

不能加 is_standard_layout、is_trivially_copyable、is_integral、is_pointer 的类型限制断言、sizeof==8 断言、lock-free 断言或 order 合法性 assert；它们不是这三个参考头的声明合同。最后一个否定断言在普通 atomic 填写完成后才具备正确依赖；分组学习不能删除它或用新空包装类凑检查。

## 7. 参考边界与技术解释

下面是实际源码的边界记录，不授权在本组更换存储或新增修复机制：

- GNU union 同时提供 value_ 的 raw 路径与 value_standard 的原子路径。跨成员的 raw 读取涉及 GNU/Clang 实现及 union 的对象模型边界；不能把这套编译器接口描述为纯标准 C++ std::atomic 等价保证。此次严格结构对齐保留它，上一版单槽修正差异已经撤回。
- MSVC get_atomic_ptr 去掉 const 并转成 intrinsic 指针，const load 通过 ExchangeAdd(0) 写回同值。若整个原对象真实为 const，不能因为成员函数标为 const 就声称这条 RMW 路径具备标准 C++ 安全保证。保持参考结构，但不静默声称加入了 mutable 的修复。
- GNU :242-247 和 :281-286 中，success_order=release 且 fail_order 不为 relaxed，会把 failure order 传为 ACQUIRE。2026-10-10 已讨论：用户选择保留原映射并明确记录 GCC 合同边界，详见第11节；新候选仅编译。不能静默改成 relaxed 或新增 order assert。第9节第一组不含 CAS。
- MSVC :221-234 的 CAS 失败再次 load(fail_order) 更新 expected，strong 委托 weak。它与 GNU 直接由 CAS 写回 expected 的具体步骤不同；进入 CAS 时再解释其并发语义，不擅自统一。
- 平凡类型后置 ++/-- 在原子更新前有隐式复制，不保证并发下复制快照本身是原子操作；普通 atomic 的复制经 load_seq_cst。保持两个类型的差异，不将所有运算符都宣称为 std::atomic 等价操作。

面试解释：平凡构造不意味着每个对象自动清零；静态初始化与无初值自动对象是不同情形。load 接口也不保证硬件只读，MSVC 的 ExchangeAdd(0) 是 RMW。relaxed 保证该原子访问的原子性，不建立对其他内存的发布/获取同步；x86 同一 intrinsic 为多个 order 提供更强语义，不代表 ARM 的 _nf/_acq/_rel 可合并。exchange 返回旧值，自旋锁依据旧值判断是否获得锁。volatile 是部分 intrinsic 的参数类型要求，不能替代原子操作与同步关系。raw/平凡复制仍要避免数据竞争。

## 8. 本次检查与之后的验证

本次仅重新核对实际 Git/参考差异、抽取声明/断言清单、修订文档，未进行新生产原子编译、运行或并发验收。QLog 部分原子草稿仍未完成，不能记为原子已编译通过。旧候选的 633/20000 结果不转移为本版证据；实际线程基础已有证据保留。ARM/32 位、完整 CAS/算术/运算符并发、sanitizer 与性能继续未验。

用户填写后，先检查编译器选择、标识符、public/protected、字段、声明的 const/noexcept/默认参数，再做实际 GNU/MSVC 编译、调用方实例化与链接。调用方还应确认 load() 可省 order，而 store/exchange/十个算术入口不能省 order；CAS 的两个默认值需保持对应声明。行为验证使用实际参考头，不把相同尺寸/普通值结果当作结构相同的证据。

本次核对证据目录：D:/qq344/Documents/qlog-atomic-contract-correction-20261009-a42e8d。包含工作树/HEAD、三个参考文件 SHA-256、逐函数原始声明及断言、旧文档备份和生产文件前后 SHA-256。参见 [线程证据与本次修订记录](../validation/THREAD_OPS_REVIEW_ATOMIC_GUIDANCE_20261009_CHS.md)。

## 9. 当前第一组的完整三文件正文（2026-10-09 补充）

本节把上一节确认的第一组一次写完整：memory_order/编译器选择、两个后端实际存储与帮助函数、explicit_copy、load/store/exchange 的全部 raw 和固定序变体，以及公共转发与现阶段可放置的六个参考平凡断言。CAS、算术/位运算、运算符和普通 atomic 在同一个原类/宏中后续补齐；本节不是另立仅三个方法的最终接口。第七个否定断言等普通 atomic 实际填写完成后放回，不用空类凑检查。

下方是临时指导候选，没有写到 QLog 的生产目录。实际草稿及 CR 保留，由用户填写；三个文件只有模板，不增加 cpp 或根 CMake 源项。保留参考文件头版权说明。qlog_forceinline、编译器宏暂随原子公共头提供，逻辑与 BQLog 的宏基础一致；其他模块需要共用时再按实际依赖整理，不在本组新增包装类。

按依赖先写公共头宏/枚举，再 GNU 基础与原语、MSVC 基础与原语，最后回到公共类型转发。公共头在包含后端前关闭 namespace，后端都自行打开 qlog::platform；后端不直接给调用方包含。MSVC 保持先定义特化宏，再声明宽度映射和转换函数，最后展开四个特化的次序。

**公共头：include/qlog/platform/atomic.hpp**

```cpp
/* Copyright (C) 2025 Tencent.
 * BQLOG is licensed under the Apache License, Version 2.0.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#ifdef __clang__
#define QLOG_CLANG 1
#elif defined(_MSC_VER)
#define QLOG_MSVC 1
#elif defined(__GNUC__)
#define QLOG_GCC 1
#endif

#ifdef QLOG_MSVC
#define qlog_forceinline __forceinline
#elif defined(QLOG_GCC)||defined(QLOG_CLANG)
#define qlog_forceinline __inline__ __attribute__((always_inline))
#else
#define qlog_forceinline inline
#endif

namespace qlog::platform{
enum class memory_order : std::uint8_t {
    relaxed,
    acquire,
    release,
    acq_rel,
    seq_cst
};
}

#ifdef QLOG_MSVC
#include "qlog/platform/atomic/_inner_atomic_msvc.h"
#elif defined(QLOG_CLANG)||defined(QLOG_GCC)
#include "qlog/platform/atomic/_inner_atomic_GNU_C.h"
#else
static_assert(false,"qlog::platform::atomic is not supported in your compiler");
#endif

namespace qlog::platform{
template <typename T>
class atomic_trivially_constructible : public _atomic_base<T, sizeof(T)> {

public:
    typedef _atomic_base<T, sizeof(T)> base_type;
    static_assert(base_type::supported, "atomic type is not supported on this platform!");
    typedef typename std::decay<T>::type value_type;

    // load
    qlog_forceinline value_type load(memory_order order = memory_order::seq_cst) const noexcept{
        return base_type::load(order);
    }

    qlog_forceinline value_type load_raw() const noexcept{
        return base_type::load_raw();
    }

    qlog_forceinline value_type load_acquire() const noexcept{
        return base_type::load_acquire();
    }

    qlog_forceinline value_type load_relaxed() const noexcept{
        return base_type::load_relaxed();
    }

    qlog_forceinline value_type load_acq_rel() const noexcept{
        return base_type::load_acq_rel();
    }

    qlog_forceinline value_type load_seq_cst() const noexcept{
        return base_type::load_seq_cst();
    }

    // store
    qlog_forceinline void store(value_type value, memory_order order) noexcept{
        base_type::store(value, order);
    }

    qlog_forceinline void store_raw(value_type value) noexcept{
        base_type::store_raw(value);
    }

    qlog_forceinline void store_release(value_type value) noexcept{
        base_type::store_release(value);
    }

    qlog_forceinline void store_relaxed(value_type value) noexcept{
        base_type::store_relaxed(value);
    }

    qlog_forceinline void store_acq_rel(value_type value) noexcept{
        base_type::store_acq_rel(value);
    }

    qlog_forceinline void store_seq_cst(value_type value) noexcept{
        base_type::store_seq_cst(value);
    }

    // exchange
    qlog_forceinline value_type exchange(value_type value, memory_order order) noexcept{
        return base_type::exchange(value, order);
    }

    qlog_forceinline value_type exchange_raw(value_type value) noexcept{
        return base_type::exchange_raw(value);
    }

    qlog_forceinline value_type exchange_acquire(value_type value) noexcept{
        return base_type::exchange_acquire(value);
    }

    qlog_forceinline value_type exchange_release(value_type value) noexcept{
        return base_type::exchange_release(value);
    }

    qlog_forceinline value_type exchange_relaxed(value_type value) noexcept{
        return base_type::exchange_relaxed(value);
    }

    qlog_forceinline value_type exchange_acq_rel(value_type value) noexcept{
        return base_type::exchange_acq_rel(value);
    }

    qlog_forceinline value_type exchange_seq_cst(value_type value) noexcept{
        return base_type::exchange_seq_cst(value);
    }
};

static_assert(std::is_trivially_constructible<atomic_trivially_constructible<std::int32_t>>::value, "atomic_trivially_constructible must be trivial type");
static_assert(std::is_trivially_copy_assignable<atomic_trivially_constructible<std::int32_t>>::value, "atomic_trivially_constructible must be trivial type");
static_assert(std::is_trivially_destructible<atomic_trivially_constructible<std::int32_t>>::value, "atomic_trivially_constructible must be trivial type");
static_assert(std::is_trivially_copy_constructible<atomic_trivially_constructible<std::int32_t>>::value, "atomic_trivially_constructible must be trivial type");
static_assert(std::is_trivially_move_assignable<atomic_trivially_constructible<std::int32_t>>::value, "atomic_trivially_constructible must be trivial type");
static_assert(std::is_trivially_move_constructible<atomic_trivially_constructible<std::int32_t>>::value, "atomic_trivially_constructible must be trivial type");
}
```

**GNU 后端：include/qlog/platform/atomic/_inner_atomic_GNU_C.h**

```cpp
/* Copyright (C) 2025 Tencent.
 * BQLOG is licensed under the Apache License, Version 2.0.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */
#pragma once

#if defined(QLOG_GCC)||defined(QLOG_CLANG)
#include <type_traits>

namespace qlog::platform{
template <std::size_t N>
struct _atomic_gnu_c_standard_type {
};

template <>
struct _atomic_gnu_c_standard_type<1> {
    typedef std::uint8_t* ptr_type;
    typedef std::uint8_t value_type;
};

template <>
struct _atomic_gnu_c_standard_type<2> {
    typedef std::uint16_t* ptr_type; // std::int16_t is not compatible to VC++ type short in the intrinsic functions
    typedef std::uint16_t value_type;
};

template <>
struct _atomic_gnu_c_standard_type<4> {
    typedef std::uint32_t* ptr_type; // std::int32_t is not compatible to VC++ type short in the intrinsic functions
    typedef std::uint32_t value_type;
};

template <>
struct _atomic_gnu_c_standard_type<8> {
    typedef std::uint64_t* ptr_type; // std::int64_t is not compatible to VC++ type short in the intrinsic functions
    typedef std::uint64_t value_type;
};

template <typename T>
struct _atomic_gnu_is_reinterpret_cast_enable {
    static constexpr bool value = std::is_pointer<T>::value;
};

template <typename T>
qlog_forceinline std::enable_if_t<_atomic_gnu_is_reinterpret_cast_enable<T>::value, typename _atomic_gnu_c_standard_type<sizeof(T)>::value_type> get_atomic_value(const T& value){
    return reinterpret_cast<typename _atomic_gnu_c_standard_type<sizeof(T)>::value_type>(value);
}

template <typename T>
qlog_forceinline std::enable_if_t<!_atomic_gnu_is_reinterpret_cast_enable<T>::value, typename _atomic_gnu_c_standard_type<sizeof(T)>::value_type> get_atomic_value(const T& value){
    return static_cast<typename _atomic_gnu_c_standard_type<sizeof(T)>::value_type>(value);
}

template <typename T>
qlog_forceinline typename _atomic_gnu_c_standard_type<sizeof(T)>::ptr_type get_atomic_ptr(const T& value){
    return reinterpret_cast<typename _atomic_gnu_c_standard_type<sizeof(T)>::ptr_type>(const_cast<T*>(&value));
}

template <typename To, typename AtomicStandardType>
qlog_forceinline std::enable_if_t<_atomic_gnu_is_reinterpret_cast_enable<To>::value, To> get_value_type_from_atomic_standard_value(const AtomicStandardType& value){
    return reinterpret_cast<To>(value);
}

template <typename To, typename AtomicStandardType>
qlog_forceinline std::enable_if_t<!_atomic_gnu_is_reinterpret_cast_enable<To>::value, To> get_value_type_from_atomic_standard_value(const AtomicStandardType& value){
    return static_cast<To>(value);
}

template <typename T, std::size_t N>
class alignas(8) _atomic_base {
public:
    static constexpr bool supported = true;

protected:
    static_assert(sizeof(T) == N, "please don't use qlog::platform::_atomic_base, use qlog::platform::atomic instead.");
    static_assert(N == 1 || N == 2 || N == 4 || N == 8, "please don't use qlog::platform::_atomic_base, use qlog::platform::atomic instead.");
    typedef typename std::decay<T>::type value_type;
    typedef typename _atomic_gnu_c_standard_type<sizeof(T)>::ptr_type api_ptr_type;
    typedef typename _atomic_gnu_c_standard_type<sizeof(T)>::value_type api_type;
    union {
        alignas(8) value_type value_;
        alignas(8) api_type value_standard;
    };

    qlog_forceinline void explicit_copy(const value_type& val){
        __atomic_store_n(&value_standard, get_atomic_value(val), __ATOMIC_SEQ_CST);
    }

    qlog_forceinline value_type load(memory_order order = memory_order::seq_cst) const noexcept{
        switch (order) {
        case memory_order::relaxed:
            return get_value_type_from_atomic_standard_value<value_type>(__atomic_load_n(&value_standard, __ATOMIC_RELAXED));
        case memory_order::acquire:
            return get_value_type_from_atomic_standard_value<value_type>(__atomic_load_n(&value_standard, __ATOMIC_ACQUIRE));
        default:
            return get_value_type_from_atomic_standard_value<value_type>(__atomic_load_n(&value_standard, __ATOMIC_SEQ_CST));
        }
    }

    qlog_forceinline value_type load_raw() const noexcept{
        return value_;
    }

    qlog_forceinline value_type load_acquire() const noexcept{
        return get_value_type_from_atomic_standard_value<value_type>(__atomic_load_n(&value_standard, __ATOMIC_ACQUIRE));
    }

    qlog_forceinline value_type load_relaxed() const noexcept{
        return get_value_type_from_atomic_standard_value<value_type>(__atomic_load_n(&value_standard, __ATOMIC_RELAXED));
    }

    qlog_forceinline value_type load_acq_rel() const noexcept{
        return get_value_type_from_atomic_standard_value<value_type>(__atomic_load_n(&value_standard, __ATOMIC_SEQ_CST));
    }

    qlog_forceinline value_type load_seq_cst() const noexcept{
        return get_value_type_from_atomic_standard_value<value_type>(__atomic_load_n(&value_standard, __ATOMIC_SEQ_CST));
    }

    qlog_forceinline void store(value_type val, memory_order order) noexcept{
        switch (order) {
        case memory_order::relaxed:
            return __atomic_store_n(&value_standard, get_atomic_value(val), __ATOMIC_RELAXED);
        case memory_order::release:
            return __atomic_store_n(&value_standard, get_atomic_value(val), __ATOMIC_RELEASE);
        default:
            return __atomic_store_n(&value_standard, get_atomic_value(val), __ATOMIC_SEQ_CST);
        }
    }
    qlog_forceinline void store_raw(value_type val) noexcept{
        value_ = val;
    }

    qlog_forceinline void store_release(value_type val) noexcept{
        return __atomic_store_n(&value_standard, get_atomic_value(val), __ATOMIC_RELEASE);
    }

    qlog_forceinline void store_relaxed(value_type val) noexcept{
        return __atomic_store_n(&value_standard, get_atomic_value(val), __ATOMIC_RELAXED);
    }

    qlog_forceinline void store_acq_rel(value_type val) noexcept{
        return __atomic_store_n(&value_standard, get_atomic_value(val), __ATOMIC_SEQ_CST);
    }

    qlog_forceinline void store_seq_cst(value_type val) noexcept{
        return __atomic_store_n(&value_standard, get_atomic_value(val), __ATOMIC_SEQ_CST);
    }

    qlog_forceinline value_type exchange(value_type val, memory_order order) noexcept{
        switch (order) {
        case memory_order::relaxed:
            return get_value_type_from_atomic_standard_value<value_type>(__atomic_exchange_n(&value_standard, get_atomic_value(val), __ATOMIC_RELAXED));
        case memory_order::acquire:
            return get_value_type_from_atomic_standard_value<value_type>(__atomic_exchange_n(&value_standard, get_atomic_value(val), __ATOMIC_ACQUIRE));
        case memory_order::release:
            return get_value_type_from_atomic_standard_value<value_type>(__atomic_exchange_n(&value_standard, get_atomic_value(val), __ATOMIC_RELEASE));
        case memory_order::acq_rel:
            return get_value_type_from_atomic_standard_value<value_type>(__atomic_exchange_n(&value_standard, get_atomic_value(val), __ATOMIC_ACQ_REL));
        default:
            return get_value_type_from_atomic_standard_value<value_type>(__atomic_exchange_n(&value_standard, get_atomic_value(val), __ATOMIC_SEQ_CST));
        }
    }

    qlog_forceinline value_type exchange_raw(value_type value) noexcept{
        value_type old = value_;
        value_ = value;
        return old;
    }

    qlog_forceinline value_type exchange_acquire(value_type val) noexcept{
        return get_value_type_from_atomic_standard_value<value_type>(__atomic_exchange_n(&value_standard, get_atomic_value(val), __ATOMIC_ACQUIRE));
    }

    qlog_forceinline value_type exchange_release(value_type val) noexcept{
        return get_value_type_from_atomic_standard_value<value_type>(__atomic_exchange_n(&value_standard, get_atomic_value(val), __ATOMIC_RELEASE));
    }

    qlog_forceinline value_type exchange_relaxed(value_type val) noexcept{
        return get_value_type_from_atomic_standard_value<value_type>(__atomic_exchange_n(&value_standard, get_atomic_value(val), __ATOMIC_RELAXED));
    }

    qlog_forceinline value_type exchange_acq_rel(value_type val) noexcept{
        return get_value_type_from_atomic_standard_value<value_type>(__atomic_exchange_n(&value_standard, get_atomic_value(val), __ATOMIC_ACQ_REL));
    }

    qlog_forceinline value_type exchange_seq_cst(value_type val) noexcept{
        return get_value_type_from_atomic_standard_value<value_type>(__atomic_exchange_n(&value_standard, get_atomic_value(val), __ATOMIC_SEQ_CST));
    }
};
}
#endif
```

**MSVC 后端：include/qlog/platform/atomic/_inner_atomic_msvc.h**

```cpp
/* Copyright (C) 2025 Tencent.
 * BQLOG is licensed under the Apache License, Version 2.0.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */
#pragma once

#ifdef QLOG_MSVC
#include <intrin.h>

namespace qlog::platform{
template <typename T, std::size_t N>
class alignas(8) _atomic_base {
public:
    static constexpr bool supported = false;

protected:
    typedef typename std::decay<T>::type value_type;
    alignas(8) value_type value_;

    qlog_forceinline void explicit_copy(const value_type& val){
        value_ = val;
    }
};

#define QLOG_ATOMIC_CONCATX(x, y) x##y

#if defined(_M_IX86) || (defined(_M_X64) && !defined(_M_ARM64EC))
#define _QLOG_INTRIN_RELAXED(x) x
#define _QLOG_INTRIN_ACQUIRE(x) x
#define _QLOG_INTRIN_RELEASE(x) x
#elif defined(_M_ARM) || defined(_M_ARM64) || defined(_M_ARM64EC)
#define _QLOG_INTRIN_RELAXED(x) QLOG_ATOMIC_CONCATX(x, _nf)
#define _QLOG_INTRIN_ACQUIRE(x) QLOG_ATOMIC_CONCATX(x, _acq)
#define _QLOG_INTRIN_RELEASE(x) QLOG_ATOMIC_CONCATX(x, _rel)
#endif

#define QLOG_ATOMIC_INTRINSIC_RELAXED(_Result, _Intrinsic, ...) \
    _Result = _QLOG_INTRIN_RELAXED(_Intrinsic)(__VA_ARGS__);

#define QLOG_ATOMIC_INTRINSIC_ACQUIRE(_Result, _Intrinsic, ...) \
    _Result = _QLOG_INTRIN_ACQUIRE(_Intrinsic)(__VA_ARGS__);

#define QLOG_ATOMIC_INTRINSIC_RELEASE(_Result, _Intrinsic, ...) \
    _Result = _QLOG_INTRIN_RELEASE(_Intrinsic)(__VA_ARGS__);

#define QLOG_ATOMIC_INTRINSIC_ACQ_REL(_Result, _Intrinsic, ...) \
    _Result = _Intrinsic(__VA_ARGS__);

#define QLOG_ATOMIC_INTRINSIC_SEQ_CST(_Result, _Intrinsic, ...) \
    _Result = _Intrinsic(__VA_ARGS__);

#define QLOG_ATOMIC_CHOOSE_INTRINSIC(_Order, _Result, _Intrinsic, ...) \
    switch (_Order) { \
    case memory_order::relaxed: \
        _Result = _QLOG_INTRIN_RELAXED(_Intrinsic)(__VA_ARGS__); \
        break; \
    case memory_order::acquire: \
        _Result = _QLOG_INTRIN_ACQUIRE(_Intrinsic)(__VA_ARGS__); \
        break; \
    case memory_order::release: \
        _Result = _QLOG_INTRIN_RELEASE(_Intrinsic)(__VA_ARGS__); \
        break; \
    case memory_order::acq_rel: \
        _Result = _Intrinsic(__VA_ARGS__); \
        break; \
    case memory_order::seq_cst: \
        _Result = _Intrinsic(__VA_ARGS__); \
        break; \
    }

#define _INTRIN_BIT_1(_Intrinsic) QLOG_ATOMIC_CONCATX(_Intrinsic, 8)
#define _INTRIN_BIT_2(_Intrinsic) QLOG_ATOMIC_CONCATX(_Intrinsic, 16)
#define _INTRIN_BIT_4(_Intrinsic) _Intrinsic
#define _INTRIN_BIT_8(_Intrinsic) QLOG_ATOMIC_CONCATX(_Intrinsic, 64)

#define _INTRIN_BIT_SUFFIX(_Intrinsic, SIZE_BYTE) QLOG_ATOMIC_CONCATX(_INTRIN_BIT_, SIZE_BYTE)(_Intrinsic)

#define SPECIALIZED_ATOMIC_BASE(SIZE_BYTE) \
    template <typename T> \
    class _atomic_base<T, SIZE_BYTE> { \
    public: \
        static constexpr bool supported = true; \
 \
    protected: \
        static_assert(sizeof(T) == SIZE_BYTE, "please don't use qlog::platform::_atomic_base, use qlog::platform::atomic instead."); \
        typedef typename std::decay<T>::type value_type; \
        typedef typename _atomic_standard_windows_type<sizeof(T)>::value_type api_type; \
        alignas(8) value_type value_; \
        qlog_forceinline void explicit_copy(const value_type& val){ \
            api_type result; \
            QLOG_ATOMIC_INTRINSIC_SEQ_CST(result, _INTRIN_BIT_SUFFIX(_InterlockedExchange, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>(val)); \
            (void)result; \
        } \
        qlog_forceinline value_type load(memory_order order = memory_order::seq_cst) const noexcept{ \
            api_type result; \
            QLOG_ATOMIC_CHOOSE_INTRINSIC(order, result, _INTRIN_BIT_SUFFIX(_InterlockedExchangeAdd, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>((value_type)0)); \
            return (value_type)result; \
        } \
        qlog_forceinline value_type load_raw() const noexcept{ \
            return value_; \
        } \
        qlog_forceinline value_type load_acquire() const noexcept{ \
            api_type result; \
            QLOG_ATOMIC_INTRINSIC_ACQUIRE(result, _INTRIN_BIT_SUFFIX(_InterlockedExchangeAdd, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>((value_type)0)); \
            return (value_type)result; \
        } \
        qlog_forceinline value_type load_relaxed() const noexcept{ \
            api_type result; \
            QLOG_ATOMIC_INTRINSIC_RELAXED(result, _INTRIN_BIT_SUFFIX(_InterlockedExchangeAdd, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>((value_type)0)); \
            return (value_type)result; \
        } \
        qlog_forceinline value_type load_acq_rel() const noexcept{ \
            api_type result; \
            QLOG_ATOMIC_INTRINSIC_ACQ_REL(result, _INTRIN_BIT_SUFFIX(_InterlockedExchangeAdd, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>((value_type)0)); \
            return (value_type)result; \
        } \
        qlog_forceinline value_type load_seq_cst() const noexcept{ \
            api_type result; \
            QLOG_ATOMIC_INTRINSIC_SEQ_CST(result, _INTRIN_BIT_SUFFIX(_InterlockedExchangeAdd, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>((value_type)0)); \
            return (value_type)result; \
        } \
        qlog_forceinline void store(value_type val, memory_order order) noexcept{ \
            api_type result; \
            QLOG_ATOMIC_CHOOSE_INTRINSIC(order, result, _INTRIN_BIT_SUFFIX(_InterlockedExchange, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>(val)); \
            (void)result; \
        } \
        qlog_forceinline void store_raw(value_type val) noexcept{ \
            value_ = val; \
        } \
        qlog_forceinline void store_release(value_type val) noexcept{ \
            api_type result; \
            QLOG_ATOMIC_INTRINSIC_RELEASE(result, _INTRIN_BIT_SUFFIX(_InterlockedExchange, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>(val)); \
            (void)result; \
        } \
        qlog_forceinline void store_relaxed(value_type val) noexcept{ \
            api_type result; \
            QLOG_ATOMIC_INTRINSIC_RELAXED(result, _INTRIN_BIT_SUFFIX(_InterlockedExchange, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>(val)); \
            (void)result; \
        } \
        qlog_forceinline void store_acq_rel(value_type val) noexcept{ \
            api_type result; \
            QLOG_ATOMIC_INTRINSIC_ACQ_REL(result, _INTRIN_BIT_SUFFIX(_InterlockedExchange, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>(val)); \
            (void)result; \
        } \
        qlog_forceinline void store_seq_cst(value_type val) noexcept{ \
            api_type result; \
            QLOG_ATOMIC_INTRINSIC_SEQ_CST(result, _INTRIN_BIT_SUFFIX(_InterlockedExchange, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>(val)); \
            (void)result; \
        } \
        qlog_forceinline value_type exchange(value_type val, memory_order order) noexcept{ \
            api_type result; \
            QLOG_ATOMIC_CHOOSE_INTRINSIC(order, result, _INTRIN_BIT_SUFFIX(_InterlockedExchange, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>(val)); \
            return (value_type)result; \
        } \
        qlog_forceinline value_type exchange_raw(value_type val) noexcept{ \
            value_type old = value_; \
            value_ = val; \
            return old; \
        } \
        qlog_forceinline value_type exchange_acquire(value_type val) noexcept{ \
            api_type result; \
            QLOG_ATOMIC_INTRINSIC_ACQUIRE(result, _INTRIN_BIT_SUFFIX(_InterlockedExchange, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>(val)); \
            return (value_type)result; \
        } \
        qlog_forceinline value_type exchange_release(value_type val) noexcept{ \
            api_type result; \
            QLOG_ATOMIC_INTRINSIC_RELEASE(result, _INTRIN_BIT_SUFFIX(_InterlockedExchange, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>(val)); \
            return (value_type)result; \
        } \
        qlog_forceinline value_type exchange_relaxed(value_type val) noexcept{ \
            api_type result; \
            QLOG_ATOMIC_INTRINSIC_RELAXED(result, _INTRIN_BIT_SUFFIX(_InterlockedExchange, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>(val)); \
            return (value_type)result; \
        } \
        qlog_forceinline value_type exchange_acq_rel(value_type val) noexcept{ \
            api_type result; \
            QLOG_ATOMIC_INTRINSIC_ACQ_REL(result, _INTRIN_BIT_SUFFIX(_InterlockedExchange, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>(val)); \
            return (value_type)result; \
        } \
        qlog_forceinline value_type exchange_seq_cst(value_type val) noexcept{ \
            api_type result; \
            QLOG_ATOMIC_INTRINSIC_SEQ_CST(result, _INTRIN_BIT_SUFFIX(_InterlockedExchange, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>(val)); \
            return (value_type)result; \
        } \
};

template <std::size_t N>
struct _atomic_standard_windows_type {
};

template <>
struct _atomic_standard_windows_type<1> {
    typedef char* ptr_type;
    typedef char value_type;
};

template <>
struct _atomic_standard_windows_type<2> {
    typedef short* ptr_type; // std::int16_t is not compatible to VC++ type short in the intrinsic functions
    typedef short value_type;
};

template <>
struct _atomic_standard_windows_type<4> {
    typedef long* ptr_type; // std::int32_t is not compatible to VC++ type short in the intrinsic functions
    typedef long value_type;
};

template <>
struct _atomic_standard_windows_type<8> {
    typedef __int64* ptr_type; // std::int64_t is not compatible to VC++ type short in the intrinsic functions
    typedef __int64 value_type;
};

template <typename T>
typename _atomic_standard_windows_type<sizeof(T)>::value_type get_atomic_value(const T& value){
    return (typename _atomic_standard_windows_type<sizeof(T)>::value_type)(value);
}

template <typename T>
typename _atomic_standard_windows_type<sizeof(T)>::ptr_type get_atomic_ptr(const T* value){
    return reinterpret_cast<typename _atomic_standard_windows_type<sizeof(T)>::ptr_type>(const_cast<T*>(value));
}

#pragma warning(push)
#pragma warning(disable : 6001)
#pragma warning(disable : 4701)
SPECIALIZED_ATOMIC_BASE(1);
SPECIALIZED_ATOMIC_BASE(2);
SPECIALIZED_ATOMIC_BASE(4);
SPECIALIZED_ATOMIC_BASE(8);
#pragma warning(pop)
}
#endif
```

本组代码中的 GNU union、MSVC value_ 与 const_cast/intrinsic 路径、原 default 分支差异都按参考保留。raw 没有原子保证；无竞争条件的普通访问与并发发布路径要分开理解。真实 const 原对象的 MSVC load/RMW 和 GNU union 成员切换的语言边界见第 7 节，不把当前检查称为纯 ISO C++ 保证。测试通过 const 引用观察非 const 对象，没有运行真实 const 对象的 RMW。

### 本组临时候选检查

Linux GCC 与原生 Windows MSVC 的 Debug/Release 均编译链接通过，候选与实际 BQLog 参考各 637 项定向检查，输出一致。覆盖 bool、1/2/4/8 字节无符号、int32_t、固定底层枚举、指针；全部 raw/固定序变体、六个平凡属性、sizeof/alignof、load 的默认参数和 store/exchange 不可省 order、noexcept、静态零初始化及一次 release/acquire 发布检查。不是完整原子/CAS/算术/自旋锁/并发或性能验收。

候选和参考使用不同编译目标，防止 MSVC 同名 _INTRIN_BIT/SPECIALIZED_ATOMIC_BASE 宏相互覆盖。独立 BQLog MSVC 参考探针需提前包含 intrin.h，满足其公共 type_tools 的 _ReadWriteBarrier 依赖；未修改参考源码。探针中 noexcept 检查使用已求值的标量，避免把 vector<bool> 取值求值的异常说明混入方法合同。

MSVC /W4 /WX /permissive- /utf-8 /EHsc 检查仅额外排除原 8 字节对齐导致的 C4324；BQLog 头作为 SYSTEM 参考包含。参考原有 6001/4701 warning 区域保留；没有为消除警告给 result 加初值或 default。实际 QLog 根警告/CMake 未变，不称无例外严格通过。

证据：D:/qq344/Documents/qlog-atomic-group1-complete-guide-20261009-3fa2b7，包含候选三个头、可读 MSVC 仅缩进副本、独立探针、两系统两配置编译/运行日志、选定参考体及预处理续行后的一致性检查、生产前后 SHA-256。此次结果仅属于本组新候选，不继承此前 633/20000 的旧候选成绩。

## 附录：本次从参考逐行抽取的声明清单

下列代码块是 BQLog 原始声明头清单，故意保留 bq/std 基础适配前的词法和原参数名，行号见证据 JSON。它不是可以单独编译的实现文件；查函数体时打开对应实际参考行。QLog 只按第 2 节做必要前缀和基础库适配，不能改默认参数、修饰、返回类型或接口集合。

### atomic.h

```cpp
bq_forceinline value_type load(memory_order order = memory_order::seq_cst) const noexcept;
bq_forceinline value_type load_raw() const noexcept;
bq_forceinline value_type load_acquire() const noexcept;
bq_forceinline value_type load_relaxed() const noexcept;
bq_forceinline value_type load_acq_rel() const noexcept;
bq_forceinline value_type load_seq_cst() const noexcept;
bq_forceinline void store(value_type value, memory_order order) noexcept;
bq_forceinline void store_raw(value_type value) noexcept;
bq_forceinline void store_release(value_type value) noexcept;
bq_forceinline void store_relaxed(value_type value) noexcept;
bq_forceinline void store_acq_rel(value_type value) noexcept;
bq_forceinline void store_seq_cst(value_type value) noexcept;
bq_forceinline value_type exchange(value_type value, memory_order order) noexcept;
bq_forceinline value_type exchange_raw(value_type value) noexcept;
bq_forceinline value_type exchange_acquire(value_type value) noexcept;
bq_forceinline value_type exchange_release(value_type value) noexcept;
bq_forceinline value_type exchange_relaxed(value_type value) noexcept;
bq_forceinline value_type exchange_acq_rel(value_type value) noexcept;
bq_forceinline value_type exchange_seq_cst(value_type value) noexcept;
bq_forceinline bool compare_exchange_weak(value_type& expected, value_type desired, const memory_order success_order = memory_order::seq_cst, const memory_order fail_order = memory_order::seq_cst) noexcept;
bq_forceinline bool compare_exchange_strong(value_type& expected, value_type desired, const memory_order success_order = memory_order::seq_cst, const memory_order fail_order = memory_order::seq_cst) noexcept;
bq_forceinline value_type add_fetch(value_type val, memory_order order) noexcept;
bq_forceinline value_type add_fetch_raw(value_type val) noexcept;
bq_forceinline value_type add_fetch_acquire(value_type val) noexcept;
bq_forceinline value_type add_fetch_release(value_type val) noexcept;
bq_forceinline value_type add_fetch_relaxed(value_type val) noexcept;
bq_forceinline value_type add_fetch_acq_rel(value_type val) noexcept;
bq_forceinline value_type add_fetch_seq_cst(value_type val) noexcept;
bq_forceinline value_type fetch_add(value_type val, memory_order order) noexcept;
bq_forceinline value_type fetch_add_raw(value_type val) noexcept;
bq_forceinline value_type fetch_add_acquire(value_type val) noexcept;
bq_forceinline value_type fetch_add_release(value_type val) noexcept;
bq_forceinline value_type fetch_add_relaxed(value_type val) noexcept;
bq_forceinline value_type fetch_add_acq_rel(value_type val) noexcept;
bq_forceinline value_type fetch_add_seq_cst(value_type val) noexcept;
bq_forceinline value_type sub_fetch(value_type val, memory_order order) noexcept;
bq_forceinline value_type sub_fetch_raw(value_type val) noexcept;
bq_forceinline value_type sub_fetch_acquire(value_type val) noexcept;
bq_forceinline value_type sub_fetch_release(value_type val) noexcept;
bq_forceinline value_type sub_fetch_relaxed(value_type val) noexcept;
bq_forceinline value_type sub_fetch_acq_rel(value_type val) noexcept;
bq_forceinline value_type sub_fetch_seq_cst(value_type val) noexcept;
bq_forceinline value_type fetch_sub(value_type val, memory_order order) noexcept;
bq_forceinline value_type fetch_sub_raw(value_type val) noexcept;
bq_forceinline value_type fetch_sub_acquire(value_type val) noexcept;
bq_forceinline value_type fetch_sub_release(value_type val) noexcept;
bq_forceinline value_type fetch_sub_relaxed(value_type val) noexcept;
bq_forceinline value_type fetch_sub_acq_rel(value_type val) noexcept;
bq_forceinline value_type fetch_sub_seq_cst(value_type val) noexcept;
bq_forceinline value_type xor_fetch(value_type val, memory_order order) noexcept;
bq_forceinline value_type xor_fetch_raw(value_type val) noexcept;
bq_forceinline value_type xor_fetch_acquire(value_type val) noexcept;
bq_forceinline value_type xor_fetch_release(value_type val) noexcept;
bq_forceinline value_type xor_fetch_relaxed(value_type val) noexcept;
bq_forceinline value_type xor_fetch_acq_rel(value_type val) noexcept;
bq_forceinline value_type xor_fetch_seq_cst(value_type val) noexcept;
bq_forceinline value_type fetch_xor(value_type val, memory_order order) noexcept;
bq_forceinline value_type fetch_xor_raw(value_type val) noexcept;
bq_forceinline value_type fetch_xor_acquire(value_type val) noexcept;
bq_forceinline value_type fetch_xor_release(value_type val) noexcept;
bq_forceinline value_type fetch_xor_relaxed(value_type val) noexcept;
bq_forceinline value_type fetch_xor_acq_rel(value_type val) noexcept;
bq_forceinline value_type fetch_xor_seq_cst(value_type val) noexcept;
bq_forceinline value_type or_fetch(value_type val, memory_order order) noexcept;
bq_forceinline value_type or_fetch_raw(value_type val) noexcept;
bq_forceinline value_type or_fetch_acquire(value_type val) noexcept;
bq_forceinline value_type or_fetch_release(value_type val) noexcept;
bq_forceinline value_type or_fetch_relaxed(value_type val) noexcept;
bq_forceinline value_type or_fetch_acq_rel(value_type val) noexcept;
bq_forceinline value_type or_fetch_seq_cst(value_type val) noexcept;
bq_forceinline value_type fetch_or(value_type val, memory_order order) noexcept;
bq_forceinline value_type fetch_or_raw(value_type val) noexcept;
bq_forceinline value_type fetch_or_acquire(value_type val) noexcept;
bq_forceinline value_type fetch_or_release(value_type val) noexcept;
bq_forceinline value_type fetch_or_relaxed(value_type val) noexcept;
bq_forceinline value_type fetch_or_acq_rel(value_type val) noexcept;
bq_forceinline value_type fetch_or_seq_cst(value_type val) noexcept;
bq_forceinline value_type and_fetch(value_type val, memory_order order) noexcept;
bq_forceinline value_type and_fetch_raw(value_type val) noexcept;
bq_forceinline value_type and_fetch_acquire(value_type val) noexcept;
bq_forceinline value_type and_fetch_release(value_type val) noexcept;
bq_forceinline value_type and_fetch_relaxed(value_type val) noexcept;
bq_forceinline value_type and_fetch_acq_rel(value_type val) noexcept;
bq_forceinline value_type and_fetch_seq_cst(value_type val) noexcept;
bq_forceinline value_type fetch_and(value_type val, memory_order order) noexcept;
bq_forceinline value_type fetch_and_raw(value_type val) noexcept;
bq_forceinline value_type fetch_and_acquire(value_type val) noexcept;
bq_forceinline value_type fetch_and_release(value_type val) noexcept;
bq_forceinline value_type fetch_and_relaxed(value_type val) noexcept;
bq_forceinline value_type fetch_and_acq_rel(value_type val) noexcept;
bq_forceinline value_type fetch_and_seq_cst(value_type val) noexcept;
bq_forceinline atomic_trivially_constructible<T>& operator++();
bq_forceinline atomic_trivially_constructible<T> operator++(int32_t);
bq_forceinline atomic_trivially_constructible<T>& operator--();
bq_forceinline atomic_trivially_constructible<T> operator--(int32_t);
bq_forceinline atomic_trivially_constructible<T>& operator+=(value_type value);
bq_forceinline atomic_trivially_constructible<T> operator+(value_type value);
bq_forceinline atomic_trivially_constructible<T>& operator-=(value_type value);
bq_forceinline atomic_trivially_constructible<T> operator-(value_type value);
atomic();
atomic(const value_type& value);
atomic(const atomic<T>& rhs);
atomic(const trivially_constructible_type& rhs);
bq_forceinline atomic<T>& operator=(const value_type& value);
bq_forceinline atomic<T>& operator=(const atomic<T>& rhs);
bq_forceinline atomic<T>& operator=(const trivially_constructible_type& rhs);
bq_forceinline atomic<T>& operator++();
bq_forceinline atomic<T> operator++(int32_t);
bq_forceinline atomic<T>& operator--();
bq_forceinline atomic<T> operator--(int32_t);
bq_forceinline atomic<T>& operator+=(value_type value);
bq_forceinline atomic<T> operator+(value_type value);
bq_forceinline atomic<T>& operator-=(value_type value);
bq_forceinline atomic<T> operator-(value_type value);
```

### _inner_atomic_GNU_C.h

```cpp
bq_forceinline bq::enable_if_t<_atomic_gnu_is_reinterpret_cast_enable<T>::value, typename _atomic_gnu_c_standard_type<sizeof(T)>::value_type> get_atomic_value(const T& value);
bq_forceinline bq::enable_if_t<!_atomic_gnu_is_reinterpret_cast_enable<T>::value, typename _atomic_gnu_c_standard_type<sizeof(T)>::value_type> get_atomic_value(const T& value);
bq_forceinline typename _atomic_gnu_c_standard_type<sizeof(T)>::ptr_type get_atomic_ptr(const T& value);
bq_forceinline bq::enable_if_t<_atomic_gnu_is_reinterpret_cast_enable<To>::value, To> get_value_type_from_atomic_standard_value(const AtomicStandardType& value);
bq_forceinline bq::enable_if_t<!_atomic_gnu_is_reinterpret_cast_enable<To>::value, To> get_value_type_from_atomic_standard_value(const AtomicStandardType& value);
bq_forceinline void explicit_copy(const value_type& val);
bq_forceinline value_type load(memory_order order = memory_order::seq_cst) const noexcept;
bq_forceinline value_type load_raw() const noexcept;
bq_forceinline value_type load_acquire() const noexcept;
bq_forceinline value_type load_relaxed() const noexcept;
bq_forceinline value_type load_acq_rel() const noexcept;
bq_forceinline value_type load_seq_cst() const noexcept;
bq_forceinline void store(value_type val, memory_order order) noexcept;
bq_forceinline void store_raw(value_type val) noexcept;
bq_forceinline void store_release(value_type val) noexcept;
bq_forceinline void store_relaxed(value_type val) noexcept;
bq_forceinline void store_acq_rel(value_type val) noexcept;
bq_forceinline void store_seq_cst(value_type val) noexcept;
bq_forceinline value_type exchange(value_type val, memory_order order) noexcept;
bq_forceinline value_type exchange_raw(value_type value) noexcept;
bq_forceinline value_type exchange_acquire(value_type val) noexcept;
bq_forceinline value_type exchange_release(value_type val) noexcept;
bq_forceinline value_type exchange_relaxed(value_type val) noexcept;
bq_forceinline value_type exchange_acq_rel(value_type val) noexcept;
bq_forceinline value_type exchange_seq_cst(value_type val) noexcept;
bq_forceinline bool compare_exchange_weak(value_type& expected, value_type desired, const memory_order success_order = memory_order::seq_cst, const memory_order fail_order = memory_order::seq_cst) noexcept;
bq_forceinline bool compare_exchange_strong(value_type& expected, value_type desired, const memory_order success_order = memory_order::seq_cst, const memory_order fail_order = memory_order::seq_cst) noexcept;
bq_forceinline value_type add_fetch(value_type val, memory_order order) noexcept;
bq_forceinline value_type add_fetch_raw(value_type val) noexcept;
bq_forceinline value_type add_fetch_acquire(value_type val) noexcept;
bq_forceinline value_type add_fetch_release(value_type val) noexcept;
bq_forceinline value_type add_fetch_relaxed(value_type val) noexcept;
bq_forceinline value_type add_fetch_acq_rel(value_type val) noexcept;
bq_forceinline value_type add_fetch_seq_cst(value_type val) noexcept;
bq_forceinline value_type fetch_add(value_type val, memory_order order) noexcept;
bq_forceinline value_type fetch_add_raw(value_type val) noexcept;
bq_forceinline value_type fetch_add_acquire(value_type val) noexcept;
bq_forceinline value_type fetch_add_release(value_type val) noexcept;
bq_forceinline value_type fetch_add_relaxed(value_type val) noexcept;
bq_forceinline value_type fetch_add_acq_rel(value_type val) noexcept;
bq_forceinline value_type fetch_add_seq_cst(value_type val) noexcept;
bq_forceinline value_type sub_fetch(value_type val, memory_order order) noexcept;
bq_forceinline value_type sub_fetch_raw(value_type val) noexcept;
bq_forceinline value_type sub_fetch_acquire(value_type val) noexcept;
bq_forceinline value_type sub_fetch_release(value_type val) noexcept;
bq_forceinline value_type sub_fetch_relaxed(value_type val) noexcept;
bq_forceinline value_type sub_fetch_acq_rel(value_type val) noexcept;
bq_forceinline value_type sub_fetch_seq_cst(value_type val) noexcept;
bq_forceinline value_type fetch_sub(value_type val, memory_order order) noexcept;
bq_forceinline value_type fetch_sub_raw(value_type val) noexcept;
bq_forceinline value_type fetch_sub_acquire(value_type val) noexcept;
bq_forceinline value_type fetch_sub_release(value_type val) noexcept;
bq_forceinline value_type fetch_sub_relaxed(value_type val) noexcept;
bq_forceinline value_type fetch_sub_acq_rel(value_type val) noexcept;
bq_forceinline value_type fetch_sub_seq_cst(value_type val) noexcept;
bq_forceinline value_type xor_fetch(value_type val, memory_order order) noexcept;
bq_forceinline value_type xor_fetch_raw(value_type val) noexcept;
bq_forceinline value_type xor_fetch_acquire(value_type val) noexcept;
bq_forceinline value_type xor_fetch_release(value_type val) noexcept;
bq_forceinline value_type xor_fetch_relaxed(value_type val) noexcept;
bq_forceinline value_type xor_fetch_acq_rel(value_type val) noexcept;
bq_forceinline value_type xor_fetch_seq_cst(value_type val) noexcept;
bq_forceinline value_type fetch_xor(value_type val, memory_order order) noexcept;
bq_forceinline value_type fetch_xor_raw(value_type val) noexcept;
bq_forceinline value_type fetch_xor_acquire(value_type val) noexcept;
bq_forceinline value_type fetch_xor_release(value_type val) noexcept;
bq_forceinline value_type fetch_xor_relaxed(value_type val) noexcept;
bq_forceinline value_type fetch_xor_acq_rel(value_type val) noexcept;
bq_forceinline value_type fetch_xor_seq_cst(value_type val) noexcept;
bq_forceinline value_type or_fetch(value_type val, memory_order order) noexcept;
bq_forceinline value_type or_fetch_raw(value_type val) noexcept;
bq_forceinline value_type or_fetch_acquire(value_type val) noexcept;
bq_forceinline value_type or_fetch_release(value_type val) noexcept;
bq_forceinline value_type or_fetch_relaxed(value_type val) noexcept;
bq_forceinline value_type or_fetch_acq_rel(value_type val) noexcept;
bq_forceinline value_type or_fetch_seq_cst(value_type val) noexcept;
bq_forceinline value_type fetch_or(value_type val, memory_order order) noexcept;
bq_forceinline value_type fetch_or_raw(value_type val) noexcept;
bq_forceinline value_type fetch_or_acquire(value_type val) noexcept;
bq_forceinline value_type fetch_or_release(value_type val) noexcept;
bq_forceinline value_type fetch_or_relaxed(value_type val) noexcept;
bq_forceinline value_type fetch_or_acq_rel(value_type val) noexcept;
bq_forceinline value_type fetch_or_seq_cst(value_type val) noexcept;
bq_forceinline value_type and_fetch(value_type val, memory_order order) noexcept;
bq_forceinline value_type and_fetch_raw(value_type val) noexcept;
bq_forceinline value_type and_fetch_acquire(value_type val) noexcept;
bq_forceinline value_type and_fetch_release(value_type val) noexcept;
bq_forceinline value_type and_fetch_relaxed(value_type val) noexcept;
bq_forceinline value_type and_fetch_acq_rel(value_type val) noexcept;
bq_forceinline value_type and_fetch_seq_cst(value_type val) noexcept;
bq_forceinline value_type fetch_and(value_type val, memory_order order) noexcept;
bq_forceinline value_type fetch_and_raw(value_type val) noexcept;
bq_forceinline value_type fetch_and_acquire(value_type val) noexcept;
bq_forceinline value_type fetch_and_release(value_type val) noexcept;
bq_forceinline value_type fetch_and_relaxed(value_type val) noexcept;
bq_forceinline value_type fetch_and_acq_rel(value_type val) noexcept;
bq_forceinline value_type fetch_and_seq_cst(value_type val) noexcept;
```

### _inner_atomic_msvc.h

```cpp
bq_forceinline void explicit_copy(const value_type& val);
bq_forceinline void explicit_copy(const value_type& val);
bq_forceinline value_type load(memory_order order = memory_order::seq_cst) const noexcept;
bq_forceinline value_type load_raw() const noexcept;
bq_forceinline value_type load_acquire() const noexcept;
bq_forceinline value_type load_relaxed() const noexcept;
bq_forceinline value_type load_acq_rel() const noexcept;
bq_forceinline value_type load_seq_cst() const noexcept;
bq_forceinline void store(value_type val, memory_order order) noexcept;
bq_forceinline void store_raw(value_type val) noexcept;
bq_forceinline void store_release(value_type val) noexcept;
bq_forceinline void store_relaxed(value_type val) noexcept;
bq_forceinline void store_acq_rel(value_type val) noexcept;
bq_forceinline void store_seq_cst(value_type val) noexcept;
bq_forceinline value_type exchange(value_type val, memory_order order) noexcept;
bq_forceinline value_type exchange_raw(value_type val) noexcept;
bq_forceinline value_type exchange_acquire(value_type val) noexcept;
bq_forceinline value_type exchange_release(value_type val) noexcept;
bq_forceinline value_type exchange_relaxed(value_type val) noexcept;
bq_forceinline value_type exchange_acq_rel(value_type val) noexcept;
bq_forceinline value_type exchange_seq_cst(value_type val) noexcept;
bq_forceinline bool compare_exchange_weak(value_type& expected, value_type desired, const memory_order success_order = memory_order::seq_cst, const memory_order fail_order = memory_order::seq_cst) noexcept;
bq_forceinline bool compare_exchange_strong(value_type& expected, value_type desired, const memory_order success_order = memory_order::seq_cst, const memory_order fail_order = memory_order::seq_cst) noexcept;
bq_forceinline value_type add_fetch(value_type val, memory_order order) noexcept;
bq_forceinline value_type add_fetch_raw(value_type val) noexcept;
bq_forceinline value_type add_fetch_acquire(value_type val) noexcept;
bq_forceinline value_type add_fetch_release(value_type val) noexcept;
bq_forceinline value_type add_fetch_relaxed(value_type val) noexcept;
bq_forceinline value_type add_fetch_acq_rel(value_type val) noexcept;
bq_forceinline value_type add_fetch_seq_cst(value_type val) noexcept;
bq_forceinline value_type fetch_add(value_type val, memory_order order) noexcept;
bq_forceinline value_type fetch_add_raw(value_type val) noexcept;
bq_forceinline value_type fetch_add_acquire(value_type val) noexcept;
bq_forceinline value_type fetch_add_release(value_type val) noexcept;
bq_forceinline value_type fetch_add_relaxed(value_type val) noexcept;
bq_forceinline value_type fetch_add_acq_rel(value_type val) noexcept;
bq_forceinline value_type fetch_add_seq_cst(value_type val) noexcept;
bq_forceinline value_type sub_fetch(value_type val, memory_order order) noexcept;
bq_forceinline value_type sub_fetch_raw(value_type val) noexcept;
bq_forceinline value_type sub_fetch_acquire(value_type val) noexcept;
bq_forceinline value_type sub_fetch_release(value_type val) noexcept;
bq_forceinline value_type sub_fetch_relaxed(value_type val) noexcept;
bq_forceinline value_type sub_fetch_acq_rel(value_type val) noexcept;
bq_forceinline value_type sub_fetch_seq_cst(value_type val) noexcept;
bq_forceinline value_type fetch_sub(value_type val, memory_order order) noexcept;
bq_forceinline value_type fetch_sub_raw(value_type val) noexcept;
bq_forceinline value_type fetch_sub_acquire(value_type val) noexcept;
bq_forceinline value_type fetch_sub_release(value_type val) noexcept;
bq_forceinline value_type fetch_sub_relaxed(value_type val) noexcept;
bq_forceinline value_type fetch_sub_acq_rel(value_type val) noexcept;
bq_forceinline value_type fetch_sub_seq_cst(value_type val) noexcept;
bq_forceinline value_type xor_fetch(value_type val, memory_order order) noexcept;
bq_forceinline value_type xor_fetch_raw(value_type val) noexcept;
bq_forceinline value_type xor_fetch_acquire(value_type val) noexcept;
bq_forceinline value_type xor_fetch_release(value_type val) noexcept;
bq_forceinline value_type xor_fetch_relaxed(value_type val) noexcept;
bq_forceinline value_type xor_fetch_acq_rel(value_type val) noexcept;
bq_forceinline value_type xor_fetch_seq_cst(value_type val) noexcept;
bq_forceinline value_type fetch_xor(value_type val, memory_order order) noexcept;
bq_forceinline value_type fetch_xor_raw(value_type val) noexcept;
bq_forceinline value_type fetch_xor_acquire(value_type val) noexcept;
bq_forceinline value_type fetch_xor_release(value_type val) noexcept;
bq_forceinline value_type fetch_xor_relaxed(value_type val) noexcept;
bq_forceinline value_type fetch_xor_acq_rel(value_type val) noexcept;
bq_forceinline value_type fetch_xor_seq_cst(value_type val) noexcept;
bq_forceinline value_type or_fetch(value_type val, memory_order order) noexcept;
bq_forceinline value_type or_fetch_raw(value_type val) noexcept;
bq_forceinline value_type or_fetch_acquire(value_type val) noexcept;
bq_forceinline value_type or_fetch_release(value_type val) noexcept;
bq_forceinline value_type or_fetch_relaxed(value_type val) noexcept;
bq_forceinline value_type or_fetch_acq_rel(value_type val) noexcept;
bq_forceinline value_type or_fetch_seq_cst(value_type val) noexcept;
bq_forceinline value_type fetch_or(value_type val, memory_order order) noexcept;
bq_forceinline value_type fetch_or_raw(value_type val) noexcept;
bq_forceinline value_type fetch_or_acquire(value_type val) noexcept;
bq_forceinline value_type fetch_or_release(value_type val) noexcept;
bq_forceinline value_type fetch_or_relaxed(value_type val) noexcept;
bq_forceinline value_type fetch_or_acq_rel(value_type val) noexcept;
bq_forceinline value_type fetch_or_seq_cst(value_type val) noexcept;
bq_forceinline value_type and_fetch(value_type val, memory_order order) noexcept;
bq_forceinline value_type and_fetch_raw(value_type val) noexcept;
bq_forceinline value_type and_fetch_acquire(value_type val) noexcept;
bq_forceinline value_type and_fetch_release(value_type val) noexcept;
bq_forceinline value_type and_fetch_relaxed(value_type val) noexcept;
bq_forceinline value_type and_fetch_acq_rel(value_type val) noexcept;
bq_forceinline value_type and_fetch_seq_cst(value_type val) noexcept;
bq_forceinline value_type fetch_and(value_type val, memory_order order) noexcept;
bq_forceinline value_type fetch_and_raw(value_type val) noexcept;
bq_forceinline value_type fetch_and_acquire(value_type val) noexcept;
bq_forceinline value_type fetch_and_release(value_type val) noexcept;
bq_forceinline value_type fetch_and_relaxed(value_type val) noexcept;
bq_forceinline value_type fetch_and_acq_rel(value_type val) noexcept;
bq_forceinline value_type fetch_and_seq_cst(value_type val) noexcept;
```

MSVC 原帮助函数未带强制内联宏，声明另列（参考 :654、:660）：

```cpp
typename _atomic_standard_windows_type<sizeof(T)>::value_type get_atomic_value(const T& value);
typename _atomic_standard_windows_type<sizeof(T)>::ptr_type get_atomic_ptr(const T* value);
```


## 10. 2026-10-10 当前第一组闭合

当前 GNU 草稿路径为 _inner_atomic_GNU_c.hpp；先依审查表修正拼写/重载及 load_acq_rel=SEQ_CST、exchange acquire 分支、重复固定序命名。MSVC 修复原 CONCATX 与 intrinsic/选择宏，宏每个物理续行带反斜杠，选择宏仍无 default。两个后端最终按已确认名称改为 _inner_atomic_GNU_C.h/_inner_atomic_msvc.h，并同步公共 include；助手本轮只审查，未搬移草稿。

GNU 非指针转换放在现有指针转换之后、_atomic_base 之前，参考 :82-86；公共19个转发与六 trait 放在 atomic.hpp 后端 include 之后重新打开 qlog::platform，完整正文依第9节，不额外加构造/初始化、assert、wrapper或空cpp。MSVC 4字节 intrinsic 类型继续 long。逐项 CR 和实际失败日志见本轮审查记录。

## 11. 下一组：weak/strong CAS 完整接续（2026-10-10）

完成第一组后，在原类/宏中的 exchange_seq_cst 后补两函数。保持 expected 引用、desired 值、两个 const memory_order 参数各自默认 seq_cst、noexcept；公共与两后端对应声明一致。原 raw/固定序辅助 CAS 接口不存在，不新增。以下只有本组新函数，现有第一组正文不重写。

### 11.1 GNU

下表是参考原映射，fail_order 的其他值进入 default，不加合法性断言：

| success_order | fail_order | 实际成功/失败常量 |
| --- | --- | --- |
| relaxed | 任意 | RELAXED / RELAXED |
| acquire | relaxed；其他 | ACQUIRE / RELAXED；ACQUIRE / ACQUIRE |
| release | relaxed；其他 | RELEASE / RELAXED；RELEASE / ACQUIRE |
| acq_rel | relaxed；其他 | ACQ_REL / RELAXED；ACQ_REL / ACQUIRE |
| seq_cst/default | relaxed；acquire；其他 | SEQ_CST / RELAXED；SEQ_CST / ACQUIRE；SEQ_CST / SEQ_CST |

已确认：release/非relaxed 的 RELEASE/ACQUIRE 分支超出 GCC 文档合同，仍按用户选择保留并记录。编译接受不等于消除这一边界；不描述为所有组合安全。使用实际参考 generic __atomic_compare_exchange；desired 参数为地址，经 get_atomic_ptr 转换，不能直接把 desired 值传给 generic 接口。weak 的 bool 为 true，strong 为 false；GNU strong 不委托 weak。

```cpp
qlog_forceinline bool compare_exchange_weak(value_type& expected, value_type desired, const memory_order success_order = memory_order::seq_cst, const memory_order fail_order = memory_order::seq_cst) noexcept
{
    switch (success_order) {
    case memory_order::relaxed:
        return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), true, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    case memory_order::acquire:
        switch (fail_order) {
        case memory_order::relaxed:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
        default:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), true, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
        }
    case memory_order::release:
        switch (fail_order) {
        case memory_order::relaxed:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), true, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
        default:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), true, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
        }
    case memory_order::acq_rel:
        switch (fail_order) {
        case memory_order::relaxed:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
        default:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        }
    case memory_order::seq_cst:
    default:
        switch (fail_order) {
        case memory_order::relaxed:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), true, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
        case memory_order::acquire:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), true, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);
        default:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), true, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        }
    }
}

qlog_forceinline bool compare_exchange_strong(value_type& expected, value_type desired, const memory_order success_order = memory_order::seq_cst, const memory_order fail_order = memory_order::seq_cst) noexcept
{
    switch (success_order) {
    case memory_order::relaxed:
        return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    case memory_order::acquire:
        switch (fail_order) {
        case memory_order::relaxed:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
        default:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
        }
    case memory_order::release:
        switch (fail_order) {
        case memory_order::relaxed:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
        default:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
        }
    case memory_order::acq_rel:
        switch (fail_order) {
        case memory_order::relaxed:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
        default:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        }
    case memory_order::seq_cst:
    default:
        switch (fail_order) {
        case memory_order::relaxed:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
        case memory_order::acquire:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), false, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);
        default:
            return __atomic_compare_exchange(&value_standard, get_atomic_ptr(expected), get_atomic_ptr(desired), false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        }
    }
}
```

### 11.2 MSVC

把以下两函数加入 SPECIALIZED_ATOMIC_BASE(SIZE_BYTE)，在类的最后 }; 前；每个物理行包括函数 } 都保持宏续行。成功序通过原 CHOOSE_INTRINSIC 决定 intrinsic；没有默认 case、result 初值、新断言或额外存储。

```cpp
        qlog_forceinline bool compare_exchange_weak(value_type& expected, value_type desired, const memory_order success_order = memory_order::seq_cst, const memory_order fail_order = memory_order::seq_cst) noexcept \
        { \
            api_type result; \
            QLOG_ATOMIC_CHOOSE_INTRINSIC(success_order, result, _INTRIN_BIT_SUFFIX(_InterlockedCompareExchange, SIZE_BYTE), get_atomic_ptr(&value_), get_atomic_value<value_type>(desired), get_atomic_value<value_type>(expected)); \
            bool is_suc = ((value_type)result == expected); \
            if (!is_suc) { \
                expected = load(fail_order); \
            } \
            return is_suc; \
        } \
        qlog_forceinline bool compare_exchange_strong(value_type& expected, value_type desired, const memory_order success_order = memory_order::seq_cst, const memory_order fail_order = memory_order::seq_cst) noexcept \
        { \
            return compare_exchange_weak(expected, desired, success_order, fail_order); \
        } \
```

结果的旧值转回 value_type 与 expected 比较；失败再次 load(fail_order)，此后 expected 可能是后一次读取的值。GNU 同一次 CAS 写回的 expected 与此行为不同，保持实际参考。MSVC strong 调用 weak；该 weak 使用 interlocked CAS，没有为模拟其他平台弱 CAS 而人为制造伪失败。

### 11.3 公共转发

放入现有 atomic_trivially_constructible 的 public，依原顺序在 exchange_seq_cst 后；没有数据成员。

```cpp
qlog_forceinline bool compare_exchange_weak(value_type& expected, value_type desired, const memory_order success_order = memory_order::seq_cst, const memory_order fail_order = memory_order::seq_cst) noexcept
{
    return base_type::compare_exchange_weak(expected, desired, success_order, fail_order);
}

qlog_forceinline bool compare_exchange_strong(value_type& expected, value_type desired, const memory_order success_order = memory_order::seq_cst, const memory_order fail_order = memory_order::seq_cst) noexcept
{
    return base_type::compare_exchange_strong(expected, desired, success_order, fail_order);
}
```

### 11.4 本组技术点与验证边界

expected 是失败时需要更新的观察值，所以按引用；desired 是拟写入值，所以按值。成功 CAS 是读改写，失败 CAS 不写 desired，因此成功/失败序有两个入口。弱 CAS 在支持弱指令的后端允许相等时伪失败，循环中通常重试；强 CAS 不允许伪失败，GNU 通过 bool=false 表达，MSVC 原实现共享同一 intrinsic。失败更新 expected 可以帮助下一次重试适应新状态，但原 MSVC 再读取不保证它是失败 CAS 时的精确旧值。

原子 CAS 不自动解决 ABA 或对象回收问题；本组不加版本字段、Manager或其他算法。模板代码需要实际类型实例化才能验证相关函数体；仅编译根库不能替代原子调用方检查。候选已完成两平台两配置目标文件编译，原生MSVC仅排除既有 C4324；本轮无运行、发布/获取或并发验收。之后仍依次接算术/位运算、运算符、普通 atomic、最后否定 trait与实际自旋锁。
