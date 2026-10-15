# QLog V1 后续开发路线：统一先对齐 BQLog

> **2026-10-15 当前接续（优先）：** SISO与Buffer公共类型、工程宏/线程名和RSA两处原零整数规则已按用户确认收口；SISO尚有实现差异，详见[当前差异](BQLOG_ALIGNMENT_GAPS_20261015_CHS.md)。本轮已重跑当前QLog与BQ086官方benchmark，1/4生产者各3轮，完整结果见[README](../../README.md)与[验证](../validation/BQLOG_NAMING_RSA_BENCHMARK_20261015_CHS.md)。生产基准仍为78，FAST/新Record读取链未自动迁入；下一项严格对齐先收SISO完整实现，性能方向另行profile。下方保留历史快照。

> **2026-10-15 官方 benchmark 完成（最新）：** 新 benchmark/cpp 只对齐官方普通接口场景，原库与生产源码不改；同级新参考固定 086f2fd831af86fcfa40e90015f89626d498d3d7，原参考脏树保留。用户选择 BQ FAST 单列、QLog 未支持。WSL Release 1/4线程各3轮、12进程、9.6亿名义吞吐调用；GNU恢复后40/40 CTest、原生MSVC严格构建及单线程整套实际运行通过。初批环境故障数据全部排除，正式批次无源码漂移；不计零丢失/耐久性/新参考解码互通验收。结果与复现见[根README](../../README.md)、[机器结果](../validation/BQLOG_BENCHMARK_20261015_RESULTS.json)及[决定](BQLOG_BENCHMARK_ALIGNMENT_20261015_CHS.md)。后续性能优化应先测量定位，不据单项结果改库；FAST接口迁移未授权。下方历史快照。

> **2026-10-15 FileCompressed实际补全（最新）：** 用户明确授权扩入压缩文件模块。原appender_file_binary/compressed、bounded缓存、VLQ/ZigZag、UTF-Mixed和RSA/AES-CBC/Vernam及必要随机/对齐依赖已实现并接库，工厂识别compressed_file；保留v10/.logcompr、原L1/L2及配置/分段规则，Record/Layout/Buffer生产源码不变。已有生产只改CMake与工厂，CR/草稿保留；新RSA两处零整数越界显式修正，std容器/分配与软件UTF适配明示。GNU/Linux57cpp、原生MSVC58cpp Debug/Release各40/40实际CTest，PUBLIC宏逐TU核对；固定参考原解码器对四配置40文件24008记录含加密互通通过。未称新Compress性能/完整损坏恢复/TSAN/ARM验收，Raw/生产Decoder/跨语言仍未扩。见[决定](FILE_COMPRESSED_ALIGNMENT_20261015_CHS.md)及[验证](../validation/FILE_COMPRESSED_ALIGNMENT_20261015_CHS.md)。下方历史快照。

> **2026-10-14 用户授权助手完成V1主链及压测（本段最新）：** 本轮明确授权覆盖此前新生产由用户填写的分工；线程/CV误放草稿修正、缺失成员补全并接库；新增原Logger运行态/Manager/Worker/Console/TextFile/Snapshot、原全局生命周期、公开qlog::log记录入口和两系统堆栈，复用既有Record/Layout/配置/HashMap，CR及无关草稿保留。实际GNU/Linux50cpp、原生MSVC51cpp Debug/Release均严格构建且31/31 CTest；不是仅候选/编译。WSL Release固定BQ78cbfbef导出对比，主测54次、1.56亿调用、1.44亿有效日志、6000万文本条目核对，通过计数/内容/每生产者顺序及源漂移检查；Windows不计性能。Console独立Worker仍有16%–23%吞吐差距和部分P99.9差距，未称普遍胜过BQ。原std排序/重叠搬移/失败地址修正明示，既有参考UB/原子/恢复等边界继续记录；raw/compressed/Decoder/跨语言未扩、整数越界与custom UTF32未决。见[实现决定](V1_RUNTIME_MAINLINE_COMPLETION_20261014_CHS.md)和[性能/验证报告](../validation/V1_RUNTIME_PERFORMANCE_20261014_CHS.md)。下方历史快照。

> **2026-10-14 完整LogBuffer实际收口与Worker线程依赖（本段最新）：** 消费者17定义与上轮候选一致，无生产修正；209 include/src/tests/CMake哈希全不变、CR保留。实际完整Buffer GNU/原生MSVC Debug/Release各37/38库和两TU严格编译链接通过，UNIT仅GNU语法；均未运行。已核BQ log_imp按值持有log_worker，而Worker继承thread并持有condition_variable；QLog仅有flat线程服务/Mutex，下一组由用户一次补齐原线程对象及条件变量，共享static服务、系统生命周期合现有thread_ops.cpp、原状态/CAS/错误/断言/CV时钟与旧事件规则保留，随后Logger/Manager/Worker/Appender主声明与运行链。完整候选仅仓库外39/40库及两TU四配置编译链接，不接新空生产源、不建替身。原this寿命/普通ID双写、首次API探测/时钟和超时边界记录，不称安全；行为/并发/退出/恢复/性能仍后置。见[决定](THREAD_WORKER_DEPENDENCIES_ALIGNMENT_20261014_CHS.md)与[验证](../validation/LOGBUFFER_CONSUMER_THREAD_BUILD_20261014_CHS.md)。下方历史快照。

> **2026-10-14 LogBuffer写侧已填与消费者接续（本段最新）：** 九函数实际审查无修正，209源/测试/CMake全不变、CR保留；实际写侧与完整消费者候选四配置37/38cpp、两TU严格编译链接，PUBLIC宏/UNIT GNU语法通过，未运行调用方/CTest。实际读侧仍声明，新消费者由用户写；一次补齐验证/运输注销、LP读取、HP批次/组块遍历/回收/空洞迁移、代际推进、完整和inline read/return、Oversize读归还与回收。原消费者cpp TLS、末项替换删除语义及原容器assert保留；锁外empty/普通字段/恢复map无效Release及原回绕/typed边界不称安全。填写后实际审查/编译，再进入运行态及Worker/Appender实际依赖核对，运行验收可后置但不取消。见[决定](LOG_BUFFER_CONSUMER_ALIGNMENT_20261014_CHS.md)与[验证](../validation/LOGBUFFER_PRODUCER_CONSUMER_BUILD_20261014_CHS.md)。下方历史快照。

> **2026-10-14 LogBuffer TLS实际收口与写侧接续（本段最新）：** 声明误放include内.cpp已按原字节移.hpp，实际TLS cpp接根库；BlockList仅三处[[maybe_unused]]解决UNIT+Release/NDEBUG警告，原断言/order不变；用户新增CR保留，209基线其余205文件不变。实际及完整构造/恢复/producer/Oversize写候选GNU/原生MSVC四配置37/38cpp各两TU严格编译链接通过，PUBLIC宏核对，UNIT仅GNU两配置语法。生产新九函数由用户写；实际主LogBuffer尚未构造可用。原代际/运输seq/1000ms调用统计/父子发布及原is_valid/IO-drop处理保留；参考恢复size回绕、重开misc缺检查、hpMax普通读写和TLS无限重试边界记录。程序/CTest未执行，不计并发/恢复/退出/故障/性能。随后完整消费者（context验证/LP-HP遍历/read-return快路径/Oversize读回收），完成LogBuffer再运行态/Worker/Appender。见[决定](LOG_BUFFER_PRODUCER_ALIGNMENT_20261014_CHS.md)与[验证](../validation/LOGBUFFER_TLS_PRODUCER_BUILD_20261014_CHS.md)。下方历史快照。

> **2026-10-14 Oversize完成后的主线（最新）：** Oversize已填并接实际库，最终GNU/原生MSVC四配置36/37库与调用方编译链接、未运行/CTest。Atomic模板入口按CR移platform/atomic/atomic.hpp，原代码/CR保留。当前[LogBuffer完整声明/context/scoped/TLS基础](LOG_BUFFER_DECLARATIONS_ALIGNMENT_20261014_CHS.md)同组原shared/make_shared/launder，37/38候选四配置只编译链接、未实例化主对象，新生产由用户写。随后构造/版本恢复/producer，再consumer/Oversize运输回收，Logger/Manager/Worker/Appender后置；未用MISO链表不作前提。

> **2026-10-14 GroupList实际完成后的接续（本段最新）：** MemoryPool/GroupList及全部直接依赖已由用户填写，助手修T.bool前置模板并接实际GroupList/两native时钟；GNU/原生MSVC Debug/Release库35/36与公开调用方严格编译链接通过、未运行/CTest，CR保留。下一模块[OversizeBuffer](OVERSIZE_BUFFER_ALIGNMENT_20261014_CHS.md)完整候选仅仓库外四配置36/37cpp及两TU编译链接；新生产由用户写。完成后审查/接真实cpp并直接LogBuffer声明/context/scoped handle/TLS/LP-HP及读写恢复主线。纠正此前“必须先迁miso_linked_list”的说明：固定BQ78cbfbef生产只有未使用include，没有LogBuffer实例化，实例化在测试；迁该容器留对应测试/独立学习，不阻塞主链。验证见[记录](../validation/GROUP_LIST_OVERSIZE_BUILD_20261014_CHS.md)。下方旧进度按日期解读。

> **2026-10-14 SPSC128实际修正与GroupList完整指导（最新）：** 用户选择参考128并由其从MemoryPool开始写新模块。助手仅SPSC头接公共128并同步offset，Head512/cpp公式+640，原CR保留；199原生产/测试/CMake其余198不变。实际GNU/原生MSVC四配置33/34库与BlockList/MISO调用方链接通过；完整原GroupList/RW锁/对齐工具/原墙上毫秒时钟/警告宏及两TU候选四配置仅仓库外35/36源编译链接通过，PUBLIC宏核对。首次候选漏独立Config头已补，生产没有此失败。原unsafe锁交接、OOM/异常、stage重叠break、no_lock普通读取、strtoull及旧64映射边界明示；不增Manager/assert/回滚。新生产未代写/接空源，测试仍后置，不算运行/恢复/并发/性能。见[决定](GROUP_LIST_ALIGNMENT_20261014_CHS.md)与[验证](../validation/SPSC128_GROUP_LIST_BUILD_20261014_CHS.md)。下方为历史快照。

