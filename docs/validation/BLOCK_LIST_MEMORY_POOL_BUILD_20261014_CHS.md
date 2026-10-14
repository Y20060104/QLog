# BlockList 实际接库与 MemoryPool 候选编译记录（2026-10-14）

用户已允许测试后置。本轮只有静态审查、编译链接、PUBLIC宏和原文件保护检查；不执行程序或 CTest。

| 工具链 / 配置 | 实际库 cpp | BlockList 实际调用方 | MISO 实际调用方 | MemoryPool 候选 | Debug宏 |
| --- | ---: | --- | --- | --- | --- |
| GNU/Linux Debug | 33 | 两TU链接通过 | 两TU链接通过 | 两TU链接通过 | 定义且传播一致 |
| GNU/Linux Release | 33 | 两TU链接通过 | 两TU链接通过 | 两TU链接通过 | 未定义 |
| 原生MSVC x64 Debug | 34 | 两TU链接通过 | 两TU链接通过 | 两TU链接通过 | 定义且传播一致 |
| 原生MSVC x64 Release | 34 | 两TU链接通过 | 两TU链接通过 | 两TU链接通过 | 未定义 |

独立工程直接 add_subdirectory 权威 QLog，BUILD_TESTING=OFF。BlockList/MISO 实际调用方没有提供替代定义，引用用户实际源码；MemoryPool 候选只增加仓库外头，复用现有类型trait/锁/原子/线程接口。候选涵盖true/false节点基类、POD与继承断言、push/pop/evict/size及跨TU包含；编译链接不证明链表操作、节点生命周期或并发运行正确。

GNU 使用-Werror并继承根警告；MSVC使用/WX，仅沿用外部/wd4324、/wd4146，不修改生产警告。保护清单199个既有生产/测试/CMake文件中只改变CMakeLists.txt一行注册，其余198不变；BlockList头/cpp、type_tools、macros、defs及CR/草稿未改。实际MemoryPool生产文件尚未创建，未建立空cpp。

QLog HEAD a1ba6b4d45a25d60242327b68894c23d56c027e0，BQLog固定参考78cbfbef4f87e7558335f0c04bf6dd6894b46451，两树原脏状态保留。BQ原typed-pointer、恢复和CAS/GCC内存序边界继续记录，SISO64/参考128差异未在本轮变更。

产物位于D:/qq344/Documents/qlog-memory-pool-guidance-20261014-01，包含before/、reference-head/、before-production.json、原候选对照、实际/候选调用方、四配置配置/构建日志、compile_commands.json、库源列表及validation-matrix.json。完整教学在对话，文档仅记录决定和证据。

本轮不计入运行/行为、并发、恢复、持久化、TLS/退出、性能验收，也不沿用历史测试数量。
