# common_global_vars 完整声明接续决定

日期：2026-10-11。详细指导留在对话，本文只记录技术决定和验证边界。

## 2026-10-12 实际基础接库与FileManager主线（最新）

最后一组已由用户填写并实际接CMake，Linux27/Windows28cpp。两mutex局部拼写/原store_seq_cst已同步，CR保留；实际GNU/原生MSVC Debug/Release严格构建和基础调用方链接、现有CTest各12/12通过，PUBLIC宏核对，MSVC仅外部原C4324/C4146排除。新模块沿用既有file_handle/file_manager声明与6实现；剩余47定义由用户写，完整外部候选四配置仅编译/链接通过，未运行，不算FileManager行为/并发/退出/MISO验收。参考赋值、copy循环及路径替换边界明确记录；详细指导仅留对话。见[FileManager决定与证据](FILE_MANAGER_ALIGNMENT_20261012_CHS.md)，后续MemoryMap→NormalBuffer→MISO，不再补基础组。

## 2026-10-12 完整指导重新核对（本段最新）

用户要求根据最新草稿给齐本轮指导，不再以规则表代替缺失函数体。重新读取当前源码与实际BQLog：Windows线程名、common_global_vars构造及原空partial_destruct已经填写；两能力helper仍仅声明，两个mutex只有内部结构，两native file_io九项仍未定义。已写hash、路径/UTF转换、基准目录、Windows版本、std::hash及FileManager三启动函数继续复用，不作为新任务。

助手按既有修正授权仅改3个已写生产文件：Windows线程名修正get_thread_desc_func_、current_thread_handle、WideCharToMultiByte拼写；common_vars.cpp移除重复的第二个默认提前入口并给AVX2前置声明加原架构条件；Windows mutex修正CRITICAL_SECTION并补现有结构所需头。构造/partial及CR保持，未代写缺失生产函数。174个生产/测试/CMake文件中其余171个SHA-256不变，根CMake仍20cpp。

本轮指导在对话中给齐5个cpp的全部剩余函数体、必要包含项和插入位置：common两能力helper、两mutex各7成员、两native file_io各9公开函数及BQ原holder/递归/独占helper。保持既有模块拆分与参考参数名，bool构造不加默认参数；Windows owner原store_seq_cst/exchange_relaxed/release及参考注释掉的断言保留。独占表仍只有mode，无引用计数、关闭直接erase；Windows force_to_abs_path仍直接拼接基准目录，不额外补分隔符，make_dir_recursive原prefix_size=2保留，不误称长路径文字前缀只有两个字符。既有Windows失败/泄漏/普通首次API状态严格对齐决定继续有效。原native open的BQ_UNIT_TEST故障注入依赖尚未引入，按本组既有范围不搬该测试设施；能力诊断的QLOG_UNIT_TEST条件保留。ARM代码仅静态核对，不记目标验收。

仓库外把这5份完整候选合入当前cpp，保留其已写函数/CR后组库：Linux/GCC及原生MSVC x64 Debug/Release四配置均严格编译、公开调用方链接通过，编译清单分别27/28个库源及1个调用方，PUBLIC QLOG_DEBUG只在Debug出现；MSVC仅外部排除既有C4324/C4146。实际Windows线程名与mutex结构两cpp在两配置也严格编译通过，但实际common cpp仍缺helper定义，根生产依赖尚未闭合。没有运行候选/CTest、新增生产测试或改根CMake，不计存储行为/锁并发/首次初始化退出/MISO/性能。证据与备份：D:/qq344/Documents/qlog-storage-complete-guide-20261012-022327。

用户填完本组后下一轮同轮审查修正、根CMake接入，直接进入FileHandle/FileManager的描述符、引用管理和读写，再MemoryMap→NormalBuffer→MISO，不再提供零散基础组。预计新注册公共6cpp、POSIX mutex_posix.cpp或Windows mutex_win64.cpp/system_api.cpp，两native原file_io仍沿用现有注册。

## 2026-10-12 最后一组存储基础复核与实际修正（最新）

用户要求先修复已写问题，再一次给齐最后一组存储基础指导；新函数仍由用户填写。重新核对QLog feat/spsc-ring-opt@f24f3afc、BQLog main@78cbfbef；两树既有未提交修改保留，本组14个采用参考文件相对HEAD无差异，不称BQ整树干净。

