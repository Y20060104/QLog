# SPSC128 与 GroupList 完整指导对齐决定（2026-10-14）

> **2026-10-14 实际填写后收口（本段最新）：** 用户已完成MemoryPool/GroupList及全部直接依赖；助手只修正MemoryPool前置声明T.bool → T, bool并注册实际GroupList/两native时钟cpp，所有CR保留。实际GNU/原生MSVC四配置库35/36及公开调用方严格编译链接通过，未运行/CTest。下一组Oversize完整候选仅仓库外四配置36/37cpp及两TU链接通过；随后直接LogBuffer，原miso_linked_list生产只有未使用include，不再作为迁移前提。见[最新决定](OVERSIZE_BUFFER_ALIGNMENT_20261014_CHS.md)与[最新验证](../validation/GROUP_LIST_OVERSIZE_BUILD_20261014_CHS.md)。下方原“尚未生产填写”和“再MISO链表”的内容保留为此前指导快照，不代表当前状态。

## 用户选择与实际修改

用户明确“SPSC 对齐参考128”，并要求完整 group_data_head、group_node、遍历、分配、回收和恢复指导，新模块将由用户从 memory_pool 开始填写。助手只修正已写 SPSC：使用公共 QLOG_CACHE_LINE_SIZE=128，同步四个游标 offsetof 断言；Head=512，writer缓存偏移128/132，read/write原子偏移256/384。cpp原容量公式自动成为 roundup(expected)+640，不改其函数体。原CR保留，不重写标准原子/块字段memcpy适配，也不宣称整套对象布局或旧64映射格式兼容。

199原生产/测试/CMake保护核对，仅include/qlog/buffer/siso_ring_buffer.hpp变化，其余198不变；CMake已接SPSC/BlockList，不注册尚未填写的新GroupList/时钟源。

## 对齐基准与新模块边界

QLog权威 /home/qq344/QLog，feat/spsc-ring-opt@a1ba6b4d45a25d60242327b68894c23d56c027e0。BQLog使用提交78cbfbef4f87e7558335f0c04bf6dd6894b46451；采用group_list、memory_pool、siso、spin_lock、util、两native时钟和macros逐文件无差异，整树仍有MISO/CMake等修改。读取提交快照。

MemoryPool沿用上一轮完整指导，不建空cpp。GroupList原成员名称/默认/断言/原子序/函数归属及条件单测接口保留。新生产仍由用户实现，完整代码指导只在对话，候选在仓库外。

直接依赖同组给齐：spin_lock_rw_crazy及四个原scoped形式追加到platform/spin_lock.hpp；aligned_new/delete补入既有utility::util，调用已有平台分配与原object_destructor；原null-deref警告宏追加platform/macros.hpp；time_ops公共声明及Windows/Linux定义按系统拆分。Linux时钟保留CLOCK_REALTIME_COARSE且不检查返回，Windows保留FILETIME Unix毫秒换算；不改monotonic、不加锁公平性/断言/回滚。RW锁counter公开、128padding、全部原子序按原，write_lock_mark_value用numeric_limits<counter_type>::min取得同值。

group_data_head三链used/free/stage同一块区域，free始终新建；恢复先收集used/stage，再调用各recovery_blocks，最后初始化未占用块入free。原重叠分支remove后break与枚举顺序保留，不自创新恢复保证。

group_node依赖NormalBuffer；映射meta128、group_data_head384及固定块区域，logical长度按计算器，不用更大的物理映射长度扩容。failed回退heap且head位于data基址；映射head位于data+meta。新映射清零、写checksum，旧映射核对checksum后重建，失败则原初始化。析构仅head=null，unique_ptr析构交给NormalBuffer；其默认不删除映射文件。

组链迭代器保存incoming pointer、当前节点、锁类型，不增加RAII迭代器析构。next先取得下一link锁再释放旧锁；早退按原next(iter,no_lock)；delete_and_unlock先取得/释放outgoing写锁、unlink、释放incoming锁、时间戳、入pool，并不直接delete。分配先遍历free，再head写锁double-check，然后pool复用/新index组，写misc/renew/入stage；recycle从used移除后入free。GC原1000ms尾部条件及墙上时钟规则保留。

std::string/unique_ptr/vector为既有适配，原hash_map复用。路径根qlog_mmap与现有MISO一致。LogBufferConfig独立模块，GroupList头必须显式包含log_buffer_config.hpp。Release只用于assert的break_link加maybe_unused编译注记，原行为不变。

## 明确保留的参考边界

aligned_new构造抛异常不释放原分配；alloc_new_block原分支未处理null node/result且手工锁无异常回滚，不加新失败合同。next(no_lock)保留原普通指针读取，不据此宣称无数据竞争。原stage重叠分支break后未完整重收stage_map，不改为继续扫描。目录恢复保留strtoull/ERANGE/end_ptr规则，包括原空数字、符号/空白规则；不加digit检查。回调/unsafe调用、非零块数量和节点安全退役仍是调用前提。旧64字节SISO与当前128不能默认直接续用；具体恢复/并发边界后续验证，不以编译代替证明。

## 验证与后续

实际GNU/原生MSVC x64 Debug/Release库与BlockList/MISO各两TU编译链接通过，库源33/34；完整GroupList+全部依赖候选库35/36、两TU调用方四配置仅编译链接通过。候选所有库/调用方TU使用一致扩展头避免混用类定义，PUBLIC Debug宏核对；未运行/CTest，不验证专用QLOG_UNIT_TEST或Java/其他OS、并发、恢复、退出、性能。见[验证](../validation/SPSC128_GROUP_LIST_BUILD_20261014_CHS.md)。新模块写完后审查/接库，再Oversize与MISO链表，后LogBuffer，保持测试后置。
