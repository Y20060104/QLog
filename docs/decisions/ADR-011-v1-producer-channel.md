# ADR-011：V1 Producer / Channel 接入、动态过滤与条件诊断

> 2026-09-15 覆盖说明：以 [ADR-013](./ADR-013-v1-automatic-producer-context.md) 为当前 Producer 入口合同。
> 以下涉及显式 bind/public Handle、禁止 TLS、注册 mutex、owner vector 的历史条款不再生效。
> Appender 管理锁也不再作为下一步实施方案；无锁配置交接的线程数/队列/API 尚待 I3 商榷，未随自动入口一并确认。
> 多目标、独立过滤、Console 默认输出和释放 Frame 后 I/O 等未冲突合同仍有效。当前步骤见 [主指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)。


- 状态：已接受；2026-09-13 用户同意冻结，并补充过滤/生命周期/接口对齐选择。
- 范围：I2 设计；生产骨架已开始，尚未形成验收完成的实现。
- 前置：I1 按 WSL2 开发范围收口，见 [验收报告](./I1D_ACCEPTANCE_20260913_CHS.md)。
- 实施：[I2 计划](./MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md)、[I2 动手指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)。

## 1. 取代哪些旧建议

本 ADR 取代 I2 初稿中的“运行中只读过滤”“Release 常驻本地计数”“move-only ProducerHandle”
以及 trace/warn/critical 等级候选。历史讨论保留在 [I2 草案](./MILESTONE2_I2_PLAN_DRAFT_CHS.md)，
不能再根据草案早期段落实现不同合同。

RecordHeader、ArgumentTag、hash identity、checked measure、encoder 强失败保证、Release decoder 检查不变。
BQLog 是行为与成本参考，不整体移植其路由、阻塞/扩容策略、配置解析器或对象线程语义。

## 2. Producer 核心入口

V1 显式冷路径 bind_producer，之后通过轻量 ProducerHandle 直达本线程专属 SPSC Channel。
同一生产线程/同一 Logger 重复绑定得到同一 Channel；不同 Logger 完全隔离。
Handle 可轻量复制，副本只在绑定线程内顺序使用；复制不分配、不增加共享引用计数、不生成新 Ring。
析构无 commit/abort、注销、等待或回收副作用。
空 Handle 可以返回 invalid_handle；Logger 销毁后的悬空 Handle 是调用方违约。

BQLog 的 log 可跨线程调用依赖其内部 TLS 路由；QLog 的已绑定 Handle 不继承该行为。
其他线程必须自己绑定。不能因为 C++ 对象可复制就允许多个 Writer 共享同一个 SPSC。

V2 可以让不同线程的 ProducerContext 指向同一/分片 MPSC；绑定方式和队列拓扑分离。
V1 不增加频率检测、MPSC 分支、虚派发或永久单指针 public ABI 承诺。
这个入口选择基于避免重复路由的成本分析，不是已实测全负载性能最优的结论。

## 3. public level 与 format

等级命名/数值对齐 BQLog：verbose=0、debug=1、info=2、warning=3、error=4、fatal=5。
QLog wire 仍为 u8；public 枚举使用 uint8_t 底层，不复制 BQLog 自身 C++ enum 的 int32_t ABI。
fatal 不隐式终止或 durable flush。位图零值表示全部关闭，不写 off 等级。
I1 RecordValidationPolicy 允许六个合法 wire 值；不能将运行过滤 bitmap 作为合法值 mask。

public format 入口封装 runtime pointer/size 与可信 literal 编译期 hash，调用同一条 Record 写入路径。
不公开可任意填写的 precomputed hash；不解析格式，不注册 Callsite，不新增 wire flag。
参数表达式在普通 C++ 函数调用前求值；I2 不承诺宏级惰性求值。
无分配、可检查的结果区分 accepted、filtered、输入/容量拒绝和内部失败。

## 4. 运行过滤：独立标量生效

多 Appender 补充合同见 [ADR-012](./ADR-012-v1-multi-appender.md)：level 位图由所有 Appender levels 做 OR 派生（不排除禁用目标），不提供独立 Logger level setter。后台按处理时配置再次检查 Logger category 和各 Appender 过滤。

用户明确要求与 BQLog 一致：level 位图和 category 开关独立更新，不保证整组配置一致切换。
Logger 创建时固定 category 数量/名称，category=0 为默认；运行期间不扩容或替换过滤数组地址。
每条 level/category 仍可选择不同输入；配置热更新只改过滤值。

用稳定地址 atomic uint32_t level bitmap 和 atomic uint8_t category 开关保存可变值。
查询/设置采用独立 relaxed load/store，Tier 1 验证相应 atomic lock-free。
先验证 level/category 范围，再做位移和数组访问；bitmap unknown bits、越界 category 拒绝且不写。
批量 reset 先完整验证输入，合法后依次 store；它不是事务，读者可观察到混合新旧值。
不做 seqlock 重试、不逐条加锁、不用 atomic 值发布新对象。