实际修正7个已有生产文件：Windows基准目录构造误写在POSIX文件，连同原注释移回现有windows/file_path.cpp并修正bse_dir；POSIX词法函数补return，按当前BQ POSIX原函数保留反斜杠，不采用Windows的替换步骤；两native身份hash补cstring/hash.hpp；FileManager已写构造/instance/get_base_dir补必要头，不改mutex/seq_generator原成员；close_file声明返回纠正为int32_t。用户写在公共头的提前入口移到新增src/global/common_vars.cpp，并按原名default_initer_纠正拼写；新cpp仅包含已写入口，构造/能力检查/partial_destruct仍待用户填写。CR和其他注释保留，没有代写剩余函数。

172个原生产/测试/CMake文件中7个改变，其余165个SHA-256不变；新增移动入口cpp后173个。根CMake未修改，当前仍20cpp。已有std::hash适配、两个native身份hash函数体、FileManager三启动函数及Windows版本查询均按参考复核，不再作为待重写任务。

实际GNU/Linux x64 Debug/Release相关9个cpp严格编译通过，实际根库两配置-Werror构建和现有CTest各12/12通过；实际原生MSVC x64两配置相关10个cpp严格编译通过。MSVC检查仅沿用已有C4324/C4146排除。实际代码与仓库外仅剩余定义组成后，Linux/GCC及原生MSVC四配置严格构建、公开调用方链接通过；外部初次POSIX摘录重复已有handle-size重载，已仅在外部移除并重链，两配置最终通过。候选程序未运行；没有新增/修改生产测试，不计文件I/O、mutex并发、首次初始化/退出、MISO或性能验收。根库独立公开基础调用仍待本组生产定义及CMake接入，不以候选链接冒充生产闭合。

最后一组完整指导留对话，文件只记决定：POSIX现有mutex_posix.cpp补剩余普通成员；Windows建立对应mutex_win64.cpp并补现有thread_ops中的原API初始化/get_current_thread_name；两native file_io补9个现有声明和原私有helper（含原posix_dir_stack_holder，不新增另一层拥有包装）；common_vars补两能力helper、原构造和原空partial_destruct。Windows基准目录/版本已实现，不重复补；此前_wgetcwd、版本结构、线程名缓冲和API普通首次状态的严格BQ边界继续有效，软件CRC保持已选BQ原表，不改回生成表。

用户填完后下一轮同轮审查修正、根CMake接入，并直接进入FileHandle/FileManager描述符、引用和读写管理，随后MemoryMap→NormalBuffer→MISO，不再拆零散基础指导。最终生产注册公共6个cpp、POSIX mutex_posix.cpp，Windows mutex_win64.cpp/system_api.cpp，预期Linux27cpp、Windows28cpp；实际native file_io/file_path/thread_ops已有注册，不重复添加。

证据与备份：D:/qq344/Documents/qlog-storage-final-foundation-20261012-a30266。下方为此前阶段快照。

## 2026-10-12 哈希复用迁移与 CRC 参数命名（最新）

用户要求复用此前已实现的hash核心，并由助手放到正确模块；接着要求hash/CRC内部参数名与BQLog对齐。本轮复用原src/format_hash_core.hpp、format_hash_software.cpp和format_hash_x86_crc32c.cpp，迁到src/utility/hash_core.hpp、hash_software.cpp、hash_x86_crc32c.cpp，namespace改为qlog::utility::hash_impl。原核心函数体经命名与std::uint32_t限定名适配后逐字一致；没有重新抄写整个宏核心或恢复旧FormatHashDispatch/HashResult/Record包装。新增include/qlog/utility/hash.hpp及src/utility/hash.cpp，提供非inline qlog::utility::get_hash_64(data,size)薄入口。

参数采用实际参考语义：软件/硬件u8/u16/u32/u64均为crc和v，内部原始hash/复制及模板核心为src/dst/len，短输入uint32长度混合量为len_mix；公开get_hash_64保留参考的data/size。名称同步不改变原内部函数参数顺序、类型、noexcept、已有静态断言或算法。硬件文件按参考补i386的两次u32处理，不向其他cpp传播SSE4.2要求。

差分核对纠正了最初“原软件表与当前参考完全一致”的判断：BQLog util.cpp:56的软件表索引206、207为0x6FD599E3、0x9DBE1AE0，而原QLog按0x82F63B78生成的是0x6FF599E3、0x9D9E1AE0。用户明确选择“采用 BQLog 原表，严格对齐”，因此以当前参考256项原表替换旧consteval生成表，保留软件后端其余实现。软件与硬件各自对照BQ对应路径，不设置跨后端相等的新合同；23字节全零可复现表值影响，1344组对照中参考两条路径有271组不同。BQLog参考源码没有修改。

