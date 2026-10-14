# Oversize、Atomic目录及LogBuffer声明/TLS编译记录（2026-10-14）

| 配置 | 最终实际库cpp与公开调用方 | 最终声明/TLS候选cpp与两TU | PUBLIC宏 |
| --- | --- | --- | --- |
| GNU/Linux Debug | 36；Oversize/Group/Block/MISO各两TU＋Pool链接，原Atomic仅编译 | 37；通过 | 所有TU定义 |
| GNU/Linux Release | 36；上述通过 | 37；通过 | 所有TU未定义 |
| 原生MSVC x64 Debug | 37；上述通过 | 38；通过 | 所有TU定义 |
| 原生MSVC x64 Release | 37；上述通过 | 38；通过 | 所有TU未定义 |

BUILD_TESTING=OFF；没有运行程序/CTest。GNU -Werror继承库警告；MSVC /WX仅外部原有/wd4324、/wd4146，生产警告不改。compile_commands/source list/已链接产物及宏逐TU核对；实际9调用方TU+1原测试object，候选2TU，object不计行为验收。

207基线一头原字节移动、11既有文件变更（CMake+十include）、其他195哈希不变；原CR及Spin新保存扩充保留。实际Oversize已接库，新LogBuffer/工具生产未代写/挂空源。Atomic新入口include/qlog/platform/atomic/atomic.hpp，没有cpp。

候选全部库/调用方一致使用utility_types/type_tools/defs扩展头及新LogBuffer头，shared同T全部API/两工厂、原launder/scoped、context16/block_misc32/128编译断言、TLS析构/lookup链接通过；未构造主LogBuffer或调用未实现运输接口，不等于完整对象可用。

BQ shared对象==/!= GNU C++20实例化在438/447失败，候选显式rhs.ptr_修正后通过；nullptr原模板保留并编译普通/显式调用。初候选漏thread_id平铺映射Debug失败已修，first-candidate-failure留日志；Atomic迁移脚本误匹配旧路径CR、中断后暂时缺头Debug失败已修，atomic-relocation-failure留日志，最终目录下四配置重编译通过。嵌套VS二次环境初始化另有PATH脚本错误，独立新进程重跑候选后通过，非C++错误。不以旧结果冒充最终通过。

QLog a1ba6b4d45a25d60242327b68894c23d56c027e0；BQ78cbfbef4f87e7558335f0c04bf6dd6894b46451，两树脏、参考未改。产物D:/qq344/Documents/qlog-log-buffer-declarations-20261014-01：before/reference、Atomic迁移前快照/清单、保护清单、候选/consumers、参考语法/失败日志、最终实际与候选四配置日志/compile_commands、validation-matrix及最终状态。

未验UNIT_TEST/mobile/32位/Java/其他平台、TLS/Buffer析构交错、并发/顺序/恢复、故障/退出/性能；不沿用旧通过数。见[决定](../decisions/LOG_BUFFER_DECLARATIONS_ALIGNMENT_20261014_CHS.md)。
