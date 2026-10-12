# 全局生命周期基础与 mutex 依赖接续决定

日期：2026-10-10。详细实现指导留在对话，本文只记录决定与验证边界。

> **2026-10-10 本次实际复核（最新，替代下方进度快照）：** spin_lock 的原宏、relaxed 等待、类分号及 scoped_try_spin_lock 已补齐；实际头和 global_vars.cpp 在 GNU/MSVC Debug/Release 各定向编译通过。仍有三类遗漏：平凡 atomic 两个后置副本仍为引用、前置 -- 多余 return；mutex 的 scopred_mutex 拼写和移动赋值少一个 &；global_vars_base::get 遗漏 aligned_alloc，第一次构造将对空指针 placement new。全局 get 单独实例化也能编译，不能据此接受初始化行为。
>
> 只在外部副本修正这些遗漏，四配置编译通过；实际生产函数、CR、测试和 CMake 的 160 个文件哈希不变。没有运行测试或链接验证，根库仍为17个cpp；global_vars.cpp 暂未接入，修正并复核后再接。证据：D:/qq344/Documents/qlog-global-review-thread-guide-20261010-af0a5f。
>
> **下一组依赖决定：** mutex 的诊断需要当前线程名。POSIX 线程名可独立实现；Windows 线程名经 get_windows_version_info 依赖尚未闭合的 common_global_vars::win_api_mutex_。先完成原 POSIX get_thread_name_impl 的四分支/三项 SFINAE 与 get_current_thread_name，以及 Windows 原 get_process_adress/get_sys_api 查询基础；不提供临时 Windows 线程名或诊断替身。随后完整声明 common_global_vars 的真实组成、闭合 Windows 线程名/诊断/mutex，再返回文件存储依赖链。get_windows_version_info 和 Windows 线程名本次未指导实现、未记完成。
>
> POSIX 定义追加到现有 src/platform/posix/thread_ops.cpp，公共 get_current_thread_name 声明放 include/qlog/platform/thread_ops.hpp。Windows 查询放 include/qlog/platform/windows/system_api.hpp 与 src/platform/windows/system_api.cpp，namespace qlog::platform；保留参考 adress 拼写、参数、无默认参数、4191 pragma 以及 GetModuleHandleA/GetProcAddress 调用，不加入 LoadLibrary/FreeLibrary/缓存/新断言或包装类。普通函数在 cpp 定义，模板留在头中。未实现的文件本次不接 CMake。
>
> POSIX 分支优先级、宏选择和失败返回空串按原源码；保留 QLOG_HAVE_DLFCN 的参考宏含义（探测 sys/prctl.h）、三个原 trait、四个互斥重载和原 prctl 当前线程断言。标准库 trait/string 与编译器系统宏仅作工程适配。Linux 当前环境实际选 prctl 分支，调用失败不再尝试下一分支，三个 API 都不可用才用 pthread_ 数字名称。外部完整 POSIX 候选 GNU 两配置及 Windows 查询候选 MSVC 两配置仅编译通过；没有覆盖 Apple/BSD 或运行期名称/API 获取，不计线程名/锁/退出/并发/MISO/性能验收。MSVC 原子相关外部检查仅排除已记录的 C4324/C4146；新查询代码使用原4191 pragma，无该两项额外排除。

## 源码依据

QLog：feat/spsc-ring-opt@f24f3afcd8e9418ea48ad5a6b05680c622008145。BQLog：main@78cbfbef4f87e7558335f0c04bf6dd6894b46451。两个工作树都有既有修改；本次核对的 common_vars.h/.cpp、mutex.h、mutex_posix.cpp、mutex_win64.cpp、thread_posix.cpp、thread_win64.cpp 与 util.cpp 相对参考 HEAD 无差异。