运行时分派仍按BQLog simd.cpp:16取得common_global_vars::get().crc32_supported_的初始化快照，不另建独立CPUID状态/默认软件包装。原QLog声明但从无定义的x86_crc32c_available移除。BQ_GEN_HASH_CORE在hash-only的len>=32分支对dummy空dst计算dst_end；复用核心已有if constexpr(Copy)，保持不对空目标指针运算，不退回该参考表达式。对照使用原DO_COPY=true宏和有效src/dst，比较各后端数值和复制/边界，未执行大块dummy-null分支。旧cpuid_leaf0_probe仍未注册且引用早已删除的detail接口，本轮原样保留，不算当前测试。

根CMake加入hash.cpp、hash_software.cpp和hash_x86_crc32c.cpp，当前每个已验平台20cpp；GNU/Clang x86的-msse4.2仅设置在硬件cpp，库其余cpp和消费者没有该选项。MSVC补/utf-8到实际库和测试target，修正原本中文CR/源码按本地字符集误读造成的测试语法诊断；BQ源/测试构建也使用此选项。QLOG_DEBUG仍只在Debug PUBLIC定义，NDEBUG仍由构建配置控制。

本轮实际Linux/GCC x64、原生Windows/MSVC x64 Debug/Release严格构建与当前库回归各13/13通过（生产原有12项＋仓库外核心对照1项）；每配置1344组、9412检查通过，覆盖0/短输入/32字节边界/重叠尾块/未对齐输入、复制内容及两端guard。参数名修改后已重新编译和核对。公开get_hash_64与前轮仓库外完整存储基础候选组成后，四配置构建、链接及CTest各14/14通过，公开入口每配置1344组与参考匹配；这些待写基础定义没有搬进生产树。仅当前实际库的公开哈希调用方仍因common_global_vars构造、get_global_var_destructor及register_destructible_var未闭合而链接失败，已记录实际诊断，不称生产共享状态完成。MSVC外部严格审查仅沿用已有C4324/C4146排除。

额外MSVC x86-32定向检查：Debug的1344组通过；Release复制并哈希差分未通过，不计作已验平台。进一步采用BQ原macros.h、restrict和内联结构核对后仍有差异；16字节全零的QLog/BQ纯哈希均为0x55D8F07FFA54A237，而该配置复制返回不同。原因尚未闭合，不把它称为已定位的参考缺陷或静默改变生产算法。ARM硬件路径没有在本轮迁移/验证。以上均不是完整存储、退出/并发、MISO或性能验收。

迁移前168个生产/测试/CMake文件中，三份hash文件搬移、CMake改变，其余164个原文件SHA-256不变；新增公开头/薄入口后现170个文件，用户其他草稿和CR原样保留，生产测试文件未改。QLog f24f3afc和BQLog78cbfbef HEAD不变；采用的util/simd/common参考文件相对HEAD无差异，BQ整树既有MISO/构建本地修改仍保留。

后续指导删去“重新实现哈希核心”任务。native file_node_info::hash_code只按参考顺序打包字段并调用qlog::utility::get_hash_64，std::hash仍为薄适配；其余上一组基础定义继续由用户填写。全部依赖填写后同轮审查、接CMake并直接进入FileHandle/FileManager描述符/引用与读写，随后MemoryMap→NormalBuffer→MISO。其余定义按前轮约定合入实际native文件后，预期根库Linux27cpp、Windows28cpp，取代此前25/26的计划计数。详细指导在对话，文件只记录决定与验证边界。

证据与备份：D:/qq344/Documents/qlog-hash-utility-reuse-20261011-f6effa。2026-10-11末尾检查曾因自动审批服务额度错误中断，2026-10-12接续后已恢复并完成上述收口。

下方为此前阶段快照。

## 2026-10-11 最后一组存储基础依赖收口（最新）

用户要求下一份指导一次完成所有存储基础依赖，完成后的下一轮直接进入主线。主线入口固定为原FileHandle/FileManager描述符、引用和读写管理，随后MemoryMap→NormalBuffer→MISO。下一轮审查修正和根CMake集成与主线指导放在同一轮，不再把单个基础函数拆成新指导组。

本轮实际只修正src/platform/file_path.cpp的namespace platfrom、scoped_try_spin_lock未检查成功、setter锁对象/成员下划线/dir调用及参考诊断文本。该cpp原CR完整保留；POSIX构造已由用户写齐。实际公共/POSIX的GNU Debug/Release严格编译及公共cpp的原生MSVC两配置严格编译通过。168个生产/测试/CMake文件中仅该cpp改变，其余167个不变；根CMake仍17cpp，未写下一组新生产函数。

用户对Windows三项明确修正候选回复“严格对齐BQlog”。本轮据此保留参考_wgetcwd返回未检查/缓冲未释放、RtlGetVersion输入未清零及未填大小、GetThreadDescription成功缓冲未LocalFree。之前提出的失败保留空字符串、释放缓冲和结构初始化方案没有采纳；原先“尚未选定”的记录由此回复取代。不把这些参考边界称为安全；线程名称API首次初始化普通状态的并发边界也保持原结构并单列说明。