过滤通过后才执行 measure/strlen/reserve/clock。更新不撤销入队事实，但可改变积压记录最终的输出目标；不改变 codec policy。
不承诺跨线程瞬时截止点；需要确定更新先后的测试/调用方应建立显式同步。
冷配置管理和 Producer 可并发，但 Logger 销毁必须等待相关管理操作结束。

## 5. Channel 生命周期与发布

用户接受：允许运行中显式绑定新线程，Channel 地址稳定并保留到排空，Producer 停止/join 后 shutdown。
Logger 持有稳定堆分配 Channel。注册锁只在冷路径；所有权容器增长不得搬动 Channel。
进程内不复用 token 标识生产线程，显示用 OS thread_id/name 分开保存，避免线程 ID 回收误匹配。

实现基线采用串行注册与 append-only 发布链：节点完整构造并由 owner 持有后，
release 发布链头；reader acquire 取得链头，只读发布后不可变 next。
Backend 不并发读取可增长的 owner vector；Channel 在 Backend join 前不回收。
绑定分配失败不得发布半初始化节点，已有 Channel 不受影响。

默认 Ring 容量沿用 QLog 64KiB，默认完整 Record quota=8KiB；创建前允许配置。
复用现有 Ring 校验，并要求 quota>=32；64KiB 与 BQLog 桌面默认 buffer 一致，但不继承 block/expand。
注册容器冷路径增长，不凭空指定 128 为生产上限；资源不足可检查返回。
大量短命线程会累积 Channel 内存到 shutdown，这是此生命周期的明确代价。

关闭顺序：停止并 join Producer → 停止其他注册/配置活动 → 关闭注册 → Backend 排空并退出 →
销毁 Channel → 销毁其借用的 Logger 元数据。
不支持 shutdown 与业务日志调用真正并发，不给热路径增加在途计数/共享 closing 检查。
I2 提供冷绑定/关闭注册与直写；真正 Backend/shutdown 排空由 I3 实现，I2 不提供空实现冒充完成。

## 6. Producer 精确顺序

```text
handle / level / category 基础校验
  → 一次过滤
  → measure_record 一次，复用 PreparedRecord
  → try_reserve(exact payload) 一次
  → admission timestamp
  → encode_v1 直接写 Ring
  → 成功 commit / 可恢复失败 abort
```

full 不重试、不分配、不读时钟。非 full reserve 失败属于内部诊断，不 abort 不属于当前调用的 pending。
成功 reserve 已写外层 FrameHeader；abort 保证不发布/不推进写游标，不承诺整个 Ring storage 字节不变。
encoder 继续 Header-last；真正跨线程发布由 Ring commit 完成。
时钟描述符按 ADR-008 冷路径 probe 构造；realtime 若被选为 primary，记录 flags=primary_valid。
两级取时失败仍提交 unavailable Record；policy 只在 descriptor 配置了 fallback 时允许 fallback status。

## 7. 诊断与验收分离

对应 Producer 结果/字节/数量诊断在 Debug 启用，常规 Release 编译移除字段和更新。
不新增 public 运行中统计快照 API，不将 BQLog 日志内容 take_snapshot 引入 I2。
Debug 使用内部诊断计数，精确聚合在停止 Producer 后；不承诺多计数的运行时同时刻快照。
建议独立 AUTO/ON/OFF 配置供诊断构建验证，默认 AUTO 随 Debug 开启。
影响 public 模板/Channel 布局的宏必须由 target PUBLIC 一致传播，禁止跨 TU 布局/ODR 不一致。

有效 Handle、全部调用终结且计数未溢出的验收公式：

```text
calls = filtered + rejected_pre_admission + attempted
attempted = accepted + dropped_full + failed_after_attempt
I3 排空后 accepted = processed
```

invalid_handle 无 Channel，由外部测试单独计数。编译期拒绝不产生 calls。
rejected_pre_admission 包括输入拒绝与 measure 阶段内部配置错误；
failed_after_attempt 包括非 full reserve 失败与 encode_abort，互斥计数。
Release 用调用结果、测试 Record ID、test consumer 外部验证守恒，不能用假零 getter 验收。
shutdown 按无 Producer 且队列排空完成，不依赖已经移除的计数。
Ring 同步/回收/关停状态、错误返回、Release decoder 检查不能随诊断开关移除。

## 8. 成本、ABI 与验证影响

- 稳态：过滤标量 load、既有 measure/codec/Ring 和 admission clock；无注册 map/TLS、mutex、分配、共享 RMW。
- Debug 诊断有额外原子计数开销，不用于默认 Release 性能结论。
- 冷路径：锁、token 分配、Clock probe、metadata/Channel/过滤表分配；数量/内存成本需测量。
- 编译期：Producer 模板与 literal hash 可增加实例化量；不以全部 always_inline 代替 codegen 检查。
- Record/SPSC wire 和 16B 被动 Handle ABI 不变；新增 public Logger 接口不承诺 V2 二进制 ABI。
- 注册发布/过滤需要并发验证；原生 Linux TSan 发布门禁未执行时明确留待后续，不伪称 WSL ASan 可替代。

本次只完成合同与指南。I2 的生产、测试、benchmark 和构建门禁状态仍为未完成。
