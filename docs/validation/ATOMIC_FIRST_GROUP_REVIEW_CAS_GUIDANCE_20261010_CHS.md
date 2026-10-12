# 原子第一组实际代码审查与 CAS 接续

> **2026-10-10 算术实施后的补记（当前状态）：** 三个原子头各70个算术/位运算方法与参考逐函数核对，用户GNU add_fetch五个分支保留，修正末尾多余右括号；第一组/CAS及全部CR保留。只增加测试源与测试注册，根CMake不变，151个无关生产/测试/CMake文件SHA-256不变。
>
> 实际Linux GCC与原生Windows MSVC Debug/Release根库、链接调用方和CTest各15/15（常驻12＋外部3）通过；新算术各9858项与实际BQLog头对照一致，覆盖8整数类型的全部70入口、返回值/存储值、有符号正常减量、无符号回绕/高位及4线程各4000次旧/新票号；第一组/CAS各1518检查也与参考一致。GCC Werror及原警告检查通过；MSVC外部严格检查排除原C4324和参考-val的C4146，生产警告不改。首次Windows外部参数覆盖漏带EHsc引发测试C4530，已恢复EHsc并重跑，初次日志保留。未验证有符号溢出、ARM/32位或完整Ring/MISO并发、恢复、性能。
>
> 决策：本组采用已确认的原参考实现和边界，不增加防御机制/生产类/断言/默认参数；GNU CAS合同边界与MSVC再次load规则继续保留。接续运算符＋普通atomic＋第七个trait，再普通自旋锁和真实存储依赖。详细指导仅在对话；证据：[实际算术实施目录](/mnt/d/qq344/Documents/qlog-atomic-arithmetic-implementation-20261010-be0615)。下方记录属于前阶段快照。

> **2026-10-10 授权修正后的补记（当前状态）：** 用户授权助手修正并补齐本次未完成部分。已修正 GNU 拼写、load_acq_rel 的 SEQ_CST、exchange 命名及 CAS 语法；MSVC 恢复原宏续行、CONCATX 与 intrinsic 名称，CAS 归回原特化宏；公共 generic exchange 已补齐。后端改为参考确认的 `_inner_atomic_GNU_C.h` / `_inner_atomic_msvc.h`。不填下一组生产算术函数，不改根 CMake；六个 trait 与所有 CR 保留，152 个无关生产/测试/CMake 文件 SHA-256 不变。
>
> 实际 Linux GCC、原生 Windows MSVC 的 Debug/Release 根库和链接调用方均通过，各配置外部 CTest 2/2、QLog 定向 1518 项与实际 BQLog 输出相同，含一次发布检查；PUBLIC QLOG_DEBUG 传播已检查。GCC 使用 Werror；实际 MSVC 严格审查仅排除既有 C4324。外部算术指导候选各配置 1760 项与参考一致；其 MSVC 审查另外排除原 -val 触发的 C4146，不修改生产警告配置。候选结果不计未写入生产的算术验收，未做完整并发、sanitizer、ARM/32位、恢复或性能验收。
>
> 决策：GNU CAS 原 RELEASE/ACQUIRE 映射及 MSVC 失败后再次 load 保留；后续三组详细指导只放对话，文件记录决定。证据：[本轮审查目录](/mnt/d/qq344/Documents/qlog-atomic-repair-arithmetic-20261010-958bc1)。以下第1–6节是修正前的原审查记录，段内“未改源码/实际失败”只描述当时快照。

日期：2026-10-10。按实际源码审查，未填入新生产函数，也未修改生产函数、文件名、CR、测试或 CMake。

## 1. 工作树与参考

QLog：/home/qq344/QLog，feat/spsc-ring-opt@f24f3afcd8e9418ea48ad5a6b05680c622008145。工作树含既有修改和未跟踪平台草稿，未清理。

BQLog：/home/qq344/BqLog，main@78cbfbef4f87e7558335f0c04bf6dd6894b46451。MISO 和构建等本地修改仍在；本轮三原子头、platform/macros.h 和 types/type_traits.h 相对 HEAD 没有差异，不称整树干净。

