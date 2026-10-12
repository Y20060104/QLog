# 线程基础实际审查与原子基础指导验证

> **2026-10-09 完整第一组指导及新候选检查（本段最新）：** 按严格对齐决定给出公共/GNU/MSVC 三文件完整正文，包含原存储、帮助函数、explicit_copy、load/store/exchange 与所有已有 raw/固定序变体。Linux GCC/原生 Windows MSVC 的 Debug/Release 各与实际 BQLog 独立参考完成 637 项定向检查，输出一致；选定参考方法/宏在仅前缀、标准基础、格式和预处理续行适配后内容一致。当前六个参考平凡断言保留，普通 atomic 及其否定断言随后续真实实现接续；不增加包装类、默认值、字段或 assert。只是新指导候选，未填入生产原子模块，未重跑根库/CTest或完整并发/性能，旧 633/20000 候选仍被替代。MSVC /WX 仅排除原对齐 C4324，保留参考宏 warning 区域，BQLog 作为 SYSTEM 包含。详见 [完整第一组正文](../decisions/PLATFORM_ATOMIC_PRIMITIVES_IMPLEMENTATION_GUIDE_20261009_CHS.md) 和 D:/qq344/Documents/qlog-atomic-group1-complete-guide-20261009-3fa2b7。初始153个生产include/src/tests/rootCMake文件哈希不变；末尾观察到用户新增的两个空后端草稿，已保留且没有搬移/覆盖，实际当前155个文件。用户最新明确指导不沿用这两个名字，使用BQLog后端原名。核对记录见该目录。

> **2026-10-09 最新修订：原子结构和默认声明严格对齐。** 用户不接受新增类/封装、默认参数或断言，要求逐项与 BQLog 对应声明一致。以下“下一组候选”描述的 Pascal 命名、单一 mutable 原生槽和 633/20000 检查是此前已被替代的候选快照，不作为当前填写依据或严格原子验收。实际线程生产修正及 Linux/Windows 两配置 CTest 各 12/12 的证据仍有效。

## 本次原子指导纠正的检查范围

已重新核对 QLog feat/spsc-ring-opt@f24f3afc、BQLog@78cbfbef 及工作树；两树有既有修改。BQLog 三个原子头、platform/macros.h 和 types/type_traits.h 的 HEAD 差异为空。逐行抽取 114/97/93 条带强制内联的方法头或构造声明，以及 9/2/1 处源文件 static_assert（MSVC 宏的一个断言随四个特化展开）；这三个文件没有运行时 assert。

完整指导现保留 _atomic_base<T,N>、atomic_trivially_constructible、atomic 的实际名称/归属，GNU 原 union 和 MSVC 原存储/宏/四特化，及公共和后端的所有原默认参数。不增加新类/字段/默认回退/断言。严格版仅做文档及声明来源检查，没有编译运行新原子实现；QLog 的 atomic.hpp 当前仍是用户部分草稿，未替用户填写。宏和 trait 的前缀/标准基础适配不改变原子结构合同；参考 union、const load 的 RMW、CAS 边界均在指导中明示。

本次仅更新 AGENTS、原子指导、两份路线入口及本验证文档。生产 include/src/tests/rootCMake 的前后 SHA-256 核对另附本次证据目录；没有新增 cpp、CMake target、生产函数或测试。此前候选数据不迁移为严格版本成绩，ARM/32 位、完整原子行为/并发/性能继续未验。

证据目录：D:/qq344/Documents/qlog-atomic-contract-correction-20261009-a42e8d；包含 production-before/after、reference inventory、Git 状态/HEAD、旧文档备份及 scope-verification。当前依据为 [修订后的严格对齐指导](../decisions/PLATFORM_ATOMIC_PRIMITIVES_IMPLEMENTATION_GUIDE_20261009_CHS.md)。以下原审查正文保留为历史记录。

日期：2026-10-09。任务：提供下一步完整实现指导。

## 实际状态与修改范围