本组一次覆盖：utility身份哈希原CRC32C core/硬件与软件分派和非inline get_hash_64；native file_node_info.hash_code及std::hash薄适配；POSIX完整get_lexically_path及既已确认的HOME/account/~user边界；两系统get_file_size(path)、is_dir/is_regular_file、make_dir/remove_dir_or_file、open/close、修改时间/列举9项和原私有helper；Windows基准目录、版本和名称；两个native mutex的全部普通成员；common_global_vars构造、CPUID/CRC能力、原空partial_destruct与默认提前入口；FileManager原构造、instance和get_base_dir三个启动函数。保持原成员顺序/default/order/断言，不加防御包装、登记计数、另一套Manager或删减共享类。原UNIT_TEST故障注入服务不属于当前native存储生产依赖。

以根库加外部完整候选进行依赖核对，Linux/GCC及原生Windows/MSVC x64 Debug/Release均严格构建并链接真实公开调用方通过。初版候选摘录的括号/restrict宏、重复编译器宏和重复handle get_file_size已在外部副本修正并复核，最终不含重复定义；不归因生产或参考错误。MSVC外部仅沿用既有C4324/C4146排除，不改生产warning。未运行程序或CTest，未验证目录/I/O行为、BQ差分、首次并发/退出、故障、MISO或性能。该结果证明指导候选能闭合编译链接，不等于实际生产依赖组已完成。

参考边界随严格对齐保留并在对话说明：BQ_GEN_HASH_CORE的hash-only大块分支仍对dummy空dst计算dst_end，当前native身份缓冲为12/16字节不走该分支；Windows force_to_abs_path直接拼接基准目录且make_dir_recursive的prefix_size仍为2；exclusive登记只保存mode、不计数，close按身份erase。没有静默采用旧hash包装替代、修路径拼接/前缀或补登记计数，也没有据编译链接称这些行为/UB边界已验收。

填写后公共新增注册src/platform/file_path.cpp、file_defs.cpp、src/utility/diagnostics.cpp、hash.cpp、src/global/global_vars.cpp、common_vars.cpp、src/io/file_manager.cpp；native新增mutex.cpp，Windows另注册现有system_api.cpp。其余native平台新定义合入现有file_io/file_path/thread_ops，不重复登记原文件。预期根库Linux25cpp、Windows26cpp；本轮未修改根CMake。

QLog f24f3afc、BQLog78cbfbef HEAD不变；16个采用参考文件相对HEAD无差异，参考整树有既有MISO/构建修改。详细指导只在对话，证据与备份：D:/qq344/Documents/qlog-dependency-closeout-guide-20261011-c11999。

下方为此前阶段快照。

## 2026-10-11 诊断实际审查修正与基准目录接续（最新）

用户已填写diagnostics.hpp/.cpp并授权助手修正审查问题。实际修正前GNU/MSVC Debug/Release编译失败：格式化定义误写setter、plain_text调用遗漏console、条件setter误写返回LogLevel的自由函数、LQOG_UNIT_TEST宏拼写、Windows句柄判断抄写，以及POSIX指针初始化/作用域/括号和written变量不一致。助手已修正实际src/utility/diagnostics.cpp，补GitHub Actions info分支break、级别标记/空格及统一[QLog]前缀；保留用户统一的console_callback_/type_func_ptr_qlog_util_console_callback和written正规拼写，功能对应原consle/writen，不添加别名。诊断头保持用户稿，8条CR内容完全保留。

用户已修正file_handle移动赋值&&和is_valid；助手只补剩余open_mode参数及Windows get_windows_version_info声明拼写。共修改3个生产文件，未代写下一组新生产定义、未改测试或CMake。原printf式接口、共享buffer、递归mutex、翻倍扩容、每轮va_start/va_end成对、普通callback/minimum静态状态、native写出及原返回忽略均保留；不新增assert、atomic、默认参数、错误返回或短写重试。

修正后的实际诊断cpp与API/声明探针在GNU Linux和原生MSVC Windows x64 Debug/Release下，普通、QLOG_UNIT_TEST、QLOG_TOOLS、QLOG_IN_GITHUB_ACTIONS四分支均严格编译通过。API探针核对callback调用约定、五个适用诊断入口、is_valid、move赋值和Windows版本查询类型。MSVC外部检查沿用既有C4324/C4146排除，不改变生产warning。未运行任何程序/CTest，未链接完整诊断/common_global_vars/mutex，不计行为、并发、信号、退出、存储/MISO或性能验收；根库仍17cpp。

