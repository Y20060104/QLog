# ADR-013：Logger 直接写入、TLS 自动 ProducerContext 与 V2 路由边界

- 日期：2026-09-15；状态：用户明确同意，已接受。
- 权威源码：/home/qq344/QLog；本次只修改文档，I2 尚未验收。
- 覆盖 ADR-011 的“必须显式绑定、public ProducerHandle、热路径不访问 TLS、冷注册 mutex”部分。
- 保留 I1 wire/codec、独立原子过滤、固定 category、admission clock、停止后回收等合同。
- 实施入口：[当前 I2 指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)。

## 1. 已接受的公共入口与成本范围

公共类继续叫 AsyncLogger，不另增第二个 Logger 类；主要入口改为 AsyncLogger::try_log(category_id, level, format, args...)。
调用方无需 bind_producer 或保存 ProducerHandle。public BindResult/BindError 随显式入口撤出；首次接入错误由 LogResult 表达。
可选预热以后再设计，不是 V1 使用前提，本次不新增承诺。

每次有效 Logger 调用先验证 level/category，再执行一次粗过滤；拒绝时不创建 TLS 上下文、不分配 Ring、不 measure/strlen/取时。
通过过滤后自动取得本线程对本 Logger 的 ProducerContext，再 measure、reserve、取时、encode、commit/abort。
首次通过过滤的调用可能创建 TLS 索引、上下文和 Ring；随后在同一线程/Logger 上的稳态调用不分配。
首次通过过滤但 measure 最终拒绝，也允许已经创建上下文；“通过过滤”不等于“成功入队”。
普通函数调用前参数表达式已求值，不承诺宏式惰性求值。

不引入 mutex/spinlock 到注册与生产协议；CAS 发布的 lock-free 条件须检查目标原子类型。
包含通用内存分配、系统时钟与输出 I/O 的整条调用不作严格 lock-free/wait-free 保证；冷分配和首次延迟单独测量。

## 2. 上下文与队列分离

ProducerContext 表示一个生产线程对一个 Logger 的生产状态，不是对固定 Ring 的 public ABI 承诺。
V1 每个线程/Logger 仍使用独立 SPSC Channel；一个线程的重复调用复用上下文。
V2 可按频率选择独立 SPSC/共享 MPSC，不改变主要公共 API。
V1 不提前增加频率计数、动态队列判断或每条虚队列派发。

V2 必须另行确定阈值/滞回、频率采样成本、过渡记录、同生产者 FIFO、退役和回收协议。
同一线程先写旧队列的 A、后写新队列的 B，不能只因 Backend 扫描顺序不同就允许 B 越过 A。
本次只接受自适应方向，不认为一个 if 切换指针即可满足顺序与发布合同。

## 3. TLS 缓存与对象身份

每个 Logger 实例有进程内不复用的非零身份，不能仅以 this 地址作缓存键；编号耗尽不能回绕。
2026-09-15 用户补充：身份字段、函数参数和返回类型直接使用 std::uint64_t，不增加身份类型别名；以 logger_id/producer_token 名称区分用途。此项仅简化类型写法，不改变两个计数器域与不复用合同。
TLS 保存最近 Logger 身份/上下文的快速缓存，以及多个 Logger 的索引；单 Logger 连续写命中快缓存。
索引存储具体表示由实现选定，不承诺所有 Logger 切换 O(1)；第一次插入允许分配，命中不分配。
身份必须在读取旧 context 之前匹配；Logger 地址复用不能命中旧实例。
Logger 销毁必须无并发调用/Backend 借用。TLS 不拥有 Channel，也不能在析构时访问已经销毁的 Logger/context。
线程退出只清理自身索引容器；已发布节点留给 Logger，直到全部使用者退出后统一删除。
允许 TLS 保留过期键直到线程退出的最小基线，但要记录长期管理线程经历大量 Logger 重建时的索引增长成本。
无需为了单一缓存正确性引入每条 shared_ptr 引用计数；运行期清扫优化须另证安全性。