> **2026-10-14 BlockList实际接库与MemoryPool指导（最新）：** 用户BlockList及三个工具已填，源码审查无必要修正；助手只注册实际block_list.cpp，199原生产/测试/CMake其余198不变，CR/草稿保留。GNU/原生MSVC x64 Debug/Release实际整库与BlockList/MISO各两TU链接通过，库源33/34、PUBLIC宏核对；运行/CTest仍后置。本组原memory_pool两个节点偏特化及push/pop/evict/size完整候选仅仓库外四配置两TU编译链接，新生产由用户填，不建空cpp。GroupList尚缺原rw_crazy/对齐对象构造/毫秒时钟，后续须连同分组存储补齐，SISO64/参考128边界保留。见[决定](MEMORY_POOL_ALIGNMENT_20261014_CHS.md)与[验证](../validation/BLOCK_LIST_MEMORY_POOL_BUILD_20261014_CHS.md)。下方为历史快照。

> **2026-10-13 MISO读侧实际接库与BlockList集成指导（最新）：** 用户已写五读函数；助手仅修正read_chunk的waiting_for_retrun_拼写，原CR、status→block_count→switch、N/4批量归还及原边界保留。实际GNU/原生MSVC x64 Debug/Release严格整库与两TU全MISO接口链接通过，库源32/33及PUBLIC宏核对；197原生产/测试/CMake仅该cpp一行变化。用户允许测试后置，本轮未运行/CTest，不沿用历史计数。下一模块原block_node_head/block_list承接现有SISO，再memory_pool/group_list、oversize/miso_linked_list及LogBuffer；完整候选和三个依赖仅在仓库外四配置三TU编译链接通过，生产由用户填写。见[决定](BUFFER_BLOCK_LIST_ALIGNMENT_20261013_CHS.md)与[验证](../validation/MISO_BUILD_INTEGRATION_20261013_CHS.md)。下方为历史快照。

> **2026-10-13 MISO读侧Debug依赖收口（最新）：** 用户授权助手补原log_global_vars计数器/RAII，保留原名称/继承/relaxed/嵌套规则，Manager/Appender与完整退出逻辑后补；SISO/MISO查询共享实例开关与全局counter。已写TLS赋值/QLLOG_DEBUG/fetch_add_relaxed三处修正，原CR保留，src/global/log_vars.cpp接库。实际GNU/原生MSVC Debug/Release严格库及两TU依赖链接、常驻20/20＋参考4/4各24/24；线程策略14检查，初次夹具对齐错误已修正。读侧五函数/三TU候选只在仓库外四配置编译链接、未运行，生产由用户写。用户明确原status→block_count→switch顺序不改，保留unused未初始化/并发元数据读取及原typed-pointer边界，不记MISO行为/并发/恢复/退出/性能。见[决定](MISO_READ_ALIGNMENT_20261013_CHS.md)与[验证](../validation/MISO_READ_DEPENDENCIES_20261013_CHS.md)。下方为历史快照。

> **2026-10-13 原HashMap既有调用并发迁移（最新）：** 用户写MISO，助手只迁移此前四处std::unordered_map：Layout线程缓存、Property/PropertyValue字典和文件独占登记，按BQ均用普通hash_map；MISO仍用inline版本。Layout恢复set_expand_rate(4)，std::string哈希在原calculator中按BQ string::get_hash适配；add/is_empty/key/value及深复制调用同步，原CR和格式保留。实际七cpp/五独立公开头四配置严格编译通过；用户MISO草稿导致首次实际整库失败未改，隔离副本冻结三项旧依赖，四配置常驻19/19＋参考4/4，各23/23，四组差分输出一致。193原文件保护核对无越界改动，13受改文件与验证副本一致；不接受当前MISO/并发/退出/恢复/性能。见[决定](HASH_MAP_CALLERS_ALIGNMENT_20261013_CHS.md)与[验证](../validation/HASH_MAP_CALLERS_BEHAVIOR_20261013_CHS.md)。下方为历史快照。

> **2026-10-13 原HashMap实际补全与MISO写侧接续（最新）：** 用户授权补全Hash部分。原HashMap双版本、完整type_traits/声明/定义和已写type_tools修正已落实际源码；正确allocator/入口、平台宏、MISO草稿和原CR保留，impl无pragma once。新增3组常驻HashMap回归，每配置48324检查；GNU/原生MSVC x64 Debug/Release严格实际库及公开双翻译单元调用方链接通过，常驻15/15＋外部固定提交参考3/3，各18/18，结果与BQ完全一致，Linux定向ASan/UBSan三组通过。PUBLIC Debug宏与31/32源核对，MSVC只沿用外部C4324/C4146排除。下一组原TLS宏/缓存及fetch_add预留、CAS末尾回退、commit完整候选四配置仅仓库外编译链接通过、未运行，实际新MISO定义由用户填；不记TLS退出/并发/恢复/性能验收。指南留对话、文件只记决定/证据。见[验证](../validation/HASH_MAP_BEHAVIOR_20261013_CHS.md)。下方为此前快照。

> **2026-10-13 MISO初始化实际收口与原HashMap路线（本段最新）：** 用户已填写MISO初始化；助手修正已写头/cpp编译错误、原成员/getter与commit签名及旧映射恢复失败的初始化分支，11行原CR/注释保留，get_buffer_addr遗漏由用户补写。根CMake接实际MISO cpp，实际GNU/原生MSVC x64 Debug/Release严格库与双翻译单元初始化调用方链接、现有CTest各12/12通过，源数31/32与PUBLIC Debug宏核对；新调用方未运行，不计恢复/并发/性能。用户明确选择“先迁移BQLog原hash_map_inline”，所以先原容器及分配/类型依赖，再原TLS缓存与fetch_add预留/CAS回退/commit；不采用std::unordered_map。完整原容器候选四配置仅仓库外严格模板编译链接通过、未运行，新生产由用户写，macros/type_tools生产不改。保留原bucket/node/list/free布局、双版本、默认/断言、set_expand_rate实际目标桶数规则及原对象访问/失败边界；指导留对话、文件只记决定。见[本组决定](HASH_MAP_ALIGNMENT_20261013_CHS.md)。下方为历史快照。

> **2026-10-13 NormalBuffer实际收口与MISO基准确认（本段最新）：** 用户已填NormalBuffer；助手仅修正已写cpp四处抄写/备份名并接CMake，183生产/测试/CMake其余181哈希不变，原CR保留。实际GNU/Linux与原生MSVC x64 Debug/Release严格库及双翻译单元公开调用方链接通过，现有CTest各12/12，源数30/31、PUBLIC Debug宏核对；未运行新存储/resize/恢复/并发/性能。用户已确认MISO以BQ提交78cbfbef为准，保留head 128对齐和完整Debug，不采用本地alignas(4)/删检查草稿。本组完整声明/布局及构造、创建、初始化、恢复、析构、setter候选只在仓库外四配置严格编译链接通过、未运行，源数31/32；新生产由用户写。原OOM、零块损坏恢复循环、packed/offset访问及fresh mmap仍init_with_memory边界明示。Debug全局线程检查真实依赖尚缺，保留查询声明，不加固定返回替身；后续生产者组，再消费者/Debug依赖收口。完整指导留对话、文件仅记决定。见[本组决定](MISO_ALIGNMENT_20261013_CHS.md)。下方为历史快照。

> **2026-10-12 MemoryMap实际收口与NormalBuffer接续（本段最新）：** 用户已填写MemoryMap并正确接入CMake。助手只修复4个已写文件：公共最小文件长度接口名称、POSIX遗漏长度变量/写循环+=/末尾endif及Windows空转包含头；原注释/CR和size_t*/HANDLE&存取、__memory_map_size_unit、CreateFileMapping均保留。180个生产/测试/CMake其余176个SHA-256不变，CMake及FileManager不改。实际GNU/Linux与原生MSVC x64 Debug/Release严格库及两翻译单元公开调用方链接通过，现有CTest各12/12，源数29/30、PUBLIC Debug宏核对；未运行新映射行为/恢复/并发/性能。下一组NormalBuffer采用原normal_buffer、create_memory_map_result和成员/参数/default/虚析构，公共缓存行常量按BQ为128；完整候选仅仓库外四配置严格编译链接通过、未运行，源数30/31。新生产定义由用户填写，指南留对话，记录只存决定。BQ九个采用参考相对78cbfbef HEAD无差异，整树仍脏；resize原OOM/失败删除/无强回滚与原测试注入未迁移边界明示。NormalBuffer后直接MISO，再完成其余Buffer基础与LogBuffer。见[本组决定](NORMAL_BUFFER_ALIGNMENT_20261012_CHS.md)。下方为历史快照。

> **2026-10-12 MemoryMap原指针规则确认（本段最新）：** 用户明确要求字节区直接size_t*/HANDLE*访问，上一轮memcpy适配撤回；保留原POSIX size_t*/const size_t*与Windows HANDLE&、__memory_map_size_unit及CreateFileMapping，原字段/default/assert/错误规则不增改。QLog HEAD已a1ba6b4，六个MemoryMap文件已创建但为空；180生产/测试/CMake哈希全不变，未代写或挂空文件。BQ五采用参考相对78cbfbef HEAD无差异，整树仍脏。原指针完整候选仅仓库外GNU/原生MSVC x64 Debug/Release严格编译链接四配置通过，候选源数29/30、PUBLIC Debug宏核对，无别名绕过选项；未运行/CTest/映射/存储/并发/性能，不复用上轮memcpy结果。容量断言与对齐/对象访问边界区分说明；填写后实际审查/CMake并接NormalBuffer→MISO。完整指导留对话，见[本次决定](MEMORY_MAP_ALIGNMENT_20261012_CHS.md)。下方为历史快照。

> **2026-10-12 FileManager实际收口与MemoryMap主线（本段最新）：** 用户已填47定义；助手将现有定义/TLS错误槽从公共头移cpp，修正遗漏seq=0、比较误赋值、两个接口拼写及原参数/局部名，原12行注释全保留。174生产/测试/CMake仅FileManager头/cpp改变，其余172哈希不变，根库仍Linux27/Windows28无需重复注册。实际GNU/原生MSVC Debug/Release严格库、双翻译单元调用方链接及现有CTest各12/12，Debug PUBLIC宏核对；不是文件行为/并发验收。MemoryMap原五参考及相关FileManager/NormalBuffer相对BQ HEAD无差异，整树仍脏。完整6文件候选只在仓库外四配置编译链接通过、未运行，指导留对话由用户写；生产未创建、不挂不存在源。8字节区显式用memcpy存取；原Windows页大小/非零偏移边界、POSIX物化、复制映射释放和原void错误/flush规则明示保留，不加新类/assert/atomic/默认。填写后审查/接库并直接NormalBuffer→MISO，无更多基础组。见[本组决定](MEMORY_MAP_ALIGNMENT_20261012_CHS.md)。下方为历史快照。

