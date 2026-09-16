# QLog I2 执行计划：自动 ProducerContext

日期：2026-09-15；状态：ADR-013 已接受，生产仍在基础接线阶段，未验收。
当前入口：[动手指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)；决策：[ADR-013](./ADR-013-v1-automatic-producer-context.md)。
I1 以 [I1-D WSL2 验收](./I1D_ACCEPTANCE_20260913_CHS.md) 为准，不重复启动 Record Core 开发。

## 1. 当前合同

- AsyncLogger::try_log 为公共入口，取消强制显式绑定；TLS 自动取得 ProducerContext。
- 先校验 level/category、一次过滤，再取得上下文、measure、reserve、clock、encode、commit/abort。
- 首条通过过滤可能分配 TLS/Context/Ring；同线程同 Logger 已接入后的稳态不分配。
- V1 每线程/Logger SPSC；V2 再讨论频率切换共享 MPSC，API 不感知队列；V1 不预加频率分支。
- 注册 CAS append-only；无 mutex/spinlock，无并发 owner vector；关闭后统一回收。
- 不复用 Logger 身份、TLS 不拥有发布节点；同地址重建、线程退出与 Logger 退出都必须验证。
- close 前停止全部调用/接入；atomic open 不构成并发关闭协议。
- Console/TextFile、空配置默认 Console、Appender 抽象基类沿用；动态配置无锁交接协议待 I3 商榷。

## 2. 生产分批

| 包 | 文件与函数 | 交接条件 |
|---|---|---|
| A 基础接线与 API | async_logger.cpp 的 Impl 三处修正；async_logger.hpp 去 Bind 公共接口；log_result 的 context 错误；log_format | 配置/过滤/时钟实际可链接，结果与 format 契约明确 |
| B 上下文接入 | detail/producer_context.hpp、src/producer_context.cpp、channel.hpp/cpp、Impl 身份/发布链 | TLS 快缓存/索引、准备存储再发布、身份/失败回滚和生命周期正确 |
| C Producer | async_logger.hpp 模板与内部实现头，分类桥接、上下文结果、I1 复用 | runtime 两整数闭环，再 literal/字符串/全部错误分支 |
| D 诊断与验证 | ProducerDiagnostics、PUBLIC AUTO/ON/OFF 宏、根 qlog target、Codex 测试/基线 | Debug/Release 矩阵、外部守恒、无稳态分配、真实 Release 代码生成 |

维护者完成生产实现，Codex 按交接创建并执行验收；不把片段检查算作生产完成。
原有 filter/clock 修正、LogResult 工厂已写，先按主指南 §0 核对，不重复旧快照待办。
第一批是 A，不是先实现 Appender 虚函数或 V2 队列切换。

## 3. 验收新增范围

保留 I1 wire、filter 独立原子、精确 reserve/abort/commit 与 clock 失败继续记录的验收。
增加过滤不建 TLS、首用与稳态分配区别、TLS 容量失败、发布后无失败操作、Logger 地址复用、身份耗尽、
两种退出顺序、多个 Logger 切换、并发首用及头文件 PImpl/模板真实消费方链接。
calls=filtered+rejected_pre_admission+attempted；context 错误归准入前拒绝；Debug 可用 Logger 范围统计，Release 外部验证。
I3 排空后才验证 accepted=processed。未执行原生 Linux TSan/发布复核仍保留待办。
性能区分 first-use、same-logger、alternating-loggers、filtered/full；不能声称移除冷注册锁必然改善稳态吞吐。

## 4. 后续分界

I2：自动上下文、稳定 SPSC/注册、Producer 写入；测试消费者直接读取，不代表真实 Backend。
I3：Backend 公平扫描、Console 基础文本格式化/输出、排空；运行 Appender 配置交接协议先商榷，不沿用旧管理锁。
I4：TextFile 独立资源、输出配置与文件 reset 失败语义；复用 I3 基础文本能力。
V2：频率测量、滞回、SPSC/MPSC 转换、同生产者跨队列 FIFO 和回收；当前只保留上下文抽象，不实现这些策略。


## 5. 当前实施细化入口

最新读取已确认身份头、Channel 引用/去重复、make_context 与 last 缓存安装推进；当前仍有 Context 指针类型、最大值类型、容量判断、槽分支括号、回滚实参与 Impl 构造/析构问题。
按 [runtime 下一步指南](./MILESTONE2_I2_RUNTIME_NEXT_GUIDE_CHS.md) §1 修完，再依次恢复公共 LogResult、完成四个 mapper、runtime try_log、根 qlog target 接线。
这一批只要求 runtime 两整数的真实 Producer→Ring→decoder 验证，literal、Debug 诊断与完整 I2 验收继续保留；输出 Backend 为 I3。
现有合同足以开展此批，无需重复确认；Appender 动态配置、文件 reset 与 V2 切换留待对应阶段商榷。
