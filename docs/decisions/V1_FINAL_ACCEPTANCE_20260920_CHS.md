# QLog V1 最终开发验收

本轮用户授权补全整个 V1 并最终性能测试，替代此前 worker 留给用户的分工。

结论：V1 生产功能已接通，WSL2 开发范围内的功能、并发、内存与性能验证完成。原生 Linux 发布复核尚需实际主机或远程 CI 证据，不能写成全平台发布验收已通过。

## 实现范围

- try_log → 自包含 Record/SPSC → 真实 worker → 一次正文渲染 → 每目标完整行 → 批次 → Console/TextFile。
- 默认共享、可选独立 worker；自动 Runtime 冷初始化；CAS 只增登记、worker 私有活动链、稳定节点在 Engine join 后回收。
- 66 ms 兜底、低空间/full/管理/注册/stop 唤醒、有限 Channel 配额、共用 Console gate、worker SIGPIPE 屏蔽。
- 配置冷准备、单槽请求/响应、兼容复用与重建 reset、flush/drain、自动文件故障恢复、正常与失败关闭。
- 独立 shutdown join、共享 shutdown 只解绑当前 Session、关闭后 Context 回收、重复 shutdown 稳定结果。
- 完整可运行示例、可重复性能程序、Linux CI 定义及使用/实现文档。

## 最终验证

| 项目 | 实际结果 |
|---|---|
| GCC Debug | 218/218 通过 |
| GCC Release | 217 通过，1 原有 Ring 调试检查跳过，0 失败 |
| ASan + UBSan | 218/218 通过 |
| 真实 worker Debug 稳定性 | 14 项，每项连续 5 次通过 |
| TSan | 14 项，每项连续 3 次通过，无报告竞态 |
| Debug/Release × AUTO/ON/OFF | 六配置各 39 项通过；关闭时诊断字段和 Producer 符号移除核验通过 |
| C++ 分配探针 | 原有受控路径和新增真实共享/独立路径通过 |
| 示例 | 共享和独立模式实际运行，完整文件输出和正常退出 |
| 最终性能 | 七场景 × 三轮，全部 accepted ID 在各目标精确验证；另有低流量可见延迟、正文/过滤微基准 |

真实线程专项覆盖 8 Producer 精确计数/各自顺序、共享 logger 解绑后其他 logger 存活、反复管理唤醒、Producer 并发 reset、pending reset shutdown、150 次并发构造关闭、忙/稀疏 logger 公平访问、broken pipe、启动信号设置失败回滚、100 个短命线程注册及磁盘满自动恢复。

已有 Appender 专项覆盖 short write、EINTR、零进展、EAGAIN、ENOSPC/EDQUOT、open/write/sync/close 故障与退休损失；已有 Session 专项覆盖部分 reset 所有权转移后的失败清理。实际线程创建资源耗尽和任意调度异常的每个分支未逐一做故障注入，不把启动 mask 失败测试宣称为全部 OS 故障覆盖。

验证过程中修正了基准浮点预期、采样与限速周期重合，以及初版 Runtime 登记锁与活动 ADR 不一致。最终性能仅采用 CAS 版本。一次 TSan 增量构建因 Make 重新生成前未知新 target 中止，重新配置后完成；不是 TSan 竞态失败。

## 证据位置

- `docs/validation/v1-final/{debug,release,san}-tests.log`：完整回归。
- `worker-debug-stress.log`、`tsan-tests.log`：重复线程测试。
- `matrix-*-test.log`、`diagnostics-matrix.json`：六配置与字段/符号验证。
- `performance-release/`：唯一最终性能数据，说明见 [性能报告](./V1_PERFORMANCE_20260920_CHS.md)。
- `source-manifest.json`：源文件与测试 SHA256；`final-checks.json`：文档和差异核验。

原生 Linux CI 已写入 `.github/workflows/v1-linux.yml`，尚未推送触发或获取外部运行结果。本轮没有提交 Git、推送、发布，也没有替换用户原有未提交修改。

## 使用边界

管理线程单一，drain 前停止 Producer，shutdown 前停止并 join 所有使用者。accepted 是入队成功，不能代替投递/持久化状态。默认批次刷新有可见延迟；共享 worker 会受到慢同步 I/O 影响。失败接管只在确认释放 Session 借用后执行；V1 不提供线程 watchdog、崩溃信号强制 flush、无限磁盘恢复等待或进程崩溃后的数据保证。

入口：[README](../../README.md)、[实现地图](./V1_REMAINING_CODE_IMPLEMENTATION_GUIDE_CHS.md)、[Runtime 说明](./V1_RUNTIME_IMPLEMENTATION_20260920_CHS.md)。
