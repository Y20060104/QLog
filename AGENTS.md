# QLog 工作约定

- 实际生产源码、测试、构建与权威设计文档位于 /home/qq344/QLog。
- Windows 入口：\\wsl.localhost\Ubuntu\home\qq344\QLog。
- E:\VisualStudioProject\BqLog 是 BQLog 参考和历史暂存目录；不得将其中的 QLog 快照当作当前生产仓库。
- 执行前检查实际仓库路径、git status 和最新验收报告；设计/计划中的旧进度不能覆盖较新的明确验收决定。
- 对未明确或相互冲突的设计，先给出问题、建议、语义/生命周期/成本影响，与用户商量后再冻结；草案不能作为已接受合同。
- 用户仅要求浏览文档与制定计划时，不据此开始生产代码开发。
- 当前阶段以 docs/decisions/I1D_ACCEPTANCE_20260913_CHS.md 为准：I1 已按 WSL2 开发范围收口，原生 Linux 发布复核与自动 CI 尚未完成。
- 当前 I2 Producer 合同：docs/decisions/ADR-013-v1-automatic-producer-context.md；覆盖 ADR-011 的显式绑定与注册锁方案。
- 执行计划：docs/decisions/MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md；动手指南：docs/decisions/MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md。
- I2 原草案仅保留讨论历史；不要重新采用旧 move-only、只读过滤或 Release 常驻统计候选。
- I2 生产骨架已开始，尚未完成；文档或骨架编译通过不等于生产实现/验收完成。

- 多 Appender 合同见 docs/decisions/ADR-012-v1-multi-appender.md；文件 reset 的 I/O 失败策略在 I4 前先讨论。

- 2026-09-15 用户接受 AsyncLogger::try_log + TLS 自动 ProducerContext；首条通过过滤可分配，稳态不分配。注册无 mutex/spinlock，CAS 只增链，停止全部使用者后回收。
- public BindResult/ProducerHandle 不再扩展；当前实施仅看新版主指南，显式绑定归档不是生效步骤。
- Appender 无锁配置命令交接仍待商榷，不把单管理线程/SPSC 命令候选当成用户已确认。