下一组为基准目录模块：include/qlog/platform/file_path.hpp已有完整声明/getter/CR不重建；src/platform/file_path.cpp放get_base_dir和两个setter，namespace qlog::platform中保持原静态spin_lock_zero_init lock_。native构造合入现有posix/windows/file_path.cpp，保留原is_absolute/词法路径等代码，不另建Manager。get_base_dir按0/1选择，非法类型warning并回退1，持锁返回std::string副本；setter持同一锁赋值，!defined(NDEBUG)下输出原info诊断，不改成QLOG_DEBUG，也不新加路径归一化或断言。

POSIX构造直接getcwd，初始1024字符，每次NULL增加1024后重试，再通过原两个setter写入；标准容器适配为vector.resize(size+1024)，保留参考未区分errno的循环边界。Linux/Windows正常路径两个槽初始都表示当前工作目录，不能解释成exe目录和cwd；两个槽独立，iOS/Android实际赋不同目录为结构来源，不在本组移植这些平台。

首次初始化顺序仍待共享状态闭合：原get_base_dir先持不可重入lock_再common_global_vars::get；若它触发第一次构造，native base_dir构造又经setter获取lock_，会重入。参考common_vars.cpp:38有提前初始化入口。当前不提前写该入口或通过空替身运行，等mutex/共有构造/FileManager必要定义完成后按参考一起接入；外部候选仅编译，不以编译通过声称首次调用安全。

Windows构造边界已向用户提出，尚未收到选择：win64_misc.cpp:92-93对_wgetcwd(nullptr,0)返回值未判空，且返回的CRT分配未释放。提出的修正方案是成功后释放缓冲、查询失败保留两个默认空字符串；它尚未成为已选合同。不复制该空指针使用，也不擅定新失败规则；本轮完整指导先覆盖公共/POSIX部分，Windows构造等待选择。

公共/POSIX基准目录完整外部候选GNU Debug/Release仅编译通过，公共部分MSVC Debug/Release仅编译通过；未写入新生产函数、未链接运行或接CMake。采用的12个BQ参考文件（诊断、基准目录、原共享状态/array/FileManager及移动平台目录槽来源）相对78cbfbef HEAD无差异，整参考树仍脏；QLog f24f3afc HEAD不变。167个生产/测试/CMake快照中仅上述3个改变，其余164个不变。证据和备份：D:/qq344/Documents/qlog-diagnostics-review-base-dir-guide-20261011-25c809。

详细CR答复和完整指导留在对话，本文只记录决定与验证边界。此后继续Windows版本/线程名 → mutex/共享构造 → FileManager引用与描述符/读写 → MemoryMap → NormalBuffer → MISO。

下方为此前快照，以本节为最新进度。

## 2026-10-11 纯路径实际复核与诊断接续（最新）

用户已填写src/io/file_manager.cpp三个纯路径函数；静态核对与此前确认的BQ规则一致。GNU/Linux和原生MSVC Windows Debug/Release下，实际共享头探针编译、实际三个函数及公开调用方编译链接均通过。此前std::hash命名空间、defined、Windows hash_code/minor_version、析构等抄写问题已由用户修正。仍须把file_handle移动赋值参数从&改为&&、is_invalid改回is_valid、open_file参数oepn_mode改为open_mode，并补get_windows_version_info的const引用声明；这些缺项不会全部表现为当前探针的编译错误，不能以编译通过代替原接口核对。新增std::replace CR的答复留对话，原CR保留。

下一组只补必要控制台诊断：include/qlog/utility/diagnostics.hpp、src/utility/diagnostics.cpp，namespace qlog::utility内采用参考已有util静态函数结构。公开log_device_console、log_device_console_plain_text、set_console_output_callback、_default_console_output，以及仅在QLOG_TOOLS或QLOG_UNIT_TEST下声明的set_log_device_console_min_level；使用现有qlog::LogLevel，保留consle_callback_拼写和Windows stdcall回调签名。callback和条件minimum保持原cpp内普通静态变量，不添加atomic、包装类、额外assert、默认参数或新错误返回。utility独立归属不变，两个现有utility头不堆入所有功能。

诊断保留BQLog的同一共享buffer/可重入console_mutex、按cast(vsnprintf返回)+1>=size判定并翻倍扩容、格式化后同步调用纯文本分派、回调优先及公开默认输出旁路。bq::array::fill_uninitialized(size)在参考中追加size个元素，std::vector适配为resize(size*2)，不可改为只reserve。项目级适配为现有LogLevel、标准容器/字符串函数与[QLog]前缀；普通库不擅自开启TOOLS/UNIT_TEST过滤，QLOG_DEBUG与NDEBUG原职责不变。