> **2026-10-12 存储基础实际接库与FileManager主线（本段最新）：** 最后一组已填写；助手按用户最新草稿同步POSIX析构变量拼写及Windows有参构造store_seq_cst，原CR保留。根CMake接6公共cpp和各native锁/API，实际Linux27/Windows28cpp；174个生产/测试/CMake仅两mutex及CMake改变，其余171个哈希不变。实际GNU/原生MSVC Debug/Release严格库和基础调用方链接、现有CTest各12/12通过，PUBLIC Debug宏核对；MSVC仅外部已有C4324/C4146排除。FileManager头/已写6函数不改，完整47定义指导留对话由用户写；外部完整候选四配置只编译链接通过、未运行，不计FileManager/存储/并发/退出/MISO/性能。原赋值引用泄漏、copy短读循环、路径全匹配替换、共享游标和TLS错误边界明确保留。填写后实际审查并接MemoryMap→NormalBuffer→MISO，无更多基础组。见[本组决定](FILE_MANAGER_ALIGNMENT_20261012_CHS.md)。下方为历史快照。

> **2026-10-12 完整指导重新核对（本段最新）：** 用户已填写Windows线程名及common构造/原空partial，剩余为两能力helper、两mutex成员及两native九文件接口/原helper。本轮只修正3个已写cpp的拼写/缺头/重复提前入口，174个生产/测试/CMake其余171个哈希不变，CR保留；完整新定义仍留对话由用户写。实际Windows两个cpp两配置严格编译通过；当前源码＋仓库外5cpp完整候选四配置严格编译和公开调用方链接通过，源清单Linux27/Windows28、Debug宏传播核对，MSVC仅外部既有C4324/C4146排除。未运行/CTest、未改根CMake20cpp，不计生产基础闭合/存储/并发/退出/MISO/性能；ARM未验。参考原mutex内存序、独占无引用计数、Windows路径拼接与prefix_size=2及既有失败边界继续保留。填写后下一轮同轮审查/CMake并直接FileHandle/FileManager→MemoryMap→NormalBuffer→MISO，无更多基础指导组。见[本组决定与证据](COMMON_GLOBAL_VARS_DECLARATIONS_ALIGNMENT_20261011_CHS.md)。下方为历史快照。

> **2026-10-12 最后一组存储基础实际复核（最新）：** 用户要求先修复已写问题，再一次给齐最后一组指导。助手修正已有7文件：误放POSIX的Windows基准目录构造移回Windows并补拼写/头；POSIX词法补返回且反斜杠按BQ POSIX保留；native身份hash和FileManager启动补头；close返回int32_t；公共头提前入口移到新增common_vars.cpp且纠正default_initer_。新cpp仅移动已有入口，未代写构造/能力检查/partial或其他新函数；CR保留。172原生产/测试/CMake文件其余165哈希不变，现173，根库20cpp未改CMake。实际GNU相关9cpp和MSVC相关10cpp两配置严格编译通过；Linux实际库两配置构建/现有CTest各12/12；实际＋外部剩余候选四配置严格构建及公开调用方链接通过，候选未运行，不计存储/锁并发/首次初始化退出/MISO/性能。MSVC仅已有C4324/C4146外部排除。已有std::hash/身份hash/FileManager三启动/Windows版本及基准目录不重写。最后指导覆盖两native9文件接口及原helper、Windows名称、两个mutex、common构造；用户填完后下一轮同轮审查/CMake并直接FileHandle/FileManager→MemoryMap→NormalBuffer→MISO，不再分基础组。预计Linux27/Windows28cpp，mutex文件沿用POSIX mutex_posix.cpp、Windows mutex_win64.cpp。见[本组决定与证据](docs/decisions/COMMON_GLOBAL_VARS_DECLARATIONS_ALIGNMENT_20261011_CHS.md)。下方为历史快照。

> **2026-10-12 哈希复用接续（最新）：** 保留的三份hash核心已迁utility并接入根库，当前20cpp；CRC用crc/v、内部src/dst/len、公开get_hash_64用data/size。用户已确认采用BQ软件原表（含206/207差值），分别对齐软件/硬件结果；没有重抄核心或恢复旧包装。Linux/原生Windows x64 Debug/Release实际库严格构建与CTest各13/13；公共入口补入仓库外完整依赖候选后各14/14。实际共享构造/生命周期仍待用户填写/接库，x86-32 Release复制差分未过，均不算存储/MISO/性能验收。其余164个原文件和CR不变。删除重写hash任务，其余上一组依赖完成后同轮审查/接库并直接进入FileHandle/FileManager→MemoryMap→NormalBuffer→MISO；最终预计Linux27cpp/Windows28cpp。见[最新决定与验证边界](COMMON_GLOBAL_VARS_DECLARATIONS_ALIGNMENT_20261011_CHS.md)。下方为历史快照。

> **2026-10-11 最后一组存储基础依赖收口（最新）：** 用户要求本组一次给齐依赖、完成后的下一轮直接进入主线。公共基准目录实际cpp已修正platfrom、尝试锁未检查、setter对象/成员/调用及文本拼写，原CR保留；实际GNU Debug/Release公共/POSIX与MSVC两配置公共编译通过，168个生产/测试/CMake文件仅该cpp变更，其余167个不变。用户对Windows三项候选修正回复“严格对齐BQlog”，因此_wgetcwd未检查/未释放、版本输入未初始化大小及线程名缓冲未LocalFree按参考保留，原先失败回退/释放建议未采纳，不称失败路径安全或首次并发初始化可用。本组指导覆盖身份哈希与std::hash适配、POSIX词法路径、两系统9个文件平台缺项及原私有helper、Windows基准目录/版本/名称、完整mutex、common_global_vars构造/原空partial_destruct和默认提前入口、FileManager原构造/instance/get_base_dir三启动函数；没有空替身或新Manager。完整外部候选在Linux/GCC与原生MSVC x64 Debug/Release严格构建、实际库组成和公开调用方链接通过，未运行/CTest/差分/并发/退出/存储验收；MSVC外部仅排除既有C4324/C4146。根CMake仍17cpp未改，新生产函数由用户写；填写后下一轮助手同轮审查修正、接库并直接指导FileHandle/FileManager描述符/引用和读写，随后MemoryMap→NormalBuffer→MISO，不再拆出零散基础组。16个采用参考文件相对BQ HEAD无差异，整树仍脏。详细指导在对话，文件仅记决定，见[接续决定](COMMON_GLOBAL_VARS_DECLARATIONS_ALIGNMENT_20261011_CHS.md)。下方为此前快照。

> **2026-10-11 诊断实际修正与目录接续（最新）：** 已按用户授权修正实际诊断cpp和两处声明拼写，保留8条CR及用户console命名；GNU/MSVC Debug/Release普通/测试/工具/CI分支编译通过，未运行/完整共享状态链接，根17cpp未改。下一组：基准目录公共getter/setter＋native当前工作目录构造；公共/POSIX候选只编译，Windows_wgetcwd空指针/释放规则尚待用户选择，原提前初始化顺序待共享状态闭合。随后Windows版本/线程名→mutex/共享构造→FileManager→MemoryMap→NormalBuffer→MISO。167个快照仅3个生产文件变更；详细指南留对话。见[接续决定](COMMON_GLOBAL_VARS_DECLARATIONS_ALIGNMENT_20261011_CHS.md)。下方为历史快照。

> **2026-10-11 纯路径实际复核与诊断接续（最新）：** 用户填写的FileManager三个纯路径函数及共享声明探针在Linux/原生Windows Debug/Release编译通过，纯路径公开调用方链接通过；move赋值参数、is_valid、open_mode和Windows版本查询声明仍须原接口补齐。下一组只补utility::util原控制台诊断四入口及条件minimum setter，保留共享buffer/递归锁/翻倍扩容/callback与native输出，明确成对修正参考va_list调用；随后基准目录→Windows线程名/mutex/共享状态→FileManager→MemoryMap→NormalBuffer→MISO。外部诊断候选及BQ摘录四配置含UNIT_TEST分支仅编译，未运行/完整共享状态链接；165个生产/测试/CMake文件不变。详细指南留对话。见[接续决定](COMMON_GLOBAL_VARS_DECLARATIONS_ALIGNMENT_20261011_CHS.md)。下方为历史快照。

> **2026-10-11 最新声明与依赖接续：** 新共享声明已进入实际工作区，但hash特化命名空间/宏拼写等使两平台两配置定向编译失败，完整接口仍有遗漏；生产/CR/CMake/测试164文件未修改。下一组先修声明，填写FileManager get_directory_from_path/get_file_name_from_path/combine_path三个纯路径函数，为相对路径转换和文件创建提供原有底层规则；基准目录构造本身直接读取当前工作目录。保留BQ find_last不检查索引0、min选分隔符、split忽略空项的实际行为，不将字符串拼接视为文件系统规范化。外部声明与原参考仅编译通过，纯路径候选/原参考摘录四配置仅编译链接通过，无运行或完整共享状态验收。详细指南留对话。见[接续记录](COMMON_GLOBAL_VARS_DECLARATIONS_ALIGNMENT_20261011_CHS.md)。下方为历史快照。

> **2026-10-11 上一组收口后状态（最新）：** 原子前置--已正确；POSIX线程名已在实际src/platform/posix/thread_ops.cpp闭合，7条CR保留，公开include旧cpp已不存在。实际Linux/原生Windows Debug/Release库及原子运算符实例化调用方编译链接通过，Linux名称调用方从实际库链接通过；未运行程序或CTest。根CMake仍17cpp，无重复源注册；本轮仅1个生产cpp变更，其他161个生产/测试/CMake文件不变。后续沿原完整共享声明及真实平台依赖继续；不将此次编译链接计作行为、并发、退出或MISO验收。见[实际收口记录](COMMON_GLOBAL_VARS_DECLARATIONS_ALIGNMENT_20261011_CHS.md)。下方为历史快照。

