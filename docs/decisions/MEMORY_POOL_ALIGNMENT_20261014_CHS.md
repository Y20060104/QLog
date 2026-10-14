# MemoryPool 对齐决定与 BlockList 接库（2026-10-14）

> **2026-10-14 最新接续：** 用户已选SPSC128，实际头/offset已同步；完整GroupList及原依赖指导由用户从MemoryPool开始实现。此前SISO64差异记录属于历史快照，见[最新决定](GROUP_LIST_ALIGNMENT_20261014_CHS.md)。运行测试仍后置。

## 当前事实

权威 QLog 为 /home/qq344/QLog，feat/spsc-ring-opt@a1ba6b4d45a25d60242327b68894c23d56c027e0。参考 /home/qq344/BqLog 使用提交 78cbfbef4f87e7558335f0c04bf6dd6894b46451，整树仍有 MISO/CMake 等本地修改。采用的 memory_pool、block_list、group_list、util、linux_misc/win64_misc 逐文件相对固定提交无差异；实际参考读取提交快照。

用户已填写 BlockList 头/cpp、block_list_type、原 roundup_pow_of_two 和原 maybe-uninitialized 宏。逐函数审查未发现需要改动的实现错误；与上一轮候选的差别为等价显式 cast、括号、局部值直接使用与诊断文本拆分。助手只把 src/buffer/block_list.cpp 接入根 CMake，不修改用户源码/CR。199 原生产/测试/CMake 文件保护核对，仅 CMake 一行添加，其余 198 不变。

## 本组实施边界

下一完整模块为 include/qlog/buffer/memory_pool.hpp，namespace qlog::buffer，memory_pool_obj_base<T, Align> 两个原偏特化及 memory_pool<T>。模板全在头内，不创建空 cpp，不增加 CMake 源。新生产由用户填写，完整指导保留在对话；仅候选在仓库外编译。

原继承/friend/is_aligned、两个侵入式指针、128 字节对齐版本/POD断言、单 spin_lock、head/tail 和四接口 push/pop/evict/size 均保留。push 忽略空对象；pop 从头取；evict 只检查尾节点且回调在锁内；size 持锁遍历。没有新增析构清理、计数原子、空 callback 断言、删除对象、清空弹出节点链接、复制限制或 Manager。池不分配/构造/析构对象，所有权及最终释放由 GroupList 调用层管理。

## 后续真实依赖

BQLog group_node 继承 memory_pool_obj_base<group_node,true>；group_list.pool_ 复用整组，淘汰由 GroupList 按原 GROUP_NODE_GC_LIFE_TIME_MS=1000 调用 evict 再 aligned_delete。对象池内部不取时间、不管理映射文件。

实际 QLog 尚无原 spin_lock_rw_crazy、util::aligned_new/aligned_delete 和 high_performance_epoch_ms。进入 GroupList 指导必须同组覆盖这些直接依赖及原 null-deref 警告宏，不引用未提供实现的 helper，不用普通互斥锁/chrono替身改变原合同。Linux 原 CLOCK_REALTIME_COARSE；Windows 原 GetSystemTimeAsFileTime 转 Unix 毫秒，均为墙上时钟。SISO64/参考128既有布局差异随 GroupList 分组跨度一起复核，不以旧文档的64描述作为参考事实。

后续顺序为 MemoryPool → 原读写自旋锁/对齐对象构造/原毫秒时钟与 GroupList 分组存储 → OversizeBuffer 与直接用到的 MISO链表 → LogBuffer TLS/context、LP/HP 分配发布、消费切换与回收恢复。保持新函数由用户实现，不提前 Worker/Appender。

## 验证

实际 GNU/Linux 与原生 MSVC x64 Debug/Release 库、BlockList 两TU及MISO两TU调用方严格编译链接通过，库 cpp 数33/34，PUBLIC QLOG_DEBUG传播核对。原对象池两个偏特化与四接口的外部两TU候选四配置严格模板实例化/链接通过。未执行调用方、CTest、行为/并发/恢复/性能。见 [验证记录](../validation/BLOCK_LIST_MEMORY_POOL_BUILD_20261014_CHS.md)。
