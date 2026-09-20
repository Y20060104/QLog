# 非 worker 实现与验证报告

范围：用户保留 worker 线程实现，当前交付完成冷准备、单槽管理、BackendSession 消费、公共管理与生命周期桥接。代码位于 `/home/qq344/QLog`；BQLog 仅为参考。

新增 appender_prepare、control_mailbox、management_controller、backend_session、worker_provider、filter_config_access；修改 AsyncLogger、Producer 压力通知、Context 消费链、Appender 兼容判断和失败回收、IoReport 无分配合并。CMake 已接入。

高性能设计：生产者不解析格式；正文只渲染一次、多输出独立组行；工作区/报告/目标容器冷准备；稳态 Session 不分配；Ring release 与 publish_reclaimed 后才 I/O；有界 Context 发现与 Channel 配额。flush 保留 BQLog 短写循环和 EINTR 重试。此处是实现性质，不是尚未测量的性能倍数。

修复的重要边界：发现新 Context 时强制再做完整扫描，防止 drain 因空的新节点漏掉未访问旧记录；shutdown 排空期间不恢复打开文件；失败接管覆盖 pending reset 的活目标、新目标和已退休目标，不等待故障恢复；兼容 reset 保留已有批次和文件。

## 验证

| 配置 | 结果 |
|---|---|
| GCC Debug 完整回归 | 204/204 通过 |
| Release 完整回归 | 203 通过，1 原有 Ring 调试校验测试跳过，0 失败 |
| ASan + UBSan 完整回归 | 204/204 通过 |
| Debug/Release × AUTO/ON/OFF | 六配置 backend + diagnostics 全通过；关闭配置字段/Producer 符号移除检查通过 |
| backend 专项 | 21 个 Session 场景 + 未安装 worker 边界 + 分配探针，共 23 项 |

新专项覆盖真实文件输出、嵌入 NUL、多个时区、flush 不消费 Ring、drain、公平配额、超过 64 个注册节点、无效 payload、外层异常隔离、reset 校验/打开失败/复用/重建、单槽 busy、请求 ID 耗尽、各 reset 阶段失败清理、磁盘满时健康目标继续、shutdown 不创建恢复文件、压力通知。

C++ allocation probe 测量暖机后的 4096 次 Producer→Session→行→输出、已冷准备的 reset 应用及领取、shutdown，观察到零 C++ new 分配。没有拦截 libc 全部 malloc，不宣称操作系统或标准库内部完全无分配。

完整日志位于 `../validation/nonworker-v1/`：`final-formatter-*-ctest.log`、`matrix-*-test.log`、`diagnostics-matrix.json`。名称中的 formatter 是沿用当前验收 build 目录的命名，日志包含完整测试集。最终文件哈希见 `implementation-files.json`。

## 尚未证明或尚未实现

生产 worker/runtime/wakeup 由用户实现；当前未安装 WorkerProvider 时 AsyncLogger 构造抛错。测试显式安装 manual provider，保留旧诊断的无消费者条件，不模拟生产异步功能。

未做真实共享/独立线程时序、真实异步性能、原生 Linux 发布验收；不能宣布 V1 完成或 BQLog 同等性能。下一步唯一入口为 [worker 交接指南](./V1_WORKER_HANDOFF_20260920_CHS.md)，总顺序见 [当前实施指南](./V1_REMAINING_CODE_IMPLEMENTATION_GUIDE_CHS.md)。