> **2026-10-11 最新实际状态与接续：** 前置atomic--和POSIX名称草稿的放置/拼写仍待修正，名称实际调用方链接失败；全局分配和mutex声明已修正，Windows查询/全局基础适用配置定向编译通过但未接根CMake。下一组为真实平台类型、原FileHandle/FileManager与common_global_vars完整声明及构造顺序，随后闭合基准目录/诊断/Windows线程名/mutex/共享状态，再返回文件管理 → MemoryMap → NormalBuffer → MISO。根17cpp未改，163个生产/测试/CMake文件哈希不变；外部新声明及实际BQ头四配置仅编译通过，无运行/完整共享状态链接验收。详见[接续决定](COMMON_GLOBAL_VARS_DECLARATIONS_ALIGNMENT_20261011_CHS.md)。下方为历史阶段快照。

> **2026-10-10 最新依赖进度：** 自旋锁及退出登记cpp定向编译通过，平凡atomic三个函数、mutex两处声明和全局get分配遗漏尚待修正；全局get虽能实例化编译，运行时空地址构造缺陷仍未收口。根CMake17cpp未改，生产/CR/测试/CMake160文件哈希不变。下一组：原POSIX线程名四分支/三trait＋Windows原函数地址查询，再以真实完整声明闭合common_global_vars、Windows线程名、诊断及mutex，返回文件句柄/管理 → MemoryMap → NormalBuffer → MISO。候选只有相应两平台两配置编译证据，未链接/运行，不能算完整锁/退出/Buffer验收。详见[最新决定](GLOBAL_LIFETIME_FOUNDATION_ALIGNMENT_20261010_CHS.md)。下方为历史阶段快照。

> **2026-10-10 锁与共享状态接续（当前）：** 普通 atomic 和第七个 trait 已补；平凡类型运算符及 spin_lock 尚有返回类型/副本、宏/序、分号和尝试包装遗漏，实际两平台两配置定向编译仍失败。BQLog mutex 的诊断/线程名与共享 mutex 形成依赖环；先修正原子/自旋，建立原 mutex 完整声明并填写原全局生命周期基础，再闭合共享状态、线程名、诊断和 mutex 定义，返回文件句柄/管理 → MemoryMap → NormalBuffer → MISO。临时修正及新基础候选四配置仅编译通过，未写新生产函数/未链接运行/未改 CMake，不计锁/退出/并发/性能验收。详细指导留在对话，文件仅记决定。见 [接续决定](GLOBAL_LIFETIME_FOUNDATION_ALIGNMENT_20261010_CHS.md)。下方为此前阶段快照。

> **2026-10-10 算术/位运算实际完成（当前有效）：** 公共头与 GNU/MSVC 后端各补齐原参考的70个方法，保持 val、无默认order、原结构/宏/断言；保留用户GNU add_fetch逻辑并修正其多余结束括号，CR不变。GNU sub使用自身builtin，MSVC sub包括raw仍委托add(-val)，原参考算术边界继续保留。新增常驻算术回归并注册1项，根CMake与17cpp源列表不改。实际Linux/原生Windows Debug/Release各CTest 15/15（常驻12＋外部3），新算术各9858检查、第一组/CAS各1518检查与实际BQLog一致；MSVC外部严格审查仅排除已记录C4324/C4146，不改生产警告。
>
> 后续顺序为运算符＋普通atomic＋第七个trait，再普通自旋锁及文件管理的真实平台依赖，然后文件句柄/管理 → MemoryMap → NormalBuffer → MISO。文件仅记录决策与验证边界，详细指导留在对话；当前结果不代表Ring/MISO完整并发、恢复或性能验收。下方条目按此前阶段快照解读。

> 见[实际实施记录](../validation/ATOMIC_FIRST_GROUP_REVIEW_CAS_GUIDANCE_20261010_CHS.md)。

> **2026-10-10 授权补齐后的当前决定：** 按用户要求，助手已修正本次第一组和 CAS 遗漏；后端名称已落实为 `_inner_atomic_GNU_C.h` / `_inner_atomic_msvc.h`，CR 保留。GNU CAS 的 RELEASE/ACQUIRE 合同边界及 MSVC 失败再次 load、strong 委托 weak 的规则继续保留。实际两平台 Debug/Release 库与公开调用方已编译链接，定向检查各 1518 项且与实际 BQLog 一致；这不代表完整原子、并发或性能验收。模板不建 cpp，根 CMake 未改。
>
> **后续协作决定：** 实现指导放在对话中，文件仅记录决策。接续压缩为三组：算术/位运算 → 运算符、普通 atomic、第七个 trait → 普通自旋锁。此后逐模块说明存储调用者、阻塞依赖和可继续的完成边界，再沿文件句柄/管理 → MemoryMap → NormalBuffer → MISO 返回存储主线。下方原日期条目属于此前阶段快照。

> 详见 [本轮实际补齐与验证边界](../validation/ATOMIC_FIRST_GROUP_REVIEW_CAS_GUIDANCE_20261010_CHS.md)。

> **2026-10-10 原子第一组实际审查与 CAS 决定（本段最新）：** 用户已写 GNU/MSVC 第一组，但实际头在两平台 Debug/Release 都编译失败：GNU 拼写、非指针转换缺失、load_acq_rel 序及 exchange 分支/命名；MSVC 缺 CONCATX、intrinsic 宏名及选择宏续行。公共平凡类型尚未写，先依完整第9节闭合19个转发和六个原 trait，再接 CAS。用户已确认 GNU CAS 保留 BQLog RELEASE/ACQUIRE 映射并明确记录 GCC 合同边界；MSVC 失败再次 load、strong 委托 weak 也保留，完整下一组见原子指导第11节。独立修正/补齐与 CAS 候选两平台两配置仅目标文件编译通过，未运行，不计实际原子/并发/性能验收；MSVC 审查仅排除既有 C4324。155个生产/测试/CMake文件哈希不变，CR/草稿/文件名未改，模板不建cpp；后端最终仍为 _inner_atomic_GNU_C.h/_inner_atomic_msvc.h。整数越界两方案继续暂不选定。

> 详见 [本轮实际审查与证据边界](../validation/ATOMIC_FIRST_GROUP_REVIEW_CAS_GUIDANCE_20261010_CHS.md)。

> **2026-10-09 原子第一组完整指导（本段最新）：** 用户最新明确不沿用草稿文件名，后端固定 _inner_atomic_GNU_C.h/_inner_atomic_msvc.h。 用户要求完整正确指导，已在对话/指导第9节给出公共头与GNU/MSVC后端的完整第一组正文：原存储、原帮助函数、explicit_copy、load/store/exchange及全部raw/固定序变体。仅工程前缀/std基础/格式适配，结构、命名、参数/default/order/原断言保持参考；没有单槽或新封装。普通atomic/CAS/算术/运算符按原类后续接续，现阶段保留六个已有平凡断言，普通atomic否定断言待该类填齐。Linux GCC/原生Windows MSVC Debug/Release新临时候选各637定向检查与独立实际BQLog参考一致，含一次发布检查；仅候选，不计生产或完整并发/性能，旧633/20000不复用。MSVC检查排除原对齐C4324，参考SYSTEM头单独编译。实际atomic.hpp部分草稿/CR/CMake不改，根库仍17cpp；用户继续填写新生产函数。见 [完整第一组正文](PLATFORM_ATOMIC_PRIMITIVES_IMPLEMENTATION_GUIDE_20261009_CHS.md)；证据 D:/qq344/Documents/qlog-atomic-group1-complete-guide-20261009-3fa2b7。

> **2026-10-09 原子严格对齐纠正（本段最新，替代下方同日原子候选指导）：** 用户明确要求默认参数、memory order、断言、内部结构与命名逐声明按 BQLog。实际参考 78cbfbef 的三原子头、宏及 trait 文件无 HEAD 差异，整树仍脏。类名保留 atomic_trivially_constructible、atomic、_atomic_base<T,N>；GNU 保留 value_/value_standard 的 union，MSVC 保留 value_、原宏及四个尺寸特化。不再采用 AtomicTriviallyConstructible/AtomicBase/atomic_backend/GnuAtomicType/MsvcAtomicOps 或单一 mutable 原生槽方案。load 的公共及两个后端默认 seq_cst；store/exchange/十个算术入口没有默认 order；weak/strong CAS 的 success_order/fail_order 均默认 seq_cst。仅保留参考静态断言，三个原子头没有运行时 assert；不新增类、默认值、字段、类型限制或回退。QLog 已有未完成 atomic.hpp 草稿，由用户继续填写，本次生产源码/CR/CMake 不改；仅核对和修正文档，未重跑构建测试。旧 633/20000 仅为已替代候选历史，不作为本版原子验收，实际线程基础 12/12 证据保留。union/const RMW/CAS 边界已明确记录，后续组不静默修复。见 [严格完整指导](PLATFORM_ATOMIC_PRIMITIVES_IMPLEMENTATION_GUIDE_20261009_CHS.md)、[修订与证据边界](../validation/THREAD_OPS_REVIEW_ATOMIC_GUIDANCE_20261009_CHS.md)。整数越界两方案仍暂不选定。

> **2026-10-09 线程基础实际审查与原子接续（最新）：** 用户已写线程4函数并接根CMake；助手只同步Windows thred_ops为thread_ops及四处宏/intrinsic拼写，CR保留。Linux/原生Windows实际库/调用方两配置CTest各12/12（常驻11+临时线程1），17cpp和PUBLIC宏已核对。期间用户路径头移到include并改cpp引用/格式已保留，最新Windows库再构建通过；其余149个原生产/测试/CMake文件哈希不变。本轮未改根CMake。下一组由用户写平凡构造原子load/store/exchange三项，按实际GNU/MSVC接口、8字节对齐与静态零初始化；MSVC load为ExchangeAdd(0)，不直接用C++20 std::atomic绕过平凡约束。单一原生可修改槽保留正常值和原子路径，存储差异已明确。临时两系统两配置633检查及20000受保护更新仅算候选，未写原子生产实现，不记完整并发/性能。见[实际审查](../validation/THREAD_OPS_REVIEW_ATOMIC_GUIDANCE_20261009_CHS.md)及[完整指导](PLATFORM_ATOMIC_PRIMITIVES_IMPLEMENTATION_GUIDE_20261009_CHS.md)；随后普通自旋锁，再闭合诊断/共享状态/路径。整数越界仍暂不选定。

