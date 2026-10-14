# MISO 读侧原顺序与 Debug 依赖决定

> **2026-10-13 当前更新：** 用户五读函数已写入实际源码；read_chunk一处waiting成员拼写已修正，其余原顺序不改。GNU/原生MSVC Debug/Release实际整库及五读函数调用方链接通过；本轮测试按用户要求后置。下一模块BlockList集成，见[决定](BUFFER_BLOCK_LIST_ALIGNMENT_20261013_CHS.md)与[验证](../validation/MISO_BUILD_INTEGRATION_20261013_CHS.md)。下方候选与历史测试记录是上一轮快照。

日期：2026-10-13。用户授权助手完成Debug依赖，新读侧生产定义仍由用户填写。

基准：BQLog提交78cbfbef，采用MISO原128字节布局和完整Debug。MISO工作树有删Debug及改head对齐草稿，不能直接采用或用其物理行号代替提交源码。

迁入原log_global_vars所需Debug子集：保留原名称、global_vars_base<log_global_vars, common_global_vars>继承、friend、private atomic<int32_t> thread_check_enable_counter_=0和scoped_thread_check_disable。counter用relaxed load/fetch_add/fetch_sub，只有0表示线程ID检查可用；进程共享，嵌套guard不互相提前恢复。Release字段缺席、guard无操作、查询false。没有另建布尔替身类，不增加复制禁用或断言。原类其他Manager/Appender成员及原完整构造/partial退出flush在对应模块补入，当前不记日志全局态完整完成。

SISO/MISO的is_thread_check_enable统一依原规则：实例check_thread_与全局counter查询共同决定。原读句柄配对断言仍由QLOG_DEBUG控制，不受全局counter禁用影响。公共宏仍由库PUBLIC传播，仅Debug定义。

已写MISO只修正TLS赋值、QLLOG_DEBUG及写游标fetch_add_relaxed，保留原CR和算法。src/global/log_vars.cpp接CMake，新增线程策略常驻回归。读侧五函数候选留仓库外及对话，未写生产。

用户明确确认原读取顺序不改。read_chunk/data_traverse中status.load_acquire后立即get_block_num，之后才switch状态；data_traverse在unused时仍执行原尾部游标运算。堆初始化只设置unused状态，未保证块数字段初始化，未提交头可能有生产者修改；这一原未初始化/并发元数据读取边界显式保留。不声称普通C++对象访问/并发安全证明已完成，不静默把块数读取移到used/invalid分支。

保留read_chunk对invalid的跳过、人工empty句柄、discard只作废本次读句柄、return按N/4或empty批量清unused并release发布读游标，以及data_traverse从共享读游标起扫描、不消费数据的原规则。共享读游标可能落后于本地已return位置；遍历仍可能见到尚未批量释放的旧记录。callback及handle数据为原裸指针合同，不加拥有包装、空回调断言或新同步机制。

实际GNU/原生MSVC Debug/Release库和两TU依赖调用方严格编译链接通过，常驻20/20＋外部参考4/4，各24/24；线程策略每配置14检查。五函数/三TU读侧候选四配置仅编译链接通过，未运行；不接受MISO读写/恢复/并发或性能。见[验证记录](../validation/MISO_READ_DEPENDENCIES_20261013_CHS.md)。
