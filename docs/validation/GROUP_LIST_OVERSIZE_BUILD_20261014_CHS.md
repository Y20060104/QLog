# GroupList 实际集成与 Oversize 完整候选编译链接记录（2026-10-14）

| 配置 | 实际库cpp与公开调用方 | 仓库外Oversize候选cpp与公开调用方 | PUBLIC宏 |
| --- | --- | --- | --- |
| GNU/Linux Debug | 35；BlockList/MISO/GroupList各两TU、MemoryPool两种节点模板通过 | 36；Oversize两TU通过 | 所有TU定义且一致 |
| GNU/Linux Release | 35；上述通过 | 36；Oversize两TU通过 | 所有TU均未定义 |
| 原生MSVC x64 Debug | 36；上述通过 | 37；Oversize两TU通过 | 所有TU定义且一致 |
| 原生MSVC x64 Release | 36；上述通过 | 37；Oversize两TU通过 | 所有TU均未定义 |

全部工程BUILD_TESTING=OFF。只配置、编译和链接，没有执行新程序或CTest；不引用旧47项或历史20/20作为本轮通过数。GNU -Werror并继承库警告，MSVC /WX仅外部既有/wd4324、/wd4146；生产警告和PUBLIC定义不改。compile_commands、qlog-source-list和已链接产物逐项核对。

本轮实际修正MemoryPool前置模板声明T.bool为T, bool，并把用户已写GroupList与两平台时钟接根库。205原生产/测试/CMake文件中助手仅主动改这两文件；末次又出现group_list.cpp原config CR注释空格的同步保存，保留该保存后四配置增量编译链接通过。GroupList函数代码不变，其余202 SHA-256不变，所有CR原文/草稿保留。首次完整构建日志在before-source-format，外部消费者读句柄配对调整前日志在before-consumer-pairing，最后构建日志对应最新文件。实际调用方直接使用当前生产头/cpp，无旧候选覆盖。

Oversize候选只在仓库外，使用当前实际库及已有真实工具/存储/SPSC；候选include目录只有一个全新Oversize头，没有覆盖既有类定义。所有SISO薄转发、misc模板、构造/析构及两个const getter路径编译链接；未执行构造、读写或恢复，候选不是已接入生产实现。

QLog HEAD a1ba6b4d45a25d60242327b68894c23d56c027e0，参考78cbfbef4f87e7558335f0c04bf6dd6894b46451；两树脏状态保留。采用两Oversize及核对GroupList/MemoryPool/LogBuffer文件与参考提交无差异。固定提交miso_linked_list使用检索另存reference-linked-list-uses.txt，生产只有未使用include；下一模块之后直接LogBuffer，不将该测试容器迁移列为主链前提。

产物D:/qq344/Documents/qlog-oversize-guidance-20261014-01：before、205保护清单、参考快照/采用文件hash、实际/候选四配置CMake工程/完整构建日志/compile_commands、public consumers、validation-matrix.json及最终状态清单。

未验证：运行行为/CTest、专用QLOG_UNIT_TEST、其他平台/Java、锁并发/退役、存储故障/恢复/TLS退出或性能。typed metadata、原OOM/无异常回滚、墙上时间及旧SPSC64映射边界继续保留；编译链接不提供上述正确性或安全保证。决定见[Oversize接续](../decisions/OVERSIZE_BUFFER_ALIGNMENT_20261014_CHS.md)。
