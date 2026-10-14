# Buffer 块链表集成对齐决定（2026-10-13）

> **2026-10-14 当前更新：** 用户BlockList头/cpp及三个直接工具均已填写；静态审查无必要源码修正，实际cpp已注册CMake。GNU/原生MSVC Debug/Release实际库及两TU调用方编译链接通过，未运行/CTest。下一模块原MemoryPool，见[决定](MEMORY_POOL_ALIGNMENT_20261014_CHS.md)与[验证](../validation/BLOCK_LIST_MEMORY_POOL_BUILD_20261014_CHS.md)。下方为上一轮候选快照。

## 当前范围

用户要求继续下一轮集成，运行测试可以后置。新生产函数仍由用户填写；助手审查并修正已写内容，完整实现指导留在对话中。

QLog 权威目录 `/home/qq344/QLog`，分支 `feat/spsc-ring-opt`，HEAD `a1ba6b4d45a25d60242327b68894c23d56c027e0`。BQLog 采用提交 `78cbfbef4f87e7558335f0c04bf6dd6894b46451`。BQLog 整树仍有 CMake/MISO 等本地修改；采用的 block_list、group_list、memory_pool、miso_linked_list、oversize_buffer、log_buffer、log_buffer_defs、type_tools/macros 参考与该提交逐文件核对无差异。MISO 始终读取提交快照。

## MISO 已写读侧收口

用户已填 read_chunk、read_an_empty_chunk、discard_read_chunk、return_read_chunk、data_traverse。逐函数比对上一轮候选，除等价的 `read_thread_id_ == 0` 操作数顺序外，唯一必要修正是 read_chunk 中 `is_read_chunk_waiting_for_retrun_` 改回已声明的 `is_read_chunk_waiting_for_return_`；否则 Debug 编译失败。原 CR 保留。

保留原 status acquire → get_block_num → switch 顺序，以及 data_traverse 的无条件尾部游标累加、无消费语义；保留原 N/4 批量释放、空句柄释放残批、discard 不消费。未初始化或未提交块数的普通读取边界继续明确记录，不延后读取、不添加清零或防御断言。根 CMake 原已注册实际 MISO cpp，本轮无需重复添加。

## 下一模块与依赖顺序

下一模块为 qlog::buffer::block_node_head / block_list：include/qlog/buffer/block_list.hpp 与 src/buffer/block_list.cpp。块头内 placement 构造现有 SpscRingBuffer，固定索引链表承接 HP 路径；名称、成员布局、128 字节块头保留区、32 位 index/aba_mark、原 push/pop 与两个 thread_unsafe 接口、默认参数/断言/内存序按 BQLog。

依赖只补原 block_list_type 枚举、原模板 roundup_pow_of_two 和原 maybe-uninitialized 警告作用域宏。枚举归 buffer/log_buffer_defs.hpp；泛型工具归 utility/type_tools.hpp；警告宏归 platform/macros.hpp。已有 uint32 round_pow_of_two 不改，不把 size_t 输入缩窄到 uint32，也不增加 Manager 或存储包装类。bq::launder 使用 C++20 已有 std::launder；原 enum_new_dummy placement new 和现有诊断入口复用。

当前 SpscRingBuffer 内部 CACHE_LINE_SIZE 仍为 64（旧 SISO 指南的既有实现），提交版 BQLog SISO 使用公共 128；它们的对象大小、Head 与最小申请长度不应宣称逐字节一致。本组 BlockList/块头前缀使用公共 128，并按 sizeof(SpscRingBuffer) 与其真实 calculate_min_size_of_memory 计算外部数据偏移，复用现有 SISO 接口；未扩展到整套 SISO 重写，不保证两项目映射文件互读。旧 SISO 文档中的“当前常见目标64”不能作为 BQLog 当前源码事实。

原构造的 min_size 只在 assert 中使用，候选使用 [[maybe_unused]] 兼容 Release 的严格警告检查；不改变正常流程或断言。BQLog 的 packed typed-pointer 对象访问、恢复数据/断链检查边界和 16 位 ABA 标记回绕均保留并说明。CAS success release / failure acquire 原接口及 GNU 后端映射继续保留，已有 GCC 失败内存序合同边界不变。

后续为 memory_pool → group_list，oversize_buffer 与 miso_linked_list 按直接调用依赖补入，再实现 LogBuffer 的声明/TLS/context、LP/HP 分配发布、读侧切换与回收恢复。BQLog LogBuffer 并非只包 MISO；不使用固定返回替身，也不跳过 HP/oversize 或提前进入 Worker/Appender。

## 验证界限

本轮实际 GNU/Linux 与原生 MSVC x64 Debug/Release 整库及两 TU MISO 公开调用方编译链接通过；32/33 库源与 PUBLIC QLOG_DEBUG 传播核对。完整 BlockList + 三个依赖候选只在仓库外三 TU 编译链接通过，生产尚未新增该模块。运行程序、CTest、Ring/Record/MISO 行为、并发、恢复与性能均后置，不把历史测试数计为本轮验收。详见 [验证记录](../validation/MISO_BUILD_INTEGRATION_20261013_CHS.md)。