明确的参考合法性差异：util.cpp:367/370重复va_start同一args，372/382循环及外层重复va_end。指导候选按每轮vsnprintf各自va_start/va_end成对处理，保持原格式化/扩容方向，不增加负返回值、容量溢出或I/O失败的新策略。参考util.h:40的signal-safety/固定截断注释与加锁和动态扩容实现不一致，不把该注释当作已保证合同。回调/minimum原普通变量不提供并发修改同步；回调收到的共享buffer指针不得解释为独立拥有数据，递归调用可能覆盖或扩容失效。

当前原生输出范围为Windows与Linux/POSIX，系统调用分支保留在同一个utility cpp，保持原cpp局部共享状态。保留error/fatal到stderr、TTY/Windows VT判色、1024字节栈聚合及长消息分段写、MSVC OutputDebugStringA、GitHub Actions条件分支；原返回忽略/无短写重试，不增强为完整写出或持久化保证。Android/OHOS/iOS专用输出未移植，不计作能力。

本轮采用的11个BQ参考文件（util.h/.cpp、array_impl、macros、basic_types、platform_misc、mutex.h及两native cpp、FileManager.h/.cpp）相对78cbfbef HEAD无差异；整参考树仍脏。QLog f24f3afc HEAD不变；165个生产/测试/CMake文件SHA-256不变，未写新生产定义、未改CR、未添加常驻测试或接入CMake。详细完整指导留对话，文件仅记录决定。

外部完整诊断候选及原BQ诊断函数摘录在GNU/MSVC Debug/Release和普通/UNIT_TEST条件分支下仅编译通过；API探针核对回调类型及所有公开入口，MSVC外部审查只排除既有C4324/C4146，不改生产warning。未运行任何程序/CTest，也未链接完整common_global_vars/诊断/mutex；不计实际诊断行为、BQ运行差分、并发/信号/退出、文件存储/MISO或性能验收。证据与备份：D:/qq344/Documents/qlog-diagnostics-guide-20261011-5bf903。

完成本组后，按真实依赖继续基准目录 → Windows版本/线程名 → mutex及共享状态构造 → FileManager引用/描述符与读写管理 → MemoryMap → NormalBuffer → MISO。初次共享状态构造期间的诊断顺序与参考Windows资源/失败边界仍在对应函数阶段核对；不先补空实现或临时console替身来伪造链接通过。

下方为此前审查快照，以本节为最新状态。

## 2026-10-11 新声明审查与纯路径接续（最新）

重新核对：用户已创建common_global_vars和FileManager完整声明草稿。实际GNU/MSVC Debug/Release共享声明探针均编译失败，直接阻塞为std::hash特化错误地放在qlog::platform及deinfed拼写；FileHandle析构/两个赋值/is_valid、根枚举&、反向路径转换和宏、seek引用参数、Windows hash_code/minor_version/版本查询声明等仍有缺项或抄写错误。按原文件逐声明补齐，不增加另一套包装或防御性接口。

本组下一实现范围为namespace qlog中file_manager的三个public static纯路径函数：get_directory_from_path、get_file_name_from_path、combine_path，普通定义放src/io/file_manager.cpp。纯函数直接依赖现有utility::split_nonempty和标准字符串算法，为相对路径转换和文件创建提供基础；BQLog native基准目录构造本身使用getcwd/_wgetcwd读取当前工作目录，不依赖这三函数，不按文件管理头中旧“可执行文件路径”注释解释。

参考string_impl.h:323的find_last循环不检查索引0；file_manager.cpp:95、107分别查找两种分隔符并取min_ref，混用时取较早位置。按用户已经确认的严格BQ对齐保留该行为：std::string::find_last_of得到0时转为npos，再取min，不直接换成合并分隔符查找或max。来源、影响及标准库适配差异须在对话明确说明，不称根目录/混合分隔符规则已修复。split默认忽略空片段；combine仅归一反斜杠、拼接并按原path1前导/补根，不执行trim、点段折叠或右参数绝对路径覆盖。

12个采用参考文件（平台/global/FileManager/string/type_tools等）相对BQLog HEAD无差异，整树仍有MISO/构建等既有修改。QLog f24f3afc、BQLog 78cbfbef HEAD不变。本轮164个生产/测试/CMake文件SHA-256不变，用户草稿和CR未改；只准备外部修正候选，详细指南留对话。

外部完整修正声明与实际BQ头在GNU/MSVC Debug/Release仅编译通过；三函数完整候选及实际BQ函数原样摘录在四配置编译/公开调用方链接通过。参考探针最初误用std字符串empty接口并缺MSVC intrinsic声明，修正外部探针后通过，原失败日志保留，不归咎BQ函数。MSVC外部审查仅排除此前记录C4324/C4146，不改生产警告配置。