- [全局生命周期模板](/home/qq344/BqLog/src/bq_common/global/common_vars.h:22)。
- [析构登记及逆序退出](/home/qq344/BqLog/src/bq_common/global/common_vars.cpp:134)。
- [mutex 公共声明](/home/qq344/BqLog/src/bq_common/platform/thread/mutex.h:26)。
- [mutex 的 POSIX 错误分支](/home/qq344/BqLog/src/bq_common/platform/thread/mutex_posix.cpp:67)。
- [诊断输出取得全局 console_mutex](/home/qq344/BqLog/src/bq_common/utils/util.cpp:364)。

## 接续决定

实际 atomic 普通类型和第七个 trait 已补，但平凡类型运算符还有返回类型、副本引用和多余 return；spin_lock 还有单元测试宏拼写、等待 load 序、类分号及 scoped_try_spin_lock 遗漏，尚未收口。

mutex 错误分支依赖线程名和诊断输出；诊断输出又依赖 common_global_vars 的 mutex。先修正 atomic/spin_lock，建立参考 mutex 的完整公共声明，再完整填写不依赖 mutex/诊断的全局生命周期基础。随后以完整声明闭合共享状态、线程名、诊断和 mutex 定义，继续文件句柄/管理 → MemoryMap → NormalBuffer → MISO；不添加临时诊断替身或删去原错误分支。

目标文件：

- /home/qq344/QLog/include/qlog/platform/mutex.hpp：qlog::platform::mutex/scoped_mutex 完整声明，普通成员定义后续进入系统 cpp。
- /home/qq344/QLog/include/qlog/global/global_vars.hpp：namespace qlog；原 _global_vars_priority_var_initializer、global_var_destructiable、global_vars_destructor、global_vars_base 及静态成员定义。
- /home/qq344/QLog/src/global/global_vars.cpp：登记表、零初始化锁、登记及析构函数。

保留参考拼写、成员名、访问权限、默认模板参数及状态 0/1/2。沿用 QLog 标准库基础：BQ array 指针对应 std::vector 指针，BQ placement-new 枚举入口对应标准 placement new，BQ TLS 对应 thread_local；不另造 Manager 或新拥有包装。初始化 CAS 的 release/acquire 参数仍按已确认决定保留，GNU 映射合同边界继续有效。

mutex 默认构造可重入；bool 构造无默认参数、无 explicit。Windows reentrant=false 的重复获取在参考中只诊断且仍成功；unlock 无所有者验证并返回 true。公共注释不能解释为两系统均能可靠拒绝错误解锁。scoped_mutex 的隐式复制边界保留，不额外添加参考没有的删除声明。

## 生命周期边界

每个模板特化共享一份对象存储、初始化标志和初始化者标记，各线程仅缓存对象指针。优先对象先初始化；登记成功后才 release 发布状态 2；退出按登记逆序调用 partial_destruct，保留对象壳，不调用完整 T 析构或 aligned_free。

不为参考增加 OOM/异常回滚、超时、重试或新断言。分配失败没有安全回退；构造/登记抛异常可能留下状态 1。递归 get 只允许按参考取得正在构造的对象，不能据此保证成员已构造或允许循环优先依赖。退出钩子执行期间仍持有登记表自旋锁，不保证可以再登记新对象。现有 GNU union/CAS 与标准 assert/NDEBUG 边界不变。

## 验证边界

实际 atomic/spin_lock 的 GNU/MSVC Debug/Release 定向编译仍失败。只在临时目录修正这些遗漏，再编译全局生命周期头/cpp、mutex 声明与实际 BQLog 头中的模板实例化，四种配置均通过；MSVC 外部审查仍仅排除既有 C4324/C4146，不修改生产 warning 配置。

没有写入新生产函数、未改 CMake、没有链接或运行测试；不计作实际锁、退出、线程安全、MISO 或性能验收。当前实际源码、测试与 CMake 的 157 个文件哈希未变，CR 保留。证据目录：D:/qq344/Documents/qlog-global-foundation-guide-20261010-792fbf。

填写并审查通过后，global_vars.cpp 加入共用 cpp 列表；模板头不建空 cpp。mutex 两个系统 cpp 完成后按 WIN32/else 二选一接入。本轮不提前登记未实现源码。
