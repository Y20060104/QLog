# MISO 原提交版本对齐决定与 NormalBuffer 实际收口

> **2026-10-13 MISO读侧Debug依赖收口（最新）：** 用户授权助手补原log_global_vars计数器/RAII，保留原名称/继承/relaxed/嵌套规则，Manager/Appender与完整退出逻辑后补；SISO/MISO查询共享实例开关与全局counter。已写TLS赋值/QLLOG_DEBUG/fetch_add_relaxed三处修正，原CR保留，src/global/log_vars.cpp接库。实际GNU/原生MSVC Debug/Release严格库及两TU依赖链接、常驻20/20＋参考4/4各24/24；线程策略14检查，初次夹具对齐错误已修正。读侧五函数/三TU候选只在仓库外四配置编译链接、未运行，生产由用户写。用户明确原status→block_count→switch顺序不改，保留unused未初始化/并发元数据读取及原typed-pointer边界，不记MISO行为/并发/恢复/退出/性能。见[决定](MISO_READ_ALIGNMENT_20261013_CHS.md)与[验证](../validation/MISO_READ_DEPENDENCIES_20261013_CHS.md)。下方为历史快照。

> **2026-10-13 原HashMap实际补全与MISO写侧接续（最新）：** 用户授权补全Hash部分。原HashMap双版本、完整type_traits/声明/定义和已写type_tools修正已落实际源码；正确allocator/入口、平台宏、MISO草稿和原CR保留，impl无pragma once。新增3组常驻HashMap回归，每配置48324检查；GNU/原生MSVC x64 Debug/Release严格实际库及公开双翻译单元调用方链接通过，常驻15/15＋外部固定提交参考3/3，各18/18，结果与BQ完全一致，Linux定向ASan/UBSan三组通过。PUBLIC Debug宏与31/32源核对，MSVC只沿用外部C4324/C4146排除。下一组原TLS宏/缓存及fetch_add预留、CAS末尾回退、commit完整候选四配置仅仓库外编译链接通过、未运行，实际新MISO定义由用户填；不记TLS退出/并发/恢复/性能验收。指南留对话、文件只记决定/证据。见[验证](../validation/HASH_MAP_BEHAVIOR_20261013_CHS.md)。下方为此前快照。

> **2026-10-13 MISO初始化实际收口与原HashMap路线（本段最新）：** 用户已填写MISO初始化；助手修正已写头/cpp编译错误、原成员/getter与commit签名及旧映射恢复失败的初始化分支，11行原CR/注释保留，get_buffer_addr遗漏由用户补写。根CMake接实际MISO cpp，实际GNU/原生MSVC x64 Debug/Release严格库与双翻译单元初始化调用方链接、现有CTest各12/12通过，源数31/32与PUBLIC Debug宏核对；新调用方未运行，不计恢复/并发/性能。用户明确选择“先迁移BQLog原hash_map_inline”，所以先原容器及分配/类型依赖，再原TLS缓存与fetch_add预留/CAS回退/commit；不采用std::unordered_map。完整原容器候选四配置仅仓库外严格模板编译链接通过、未运行，新生产由用户写，macros/type_tools生产不改。保留原bucket/node/list/free布局、双版本、默认/断言、set_expand_rate实际目标桶数规则及原对象访问/失败边界；指导留对话、文件只记决定。见[本组决定](HASH_MAP_ALIGNMENT_20261013_CHS.md)。下方为历史快照。

日期：2026-10-13。完整逐函数指导留对话，本文件只记录决定、阶段边界及验证。

## 参考基准（用户已确认）

QLog feat/spsc-ring-opt@a1ba6b4d45a25d60242327b68894c23d56c027e0；BQLog main@78cbfbef4f87e7558335f0c04bf6dd6894b46451。用户明确选择提交版本78cbfbef：保留head的128字节对齐与完整Debug字段、检查和统计。BQ本地MISO草稿把head改为alignas(4)，并删减多处Debug逻辑；本组不采用这些修改，也不覆盖参考草稿。MISO采用git show HEAD快照，不能把本地改后行号当作提交版本函数行号。BQ整树仍脏。

## NormalBuffer实际收口

用户已填normal_buffer.hpp/.cpp、create_memory_map_result和128/LOG2=7共享常量。助手仅修正已写cpp的get_min_of_memory_map_file、memory_map_file缺下划线、f/if及back_update/back_updata不一致，统一原back_updata名；三个问题处的原CR全部保留。根CMake加入实际src/buffer/normal_buffer.cpp，源数Linux30/Windows31。183个生产/测试/CMake文件仅cpp与CMake改变，其余181个SHA-256不变。

实际GNU/Linux及原生MSVC x64 Debug/Release严格库构建、两个独立公共头调用方编译链接通过，四配置现有CTest各12/12，库和两调用方PUBLIC QLOG_DEBUG核对。MSVC仅在外部审查项目沿用既有C4324/C4146排除，没有修改生产警告。新NormalBuffer调用方未运行，不计resize/OOM/存储/恢复/并发或性能验收。原构造堆回退、resize未检查OOM、失败删除、原复制资源边界及未迁移测试注入规则不变。

## MISO本组范围及组织

目标include/qlog/buffer/miso_ring_buffer.hpp与src/buffer/miso_ring_buffer.cpp，namespace qlog::buffer，class alignas(128) miso_ring_buffer；原枚举、union block、chunk_head_def、head、cursors_set、字段顺序、全部public/private声明、原默认/断言/内存序保留。使用现有LogBufferConfig/memory_map_buffer_state/enum_buffer_result_code/读写handle名称、std::unique_ptr/string及现有utility::round_pow_of_two适配。普通setter移cpp，短getter/模板/cursor_to_block保留类内，不增加Manager/拥有包装或防御断言。