> **2026-10-09 Windows词法生产修正及线程基础接续（最新）：** 用户已填完整Windows词法函数及公共声明，POSIX定义仍未写。助手仅把已实现函数..分支的!result.empty()改为!result_split.empty()，复现后纠正行为，保留CR；148个无关原生产/测试/CMake文件哈希相同。实际Windows根库/调用方Debug及Release各1071路径对照通过，PUBLIC宏和16cpp核对；MSVC审查仅排除既有C4324，未改生产警告，未重跑常驻CTest。下一组由用户写当前线程ID/yield/cpu_relax/sleep四函数，保留BQLog pthread_self/TLS和两系统sleep差异；临时Linux/原生Windows两配置编译与定向调用通过仅算候选。没有新增线程/POSIX生产定义或改根CMake；后续先闭合锁/诊断/共享状态再账户路径/基准目录，仍不造Manager。整数越界仍暂不选定。见[实际审查](../validation/WINDOWS_LEXICAL_THREAD_GUIDANCE_20261009_CHS.md)及[完整下一组指导](PLATFORM_THREAD_OPS_IMPLEMENTATION_GUIDE_20261009_CHS.md)。

> **2026-10-09 路径基础审查及实际库接入（最新）：** 已实现路径函数算法与BQLog一致；助手按此前授权整理系统文件、将Windows内部头移到src并同步namespace，保留函数体/CR。I/O和路径cpp已在WIN32/else接根target，当前每个平台16cpp。原生整库验证发现TimeZone仍用POSIX日历调用，已按BQLog补_s/_r平台宏及Windows名称localtime分支，偏移/缓存算法不重写；现有Layout测试预期prefix改名expected_prefix消除MSVC遮蔽提示。最终Linux及原生Windows实际库/调用方Debug/Release CTest各13/13（常驻11+临时实际路径/I/O两项），两平台源列表和PUBLIC宏已核对。Windows临时验证/WX仅排除既有C4324对齐提示，根警告配置未变，不称无例外严格通过；143个无关原文件哈希不变。下一组由用户写Windows完整get_lexically_path，指导候选Linux两配置各1071场景与BQLog摘录及真实string类型一致，仅算候选。POSIX账户/诊断及基准目录真实依赖接续后再写，不造helper/Manager；严格对齐及整数越界暂不选定决定继续有效。见[实际接入记录](../validation/PLATFORM_PATH_REVIEW_CMAKE_20261009_CHS.md)和[下一组完整指导](WINDOWS_LEXICAL_PATH_IMPLEMENTATION_GUIDE_20261009_CHS.md)。下方待填写/待接入文字按原阶段保留。

> **2026-10-09 UTF与平台I/O最新审查：** 用户已填写UTF-8软件函数并在审查期间修正位运算和Windows API抄写；重新核对最新实际源码，当前范围无新增阻塞缺陷。Linux实际Debug/Release严格库/调用方及外部CTest各13/13（常驻11+临时实际UTF/I/O两项），原生Windows对应源码严格编译运行通过；UTF对1,112,064个有效Unicode标量与BQLog软件源码摘录一致并回转，LinuxASan+UBSan同组通过，实际两系统I/O各31检查通过。根库仍14cpp，I/O仍未接根target；生产145文件SHA-256及CR未改，不把临时验证写成正式I/O集成或完整系统验收。下一组由用户填写路径判定和Windows内部编码适配器。用户最新回复“严格对齐BQLog”：不选择~user分隔符修正或账户查询失败回退；参考缺陷须明确说明，不宣称失败路径安全。整数越界两方案仍暂不选定。见[本轮记录](../validation/UTF8_PLATFORM_IO_CODE_REVIEW_20261009_CHS.md)和[完整下一组指导](PLATFORM_PATH_FOUNDATION_IMPLEMENTATION_GUIDE_20261009_CHS.md)。下方旧阶段结果按原日期解读。
> **2026-10-08 UTF通用核心生产对齐（最新）：** 用户授权助手修改代码。utf16_to_utf8_sw已统一为const char16_t*及码元数量、BQLog直接读取/代理项规则；容量参数保留未使用，调用方保证空间。Layout在insert_str_utf16处把字节数转成码元数，专用花括号扫描留在Layout，格式串主函数未改，CR保留。实际库/调用方Debug/Release严格构建和CTest各12/12（常驻11+临时参考摘录1）通过；参考软件核心65761组对照一致，ASan+UBSan定向7/7通过，PUBLIC宏传播已核对。根CMake未改，生产仍14cpp；UTF-8新函数仍待用户填写，平台I/O接入及其他验收边界不变。详见[实际对齐记录](../validation/UTF_CORE_ALIGNMENT_20261008_CHS.md)。下方旧字节utility接口/仅指导进度及81、117项结果按历史快照解读。

> **2026-10-08 文件I/O实际审查与UTF接续（此前阶段快照）：** 用户已填两系统8函数及公共声明。实际POSIX Debug/Release严格编译链接、定向各29检查通过；Apple宏已由用户在审查期间修正，Linux预处理不变。Windows最新原生MSVC/SDK编译失败，尚有2处：SetFileInfoByHandle应为SetFileInformationByHandle，以及读取循环>应为<；其他本轮发现已由用户修正并复核。助手未改生产源码/CMake/CR，I/O未接根target，根库仍14cpp；临时Windows完整候选各12/12仅算候选。用户最新要求UTF软件转换完全对齐BQLog，撤销尾部不足时停止并保留前缀的规则；不加多字节尾部break或容量截断，正常调用要求完整有效输入及足够目标空间。修订指导候选Linux Debug/Release及ASan+UBSan各81项空/合法输入检查通过，未写生产新函数；旧117项只属于已被替代的前缀候选。见[实际审查](../validation/PLATFORM_IO_CODE_REVIEW_20261008_CHS.md)及[UTF完整指导](UTF8_TO_UTF16_IMPLEMENTATION_GUIDE_20261008_CHS.md)。参考整树仍脏；路径/登记边界与整数越界两方案继续未决。

> **2026-10-08 按系统拆分（本段最新）：** 用户授权助手拆分已有头文件/函数，现采用公共 API、POSIX/Windows 原生类型头和系统 cpp。两个已实现分配函数仅搬移，CMake 按 WIN32 二选一，根库每个平台仍为 14 个 cpp；公共 API/命名空间不变。file_io.hpp 当前已由用户补到 8 个声明，两个系统 file_io.cpp 只有待填写骨架、未接库；助手未写 I/O 函数体。实际 Linux Debug/Release 严格构建与公开调用方链接、CTest 各 11/11（常驻 10 + 临时拆分调用方 1）通过；Windows 仅做模拟宏头文件语法检查，未编译运行原生 Windows 实现。详见 [系统拆分记录](PLATFORM_SYSTEM_SPLIT_20261008_CHS.md)，后续仍按 [文件 I/O 指导](PLATFORM_FILE_IO_IMPLEMENTATION_GUIDE_20261007_CHS.md) 由用户完成。下方原日期结果保留为历史证据。
> **2026-10-07 平台分配收口后的完整开发次序（本段最新）：** QLog feat/spsc-ring-opt@f24f3afc，BQLog78cbfbef；参考整树仍有MISO/构建修改。实际平台分配已修好声明/定义拼写并接CMake；根库现在14个cpp，Debug/Release严格库/公开调用方与外部CTest各13/13、实际Linux分配85检查及定向UBSan通过，常驻CTest仍10项。证据见 [平台分配实际收口](../validation/PLATFORM_ALIGNED_MEMORY_CMAKE_20261007_CHS.md)。下方旧13cpp/草稿未接库进度由本段替代，其他未完成验收边界不变。
 >
 > 用户手写未实现函数；助手审查、同步已实现名字及维护授权范围内构建/记录。以下为完整接续路线，不代表新模块已实现。下一组完整指导见 [平台文件I/O](PLATFORM_FILE_IO_IMPLEMENTATION_GUIDE_20261007_CHS.md)。

| 次序 | 完整依赖组与主要工作 | 完成条件及实际参考 |
| --- | --- | --- |
| 1 当前下一组 | platform原生文件类型；get_size(handle)/read/write/seek/truncate/flush/lock/unlock 8函数 | POSIX/Windows实际返回规则；公开头/实际库链接、短读/部分写/游标测试；参考platform_misc.h、posix_misc.cpp、win64_misc.cpp |
| 2 文件平台其余依赖 | 基准目录与词法路径、目录/文件查询创建删除列举、Windows UTF8→UTF16软件转换、open/close和exclusive登记成对实现 | 先讨论参考~user拼接/账户查询和登记寿命边界，不静默改合同；参考platform_misc/各platform misc及common_vars |
| 3 FileHandle / FileManager | native句柄的共享引用；idx/seq代际身份、描述符槽、copy/move/析构/关闭、seek读写、错误槽、路径工具 | 原生标量与拥有引用分开；最后引用关闭；同一槽重用不得误认旧handle。参考bq_common/utils/file_manager.h/.cpp；这是BQLog实际FileManager职责，不新增另一套Manager |
| 4 MemoryMap | 平台能力/对齐、offset与实际映射范围、文件持有、创建/刷新/显式release、失败状态 | Linux mmap/msync/munmap与Windows映射对象/视图；失败不返回伪有效映射；释放别名及不对齐访问进入时审查。参考bq_common/platform/io/memory_map* |
| 5 NormalBuffer | mmap/对齐堆内存所有权、new_created/use_existed/failed分类、构造失败回退、resize备份/重映射、析构删除规则 | 先核对OOM/备份失败与resize边界；不提前声称失败保留旧数据。参考normal_buffer.h/.cpp |
| 6 MISO | cache-line块、Head/原子布局、多生产者reserve/commit、wrap marker、单消费者read/return、初始化与恢复/Debug | 正式采用前重新核HEAD/diff；packed原子、别名、状态发布/元数据读取、未提交洞和恢复扫描单列审查；不能外部化存储或关recovery绕依赖 |
| 7 其余Buffer基础 | BlockList、MemoryPool、GroupList及原对齐/RW/时钟已接库；当前剩oversize_buffer | Oversize审查/接库后直接LogBuffer；miso_linked_list在原LogBuffer仅有未用include，其迁移留测试/独立学习；行为/退休/恢复测试仍后置 |
| 8 LogBuffer / TLS / LP-HP | TLS身份与退出标记、频率阈值/检查时间、LP→HP/HP→LP、运输context/消费序号、三种满策略与oversize、alloc/commit/read/return | 单消费者测试驱动先贯通；并发切换、顺序、TLS/Buffer析构交错、退休回收与恢复验证。必要时补实际时钟/线程身份/同步原语，保持毫秒取时位置 |
| 9 LoggerImpl / Manager运行态与公开记录入口 | 所有权/锁序/phase；配置应用、类别/等级门控与目标工厂；Record模板接alloc-write/commit；必要诊断/console平台能力 | 只建立BQ对应新运行态，不恢复旧Logger/Channel/codec/PreparedRecord；跨循环依赖先完整声明后补函数；参数序列化/正文仍使用现有协议 |
| 10 Appender链与日志处理 | AppenderBase公共过滤与虚log_impl；Console/TextFile；文件基类缓存、滚动/清理/磁盘满、每目标完整Layout；按目标处理/恢复/时间修正 | Logger/Snapshot类别规则与Appender不同，局部不能绕过全局拒绝；多目标失败独立；按scoped read handle实际寿命归还，持有期间可I/O。参考appender_base/file_base/console/file_text及log_imp::process_log_chunk |
| 11 Worker / 3线程模式 | 公共async Worker、独立Worker、sync；Manager遍历、读到empty、低空间通知、66ms等待、等待请求/cancel/join | 根据实际处理/Appender依赖接实流程；单消费者和Layout互斥，控制唤醒/退出边界；不预设公平预算/新mailbox |
| 12 Snapshot / reset / flush / uninit | Snapshot既有过滤/内存文本接口；配置重置部分失败、对象复用替换、保存文本与bool；force处理、缓存flush与文件同步、线程/TLS/静态退出交错 | 接口和锁序在9/11先规划，本阶段做完整功能与故障收口；不声明强事务reset、固定截止flush或退出自动排空/持久化 |
| 13 V1对齐验收 | 常驻定向回归、实际BQ差分、Ring行为/并发/回绕/恢复、Buffer策略/切换、配置边界、DST、文件故障与生命周期、示例文档与公平性能基准 | 写清一致/差异/待查；Debug/Release/消费者宏一致；旧47项不算；冷接入与热路径、LP/HP、策略/模式/目标/flush分开测 |
| 14 后续优化 | SIMD、布局缓存/正文共享、调度公平性/错误报告等用户另行选题 | 本轮先完成已确认软件基线；每项另提合同和A/B证据，不能先以优化理由改BQ规则 |

