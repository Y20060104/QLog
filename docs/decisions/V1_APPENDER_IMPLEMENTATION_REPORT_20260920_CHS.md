# Appender 输出层实现报告（2026-09-20）

## 结论与边界

已在 `/home/qq344/QLog` 直接补齐 OutputBatch、IoReport、Appender 状态机、ConsoleAppender、TextFileAppender 和冷创建工厂，接入真实 qlog 静态库。工作区原有修改保留，未 commit/reset。

本轮完成的是 **Linux/WSL2 人工驱动的输出层**。尚未启动真实 worker；compose_line、PreparedReset/邮箱、BackendSession、runtime、公共管理实现与 shutdown 生命周期仍需后续接线。不得将本轮结果视为整个 V1 完成。

## 修正的实际问题

| 原问题 | 处理 |
|---|---|
| Appender 重复构造声明、类内多余限定名、两种 reopen_output | 统一头文件与真实实现 |
| 大写伪代码 helper、缺失 schedule_retry、拼写错误 | 全部落实为可链接函数 |
| accept_line 的 `< 65536` 判断拒绝正常短行 | 改为只拒绝超出上限 |
| prepare_io_report 在目标循环内 return | 所有目标准备完才返回，空输入也正常返回 |
| report 后续同槽没有 max unwritten | 保留首次 errno/path，合并 max/OR |
| IoReport 默认可复制可能丢失预留容量 | 禁止复制，允许 move，一次性 take |
| 错误历史只填 errno、不填 stage/path 等 | 冷准备名称与容量，逐字段记录，事件/实际丢弃分开 |
| 同步故障被后续写入故障覆盖 | sync_error 与阻塞故障分开保存 |
| 按退休次序合并可能选错跨目标首错 | 增加 capture_first_error，Session 每次操作后立即调用 |
| close 错误误记为 write | 独立 IoStage::close，不能覆盖主要写故障 |
| 空批次周期使用固定值而非更新后的配置 | 使用 config.text.flush_interval_us |
| 正文 API 声明混入完整行参数 | 恢复原有六参数接口，行层另做 compose_line |
| 新输出文件没有进入构建 | CMake 接入六个输出源文件和两个测试目标 |

## 保留的合同

- write_all 持续推进短写；write EINTR 同次重试；write 返回 0 保留后缀并报告 errno=0 的 incomplete；无 flush 调用/字节预算。
- Linux fdatasync 一次，EINTR 也报告失败。Console durable 只保证输出调用完成。
- ENOSPC/EDQUOT 保留后缀、EAGAIN 同 fd 重试；其他永久写错误实际丢弃并记录损失，TextFile 切换恢复文件。
- 恢复使用 O_EXCL、每次服务最多 16 个名称，当前路径准确记录；不删除碰撞或验证失败创建的文件。
- 最终关闭不等待磁盘恢复、不重开；实际后缀只丢弃一次。Linux fd 先置 -1 再 close，EINTR 不重试。
- worker 中错误记录不分配 C++ 容器，不递归日志；报告与生命周期历史分离。
- Console gate 由未来 Runtime 统一持有，寿命覆盖所有共享/独立 worker；派生对象只借用它。

本轮重新核对 BQLog 本地 `src/bq_common/platform/posix_misc.cpp::write_file/flush_file`。对齐的是该本地源码，不声称已检查远端最新版本。

## 验证结果

| 配置 | 总数 | 通过 | 跳过 | 失败 |
|---|---:|---:|---:|---:|
| GCC Debug | 171 | 171 | 0 | 0 |
| GCC Release | 171 | 170 | 1 | 0 |
| Clang ASan + UBSan | 171 | 171 | 0 | 0 |

Release 跳过既有 `SpscRingBufferRead.CorruptedFrameDoesNotAdvanceReader`（该配置关闭相应 Ring 验证），不是删除或屏蔽 Appender 测试。

新增 Appender label 共 29 项：21 项批次/报告/状态机与历史测试，8 项真实 POSIX/恢复/管道/分配测试。实际验证包括 NUL 保留、先写前缀不重放、零进展、ENOSPC、EAGAIN、永久故障、恢复路径、16 次名称上限、打开后 fstat 失败清理、close EINTR、同步 EINTR、最终关闭幂等、兼容配置和跨目标首错观察顺序。

准备完成后的接受/写失败/恢复/同步失败/关闭/历史移动/报告导出路径，通过测试对标准 C++ new/new[]（含 aligned、nothrow）进行计数，结果为 0。此证据不等于跟踪了 libc 内部所有 malloc，也不等于已经测出 V1 吞吐或 P99。

第一次 ASan 运行发现的是新增分配拦截测试遗漏 nothrow 重载，引发 GoogleTest 内部分配/释放不匹配。已补齐重载并保留原失败日志；最终没有禁用 alloc-dealloc-mismatch 检查，完整回归通过。

日志与机器可读摘要：

- [Debug 最终日志](../validation/appender-20260920/final-formatter-gcc-debug-ctest.log)
- [Release 最终日志](../validation/appender-20260920/final-formatter-release-ctest.log)
- [ASan/UBSan 最终日志](../validation/appender-20260920/final-formatter-san-ctest.log)
- [源码哈希及结果 JSON](../validation/appender-20260920/verification.json)

最终构建无 warning/error；git diff --check 通过。未执行 native Linux 发布验收、跨 worker 真实调度压力测试、恢复索引 UINT64_MAX 的动态注入测试或完整端到端性能测试。

## 下一步交付

已经提供 [完整行下一步指南](./V1_APPENDER_NEXT_IMPLEMENTATION_GUIDE_20260920_CHS.md)，包含可以追加到现有 formatter 翻译单元的完整代码与明确接线顺序。该示例经 GCC C++20、Wall/Wextra/Wpedantic/Wconversion/Wshadow/Werror 编译检查；未加入 qlog 库、未当作运行验收完成。

推荐先完成 compose_line + Context 冷路径 OS tid，再做 PreparedReset/ControlMailbox，然后单线程 BackendSession、worker/runtime，最后一次性接上公共 API 与析构。完整范围不缩减为演示日志。

原文件备份位于 `build/appender-update-20260920-backup`。备份是本轮开始前用户已有内容；不要运行历史安装脚本覆盖当前树。
