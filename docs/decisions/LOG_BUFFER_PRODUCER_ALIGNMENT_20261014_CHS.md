# LogBuffer TLS实际收口与构造/恢复/写侧决定（2026-10-14）

权威QLog /home/qq344/QLog，feat/spsc-ring-opt@a1ba6b4d45a25d60242327b68894c23d56c027e0；参考/home/qq344/BqLog@78cbfbef4f87e7558335f0c04bf6dd6894b46451。两树均有本地修改。采用的log_buffer.h/cpp、defs、utility_types、公开type_tools及BlockList相对此提交无差异；不采用参考本地MISO草稿。

用户已填shared/make_shared、launder、scoped、LogBuffer完整声明/metadata/TLS及四block helper/两个TLS析构/宏。声明误存include/qlog/buffer/log_buffer.cpp，助手按原字节rename为log_buffer.hpp，并仅将实际src/buffer/log_buffer.cpp接CMake。新构造/运输定义仍由用户填写；实际库目前只有前组TLS/工具，不能称完整LogBuffer对象可用。

严格GNU QLOG_UNIT_TEST+Release语法补查暴露BlockList原三个仅assert引用的局部索引，在NDEBUG下unused；参考BlockList同样写法。助手仅追加三处[[maybe_unused]]，变量/求值/原断言/分支/原子序不变，不添加检查或更改生产警告。第一次Release失败日志保留；修后Debug/Release单测宏候选语法通过。

209原生产/测试/CMake基线：声明初始原字节移动；助手改CMake注册和BlockList三属性。期间用户另给声明头及cpp追加saved_version/update_times/log_map/array_lock/finished等CR，全部保留；两文件去注释代码与基线一致，其余205文件SHA-256不变。保护清单和用户CR差异另存，不将用户保存算助手改实现。

本组完整指导仅对话及仓库外候选：prepare_and_fix_recovery_data、clear_recovery_data、构造/析构、alloc_new_hp_block、wt_alloc_oversize_write_chunk、wt_commit_oversize_write_chunk、公开alloc_write_chunk/commit_write_chunk九函数。包含实际调用依赖，不用空实现或未提供helper；完整消费者/快路径/运输归还及Oversize读回收仍后续。

原初始化顺序保留：copy config，先构造HP/LP，再调整内部config尺寸；16×128下限、roundup、id add_fetch_relaxed取1起；恢复saved_version原u16前置++，当前version为对象内const；读取代际从version-range起，生产range2、单测range5。原对齐断言/旧64文字不改，实际128。clear_recovery_data原移除整个该log映射目录，不新加成功返回。

恢复map索引u16(version-1-context.version)，value每个历史TLS地址的最小运输seq；旧地址仅key不解引用；LP与HP used/stage按原顺序，当前代际残项原version++失效。Oversize目录既有qlog_mmap命名及std::string/vector/unique_ptr适配；原strtoull/end_ptr/ERANGE、文件大小反推、context sentinel/版本校验保留，不引入额外Manager/类/回滚。

写侧窗口统计每次alloc调用（含失败/重试），1000ms检查与达到threshold重置保留；LP共享MISO，HP逐TLS独立SISO，Oversize另存payload且LP context作父运输项。seq按LP项/新HP块/Oversize父项分配，不按每条HP日志、不构成跨线程全序。id为本次进程Buffer实例缓存key；version为映射代际；三者不得混称ABA标志。

原父项先预留、子payload先commit、释放对象read lock/清空TLS target、最后父项commit的顺序保留。固定提交已含new_buffer is_valid及分配失败处理：rollback当前write_seq、父version改当前+1、提交无效父项、返回err_io_failure_drop；这些是参考原流程，不额外扩防御或恢复旧assert。

已明确参考边界：Oversize恢复size搜索按u32的2幂左移，未匹配可回绕0，而UINT32_MAX兜底不可由该序列正常到达（参考cpp1232-1244）；恢复新开Oversize对象在取misc前没有新建写侧同样is_valid检查（1249-1250）；原hp_buffer_max_alloc_size_普通u32首次读写没有在本函数建立原子同步，不能据此宣称并发安全（186/494）；原加sizeof的u32边界、无强异常回滚、TLS持锁无限重试与typed访问仍保留。严格对齐不静默修参考，也不将这些路径称安全/通过；后续验收与修正需要另有明确决定。

实际及完整写侧候选GNU/Linux与原生MSVC x64 Debug/Release均37/38cpp库和各两TU严格编译链接，PUBLIC QLOG_DEBUG/NDEBUG逐TU核对；候选真实构造对象并引用正常/超大/恢复写路径，但程序未执行。UNIT仅GNU两配置语法，不是UNIT链接/运行或Native UNIT验收。运行/CTest后置，不计顺序、线程退出、并发、恢复、故障、存储或性能。完整证据见[编译记录](../validation/LOGBUFFER_TLS_PRODUCER_BUILD_20261014_CHS.md)。

下一组先verify_context/verify_oversize_context/deregister_seq，再LP读取与HP组块遍历、read/return完整及inline快路径、Oversize读取/归还/回收；完成LogBuffer后才回运行态、Worker/Appender。新生产由用户写，文件只记录决定/证据，完整指导留在对话。

2026-10-14后续实际状态：用户已填本组九函数，源码审查与前候选一致，实际四配置编译链接通过；读侧仍仅声明。新完整消费者由用户按[消费者决定](LOG_BUFFER_CONSUMER_ALIGNMENT_20261014_CHS.md)接续；新[验证](../validation/LOGBUFFER_PRODUCER_CONSUMER_BUILD_20261014_CHS.md)只编译链接/语法，未运行。上方“生产新九函数待填”为历史记录。