当前三个实际文件是 include/qlog/platform/atomic.hpp、include/qlog/platform/atomic/_inner_atomic_GNU_c.hpp 和 include/qlog/platform/atomic/_inner_atomic_msvc.hpp。公共文件只有编译器选择、forceinline、memory_order 和后端包含；atomic_trivially_constructible 尚未填写。文件名不会导致当前 include 找不到文件，因为当前两处引用与草稿一致。按用户前轮决定，后端最终名称仍是 _inner_atomic_GNU_C.h 和 _inner_atomic_msvc.h；公共入口保持 atomic.hpp。

## 2. 已写后端的问题

GNU：

| 实际位置 | 问题 | 对应修正 |
| --- | --- | --- |
| :18 | gun 拼写 | _atomic_gnu_c_standard_type<2> |
| :55 | stadard 拼写 | _atomic_gnu_c_standard_type |
| :61-65 | 只有 To 为指针的转换重载 | 补参考 :82-86 非指针 static_cast 重载 |
| :83 | decay<T> : type | decay<T>::type |
| :93 | _atomic_store_n 少下划线 | __atomic_store_n |
| :126-129 | load_acq_rel 使用 RELEASE | 按参考使用 SEQ_CST |
| :145 | __atomic_store_N | __atomic_store_n |
| :157 | value_typee | value_type |
| :169-187 | memory_roder、memory_otder、gte 拼写及 acquire 分支缺失 | 修正拼写，补对应 ACQUIRE 分支 |
| :195 | 两个 exchange_release，其中第一个使用 ACQUIRE | 第一个改为 exchange_acquire |
| :217 | __atomic_excahnge_n | __atomic_exchange_n |

MSVC：缺少原有 QLOG_ATOMIC_CONCATX；三个固定序调用遗漏 _QLOG_INTRIN 的前导下划线。选择宏 :45-61 没有续行，case 进入命名空间正文，是本机编译的首要阻塞原因；此外有 memory_oder、QLOG_ATOMIC_RELEASE、_VA_ARGS__ 拼写错误。宏结束的 } 不是 namespace 的结束，原 :61 namespace 注释位置不正确。选择宏保留参考五个 case、无 default，不新增 result 初值或 assert。

已写 MSVC 主模板、四个尺寸映射、value_ 存储、特化宏的第一组方法、原 6001/4701 warning 区域与参考方向一致；GNU 原 union、原尺寸断言及现有 raw 方法也保留。不能用修正副本通过推断实际原子模块已通过。

## 3. 实际检查与候选检查

| 检查对象 | Linux GCC Debug / Release | 原生 Windows MSVC Debug / Release |
| --- | --- | --- |
| 实际 atomic.hpp 直接包含 | 均失败，退出码各 1 | 均失败，退出码各 2 |
| 独立修正副本及补齐公共第一组 | 模板实例化/目标文件编译通过 | 模板实例化/目标文件编译通过 |
| 同一候选接续 weak/strong CAS | 模板实例化/目标文件编译通过 | 模板实例化/目标文件编译通过 |

GCC 使用 C++20、Wall/Wextra/Wpedantic/Werror；实际检查为 fsyntax-only，候选检查为 -c。MSVC 使用 C++20、W4/WX/permissive-/utf-8/EHsc，仅审查命令排除既有对齐提示 C4324，源文件原 6001/4701 抑制保持；实际头检查使用 Zs，候选使用 /c。Debug 配置定义 QLOG_DEBUG，Release 定义 NDEBUG。没有更改调用方生产宏或根警告配置。

候选第一组实例化 bool、uint8_t/uint16_t/uint32_t/uint64_t、int32_t、32位枚举与指针的19个 load/store/exchange 方法；检查既有默认参数、const/noexcept、六个参考平凡 trait。CAS 检查同八类类型、双默认值及 relaxed 失败序的五类成功序。