BQLog实际group_list依赖memory_pool、block_list、NormalBuffer；LogBuffer使用GroupList、MISO与oversize_buffer。2026-10-14复核固定提交：miso_linked_list只被LogBuffer头include，生产未实例化，不是主链实现的必补依赖。因此当前补Oversize后直接进入LogBuffer，相关链表测试后置。关键公共路径为：

配置树 → Buffer配置/过滤 → LogBuffer分配运输chunk → RecordHeader/模板序列化 → commit发布 → Worker/同步处理读取 → Logger全局类别过滤 → 各Appender/Snapshot选择 → 各目标完整Layout → Console/文件缓存。

记录归还在BQ scoped read handle对应的作用域结束时发生；输出过程中可能I/O，不新增“release后才允许I/O”约束。缓存write与文件同步属于各自flush路径，不能把每条记录的归还排在所有文件同步之后。Snapshot和Appender的独立选择次序按进入模块时实际process函数再核对。

仍未选定：配置数值越界保留double/原字符串；custom四字节字符协议仍static_assert未决。SIMD后置不改变软件输出合同。任何与BQLog功能/实现大方向的冲突，先给来源和替代方案与用户商讨；测试通过不是选择未决合同的授权。

> **2026-10-07 最新实现快照与接续路线：** 实际 QLog `feat/spsc-ring-opt@f24f3afcd8e9418ea48ad5a6b05680c622008145`，BQLog 参考 `78cbfbef4f87e7558335f0c04bf6dd6894b46451`；参考整树仍有 MISO/构建修改。下方旧 HEAD、缺失文件和阶段计划均按原日期保留，当前进度以本段及 [实际收口记录](../validation/BUFFER_CONFIG_CMAKE_20261007_CHS.md) 为准。

| 当前模块 | 实际实现与构建状态 | 验证边界 |
| --- | --- | --- |
| block SISO / Record | 单条、批量、恢复、Debug、记录视图与模板序列化已填写；普通 cpp 已接根库 | 不计作 Ring 行为/并发/恢复或完整记录差分验收 |
| TimeZone / Layout / utility | TimeZone、Layout 软件路径、UTF 与共享字符串 helper 已填写并接根库 | 已有 9 组 Layout/序列化组合回归；local 日历转换使用系统 DST 规则，DST 专项/完整差分/SIMD/性能仍未验收 |
| 配置、过滤与 Buffer 配置 | Property/PropertyValue、LogLevelBitmap、两个过滤 helper、log_buffer_config 与树转换均已接根库 | 既有实际公开调用方与定向检查通过；整数越界 double/原字符串仍仅记录、暂不选定 |
| 平台存储与完整 Buffer | FileManager/MemoryMap/NormalBuffer/MISO/BlockList/MemoryPool/GroupList/Oversize已接实际根库；LogBuffer声明/TLS及运输待用户填写 | 当前GNU/原生MSVC四配置只编译链接，测试后置；当前[LogBuffer声明/TLS基础](LOG_BUFFER_DECLARATIONS_ALIGNMENT_20261014_CHS.md)，再构造恢复/producer/consumer，不把构建当行为/恢复/并发验收 |
| Logger/Manager/Worker/Appender/生命周期 | 尚未建立新主链 | 按下方路线后置，不恢复旧接口 |

用户已确认依赖顺序：平台对齐分配 → 文件句柄/文件操作 → MemoryMap → NormalBuffer → MISO → LP/HP/TLS/LogBuffer。MemoryMap 作为 NormalBuffer 存储依赖已纳入本轮路线；本次不扩展 raw/compressed、跨语言等能力。BQLog MISO 本地布局/Debug 修改在正式采用前另行审查。

已有 Debug/Release 各 12/12 为仓库常驻 10 项加外部临时配置 2 项，本次只核对日志及对应 135 文件校验值，没有重跑测试，也没有新增常驻配置测试。

> 2026-09-23接续更新：SpscRingBuffer已迁入buffer模块，第一、二批已有草稿，第三、四批尚未完成。当前动手顺序及已有/缺失指南见[补全指南第0节](./R0_POST_CLEANUP_IMPLEMENTATION_GUIDE_CHS.md)。R0/R2是模块归属编号，不阻止当前先完成Ring；不要把本路线当作MISO/TLS等模块已经具备完整逐函数实现稿。


> **模块与进度更新（2026-09-23）：** 用户已写utility函数及部分Ring结构草稿；目标buffer/utility分模块，hpp/cpp归属见 [block指南第0节](./R0_BQLOG_BLOCK_RING_GUIDE_CHS.md)。当前迁移与生产Ring实现尚未完成，不能把新增草稿当作旧文件再次清理。

> **2026-09-23 Ring补充决定：旧字节Ring/配置及相关测试基准、过期Ring文档已删除。** 当前库仅版本基础；新SISO按8字节block、外部内存、32位游标重建，见 [Ring实现指南](./R0_BQLOG_BLOCK_RING_GUIDE_CHS.md) 与 [清理记录](./R0_RING_CONFIG_CLEANUP_20260923_CHS.md)。此前47项测试只是本次Ring删除前的历史结果。


> **2026-09-22 最新确认：完整 Layout 与 Record 一起对齐 BQLog。** 已实现/冻结部分不能限制对齐；旧 32 字节头、纳秒时间、旧 tag/DecodedArg、六参数正文函数不再是当前合同。当前 R0 入口为 [完整重建指南](./R0_BQLOG_LAYOUT_REBUILD_GUIDE_CHS.md)，详见 [ADR-015](./ADR-015-bqlog-layout-record-alignment.md)。以下冲突内容只保留为历史。


- 日期：2026-09-22；修订：按用户最新“其余建议也先对齐 BQLog”统一替换首版路线。
- 状态（2026-10-07）：对齐方向已确定，按模块推进；已实现范围及下一模块以上方最新快照为准，未完成完整异步日志主链。
- 权威开发目录：`/home/qq344/QLog`，Windows 入口 `\\wsl.localhost\Ubuntu\home\qq344\QLog`。
- 起点：`feat/spsc-ring-opt@94fefc0f29b21a1dbdd258fdb5725aadf84d770e`。
- 参考源码：`E:\VisualStudioProject\BqLog@60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`。
- 当前 R0 动手入口：[清理后的补全实现指南](./R0_POST_CLEANUP_IMPLEMENTATION_GUIDE_CHS.md) → [完整记录/Layout 算法指南](./R0_BQLOG_LAYOUT_REBUILD_GUIDE_CHS.md)。旧正文抽取指南与冲突实现已删除，见 [清理记录](./R0_CONFLICT_CLEANUP_20260923_CHS.md)。
- 决定：[ADR-014](./ADR-014-v1-bqlog-first-rebaseline.md)；旧文档效力：[作废清单](./V1_DESIGN_SUPERSESSION_20260922_CHS.md)。

## 1. 唯一设计原则

本轮后续 Logger 设计统一先对齐 BQLog。正文与完整行、Producer/Channel、Logger 状态、Manager/Worker、配置、Appender、动态 reset、文件 I/O、flush、退出均适用。

不再保留上一轮“其余按推荐”的 QLog 专用差异：正文共享、固定 SPSC 到 V1 结束、单槽管理命令、提前公平调度、强制释放 Frame 后 I/O、失败必保留旧配置、有限提交快照 flush、结构化持久化成功承诺、空配置自动 Console，都不是当前基线要求。

顺序是：查清参考调用链和实际行为 → 建立对应模块 → 实现并验证对齐基线 → 再提出有依据的优化。对齐不等于将疑似缺陷写成正确性保证；未查清的分支标记待核查，不自行补成另一种 QLog 设计。

范围是当前 V1 日志主链。现有 Ring/Record 保留可复用基础，但不能用旧集成合同阻止对齐；wire 不会因文档更新自动改变。raw/compressed、snapshot、跨语言和崩溃信号处理不自动全量移植。MemoryMap 已按 2026-10-07 用户确认纳入 NormalBuffer/MISO 的存储依赖；映射、恢复实现与验收仍未完成，不据此扩展其他持久化或崩溃恢复保证。其他主链依赖先说明再确定拆分范围。

## 2. 回退时的历史基线