未运行任何程序/CTest，未链接完整common_global_vars，未接新生产cpp/CMake，不计生产路径行为/BQ运行差分/共享状态/锁/退出/MISO或性能验收。证据：D:/qq344/Documents/qlog-file-manager-path-guide-20261011-fceab0。本组由用户修声明并填写三个函数，下一轮审查后再决定实际构建接入；其后沿诊断 → 基准目录/Windows线程名/mutex → 共享构造/FileManager → MemoryMap → NormalBuffer → MISO继续。

下方为此前审查和收口快照，以本节为最新进度。

## 2026-10-11 上一组实际收口（最新）

本次用户明确授权助手完成上一组修正。重新读取实际源码时，atomic.hpp前置--已正确，POSIX名称代码已合入src/platform/posix/thread_ops.cpp，公开include下旧cpp已不存在；这些用户已完成的变化未重写。

助手仅修正合入代码仍存的QLOG_HAVE_PTHREad_NP、int32_T、第二trait分号、Value/stirng和assert分号，整理模板排版，把包含项/宏探测移到文件顶部、统一namespace。保留原四分支选择、函数签名、返回/断言规则及7条CR内容；Windows get_process_address和原子头保持当前实现。

实际Linux GNU及原生Windows MSVC Debug/Release库、已有原子运算符实例化调用方均编译链接通过；Linux两配置名称公开调用方从实际qlog库链接通过。运算符探针覆盖8种整数的平凡/普通原子全部8个运算符，并实例化普通原子的构造赋值及bool/指针/枚举支持。每平台17个库cpp和PUBLIC QLOG_DEBUG仅Debug传播已核对。根CMake原已注册POSIX thread_ops.cpp，不新增注册；生产/测试/CMake共162个原文件仅此cpp改变，其余161个SHA-256不变。

MSVC外部严格审查使用UTF-8、EHsc、W4/WX，只排除此前已记录的C4324/C4146；最初外部脚本缺少UTF-8/EHsc的诊断日志保留，补齐审查设置后通过，未修改生产警告配置。未运行程序或CTest，未新增常驻测试，不记线程名行为、其他POSIX系统分支、原子并发、退出、存储/MISO或性能验收。

本次证据和修改前备份：D:/qq344/Documents/qlog-operator-thread-closeout-20261011-ffdb71。两树HEAD不变，BQLog采用的atomic三个头及thread_posix.cpp相对HEAD无差异，整参考树仍有既有MISO/构建修改。后续完整共享声明、基准目录/诊断/Windows线程名/mutex与文件存储链的既定范围不变。

下方为本次收口前的审查快照和已有声明决定；实际进度以本节为准。

## 当前实际状态

QLog feat/spsc-ring-opt@f24f3afcd8e9418ea48ad5a6b05680c622008145；BQLog main@78cbfbef4f87e7558335f0c04bf6dd6894b46451。两树都有既有修改。采用的13个参考文件（global、thread/mutex、native file defs、platform_misc、FileManager）相对参考HEAD无差异，不称整树干净。

全局对象 aligned_alloc、两个平凡后置副本和 mutex 声明已修正；前置 -- 仍多一个 return。POSIX 名称草稿放在 include/qlog/platform/posix/thread_ops.cpp，缺公开头/pthread声明并有宏、类型、分号、模板、Value/stirng等抄写错误；现有 src/platform/posix/thread_ops.cpp 未取得名称定义，实际调用方链接失败。修正后应合入现有系统cpp并保留全部CR，不把cpp放入公开include或同时注册两份定义。

Windows get_process_address 声明/定义一致，行为对应参考 get_process_adress；本次保留用户现有拼写，不添加别名。普通查询、模板及全局基础在实际GNU/MSVC适用配置定向编译通过；根CMake仍17cpp，global及新Windows查询未接入。本轮生产源码、测试、CMake和CR的163个文件哈希不变。

## 完整声明的边界与归属

下一组建立现有Linux/Windows原生C++分支的真实完整类型和成员不变量；本组不填写尚未实现的普通成员函数，不创建空cpp或提前执行common_global_vars::get。源码顺序为平台身份/基准目录/Windows版本类型 → 原FileHandle/FileManager完整声明 → 原common_global_vars完整声明。

