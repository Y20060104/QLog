# HashMap 原容器迁移决定与 MISO 初始化实际收口

> **2026-10-13 原HashMap既有调用并发迁移（最新）：** 用户写MISO，助手只迁移此前四处std::unordered_map：Layout线程缓存、Property/PropertyValue字典和文件独占登记，按BQ均用普通hash_map；MISO仍用inline版本。Layout恢复set_expand_rate(4)，std::string哈希在原calculator中按BQ string::get_hash适配；add/is_empty/key/value及深复制调用同步，原CR和格式保留。实际七cpp/五独立公开头四配置严格编译通过；用户MISO草稿导致首次实际整库失败未改，隔离副本冻结三项旧依赖，四配置常驻19/19＋参考4/4，各23/23，四组差分输出一致。193原文件保护核对无越界改动，13受改文件与验证副本一致；不接受当前MISO/并发/退出/恢复/性能。见[决定](HASH_MAP_CALLERS_ALIGNMENT_20261013_CHS.md)与[验证](../validation/HASH_MAP_CALLERS_BEHAVIOR_20261013_CHS.md)。下方为历史快照。

> **2026-10-13 原HashMap实际补全与MISO写侧接续（最新）：** 用户授权补全Hash部分。原HashMap双版本、完整type_traits/声明/定义和已写type_tools修正已落实际源码；正确allocator/入口、平台宏、MISO草稿和原CR保留，impl无pragma once。新增3组常驻HashMap回归，每配置48324检查；GNU/原生MSVC x64 Debug/Release严格实际库及公开双翻译单元调用方链接通过，常驻15/15＋外部固定提交参考3/3，各18/18，结果与BQ完全一致，Linux定向ASan/UBSan三组通过。PUBLIC Debug宏与31/32源核对，MSVC只沿用外部C4324/C4146排除。下一组原TLS宏/缓存及fetch_add预留、CAS末尾回退、commit完整候选四配置仅仓库外编译链接通过、未运行，实际新MISO定义由用户填；不记TLS退出/并发/恢复/性能验收。指南留对话、文件只记决定/证据。见[验证](../validation/HASH_MAP_BEHAVIOR_20261013_CHS.md)。下方为此前快照。

日期：2026-10-13。完整实现指导留对话，本文件只记录决定、证据与边界。

## 用户确认与源码基准

用户在原容器/std::unordered_map选择中明确回复“先迁移 BQLog 原 hash_map_inline”。采用78cbfbef4f87e7558335f0c04bf6dd6894b46451提交原hash_map.h/hash_map_def.h/hash_map_impl.h、allocator.h和type_traits/type_tools实际依赖；六个采用类型文件及platform/macros.h均无HEAD文件差异，BQ整树仍脏，MISO仍取提交快照而非本地删Debug/改align草稿。

## 本组范围与组织

由用户填写include/qlog/utility/type_traits.hpp、allocator.hpp、hash_map.hpp、hash_map_def.hpp、hash_map_impl.hpp，补充现有platform/macros.hpp的原语言版本探测和utility/type_tools.hpp中原条件类型、数值/指针/成员hash_code散列器、tagged placement new及对象构造析构。模板全部在头内，不建空cpp、不增加新Manager/简化容器包装。

保留原三文件宏生成hash_map和hash_map_inline两套class/iterator/kv_pair，内部bucket/node索引、链表、free list、Allocator声明/default、原断言和默认expand_rate_=1.5保持。仅qlog::utility命名空间/QLOG宏、include路径、std基础/C调用限定及现有uint32 round_pow_of_two适配；不替换std::unordered_map/std::hash/CRC，不增加检查或改变算法。impl/def故意没有pragma once，保留两次包含；只允许入口hash_map.hpp直接包含。

set_expand_rate(4)实际控制expected_bucket_size=uint32(float(size()+1)*rate)+1，再将桶数向上取整到2次幂，并非每次旧容量乘4。节点容量单独按nodes_size()+1触发取整。bucket重排保持同桶节点连续；node扩容移动key/value并析构旧对象，原value地址会失效。

## 实际 MISO 初始化组修正

用户已填头/cpp。助手仅修正head_(config)->nullptr、head+1->head_+1、need_recoveery拼写、aligned_blocks_count_和getter原名、commit参数恢复WriteHandle，已有映射恢复失败改调init_with_memory_map以写回checksum；保留原fresh mmap状态init_with_memory、qlog_mmap路径命名前缀、全部11行原注释/CR。缺少的get_buffer_addr getter留本轮对话由用户补写，alloc/commit/消费者及Debug全局查询未代写。根CMake注册实际MISO cpp，实际库Linux31/Windows32源；187个生产/测试/CMake只有MISO头/cpp与CMake改变，其余184个SHA-256不变。

实际GNU/Linux、原生MSVC x64 Debug/Release严格库构建及双翻译单元公开MISO初始化调用方链接四配置通过，现有CTest各12/12，PUBLIC QLOG_DEBUG只有Debug并同时核对库与两个调用方。MSVC外部沿用C4324/C4146排除，生产警告不改。新MISO调用方未运行，结果不计恢复、并发或性能验收。

## 参考边界

原default_allocator是malloc/free，分配返回未检查、没有新增超额对齐支持；原key/value分别placement构造和显式析构；异常中途失败没有强回滚保证。原浮点hash保留union读另一成员的编译器/对象访问边界，泛型key需hash_code()，不擅加std::string散列适配。原复制/移动不传播expand_rate_，自赋值无特别分支；旧iterator合法性检查不是seq/generation方案，node扩容使旧value指针失效。未声称OOM/异常/全部泛型或跨平台对象生命周期安全。

MISO写侧实际是fetch_add_relaxed预留＋空间不足时CAS回退；上轮“CAS预留”概括不准确，后续指导保留原算法、原>与>=边界、原状态发布和Debug统计。真正TLS非POD生命周期宏尚需按原迁移，不能直接以一个thread_local对象删除recycled分支；消费者Debug全局线程检查依赖仍未闭合。

## 候选验证及后续

完整原容器及全部必要依赖仅仓库外准备，GNU/Linux和原生MSVC x64 Debug/Release严格模板编译、公开双翻译单元链接四配置通过；实例化原hash_map与hash_map_inline、指针/整数/浮点/成员hash_code键、非平凡value、const/普通iterator、增删/clear/reset、复制/移动及allocator构造析构。候选未运行、未执行CTest，不计生产HashMap完成、行为差分、TLS退出或性能验收。

证据：D:/qq344/Documents/qlog-miso-write-guide-20261013-155320。新原容器生产头尚未创建/覆盖；现有macros/type_tools生产未改。用户填完原容器后，下一组接原TLS缓存和alloc_write_chunk/commit_write_chunk，再消费者与真实Debug全局依赖。未进入Worker/Appender，整数越界两个方案继续仅记录。
