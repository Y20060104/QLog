# 实际LogBuffer消费者与线程依赖候选构建（2026-10-14）

权威与参考HEAD：QLog a1ba6b4d45a25d60242327b68894c23d56c027e0；BQLog 78cbfbef4f87e7558335f0c04bf6dd6894b46451。工作树均有修改。参考实际采用的文件已重新检查HEAD diff；固定提交副本与Git状态保存在外部证据目录。

证据目录：D:\qq344\Documents\qlog-thread-runtime-guidance-20261014-01（WSL /mnt/d/qq344/Documents/qlog-thread-runtime-guidance-20261014-01）。before-production.json、consumer-review.json、validation-matrix.json、final-production.json、final-candidate-hashes.json 以及逐配置configure/build日志均保留。

| 范围 | GNU/Linux Debug / Release | 原生MSVC x64 Debug / Release | 实际执行 |
| --- | --- | --- | --- |
| 实际完整LogBuffer消费者 | 各37库cpp+2调用方TU编译链接通过 | 各38库cpp+2调用方TU编译链接通过 | 未运行 |
| 实际LogBuffer UNIT条件代码 | 两配置严格fsyntax-only通过 | 未检查UNIT变体 | 未运行 |
| 完整thread/CV外部候选+真实LogBuffer | 各39库cpp+2调用方TU编译链接通过 | 各40库cpp+2调用方TU编译链接通过 | 未运行 |

实际项目直接add_subdirectory权威QLog，没有覆盖头/替换生产cpp或固定返回替身；真实构造、写/读/归还、oversize/recovery配置路径及scoped_log_buffer_handle的读写两种寿命都被实际引用链接，链接不等于这些配置已执行。17个新消费者定义逐函数token与上轮候选一致。

候选公开头覆盖对库和调用方一致生效，仅替换对应系统thread_ops.cpp一次、添加公共thread.cpp及当前系统CV cpp；新平台字段/原子与两类对象均真实构造引用。两TU包括派生thread的run/on_finished、start/cancel/join/detach及所有静态服务/getter、两种wait和两种wait_for模板、notify_one/all。程序与CTest没有运行。

GNU -Werror加根项目警告；MSVC /utf-8 /EHsc /WX，仅沿用外部验证 /wd4324 /wd4146，不放宽生产警告策略。全部库与调用方编译命令逐TU核对：只有Debug定义QLOG_DEBUG，Release不定义，NDEBUG与配置对应；调用方只定义EXPECT_QLOG_DEBUG用于核对，不自行改变库布局宏。BUILD_TESTING OFF。

候选首次失败为外部thread-probe.cpp误写不存在的scoped_buffer_handle及构造参数；已改为实际scoped_log_buffer_handle(buffer, read_handle)，失败日志保留为first-probe-error。这是调用方材料错误，实际生产原本四配置都通过；不据此修改生产API。候选生成时Windows已有cpp末尾无换行的脚本检查错误也只影响外部材料，不是生产编译故障。

本轮生产/include/src/tests/CMake 209文件SHA-256全不变，只更新决策/验证与接续记录。没有接入未写的生产Thread/CV或空cpp，不暂存/提交、不覆盖CR。日志构建不计顺序、batch回收、TLS/Buffer析构交错、线程/锁/通知、并发、恢复、故障或性能验收。