- include/qlog/platform/posix/file_defs.hpp 与 windows/file_defs.hpp：在既有句柄类型旁增加原 file_node_info；POSIX保留stat成员类型dev/ino，Windows保留volumn/idx_high/idx_low。hash_code仅声明，原operator==类内定义，无额外字段、默认值或断言。
- include/qlog/platform/file_defs.hpp：std::hash<file_node_info>的最薄适配声明，operator()仅声明，供标准unordered_map使用。后续定义委托原hash_code，不新增拥有对象或另选hash算法。
- include/qlog/platform/file_path.hpp：原base_dir_initializer完整成员/访问权限、两个getter和普通成员声明；原get_base_dir(base_dir_type)声明。保留现有路径函数和CR。
- include/qlog/platform/windows/system_api.hpp：追加原windows_version_info三项零默认值及get_windows_version_info的const引用声明；现有查询函数、模板、4191 pragma和CR保持。
- include/qlog/io/file_manager.hpp：namespace qlog，原file_handle、根file_open_mode_enum、file_manager、私有file_descriptor及原两个路径宏。这是BQLog实际文件管理模块，不新增第二套Manager。类型/字段/友元/访问权限/const/noexcept/default逐接口对齐；get_lexically_path参数为original_path。普通clear/构造等仅声明，后续cpp实现；参考的短getter/比较/转换保留类内定义。
- include/qlog/global/common_vars.hpp：namespace qlog，派生global_vars_base<common_global_vars>，Linux/Windows/x86字段按原顺序保留，构造与protected partial_destruct只声明。

QLog标准库适配继续使用std::string/vector/unordered_map；第一个cache元素保持NUL，原文件管理字段ref_cout等拼写不修正。不添加功能默认值、额外运行断言、shared_ptr/unique_ptr包装或临时global-state缩减结构。

Java/NAPI/Python/Android专属字段为参考条件编译分支，当前原生范围未启用；没有把这些能力记作移植完成。avx2_support_声明保留不代表本轮实现SIMD或完成CPU/OS能力检测。

## 成员不变量和参考边界

common_global_vars的console_mutex_、base_dir_init_inst_等成员必须先于file_manager_inst_构造；原FileManager构造中的诊断/目录访问依赖该顺序。保留最后成员初始化中的new file_manager()，不能仅前置声明FileManager或改成临时nullptr。file_manager.hpp只前置声明common_global_vars，common_vars.hpp包含完整FileManager类型以解除头文件循环。

file_node_info以OS文件身份比较，hash仅串联各字段字节，不直接hash含padding的整结构。相等身份必须取得相等hash；inode/file ID不同于路径文本。实际hash实现与保留format_hash核心须后续源码核对后接续，本组不恢复旧formatter/PreparedRecord。

file_handle的idx/seq判定描述符代际身份；独立handle_ptr指向的原生句柄存储保证描述符容器搬移后引用仍可使用。file_descriptor里的ref_cout是句柄共享计数；atomic计数不能替代FileManager的表锁。快速operator bool与is_valid的完整表检查不同，platform_handle原getter要求有效指针，保留原边界。

参考file_handle复制赋值未先关闭原左侧引用（file_manager.cpp:649），原字符串reset会释放旧存储（array_impl.h:707）；这些在实现函数时须明确处理差异，不从完整声明推断所有复制/移动/释放行为已经验收。本组不改复制赋值合同或静默补防御。参考common_global_vars::partial_destruct中delete FileManager仍是注释，不改成完整对象析构或自动拥有包装。

Windows版本查询与线程名称函数的参考失败/资源边界留到实际函数指导讨论，本组只声明类型和签名，不采用未初始化版本输入、漏释放线程名称等实现或声称其安全。

## 验证边界

实际GNU/MSVC Debug/Release atomic运算符实例化仍失败；实际POSIX名称独立编译与现有系统cpp调用方链接失败。外部副本仅修正前置return和POSIX草稿并合入现有cpp，GNU两配置编译/名称调用方链接通过；全局基础和Windows查询实际源码适用配置定向编译通过。

完整声明最终外部候选与实际BQLog头在GNU/MSVC Debug/Release各仅编译通过，Windows版本声明/现有查询兼容性也仅编译。原生审查原子相关仅排除已记录C4324/C4146，不改生产warning；版本/API声明检查保留原4191 pragma，无该两项额外排除。外部探针最初缺少标准string类型引用，已修正探针并保留失败日志，不归咎参考源码。

未运行测试，未链接完整共享状态或实际根库，不计文件管理、线程安全、退出、Buffer/MISO、恢复或性能验收。证据：D:/qq344/Documents/qlog-common-state-declarations-guide-20261011-68f491。

声明写齐并复核后，按真实依赖填写基准目录、必要诊断、Windows版本/线程名与mutex，再补共享状态构造及FileManager定义；已完整实现的cpp才接对应CMake分支。后续主线仍为文件句柄/管理 → MemoryMap → NormalBuffer → MISO → LP/HP/TLS/LogBuffer。
