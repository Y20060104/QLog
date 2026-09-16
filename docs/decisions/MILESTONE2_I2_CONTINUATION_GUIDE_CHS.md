# I2 续写入口：已切换到自动 ProducerContext

更新：2026-09-15。原逐函数续写方案中的 public ProducerHandle/bind_producer 已由用户确认的 ADR-013 取代。

- 当前决策：[ADR-013](./ADR-013-v1-automatic-producer-context.md)。
- 当前进度与逐函数步骤：[主指南 §0～8](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)。
- 当前分批计划：[I2 执行计划](./MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md)。
- 旧资料只作追溯：[显式绑定阶段归档](./MILESTONE2_I2_EXPLICIT_BINDING_ARCHIVE_CHS.md)。

Impl 构造的 FilterState 参数与 has_fallback/automatic 已修正。下一步直接按 [主指南 §11](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md#i2-detailed-steps) 修当前配置复制/模板位置，补 CallGate 和 ContextResult，再逐函数接 TLS。
然后撤出 public Bind 类型，完成 Format/context 错误，再实现 TLS/唯一身份/CAS 注册与 Logger::try_log。
本文件只导航，不维护第二套冲突的接口代码或旧快照。未执行的生产测试不能由旧文档验证报告替代。
