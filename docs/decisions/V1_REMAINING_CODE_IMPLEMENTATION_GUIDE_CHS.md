# QLog V1 实现入口与验收状态

用户已更新授权，由 Codex 完成整个 V1，包括 worker 和最终性能测试。此前“worker 留给用户”“未安装 provider 构造抛错”的交接状态已结束。

## 当前实现地图

| 层 | 权威源码 | 职责 |
|---|---|---|
| Producer | include/qlog/async_logger_impl.hpp | validate/filter/measure/reserve/admission timestamp/encode/commit；压力通知 |
| 格式化 | src/text_formatter.cpp | BQLog 风格正文状态机、数值转换、完整行 |
| 输出 | src/appender.cpp、console_appender.cpp、text_file_appender.cpp | 批次、短写、同步、恢复、最终关闭 |
| 冷准备与控制 | src/appender_prepare.cpp、management_controller.cpp | 配置校验、复用映射、单槽管理完成 |
| Session | src/backend_session.cpp | 按 Channel 配额消费、命令应用、输出访问、停止状态机 |
| Runtime | src/backend_runtime.cpp | 真实共享/独立线程、CAS 节点登记、等待唤醒、释放确认与 join |
| 默认 Provider | src/worker_provider.cpp | 首次构造自动选择生产 Runtime；保留内部测试替换入口 |
| 公共生命周期 | src/async_logger.cpp | request/poll/shutdown/析构 |
| 示例 | examples/v1_logging_demo.cpp | 真实双目标、reset、drain、两种线程模式与停止 |
| 性能 | benchmarks/async_logger_benchmark.cpp、visibility_benchmark.cpp | 端到端记录校验、分位延迟、吞吐和可见延迟 |

以上路径相对仓库根 `/home/qq344/QLog`。类型和函数签名以实际头文件为准，旧 PRE_SESSION_ARCHIVE 不再作为代码粘贴依据。

## 现在的阅读顺序

1. [README](../../README.md)：构建、运行和公开使用契约。
2. [Runtime 实现说明](./V1_RUNTIME_IMPLEMENTATION_20260920_CHS.md)：所有权、发布、服务、解绑与失败边界。
3. [最终开发验收](./V1_FINAL_ACCEPTANCE_20260920_CHS.md)：实际测试和环境限制。
4. [性能报告](./V1_PERFORMANCE_20260920_CHS.md)：最终版本的可复查结果。

已有完整数据/控制/生命周期实现，不再要求用户补函数。原生 Linux 发布复核需要在对应主机或实际运行 CI 后取得证据；不能拿 WSL2 性能替代目标环境结果。
