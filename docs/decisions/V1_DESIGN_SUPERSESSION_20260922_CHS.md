# 旧设计退出与删除状态

> **2026-09-23 Ring补充决定：旧字节Ring/配置及相关测试基准、过期Ring文档已删除。** 当前库仅版本基础；新SISO按8字节block、外部内存、32位游标重建，见 [Ring实现指南](./R0_BQLOG_BLOCK_RING_GUIDE_CHS.md) 与 [清理记录](./R0_RING_CONFIG_CLEANUP_20260923_CHS.md)。此前47项测试只是本次Ring删除前的历史结果。


更新：2026-09-23。用户明确授权批量删除旧冲突实现、相关测试/基准及过期文档，本轮实际删除136个文件；不是仅加作废标记。

## 当前权威入口

- [ADR-014：全主链 BQLog 优先](./ADR-014-v1-bqlog-first-rebaseline.md)
- [ADR-015：Record 与完整 Layout 一起重建](./ADR-015-bqlog-layout-record-alignment.md)
- [后续路线](./V1_BQLOG_ALIGNMENT_ROADMAP_20260922_CHS.md)
- [清理后的补全实现指南](./R0_POST_CLEANUP_IMPLEMENTATION_GUIDE_CHS.md)
- [完整 Layout 源码/算法指南](./R0_BQLOG_LAYOUT_REBUILD_GUIDE_CHS.md)
- [已执行删除清单与备份位置](./R0_CONFLICT_CLEANUP_20260923_CHS.md)

## 已删除的过期文档组

ADR-007 至 ADR-013；旧 MILESTONE2/I1/I2 指导与验收说明；旧 V1 决策日志、开发指南和两里程碑指南；NEXT_IMPLEMENTATION_GUIDE；旧六参数 R0 指导、对照清单及预演说明。逐文件名称见清理记录。不要从 git 历史中恢复其冻结条款作为新实现约束。

## 保留范围

ADR-001 至 ADR-006、Ring教学/完成报告与评审已随本次旧Ring配置删除；历史可由git或清理备份追溯，当前入口只保留新指南。新 ArgumentTag、40字节 RecordHeader/扩展头、等级枚举保留。纯 hash 内核暂为未接入库的候选，不代表已对齐。原始验证产物和语料仅历史，不纳入当前测试发现。

当前通过与否必须以新构建目录为准；删除旧测试不代表旧功能被新实现替代。新完整Layout和Logger主链仍需按指南实施和差分验收。