QLog feat/spsc-ring-opt@f24f3afcd8e9418ea48ad5a6b05680c622008145；BQLog main@78cbfbef4f87e7558335f0c04bf6dd6894b46451。两树已有草稿/修改；线程、原子、自旋及日志API相关参考相对HEAD无差异，不能称参考整树干净。

用户已写公共线程头/POSIX cpp/Windows cpp并接根CMake。POSIX按参考规则实现，无本轮阻塞问题。Windows文件名thred_ops.cpp与target指定thread_ops.cpp不符；defined(_ < SC_VER)语法错误，defined(_MSC_VER_)错误选择GNU分支，__mm_pause/__yiled应为_mm_pause/__yield。助手只同步文件名和四处拼写，CR保持。

审查期间出现用户对Windows路径内部头和cpp include/格式的重组：内部头从src移动到include/qlog/platform/windows，实际引用对应，函数算法不变。助手保留、记录并重新确认最新Windows库编译，没有移动回旧位置。初始152文件快照中，除上述3个源路径变化外，其余149个include/src/tests/rootCMake原文件SHA-256不变。本轮根CMake未改，当前每个平台17cpp；没有写入生产原子函数。

## 实际验证

Linux及原生Windows实际根库、公开线程调用方Debug/Release严格构建链接，CTest各12/12（常驻11+临时线程1）。临时调用方12项线程身份检查及四个操作完成，使用的头/库来自QLog实际源码；输出candidate字样沿用了旧探针标签，不代表替代函数体。两系统四配置compile_commands各17生产cpp，Debug-only PUBLIC QLOG_DEBUG传播核对。

Windows路径重组后再次构建最新实际库并运行原子候选，两配置通过；没有把此前路径1071项重新计为本轮运行。Windows/WX仅排除既有C4324对齐提示，根生产配置未改。线程EINTR注入/APC、ARM、巨大时长等未验。

## CR回答

- 匿名namespace：BQLog有使用，如src/bq_log/api/bq_log_go.cpp:19，但实际thread两个文件的TLS缓存使用static。两种做法都可实现文件局部可见性；当前缓存/转换模板只属于各系统cpp，不把所有私有实现集中到一个集合。真正被多个文件复用时再提取具名模块。
- nanosleep while：0成功离开循环，-1失败进入；仅EINTR用rem继续，其他错误结束。errno由<cerrno>提供访问、系统API在失败时设置，当前平台每线程维护；只在失败时判断，不需像数值转换那样先清零。
- req=rem：timespec普通结构体赋值，复制tv_sec和tv_nsec两个字段。两个栈对象继续独立存在，只更新下次等待请求，没有搬移内存地址。
- 单增tid：当前BQLog Windows取GetCurrentThreadId、POSIX取pthread_self；src/bq_log/api/bq_log_api.cpp:208将此原生ID写入线程元数据。thread_check_enable_counter是检查开关计数，不是线程ID生成器；没有用记忆替换实际取ID规则。

## 下一组候选

AtomicTriviallyConstructible的load/store/exchange基础组，保留平凡属性、8字节对齐及原生GNU/MSVC路径。MSVC load真实为ExchangeAdd(0)。只存同宽原生API槽，mutable表达const load会RMW，不复制GNU双成员union/raw访问和MSVC跨类型const_cast指针路径。正常值/序/尺寸/对齐与参考头一致，没有新错误协议；没有宣称完整atomic接口已实现。

候选Linux GCC与原生Windows MSVC各Debug/Release通过633检查及四线程20000受保护更新，直接对比实际BQLog atomic头。仅候选与定向行为，不计生产原子/自旋锁、完整并发/恢复/性能；ARM/32位及sanitizer未验。

证据：D:\qq344\Documents\qlog-thread-atomic-guide-20261009-92a7c1，production-before/after、scope-verification、reference、actual-linux-results、两个系统编译/CTest日志、atomic候选和对照日志、compile_commands。完整下一组见[原子基础指导](../decisions/PLATFORM_ATOMIC_PRIMITIVES_IMPLEMENTATION_GUIDE_20261009_CHS.md)。整数越界double/原字符串继续仅记录暂不选定。
