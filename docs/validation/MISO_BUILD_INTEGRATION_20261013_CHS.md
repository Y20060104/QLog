# MISO 实际接库与 BlockList 候选编译记录（2026-10-13）

本轮用户明确“进行下一轮集成，测试可以滞后”。所有检查仅静态审查、编译、链接及文件保护；未调用 CTest，未执行新公开调用方。

| 工具链 / 配置 | 实际库 cpp | 实际 MISO 调用方 | BlockList 指导候选 | PUBLIC Debug 宏 |
| --- | ---: | --- | --- | --- |
| GNU/Linux Debug | 32 | 两 TU 链接通过 | 三 TU 链接通过 | 库和调用方均定义 |
| GNU/Linux Release | 32 | 两 TU 链接通过 | 三 TU 链接通过 | 均未定义 |
| 原生 MSVC x64 Debug | 33 | 两 TU 链接通过 | 三 TU 链接通过 | 库和调用方均定义 |
| 原生 MSVC x64 Release | 33 | 两 TU 链接通过 | 三 TU 链接通过 | 均未定义 |

独立工程直接 add_subdirectory 权威 QLog；实际 MISO 调用方引用分配、提交、五个读侧函数与真实全局检查依赖，没有额外定义替身。BlockList 候选只在仓库外提供新头/cpp及必要枚举/原工具/原宏；实际库仍来自 QLog 原树。GNU 使用 -Werror 并继承根库警告；MSVC 使用 /WX，仅沿用外部 /wd4324、/wd4146，不修改生产警告规则。

既有 QLog SISO 内部缓存行为 64，BQLog 提交版为 128；本组复用已有 SISO，候选块链表按其真实 sizeof/最小长度计算，128 字节公共块头布局不等于跨树存储格式逐字节兼容。该既有差异另行记录，本轮未重写 SISO。

源码唯一修正为 src/buffer/miso_ring_buffer.cpp 中 read_chunk 的 waiting_for_retrun_ 拼写。197 个既有生产/测试/CMake 文件的 SHA-256 保护清单核对，其余 196 个不变；该 cpp 与备份仅一行替换，CR 原文保留。根 CMake 与测试文件未修改。当前已写读侧与原五函数候选做词法逐函数比较；不声称静态相同即运行正确。

参考固定提交 78cbfbef4f87e7558335f0c04bf6dd6894b46451，QLog HEAD a1ba6b4d45a25d60242327b68894c23d56c027e0，两树原脏状态保留。原 MISO 未初始化/未发布块数读取边界、原 typed-pointer 访问、CAS/GCC 内存序边界继续保留。BlockList 16 位 ABA 标记只缓解标记范围内的 ABA；未新增完整回收证明。

复核产物：D:/qq344/Documents/qlog-buffer-integration-20261013-01。包含 before/、reference-head/、before-production.json、实际与指导两类 GNU/native 配置/构建日志、compile_commands.json、qlog-source-list.txt 和 validation-matrix.json。完整实现教学保留在对话，文档只记录决定/证据。

不计入本轮验收：任何运行测试、历史 20/24 或旧 47 项计数、MISO/HP/LogBuffer 行为或并发、TLS 退出、映射恢复、持久化、Record 行为、全局退出以及性能。