本组给齐完整声明/布局和构造、析构、create_memory_map、init_with_memory_map、try_recover_from_exist_memory_map、init_with_memory、set_thread_check_enable。alloc/commit/read/return/discard/traverse及is_thread_check_enable本轮仅保留声明，不提供占位定义。本组所有已给初始化函数的实际依赖闭合；构造/恢复不调用尚未定义的线程检查查询。

必要新依赖：platform/macros.hpp只提供原MSVC pragma/GNU packed宏；utility/type_tools.hpp提供原typed-pointer访问模板及两个宏；现有log_buffer_defs.hpp加入原BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT和QLOG_POD_RUNTIME_OFFSET_OF。保持模块归属和参考直接指针访问，不增加检查、别名绕过选项或另一套原子类。

头布局：block=128、chunk_head数据偏移8；head=128；runtime cursors_set=256且读写原子分开128字节区。正常初始化把容量规整到2次幂且至少16块；映射页取整后的额外尾部不扩大Ring逻辑容量。原MISO容量初始化后固定，NormalBuffer可resize不等于MISO可并发resize。

原创建路径bqlog_mmap/mmap_<log_name>/lp/<log_name>.mmap、map_size到uint32_t的转换及const_cast写配置副本保留。现有calculate_check_sum对应BQ字符串散列（初始0、先乘1099511628211再xor字符），不能擅换CRC get_hash_64。原init_with_memory_map也设置init_with_memory，不能按函数名擅改init_with_memmap。

## 参考边界明示

- 原MISO构造没有检查NormalBuffer::is_valid；堆也失败时会在空地址继续初始化。容量roundup/加头转换原样，无完整超大尺寸溢出合同；不称OOM安全。
- 24位block_num原uint32指针读写跨status字节，按原小端布局；packed原子包装/裸字节对象访问、null形式data_block_offset和0x10000形式runtime offset均按原编译器路径保留。编译通过不等于对象寿命/别名/跨平台证明，未测32位/ARM/大端。
- 恢复used检查跨度与容量；invalid原先推进cursor再检查边界，没有额外零block_num检查。损坏invalid且block_num=0可使扫描不推进，不称全部损坏文件均安全返回false。本组未擅改恢复判定。
- get_mmap_misc_data<T>原断言只限制sizeof，不增加alignof检查。没有新增flush或持久化保证。
- is_thread_check_enable在Debug实际依赖BQ log_global_vars::thread_check_enable_counter_，QLog没有对应日志运行态；保留声明，消费者/Debug组前需讨论并闭合真实依赖，不返回固定true/false替身或删全局开关。原完整Debug逻辑在后续定义组继续保留。

## 验证和后续顺序

完整本组候选仅仓库外准备，实际QLog加完整声明/布局和7个初始化组定义在Linux/GCC与原生MSVC x64 Debug/Release严格编译、两翻译单元公开调用方链接四配置通过，源数Linux31/Windows32，PUBLIC Debug宏核对。候选未运行、未执行CTest，不计整个MISO实现完成或恢复/并发/性能验收。原生产新MISO及两helper头未创建，未提前接生产CMake。

证据、修正前备份及两份MISO参考：D:/qq344/Documents/qlog-normal-buffer-review-miso-guidance-20261013-102440。

接续分三组：本组声明/布局/初始化恢复；生产者TLS缓存/reserve/commit；消费者read/return/discard/traverse及Debug真实依赖收口。再完成其他Buffer基础和LP/HP/TLS/LogBuffer。未进入Worker/Appender或恢复旧Logger/codec/PreparedRecord；配置整数越界两个方案仍仅记录、暂不选定。

## 2026-10-13 写侧接续决定

沿用78cbfbef提交，head 128对齐和完整Debug保留。实际HashMap已完成后，本组由用户填写platform/macros.hpp原TLS宏与分支提示、cpp原miso_tls_buffer_info/miso_tls_buffer_map_type/miso_tls_info及TLS实例、alloc_write_chunk/commit_write_chunk。辅助类型只放buffer cpp，不新增拥有包装/Manager；保留懒建字典、最近buffer缓存、set_expand_rate(4)、is_new_created及recycled时栈读游标回退。

预留严格采用fetch_add_relaxed，后检查不足时只对末尾预留CAS回退，success/fail均relaxed。原预检查>、后检查>=、超半环时的错误分类、跨界整段need_block_count invalid标记/continue、low_space_flag刷新与原Debug统计均保留，不改CAS预留或按尾部剩余块重新计算padding。提交仅对success发布status.store_release(used)，统计按block字节。只在cpp调用TLS宏，保持原生成结构和pointer/recycled POD槽，不直接替换thread_local对象或添加backoff/取消/断言。

边界：原size+data_offset为uint32加法，极端输入可能溢出；size=0会分配1块而恢复拒绝used且data_size=0。本组保留并明示，不称所有非法size均安全拒绝。线程退出模式及指针地址复用缓存没有世代号，候选仅编译链接，未声称通用退出/复用安全；参考注释的高竞争溢出风险保留，不作wait-free或性能证明。read/return等和Debug全局查询保持未定义，下一组前闭合真实全局控制依赖，不添加固定返回替身。