2026-09-23 当前状态：旧codec、Logger/Channel/Producer、六参数格式器及关联测试已删除；当前库仅Ring/version。下列发现描述删除前的回退起点，不是现在的文件清单。

已核对 reflog reset 到 `94fefc0`；开始本轮时已跟踪文件无修改，未跟踪 `docs/validation/`、`tools/` 保留原状，不能证明当前 V1 已完成。

现存 Ring、Record codec/hash、FilterState、ProducerContext 骨架和文本格式化代码；没有后期 Appender/Worker/BackendSession。

已发现：`AsyncLogger::try_log` 尚只有声明；`TimeZoneConfig` 缺失；主库 CMake 未包含 async_logger、producer_context、filter_state、admission_clock、text_formatter 等新文件。对 `text_formatter.cpp` 的无产物语法检查实际报 `TimeZoneConfig does not name a type`。本轮未运行完整构建/测试/性能矩阵。

R0 改为完整记录/Layout 基础重建：BQLog 式 40 字节记录头、参数编码/视图、毫秒时间、TimeZone、拥有缓冲的完整 Layout 一起落地。旧六参数正文抽取方案撤销；见 R0_BQLOG_LAYOUT_REBUILD_GUIDE_CHS.md 与 ADR-015。旧 I1 验收仅说明旧协议，不能作为新记录/Layout 通过证据。

## 3. 模块对照与源码入口

下列路径相对 BQLog 根目录；行号对应参考提交。表中“对齐路线”是后续目标，不表示现有 QLog 已实现。

| 模块 | BQLog 已核实机制与入口 | 对齐路线 |
|---|---|---|
| 配置入口 | `src/bq_log/log/log_manager.cpp:41,123`：配置文本解析为 property_value；`log_imp.cpp:193,230`：按名字读取 appenders_config 对象 | 采用配置树/按名字的目标模型和 type 派发；旧扁平 AppenderConfig 不作为冻结 public 合同 |
| TLS | `src/bq_log/types/buffer/log_buffer.h:348`：最近 buffer ID 快缓存＋TLS map | 业务无需显式绑定；按 Buffer 身份访问线程上下文 |
| Buffer 路由 | `log_buffer.cpp:162`：频率统计；高频专有 SISO，低频共享 MISO | V1 纳入 LP/HP 路由；已有 SPSC 可作专有缓冲基础，新增真正共享 MPSC |
| 顺序/回收 | 同文件 `read_chunk_full_impl`、`verify_context`、`deregister_seq`、块遍历与退休 | 搬清序号、旧块、退出、消费者回收的完整职责；不先做 QLog 简化协议 |
| 满策略 | `log_buffer_defs.h:95`、`log_buffer.cpp:198`、`src/bq_log/api/bq_log_api.cpp:227` | discard/block/expand，默认 block；按参考 API 等待重试与唤醒，不保持旧“始终非阻塞”宣称 |
| Logger 状态 | `src/bq_log/log/log_imp.h:122`、`.cpp:495` | LoggerImpl 对应 log_imp：队列、目标、配置、处理、私有 layout/worker；无独立 Session 层 |
| Manager | `src/bq_log/log/log_manager.h:77`、`.cpp:163` | 拥有 Logger 集合、公共 Worker/Layout，协调创建/reset/处理/退出；不另建第二套所有权 |
| Worker | `src/bq_log/log/log_worker.cpp`、`log_imp.cpp:137` | async 公共、independent 专属、sync 调用线程三模式均进入对齐路线；先公共异步贯通，再补齐其余模式 |
| Appender | `src/bq_log/log/appender/appender_base.h`、`.cpp:79` | 非虚 init/reset/log 组织公共行为，虚 *_impl 扩展；配置数据不继承运行对象 |
| 文本 | `src/bq_log/log/layout.cpp:326`、`appender_file_text.cpp:17`、console 的 log_impl | 每个选中目标各自完整 layout；共享工作区不是共享正文；不引入跨目标文本缓存 |
| 文件层 | `appender_file_base.h/.cpp`、`appender_file_text.cpp` | 对齐文件公共层＋文本派生类、缓存、打开/切换、大小/时间滚动、清理职责；不先压成 QLog 单层设计 |
| reset | `log_manager.cpp:123` → `log_imp.cpp:218` → Appender::reset/add | 管理锁协调的同步修改；按名字复用/替换，刷新旧缓存和 I/O；不加 mailbox 和强事务保证 |
| flush | `log_manager.cpp:181,211` → `log_imp.cpp:495,561,576` | 强制消费/缓存刷新与文件同步分层，按参考调用关系实现；不加提交快照屏障 |
| 退出 | `log_manager.cpp:233`、`log_imp.cpp:120,334`、`appender_file_base.cpp:25` | 对齐 phase、cancel/awake/join、对象清理和析构刷新；不把 uninit 自动解释为可靠排空 |

## 4. 归属、调用流与生命周期

```text
业务 Logger 入口 / 配置文本
          ↓
LogManager（LoggerImpl 集合、公共 Worker、公共 Layout、管理同步）
          ├─ LoggerImpl A（LogBuffer、配置、Appender 列表）
          └─ LoggerImpl B（LogBuffer、配置、Appender 列表、独立 Worker/Layout）

异步写入：过滤 → TLS → LogBuffer LP/HP 分配 → 编码/提交
异步消费：Worker → Manager → LoggerImpl::process → 借用记录
          → Logger category → 逐 Appender 过滤
          → 该目标完整 layout → Console 输出 / 文件缓存（期间可有 I/O）
          → 归还记录 → 周期/强制刷新缓存

同步模式：调用线程同步缓冲 → sync_process → 同样的目标链
```

BQLog Manager 独占持有 log_imp；Logger 的轻量业务入口不是另一份运行态所有者。QLog 按此重新梳理 AsyncLogger 的角色，不在现有 unique_ptr<Impl> 外再套 Session。具体公有类名/签名随后按接口对照表落地，不从旧接口名称反推不相容行为。

公共 Layout 跟随公共处理路径；独立 Logger 使用自己的 Layout。管理线程可能在强制 flush 时取得排他权帮助消费，因此“永远只有固定 Worker 线程才能读”不是对齐合同；必须保证任一时刻同一 Buffer 只有一个消费者。

不沿用此前“销毁每个业务 Logger 立即注销运行态”的 QLog RAII 提案。参考的 Manager 生命周期和对象清理路径先查全，尤其 TLS 先退/后退、Buffer destruction_mark 与锁协调，之后才制定等价释放规则。

## 5. 配置与过滤

以 BQLog 配置树为基础：log 节点控制线程模式、缓冲策略、类别等，appenders_config 是 name→配置对象的映射，type 创建具体派生对象。引入解析/值模型的工作列入路线；具体 C++ 容器或解析器不是已冻结决定，不能再把“无需配置文本解析”当作限制。

当前 BQLog 核心：appenders_config 不是对象会失败；有效空对象不会自动创建 Console。旧 QLog 的“空列表规范化默认 Console”撤销为生效规则。可以提供显式含 Console 的示例配置，但不把示例当作核心隐式 fallback。

Producer 粗等级位图来自所有 Appender levels 的 OR，不排除 disabled 目标、不合并每个 Appender category；Logger category 也参与过滤。后台再次检查 Logger category 和各目标自己的 category/levels/enabled；积压按处理时配置处理。

2026-10-07 用户确认：保留参考当前两套类别匹配。Logger/Snapshot 调用 log_utils::get_categories_mask_by_config，按相等或紧接点号的子路径匹配，*default 另外允许索引0，*无通配语义；Appender::set_basic_configs 使用普通前缀或*，不特判*default。两套都只收集字符串mask，不trim/不折叠大小写；没有字符串mask时全部允许。后台 Logger 类别拒绝会在分发前返回，Appender/Snapshot 的局部允许不能绕过该拒绝。不要把 helper 直接替换 Appender 逻辑或新增统一匹配策略。源码与完整指导见 [过滤实现指南](CONFIG_FILTER_IMPLEMENTATION_GUIDE_20261007_CHS.md)。

类别掩码、等级名称、默认值、时间区字符串等先按 BQLog parser/helper 逐项核对；不拿现有 QLog 的固定 category 位图 public API、固定 offset_minutes 类型替代参考的配置能力。每种错误输入实际是拒绝、默认还是记录诊断，必须出对照测试。

## 6. Producer / Channel / Buffer

2026-10-07 最新接续：配置/过滤/Buffer配置已经完成实际CMake接入与所测公开调用方验证，见 [收口记录](../validation/BUFFER_CONFIG_CMAKE_20261007_CHS.md)。用户确认先补真实存储依赖再实现MISO，次序为平台对齐分配→文件/MemoryMap→NormalBuffer→MISO→LP/HP/TLS/LogBuffer；下一组完整指导见 [PLATFORM_ALIGNED_MEMORY_IMPLEMENTATION_GUIDE_20261007_CHS.md](PLATFORM_ALIGNED_MEMORY_IMPLEMENTATION_GUIDE_20261007_CHS.md)。不把完整MISO改成外部存储Ring来绕过NormalBuffer，也不提前创建Worker/Appender。下方前轮编译失败说明只保留为历史快照。

2026-10-07 早期审查快照（已由上方收口替代，clear 判断后续更正）：等级位图/过滤已由用户填写，当时记录了编译/链接问题，见 [实际过滤审查](../validation/FILTER_CODE_REVIEW_20261007_CHS.md)。下一组先建立Buffer配置数据与恢复身份校验、配置树映射，不提前创建Logger/Manager或完整LogBuffer。转换保留参考默认block、非对象/错误类型的默认、uint32容量转换、recovery请求与平台支持、策略不trim、0频率转UINT64_MAX；容量规范化留Buffer实际初始化。完整源码出处与逐函数指导见 [BUFFER_CONFIG_IMPLEMENTATION_GUIDE_20261007_CHS.md](BUFFER_CONFIG_IMPLEMENTATION_GUIDE_20261007_CHS.md)。候选验证不计生产Buffer/并发/恢复验收。

LP 是低频线程共享多生产者缓冲，HP 是高频线程独立缓冲；阈值和降级、旧块退休、消费序号共同工作。参考默认阈值 1000 次/秒，检查间隔 1000ms；先保留参考基线，再测是否有优化需求。

