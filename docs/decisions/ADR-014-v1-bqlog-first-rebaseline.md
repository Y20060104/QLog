# ADR-014：回退后 V1 全部主链先对齐 BQLog

> **2026-09-22 最新确认：完整 Layout 与 Record 一起对齐 BQLog。** 已实现/冻结部分不能限制对齐；旧 32 字节头、纳秒时间、旧 tag/DecodedArg、六参数正文函数不再是当前合同。当前 R0 入口为 [完整重建指南](./R0_BQLOG_LAYOUT_REBUILD_GUIDE_CHS.md)，详见 [ADR-015](./ADR-015-bqlog-layout-record-alignment.md)。以下冲突内容只保留为历史。


- 日期：2026-09-22。
- 状态：用户明确确定方向；详细路线交维护者检查，未授权据此立即替换生产实现。
- QLog 起点：`feat/spsc-ring-opt@94fefc0`；BQLog：`60ef4d3`。
- 当前后续入口：[BQLog 对齐路线](./V1_BQLOG_ALIGNMENT_ROADMAP_20260922_CHS.md)。

## 最新决定

用户先要求正文与完整行、Logger 后台状态、Worker、动态 reset、Producer/Channel 对齐 BQLog，随后明确“接受的其余建议也先对齐 BQLog”。后者覆盖本轮首版“保留 QLog 差异”的解释。

当前 V1 主链的配置、缓冲/路由、运行态/管理、线程模式、逐目标 layout、Appender、文件 I/O、reset、flush、退出均先以 BQLog 实际源码建立设计和验证基线，再谈优化。

撤销生效要求：固定 SPSC 到 V1 结束、自适应留 V2、全路径无锁/无共享 RMW、跨目标正文共享、解析缓存、旧 BackendSession、单槽 reset、自定义公平预算、Frame 释放后才能 I/O、失败必保持旧配置、有限提交快照 flush、默认结构化持久化/关闭结果、空配置自动 Console，以及固定扁平配置 public 模型。

空配置、默认 block、async/independent/sync、reset 部分失败、持有记录期间输出和 force_flush/uninit 的实际边界按源码记录。不能替 BQLog 添加其未提供的事务/可靠性保证，也不把发现的缺陷包装为理想设计。

## 文档与实现边界

[作废清单](./V1_DESIGN_SUPERSESSION_20260922_CHS.md) 列出旧指南整体退出执行入口和旧 ADR 冲突条款范围。底层 Ring/Record 的已实现代码、不冲突的安全约束与历史验收事实保留，但不能反向约束新的 Logger 主链。

路线不自动扩大为 BQLog 全产品克隆；raw/compressed、mmap 恢复、snapshot、跨语言等列入范围外清单，关联依赖需查证。具体 public 签名、配置实现库、锁与并发细节在源码详细对照后制定。

本轮仅文档更新；回退前报告及未跟踪残留不是当前验收证据。首版路线保留的 QLog 特例已被此修订撤回。