## 4. 自动注册与无锁发布

TLS 首次 miss 后：准备所有可能失败的 TLS 存储 → 构造完整 ProducerContext/Channel → CAS 发布到 Logger 的只增链 →
安装无分配、无异常的 TLS 值 → 返回内部上下文。发布后绝不因缓存插入失败销毁可见节点。
发布前由局部 unique_ptr 持有，发布成功后 Logger 拥有整条链；Backend 只借用 acquire 取得的节点。
next 仅在发布前修改，发布后不可变；运行中不删除，关停后统一回收，因此无运行期 ABA 地址复用。
不保留并发写 vector<unique_ptr<Channel>> 的另一条所有权注册路径，不以 spinlock 替代 mutex。
线程 token 如用于显示身份/诊断，采用不复用的进程 token，OS TID 单独保存；稳态不执行 token 分配。

## 5. 返回结果、诊断与生命周期

LogResult 需增加或明确 context 阶段：resource_exhausted、registration_closed、identity_exhausted 等可区分失败。
资源不足不能映射为 Ring full；首次接入失败不执行 measure/reserve/clock，不留下半发布节点。
try_log 保持 noexcept 时，内部捕获明确可恢复的分配失败并映射；不吞所有未知错误，不用错误日志递归报告自身失败。
Logger 身份耗尽发生在创建阶段，构造失败；线程身份耗尽如发生在首次接入，则返回 context 失败。
内部 TLS/分配接入不支持同一调用栈重入日志，不支持信号处理器内调用；未来支持需独立协议。

现行关闭顺序保留：停止/join Producer 和自动接入活动 → 结束管理活动 → close_registration → 排空/退出 Backend →
删除发布节点 → 销毁借用依赖。close_registration 不承诺与 try_log/接入真正并发的截止语义。
独立 atomic open 的 release/acquire 不能使“检查 open”和“CAS 发布”原子化。

数量守恒调整为有效 Logger 调用范围：calls=filtered+rejected_pre_admission+attempted；
attempted=accepted+dropped_full+failed_after_attempt。context 错误归 rejected_pre_admission。
过滤发生在上下文创建前，不能为了计数而创建 Channel；Debug 可用 Logger 范围诊断，普通 Release 移除字段/更新，外部核对。
不依赖诊断进行关闭，I3 排空后才验证 accepted=processed。

## 6. BQLog 参考与性能取舍

参考本地 BQLog 60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9：
log_buffer.h 的 log_tls_info::get_buffer_info（最近 Buffer 缓存与 TLS map）、
log_buffer.cpp 的 alloc_write_chunk（频率选择）、miso_linked_list.h 的 insert（CAS 发布）。
QLog 自动入口增加 TLS 路由成本，换取业务无需预绑定及 V2 接口稳定；单 Logger 快缓存、双 Logger 切换、首次接入分别测量。
固定 SPSC 省共享写竞争但多占 Ring 内存；冷 CAS 注册不直接提升已接入后的逐条吞吐。
无测试数字不宣称优于 BQLog。对比需统一丢弃策略、输出内容、诊断开关和 accepted 数量。

## 7. 不在此次确认范围内的方案

Appender 抽象基类、Console/TextFile、空配置默认 Console 沿用 ADR-012 最新补充。
旧 Appender 管理锁与用户“不引入锁”目标不一致，不再作为待照抄的实施方案。
“固定管理线程 + SPSC 配置命令 + 异步生效”仍是候选，用户此次仅确认自动日志入口，不能顺带冻结配置 API。
I2 可继续实现初始配置和原子 category 更新；运行期 Appender 列表变更在 I3 实施前商榷命令提交/完成/背压协议。
Console I3 基础格式化、TextFile I4 文件资源失败合同和真实关闭验证分期保持。
