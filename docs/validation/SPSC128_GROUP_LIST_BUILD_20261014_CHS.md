# SPSC128 实际修改与完整 GroupList 候选编译记录（2026-10-14）

| 配置 | 实际QLog cpp / 链接 | 完整GroupList候选 cpp / 链接 | PUBLIC宏 |
| --- | --- | --- | --- |
| GNU/Linux Debug | 33；BlockList/MISO各两TU通过 | 35；GroupList两TU通过 | 定义且一致 |
| GNU/Linux Release | 33；BlockList/MISO各两TU通过 | 35；GroupList两TU通过 | 均未定义 |
| 原生MSVC x64 Debug | 34；BlockList/MISO各两TU通过 | 36；GroupList两TU通过 | 定义且一致 |
| 原生MSVC x64 Release | 34；BlockList/MISO各两TU通过 | 36；GroupList两TU通过 | 均未定义 |

两套独立工程分别直接构建实际QLog与完整原候选；候选的扩展头对全部库/调用方TU一致可见，不混用util/锁类定义。BUILD_TESTING=OFF，未调用CTest/程序。GNU -Werror继承库警告；MSVC /WX，仅外部既有/wd4324、/wd4146。源列表、compile_commands和Debug宏核对。

实际仅SPSC头使用128公共常量并更新offset断言；Head512、四游标缓存行及cpp+640由源静态审查/编译断言确认，不是运行容量测试。199原文件仅该头变化，其他198及全部CR不变。新MemoryPool、GroupList、RW锁/工具/时钟/宏未代写生产，根CMake未变。

首次仓库外候选Debug失败为缺少QLog独立LogBufferConfig头（BQ原配置在defs），已明确包含后四配置通过；原失败日志保留first-candidate-failure/，不是实际生产编译失败。候选RW常量采用等值numeric_limits，Release break_link标maybe_unused，路径根匹配现有qlog_mmap，其余原流程保留。

参考78cbfbef4f87e7558335f0c04bf6dd6894b46451，QLog HEAD a1ba6b4d45a25d60242327b68894c23d56c027e0，两树脏状态保留。原OOM/异常锁、typed-pointer、stage重叠break、next(no_lock)原读取、墙上时间及64旧格式等边界见决定，不作安全性/恢复保证。

产物：D:/qq344/Documents/qlog-group-list-guidance-20261014-01，包括before/reference-head、候选、实际/候选四配置配置与构建日志、保护清单及validation-matrix.json。未运行/CTest、未验专用QLOG_UNIT_TEST分支、其他平台/Java、存储/并发/恢复/退出或性能，不沿用历史测试计数。
