# LogBuffer写侧实际及消费者候选编译记录（2026-10-14）

| 配置 | 实际写侧库＋两TU | 完整消费者候选库＋两TU | PUBLIC宏 |
| --- | --- | --- | --- |
| GNU/Linux Debug | 37cpp；编译链接通过 | 37cpp；编译链接通过 | QLOG_DEBUG定义，NDEBUG未定义 |
| GNU/Linux Release | 37cpp；编译链接通过 | 37cpp；编译链接通过 | QLOG_DEBUG未定义，NDEBUG定义 |
| 原生MSVC x64 Debug | 38cpp；编译链接通过 | 38cpp；编译链接通过 | 同Debug |
| 原生MSVC x64 Release | 38cpp；编译链接通过 | 38cpp；编译链接通过 | 同Release |

BUILD_TESTING=OFF。未执行调用方/CTest。GNU继承库警告并-Werror；原生MSVC /WX /utf-8 /EHsc，仅原有外部/wd4324 /wd4146，生产警告不改。八组compile_commands、库源数/唯一log_buffer.cpp、两TU/已链接产物/PUBLIC宏/NDEBUG检查通过；无QLOG_DEBUG=0或调用方改变宏。

实际完全使用仓库头/cpp，调用方源码包含真实LogBuffer构造/析构及普通/高频/超大/恢复写入表达式，未执行。九函数去注释/格式及等价字符串拼接后与上组候选一致。实际read/return仍只有声明，未在实际调用方引用，不称整个LogBuffer已完成。

候选只在D:/qq344/Documents/qlog-log-buffer-consumer-guidance-20261014-01/candidate，库及调用方所有TU一致采用新的log_buffer.hpp，单次替换实际cpp，其他依赖源码全用实际仓库。两TU引用主对象构造/析构、读写/scoped<log_buffer>两分支、Oversize/恢复构造；完整消费者真实符号链接，无空实现/虚构helper。不执行代码，不能由链接推断读行为或恢复成立。

UNIT仅GNU -fsyntax-only Debug/Release，覆盖原恢复诊断、组free链检查、get_buffer_info_for_this_thread，均通过；未做UNIT完整链接/运行或Native UNIT检查。正常四配置及所有候选编译首次通过，无生产修复需求。

include/src/tests/CMake基线209文件SHA-256全部不变，CR未删除，CMake无需再注册。固定参考五采用文件无HEAD差异，参考整树仍脏、未写参考。QLog a1ba6b4d45a25d60242327b68894c23d56c027e0；BQLog78cbfbef4f87e7558335f0c04bf6dd6894b46451。

产物D:/qq344/Documents/qlog-log-buffer-consumer-guidance-20261014-01包含before保护/固定参考、九函数审查对照、完整候选/两TU、CMake/脚本、实际和候选四配置日志/compile_commands、UNIT语法日志、矩阵与最终hash/Git状态。新源码只在外部候选，完整教学留对话；仓库文件只记决定/证据。未验运行/并发/运输顺序/空洞迁移/TLS退出/恢复损坏及版本回绕/IO或OOM/其他架构及移动端/性能，参考数据竞争和Release失效边界均未称通过。见[决定](../decisions/LOG_BUFFER_CONSUMER_ALIGNMENT_20261014_CHS.md)。