discard、block、expand 都纳入，默认 block。旧 try_log “必不阻塞”与此不相容，后续公开入口按 BQLog 行为重新整理，不能保留旧注释而让实现阻塞。共享 MPSC 允许正常路径所需原子竞争；扩容/路由转换和冷接入的分配也按实际路径描述，撤销整个 Producer 路径无共享 RMW/无分配/无锁的全局承诺。

必须成套覆盖：LP→HP、HP→LP、块替换、线程退出、Logger/Buffer 析构交错、消费者校验/推进序号、退休回收、等待重试与唤醒。BQLog 具有 oversize 外部缓冲，纳入 Buffer 机制核查，不用旧“超过固定 Ring 就拒绝”代替参考；QLog Record 安全长度检查不会凭空取消。

Record 按 ADR-015 重建为 BQLog 对应头、tag、参数布局、扩展信息和毫秒时间；旧 codec 不再是限制。BQLog 路由 header 的 context/序号仍属运输元数据，不混入 40 字节日志头。新旧进程内协议不混用，不承诺旧持久化文件兼容。

BQLog API 在分配前获取供 Buffer/记录使用的时间；不再保留上一版“路由额外取时、Record 仍按旧 admission 时点”的自定适配。时间时点、单位及 process_log_chunk 的非递减修正一并核对。保留旧时钟/Record 工具只代表可复用，不冻结原取时顺序。

## 7. Worker 与文本目标

async 默认公共 Worker，independent 专属 Worker，sync 不启动异步消费线程。公共 Worker 通过 Manager 遍历异步 Logger，各 log_imp::process 读到 empty；不先引入每 Logger N 条预算或新的公平轮转算法。

BQLog Worker 有 66ms 等待周期、低空间触发唤醒、等待请求处理与退出检查；文件缓存 process 路径使用约 100ms 刷新条件。它们是实现参数，不是端到端延迟保证。先按源码对齐通知/等待次序，再测空闲 CPU、延迟和繁忙 Logger 对其他实例的影响。

Appender 通过公共非虚 log 完成过滤，然后 log_impl。Console 与 TextFile 各自调用 do_layout：本次前缀和本次正文都生成，随后实际终端输出或写入文件缓存。现有 render_message_utf8/compose_line 可作为底层工具，但不能在 fanout 外只算一次正文。

文件缓存分配可触发 flush，文件准备/滚动也可能在记录处理链里进行；Console 可直接输出。撤销“必须先 release Frame 才允许 I/O”和强制每 Console 自有 batch 的先验设计。归还记录的时机按 BQLog 的 scoped read handle 生命周期建立对应；任何被保存的数据仍必须拥有有效存储，不能留下悬空 Ring/scratch 指针。

同一目标过滤/输出失败不阻止后续目标分发；共享线程的慢 I/O 会拖慢其他 Logger/目标，不宣称吞吐隔离。原字符串长度、格式语法、时区、线程名、换行/前缀等通过参考输出逐项核对，不预置自定义解析缓存。

## 8. 动态 reset：按参考顺序与真实返回语义

参考调用链：Manager 解析配置 → 获取 logs_lock_ 写锁 → 找到 log_imp → reset_config 获取实例保护 → 刷新旧缓存与文件 I/O → 移出旧列表 → 按名字寻找可 reset 对象 → 清理未复用对象 → 创建剩余目标 → 重算合并等级位图 → 更新类别等配置。

没有单槽 mailbox、固定唯一管理线程、busy 提交背压或等待 Worker 接受命令的前置结构。先对齐同步管理锁及嵌套关系；实际锁序统一从外层 Manager 到实例/资源，不仅查看某个局部锁就认定整个流程安全。

必须如实呈现参考行为：

- appender_base::reset 对类型作检查后可能清理并重设基本状态，再调用 reset_impl；返回 false 不等于旧对象完全没变。
- log_imp::reset_config 会先处理并移除旧对象，再创建新对象；不是先把全部新文件准备成功才交换。
- add_appender 可失败，而调用点不一定把失败聚合成整个 reset 失败；不可宣称“所有目标成功应用”。
- Manager::reset_config 找到 Logger 后返回 true，即使内部 reset_config 返回 false；内部成功时才更新保存的配置文本。该 bool 不是事务成功证明。
- 文件 init 并不保证已经成功打开最终输出文件，实际打开可延后到处理记录时。

因此撤销普通 reset 失败必保留旧配置/旧对象、不排空就拒绝切换、提交后结构化清理失败结果等 QLog 专用提案。先建立与参考一致的部分失败状态和测试；若之后要改良返回语义，另提设计，不藏进本次“对齐”。

## 9. 文件 I/O 与故障

以 appender_file_base 管理文件和缓存，appender_file_text 负责文本布局/拷贝；先对齐 init/reset、缓存分配/归还/完成游标、flush、按大小/时间切换、过期/容量清理。不要提前替换为另一个 QLog 文件状态机。

`flush_write_cache` 按实际写入字节调整未写缓存。ENOSPC 路径设置 disk_full_drop_，保留待写数据并在后续 flush 重试；新记录在该目标门口被拒绝，其他目标独立处理。成功刷新并排空后解除该标志，期间未接收记录不会自动补发。

不能推广为“所有错误保留后缀”：参考非磁盘满写错误会尝试 open_new_indexed_file_by_name；该函数先关闭旧文件、clean_cache_write，再准备新文件。这样可能丢弃未写缓存。轮转也经过有关路径，应测试真实剩余数据行为，不添加默认无损承诺。

ENOSPC、平台磁盘满、其他错误、部分写、打开失败、flush_file 失败各走哪条分支，以平台层返回值为准；EDQUOT 不自动等同 ENOSPC，EAGAIN/EINTR 也不能直接套用回退前 QLog 状态机。文件同步失败参考会记录诊断；没有据此证明 public 调用获得结构化持久化结果。

代码中疑似缺陷（例如重叠复制或异常分支）应单列证据、复现和是否修复的决定；不能为了逐字移植制造未定义行为，也不能在路线中悄悄声称参考拥有更强保证。

## 10. flush、退出与可观察结果

force_flush/force_flush_all 在 Manager 排他协调下调用 process(true)；process 读取到 empty，再刷新 Appender 缓存。它不是“调用瞬间已提交集合”的快照屏障，持续生产下不承诺固定等待上界。sync 模式的强制 flush 分支与异步不同，按参考单独验证。

flush_appenders_cache 与 flush_appenders_io 是不同函数，force_flush 的调用链不能直接等同文件同步。撤销前一版有限提交截止点、durable 选项和新 FlushResult 的预设设计；公有返回值首先按 BQLog 对照，不把入队/目标接收/写入/同步混为一谈。

uninit 路径：管理协调、解除 console callback、phase 变为 uninitialized、公共 Worker cancel/awake/join、独立 Worker cancel/awake/join。log_imp 清理目标和缓冲，文件 Appender 析构按开关尝试刷新缓存。不能仅由这些函数推导“uninit 自动排空所有 Ring”或“全部文件已持久化”。

正常结束的示例应明确停止业务日志调用后使用参考支持的 flush/退出入口；不得替参考补上不存在的可靠 shutdown 合同。生产者仍在 block 重试时退出、静态析构顺序、TLS 退出、回调重入等列入生命周期专项核查，先记录实际限制，再确定是否存在必须单独修复的问题。

诊断同样先学习 BQLog 的诊断开关、错误输出和返回路径。QLog 原计数公式可用作外部验收观察，但不据此新增生产 public statistics API、复杂 per-target 结果协议或让诊断参与正确性。

## 11. 分期路线

| 阶段 | 工作内容 | 验收与边界 |
|---|---|---|
| R0 记录/Layout 基础 | 重建 Record/编码/视图、TimeZone、完整 Layout 对象和缓冲；列主链接口映射 | 结构断言、记录往返、完整字节差分、实际库链接；撤销旧正文抽取验收 |
| R1 配置与运行态骨架 | 配置树/解析入口、Manager/LoggerImpl 归属、类型工厂 | 默认值/空配置/无效配置与参考对照；没有 Session/双重所有权 |
| R2 Producer / Buffer | 8字节block SISO已填写；先补平台分配、文件操作、MemoryMap与NormalBuffer，再补缓存行block MISO、TLS、LP/HP、频率、顺序、块退休、满策略、oversize | 切换顺序、失败/退出、竞争、block 唤醒与 expand；以测试消费者先验证 |
| R3 Worker 与模式 | 公共异步贯通，随后 independent/sync；处理、等待、管理协调 | 单消费者与 layout 互斥；按参考调度行为；不先优化公平性 |
| R4 Appender/layout 接入/文件 | 公共基类、文件基类、Console/TextFile，接入 R0 Layout、缓存/滚动/故障 | 真实字节对照、时区/过滤、多目标；持有记录期间 I/O 与参考一致；故障真实行为可复现 |
| R5 reset/flush/退出 | 完整动态替换、部分失败、强制处理、取消/join/析构关系 | 返回 bool 与实际状态分开核对；不额外声称原子 reset、有限 flush 或可靠退出 |
| R6 V1 对齐验收 | 示例、文档、测试矩阵、语义匹配基准 | 记录一致/不同/待查项；不同项必须有明确依据，不能用性能理由先改语义 |
| R7 后续优化 | 正文共享、缓存、管理无锁、调度公平性、增强错误报告等 | 每项另提合同和 A/B 证据，检查后再开发 |

reset/flush/退出的接口及锁序在 R1/R3 就设计，R5 是完整功能和故障收口。各阶段先读源码对照表，再写逐函数/成员/状态转移指南，不把本文路线当作可直接照抄的最终并发实现。

测试先覆盖参考真实行为，不强行套用已撤销的强保证。性能报告分开冷接入、LP/HP/切换、三种满策略、三种线程模式、多目标重复 layout、TextFile 缓存/write/同步；列尝试/接纳/拒绝/未开始、延迟、CPU、内存、输出核验、重复数据和源码身份。旧回退前数字仅作历史参考。

## 12. 本次交付与下一步检查

此次已更新路线、ADR、项目约定和作废标记；未修改生产源码、未提交 git commit、未运行完整生产回归。

维护者检查重点：本路线是否完整体现“所有当前 V1 主链模块先对齐 BQLog”，以及表中实现顺序、范围外能力是否合适。不再逐条要求重新接受此前的 QLog 优化建议。

检查后进入 R0/R1 的源码到类/函数/成员详细对照，记录仍需查证的 parser、平台 I/O、退出与异常分支；任何为了安全正确性必须偏离参考的修改单列讨论，任何性能优化推迟到基线之后。