本轮没有运行候选可执行程序或任何实际原子行为、发布/获取、并发、sanitizer、性能、ARM/32位测试，也未重新运行根库/CTest。此前 637 检查仅属前轮独立候选，不能算本轮实际源码验收；旧 633/20000 更不能复用。

候选最初摘录 MSVC CAS 时遗漏参考 :234 的 strong 结束括号，已在独立目录补齐并重新编译通过；首次失败日志也保留。这不是 QLog 用户源码的额外缺陷。

## 4. CR 回答与填写顺序

扩展名 h/hpp 是项目约定，模板可见性由包含定义保证；本组按已经确认的参考名称整理后端。enable_if_t<B,R> 在 B 为 true 时表示 R，否则没有对应 type，在模板直接替换上下文中排除该重载；指针与非指针 To 分别选择 reinterpret_cast 与 static_cast。两个 GNU static_assert 是编译期尺寸与支持宽度约束，所有构建配置均检查，不是运行时 assert。api_type 是后端自定义的原生整数 API 类型，不是 C++ 关键字；GNU 按1/2/4/8字节选无符号整数，MSVC 则匹配 char/short/long/__int64。__ATOMIC_SEQ_CST 等是 GNU/Clang 原子接口的预定义常量，与 QLOG_DEBUG 无关。

按依赖先修复 GNU 类型、帮助函数和三个方法族，再修 MSVC 宏，再补公共 atomic_trivially_constructible 的19个转发与六个参考 trait。不为它提供用户构造/析构或字段初值；保留后端 protected、公有转发、load 双层默认 seq_cst，store/exchange 均无默认序。普通 atomic 尚未填写，所以第七个否定 trait 留到该类完成。模板不建空 cpp，也不加根 CMake 源项。

第一组完整正文仍见 [原子指导第9节](../decisions/PLATFORM_ATOMIC_PRIMITIVES_IMPLEMENTATION_GUIDE_20261009_CHS.md)。CAS 放入原类/宏，按原文件顺序紧接 exchange_seq_cst；不是新包装类。

## 5. CAS 已确认的参考边界

用户本轮明确选择：保留 BQLog 映射，明确记录 GCC 合同边界。

GNU 源 :242-247 与 :281-286 在 success=release 且 fail!=relaxed 时传 RELEASE/ACQUIRE。[GCC 官方文档](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html)规定失败序不能是 RELEASE/ACQ_REL，也不能强于成功序。保留源码映射不代表这条分支符合编译器文档合同；不静默改为 relaxed，不添加 assert，不改双默认值。本轮实际 BQLog 参数化 CAS 的 O0/O2 语法及目标文件编译都通过；编译接受并不解决文档合同边界，也不是并发行为证明。

MSVC 源 :221-234 在失败后再次 load(fail_order) 更新 expected，strong 委托 weak；GNU 由同一次 CAS 写回 expected。MSVC 的后一次读取可能看到再次变化的值，不保证与失败 CAS 返回的 result 相同。不改为 expected=(value_type)result，不将两个后端说成完全相同的失败观察行为。

weak/strong 默认 success_order 和 fail_order 都是 seq_cst，公共/GNU/MSVC三处保持；两者返回 bool，desired 按值，expected 按引用，成功写 desired，失败不写 desired。完整接续见指导新增第11节。后续依次为算术/位运算、原运算符、普通 atomic、最终第七个 trait、自旋锁，再接诊断/共享状态与路径剩余依赖；不跳至 MISO/Worker/Appender。

## 6. 证据与保护

证据目录：[D:/qq344/Documents/qlog-atomic-review-20261010-b39f16](/mnt/d/qq344/Documents/qlog-atomic-review-20261010-b39f16)。实际三头快照在 actual-before，指导副本在 candidate/include；源清单、编译日志、GNU/MSVC/public CAS 完整片段均保留。

原有155个生产/测试/CMake文件逐一 SHA-256 保持；三原子草稿及全部 CR 均未修改。只维护 AGENTS、原子指导、路线入口及本审查记录。整数越界 double/原字符串两方案仍仅记录、暂不选定。
