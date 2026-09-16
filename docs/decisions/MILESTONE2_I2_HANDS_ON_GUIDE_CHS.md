# QLog I2 动手指南：Logger 直接写入与自动 ProducerContext

更新：2026-09-15；基准 35570da 加本轮未提交源码。实际目录 /home/qq344/QLog。
状态：本指南为后续实施指导；已有源文件不等于 I2 已编译/运行验收。本轮只更新文档。
规范顺序：用户确认 → [ADR-013](./ADR-013-v1-automatic-producer-context.md) → ADR-012 未被覆盖部分 → ADR-011 未被覆盖部分。
I1 wire、参数准入、Header-last、Release decoder 检查不变；所有区间接口沿用 pointer + 显式长度，不新增 span。

本文直接取代旧显式绑定操作步骤；旧例子仅见 [历史归档](./MILESTONE2_I2_EXPLICIT_BINDING_ARCHIVE_CHS.md)。
维护者实现生产，Codex 在交接后编写/运行验证；用户后续明确授权优先。

**本轮继续入口：[下一步：修完接入层并完成 runtime Producer](./MILESTONE2_I2_RUNTIME_NEXT_GUIDE_CHS.md)。该文 §1 是最新源码修正清单，§2～§5 给出公共结果头、完整错误映射表、runtime try_log 参考和构建接线。本文 §11/§12 保留接入层参考；旧快照待办由当前 §0 与新指南覆盖。**

<a id="i2-current-next"></a>

## 0. 当前已经写到哪里

| 当前文件/锚点 | 最新实读状态 | 本轮下一步 |
|---|---|---|
| producer_identity.hpp / Channel / ContextResult | 两函数声明、uint64_t 字段、category_names 引用及去重复 RegistryView 已完成 | 保留；Channel 补 utility 直接依赖 |
| make_context / install_tls_slot / publish_context | 节点构造、last 更新和发布已完成 | 保留，接入闭合后验证 |
| ThreadRegistry / take_id / prepare_tls_slot | Context 缓存仍少指针星号；while 最大值使用 size_t；容量条件仍错 | 新指南 §1 三处精确修正 |
| acquire_thread_context | 已加槽准备，但 if (!slot) 缺右括号，token 回滚少下标 | 闭合 if，补 *slot；完整参考仍见 §12.4 |
| Impl | dependencies 内部逗号已修，初始化列表尾逗号与 delete *node 尚未修 | 新指南 §1 |
| async_logger.hpp / 公共结果 | 已 include context_result，但公共 LogResult 仍缺；桥接仍 public | 新指南 §2 恢复公共结果并 include，桥接移 private |
| Producer 模板 / mapper / CMake | 尚无完整 runtime 写入与构建接线 | 新指南 §3～§5，之后交接 §6 验证 |

<a id="i2-filter"></a>
<a id="i2-clock"></a>

## 1. 第一批：先修现有 Impl，不改变配置模型

位置 src/async_logger.cpp 的 AsyncLogger::Impl 构造函数。下面是增加身份前的基础初始化参考，不是当前完整 Impl；最新身份/依赖/析构补齐见 §12.6：

```cpp
explicit Impl(LoggerConfig prepared_config)
    : config(std::move(prepared_config)),
      filter(merge_appender_levels(config.appenders.data(), config.appenders.size()),
             config.category_enabled.data(), config.category_enabled.size()),
      clock(detail::probe_admission_clock()),
      policy(detail::make_producer_policy(clock.has_fallback())),
      hash_dispatch(detail::FormatHashDispatch::automatic()) {}
```

这是当前字段下的构造修正，后续 Logger 身份/发布链按 §4/5 增加。字段声明仍是 config→filter→clock→policy→dispatch，
先后由声明决定；不要只改初始化列表而倒置字段。cpp 直接 include atomic，清理未使用 mutex，不能依赖传递 include。
既有 FilterConfigAccess 与 category setter 可保留；setter 只更新稳定 FilterState，不写配置 vector 的第二份实时镜像。
构造/析构定义仍放完整 Impl 之后；此时先保证基础依赖可链接，不实现空 bind 或空成功日志。

## 2. 公共 API 与头文件组织：避免模板访问不完整 Impl

位置 include/qlog/async_logger.hpp。保留 LoggerConfig、构造/析构、禁止复制移动、category setter、关闭注册。
移除 BindError/BindResult、bind_producer() 和 producer_handle.hpp include；新增 try_log 模板声明：

```cpp
template <typename... Args>
    requires(sizeof...(Args) <= detail::kMaxArgCount) &&
            (detail::SupportedArgument<Args> && ...)
[[nodiscard]] LogResult try_log(std::uint32_t category_id,
                               LogLevel level,
                               FormatView format,
                               Args&&... args) const noexcept;
```

直接 include 参数约束/record_limits、log_result/log_level/log_format；FormatView 完成后才能使用完整值。
同一 Logger 可以被不同线程调用；线程路由在内部完成，不是多个线程共写同一个 SPSC。
const 不表示内部无写入，也不允许与 Logger 析构并发。

推荐第一版用两个私有非模板桥接函数解决 PImpl 可见性，不在模板里直接写 impl_->filter：

| 建议接口 | 在哪里定义、作用 |
|---|---|
| detail::CallGate classify_call(uint32_t id, LogLevel level) const noexcept | cpp 中访问 Impl，先验证 level/category 再一次 FilterState 查询；返回 proceed/filtered/invalid_level/invalid_category；不碰 TLS |
| detail::ContextResult acquire_context_for_thread() const noexcept | cpp 中访问稳定身份与注册数据；命中 TLS 或走冷创建；只返回内部 context/错误，不作为业务 public API |
| try_log 模板 | 可放 detail/async_logger_impl.hpp，在 async_logger.hpp 类定义后、namespace 外 include；包含完整 ProducerContext/Channel 与 I1 头 |

CallGate 是独立内部 enum，不用 LogStatus::accepted 冒充“前置检查通过”。ContextResult 定义见 §3。
两个 cpp 桥接调用是初始实现的真实成本；不声称自动入口仍和单指针 Handle 一样。Release codegen/benchmark 后再决定
是否将过滤视图做成 header 可见的稳定轻量对象来帮助内联；不要通过暴露整个 Impl 或循环 include 解决模板问题。
目标 include 图：async_logger.hpp→公共值类型；模板实现→context/channel/codec；cpp→Impl/TLS；I1 不反向依赖 Logger。

<a id="i2-types"></a>

## 3. 结果与 Format：保留已写内容，补首次接入阶段

log_result.hpp 的 LogResult 私有构造和 LogResultAccess 工厂保留。
移除无 public Handle 后无意义的 invalid_handle 正常入口类别；不要尝试把悬空 Logger 访问恢复成此状态。
新增建议枚举：LogStatus::resource_exhausted、registration_closed、identity_exhausted；FailureStage::context；
FailureReason 对应增加 context_allocation_failed、tls_capacity_exhausted、registration_closed、producer_token_exhausted。
这些是 public 结果语义的具体表示建议，不是 Record wire 字段，不需要与旧 BindError 编号一致。

| 内部 ContextResult 内容 | 不变量 |
|---|---|
| ProducerContext* context | 成功才非空，指向已经发布/缓存的节点；不拥有 |
| 可选 ContextError error | 失败才有值；与成功指针互斥；受控工厂构造，不分配 |

不要把 ContextResult 命名为 BindResult 再留在 public；noexcept 接入 helper 捕获 bad_alloc 并映射。
TLS vector 到达 max_size 前先检查并返回容量耗尽，避免把 length_error 藏进 noexcept；不 catch(...) 吞未知逻辑错误。
Logger 身份分配耗尽发生在 Logger 构造，抛明确创建失败；线程 token 耗尽才在 try_log context 阶段报告。
结果 index 无参数时为 0xFF，byte_count 没有字节上下文时用 0。context 失败不伪装 full，不增加 attempted。

log_format.hpp 建议先完成 FormatView：private byte pointer/size/stored_hash，runtime 工厂固定 hash=0；
literal 工厂独占非零 hash 权限。runtime_format(pointer,size) 或 string_view 只保存 view，不 strlen/复制/解析；
过滤通过后的适配器构造 detail::FormatInput。空指针配非零长度由 measure 拒绝，不能在过滤前读正文。
runtime 源活到同步 try_log 返回，encode 完成后 Ring 深拷贝。literal 再实现 char/char8_t structural NTTP 静态存储、N-1 长度、
hash_literal_stored；编译期不做不允许的 byte reinterpret_cast，运行期取静态字符地址再适配。不公开任意 hash 注入。

<a id="i2-channel"></a>

## 4. Channel、ProducerContext 与 Logger 实例身份

| 位置/类型 | 字段建议与职责 |
|---|---|
| detail/channel.hpp 的 ChannelCold | std::uint64_t、producer token、显示 TID/name、quota、既有版本/hash 身份、Filter/Clock/Policy/Dispatch 稳定 const 引用 |
| Channel | 直接内嵌 SpscRingBuffer；构造直接用 RingConfig，不默认构造后赋值；禁复制移动；不自己注册/输出 |
| detail/producer_context.hpp 的 ProducerContext | 本线程上下文身份、context published_next、V1 内嵌 Channel；将来 V2 才替换路由表示；当前不增加频率字段/队列虚派发 |
| Impl | 不复用 Logger 身份、稳定依赖、atomic<ProducerContext*> published_head=nullptr、registration_open；不再有多注册线程修改的 owner vector |

V1 可以内嵌 Channel 以减少一次分配；Context 与 Channel 语义仍分开，V2 内部布局允许改变，没有 public ABI 承诺。
为保持唯一持有链，本指南选择发布 ProducerContext 节点，节点拥有内嵌 Channel；Backend 沿上下文链访问 Channel。
这与“发布 Channel 指针”的旧示意是内部组织差异，不能同时维护两份独立拥有链造成重复回收。

Logger 身份与 producer token 使用进程级非零不回绕计数；可用 checked CAS，UINT64_MAX 为耗尽状态而非回到 0。
Logger 身份在构造分配一次，token 在线程首次接入时分配并存 TLS；正常调用不做共享编号 RMW。
显示 OS TID 可复用，不能作永久缓存键。部署含多个 QLog 副本/DSO 时须保证所声称的身份域一致，不依赖每个 TU 一份 static 计数器。

## 5. TLS 获取、准备、发布、安装：按这个顺序写

第一版推荐 src/producer_context.cpp 中一个 thread_local ThreadRegistry：
last_logger_id、last_context、vector<Entry{std::uint64_t, ProducerContext*}>、本线程 token。
vector 只在当前线程访问、只保存非拥有地址；不在命中查询时调用 operator[] 之类隐式插入接口。
允许先用线性查找实现多 Logger 索引，连续同 Logger 走 last 快缓存；Logger 多时查找代价明确，后续再替换 map 并实测。

### 5.1 acquire_context_for_thread 的快路径

1. 仅 classify_call 返回 proceed 才进入本函数；没有过滤上下文分配。
2. 读取当前存活 Logger 的身份；先比较 last_logger_id，匹配才使用 last_context。
3. 不匹配则在 TLS entries 按唯一 Logger 身份查找；命中后更新 last 缓存，不分配。
4. 完全 miss 才执行以下冷路径。不要先解引用 last_context 去比较其中身份值，旧节点可能已回收。

### 5.2 冷路径逐函数分工

| 建议 helper | 算法/失败规则 |
|---|---|
| prepare_tls_slot(registry) | 检查容量上界，必要时 reserve；追加一个不会被快查找当成功的空槽；记录下标，后续不再增长；分配失败尚未发布任何节点 |
| acquire_thread_token(registry) | 已分配直接返回；否则 checked CAS 生成并存 TLS；耗尽回滚空槽，返回 identity_exhausted；号码可有间隙但不复用 |
| make_context(stable_dependencies, token) | 局部 unique_ptr 创建完整 Context/内嵌 Ring、复制冷名字；可能 bad_alloc；失败 pop 空槽、旧上下文不变 |
| publish_context(node) | 仅操作完整节点的 next 与 atomic head；CAS release 发布成功后 Logger 接管；不做分配/字符串写入 |
| install_tls_slot(index, id, raw) | 将已发布节点装进之前保留槽，并更新 last；只有整数/指针赋值，不抛异常；不得此时再 emplace 或触发 rehash |

此方案依赖同线程接入不重入；不在自己的分配/错误处理里再记日志。先占空槽但不将它标为命中，防止读到未发布上下文。
失败回滚仅移除当前调用准备的最后空槽，不改旧 entry。不需要全局锁，也不在每次日志遍历发布链找自己。

### 5.3 CAS 发布的代码级参考

以下仅为已构造节点的发布片段，必须满足前面的 TLS 准备与后面的无异常安装：

```cpp
auto* raw = node.get();
auto* expected = published_head.load(std::memory_order_relaxed);
do {
    raw->published_next = expected;
} while (!published_head.compare_exchange_weak(
    expected, raw, std::memory_order_release, std::memory_order_relaxed));
node.release();
// 此后 install_tls_slot 只做已准备存储中的无异常赋值。
```

读者 acquire load head 后沿发布后不变的 next 读取；旧节点不删除，新注册只改变头部。
CAS 失败只更新自己未发布节点的 next，不解引用旧 head，因此这里失败 relaxed 足够。
检查 atomic<ProducerContext*> 在目标平台 lock-free；checked 编号 CAS 也检查相应原子类型。
CAS 成功是转交节点的发布点；发布后不能再因为“缓存插入失败”删除 raw。
关停前不回收，避免运行期 ABA；不宣称每个线程必在有限次数内成功，CAS 发布不是 wait-free。

### 5.4 TLS 与 Logger 谁先退出

线程先退出：TLS 析构只释放 entries 存储，不 delete context、不解引用 Logger；已发布节点等 Logger 回收。
Logger 先退出而管理线程仍存活：调用方保证无并发日志；Logger 回收节点，TLS 可能留旧 id/地址，查找新 Logger 时只按新身份比较，
不能解引用旧地址、不能以 this 重用认定同一实例；TLS 析构也不访问旧节点。
最小基线不清扫过期 entries，长期线程频繁重建 Logger 会增长 TLS 索引；将其列为资源测试，不谎称已经自动回收。

<a id="i2-producer"></a>

## 6. AsyncLogger::try_log 唯一写入链

按以下局部变量/分支次序组织模板，不直接把旧 ProducerHandle 函数体改名字：

| 顺序 | 操作 | 失败/寿命 |
|---|---|---|
| 1 | classify_call(category, level) | 无效输入返回 validation 错误；filtered 立即返回；无 TLS/measure/clock |
| 2 | acquire_context_for_thread() | 失败映射 context；无 reserve，计入准入前拒绝；成功节点由 Logger 持有 |
| 3 | 适配 FormatInput，measure_record(input, quota, forward<Args>(args)...) | 保存 measured 到函数结束，prepared 借它；失败保留 index/byte_count |
| 4 | ring.try_reserve(prepared.payload_size()) 一次 | 此前才计 attempted；full 返回 full；pending/配额不变量错误为 internal_error，不 abort 别人的 reservation |
| 5 | sample_admission_timestamp(clock) | 只在成功 reserve 后采样；两级失败仍用 unavailable Record |
| 6 | encode_v1(write.data(), write.size(), prepared, metadata, policy, dispatch) | WriteHandle 非 const；metadata 为 time/category/level/flags，目标大小是 payload 不是 capacity |
| 7 | encode 失败 abort(write)，成功 commit(write) | 每个成功 reservation 只终结一次；commit 返回后才 accepted |

默认 format='a={} b={}'、两个 int32：payload=32+9+5+5=51B，正常外层 Frame=8+align_up_8(51)=64B。
先用 runtime 此例走 Producer→Ring→真实 decoder，再补 literal、多字符串、全部错误分支。
measure 失败映射：invalid_limits→internal_error；invalid_format_metadata/invalid_string_metadata→invalid_input；其余长度/溢出/配额→too_large。
reserve 非 full 错误、全部 EncodeError 都保留具体 reason 和 stage；映射 helper 不操作 Ring，终结集中在主函数。
reserve 后 abort 保证不发布，不承诺 Ring storage 全字节未写；Header-last 不是跨线程发布，commit 才建立可见性。

<a id="i2-diagnostics"></a>

## 7. 过滤前无上下文，诊断必须调整

旧“所有 calls 都记在 Channel”不能覆盖 filtered/首次分配失败，不能为统计先创建上下文。
建议 Debug 在 Impl 放 Logger 范围 ProducerDiagnostics（atomic<uint64_t>），classify_call 前记 calls，
filtered/validation/context/measure 失败各自终结；measure 成功后记 attempted，随后 accepted/full/failed_after_attempt 三选一。
这些共享 RMW 只存在诊断构建；常规 Release 必须移除字段和更新，不能把 Debug 性能当稳态基线。
内部可增加 Channel 级 accepted 等辅助诊断，但不要与 Logger 级 calls 混合验证公式。

calls = filtered + rejected_pre_admission + attempted
attempted = accepted + dropped_full + failed_after_attempt

计数未溢出、调用结束并 join 后检查；context 错误归 rejected_pre_admission。
普通 Release 由测试驱动读取结果、消费者核对 Record ID；不新增 wire 字段或假零统计 getter。
AUTO/ON/OFF 宏通过 qlog target PUBLIC 统一传播，不能在各 TU 自行按 NDEBUG 猜不同布局。
关闭依赖无调用者、队列排空，不依赖被移除的计数。

<a id="i2-build"></a>
<a id="i2-validation"></a>

## 8. 构建接线、验收与有界交接

| 批次 | 生产工作 | Codex 后续验收 |
|---|---|---|
| A | §1 Impl 修正、公共 API/Result/Format 调整 | 自包含头、结果组合、已有 Filter/clock 边界；不重复 I1 已验收开发 |
| B | §4 Context/Channel/唯一身份、§5 TLS 与 CAS | 默认首次接入、同线程复用、双 Logger、并发首用、分配点失败回滚 |
| C | §6 runtime→literal 全链 | 一次 reserve、拒绝不采样、deep copy、full/abort/FIFO、33 参数 compile-fail |
| D | §7 诊断、源码接线与真实消费方 | GCC/Clang Debug/Release、ASan/UBSan、软件 hash 回退、诊断 ON/OFF、Release codegen/性能 |

第一步交接是 A，不要求一次做完 TLS/Backend。生产文件完成后加入现有 qlog target：
filter_state.cpp、admission_clock.cpp、channel.cpp、async_logger.cpp，新增 producer_context.cpp（TLS 与接入辅助）。
模板定义在头中；安装/使用者从 QLog::qlog 获得一致宏。需要平台线程库时沿既有 target 依赖接入，链接线程库不等于引入 mutex。
旧构建未纳入 I2 cpp，跑通旧 target 不能证明 I2 可链接；必须实例化 Logger::try_log 并链接真实源。

新增关键测试：过滤拒绝时 TLS entry/Ring 分配均零；首条通过过滤可分配；同一已建立线程/Logger 稳态分配为零；
TLS reserve、Context、Ring、名字分配逐点失败不发布；发布后安装无异常；同地址重建 Logger 使用新身份；
线程先退出和 Logger 先退出均无 TLS 析构访问已释放对象；多个 Logger 切换、ID 耗尽不回绕；无并发 close 承诺的前置条件明确。
consumer 同时扫描时节点初始化完整；停止全部写入/接入/读者后统一回收一次；不在运行中回收。
WSL2 功能/ASan/UBSan 证据不替代原生 Linux TSan 发布门禁；未执行项明确待办。

性能基线区分 cold_first、same_logger_steady、alternating_loggers、filtered、full、context_failure；
记录 calls/accepted/dropped、P50/P99、RSS/上下文数、扫描 CPU 与外部计数成本。
Release 检查不出现稳态分配/CPUID/clock_getres/token 分配/诊断 RMW；TLS 快缓存和 cpp 桥接成本如实记录。
BQLog 对照统一异步模式、丢弃策略、输出字节、参数与统计；动态频率路径和固定 SPSC 的成本不能混同，首次调用不可从总结果隐藏。

<a id="i2-appender-runtime"></a>

## 9. I3/I4 与未决设计

Appender 仍是抽象虚基类，Logger 独占运行对象，Console/TextFile 配置与对象分开；空目标输入默认 Console。
I3 需要真实 Console 的基础格式化/自有缓冲：所有目标使用完 Frame 后统一 release，再做慢 I/O。
I4 扩展 TextFile 资源和已讨论的输出能力；文件 reset 打开/flush/close 失败策略另议。
旧管理锁方案不再沿用；配置命令队列的生产者数量、提交/应用结果、背压与完成通知尚待确认。
这不阻塞 I2 初始配置、稳定原子过滤和自动 ProducerContext。不要把未确认的单管理线程/SPSC 命令协议写成已实现或已接受。

## 10. 文档核验范围

本轮核对实际源码、I1-D 报告和现行 ADR；归档旧显式绑定指南并重写本主指南、计划和续写入口。
检查相对链接/章节/代码围栏、有效入口不再指向旧绑定步骤，以及生产源码/测试/benchmark/CMake 指纹不变。
代码片段用于实施参考，本轮不声明 I2 生产编译、自动 TLS 并发/内存安全、性能或完整关停已经验证。


> 进度说明：§11 基于上一轮快照；其中“当前”问题是否仍存在以本轮 §0/§12 为准。旧验证报告保留原始事实，不代表当前 log_level.hpp 仍有残留声明。

<a id="i2-detailed-steps"></a>

## 11. 按当前代码逐项落笔：先完整类型，再成员声明，再函数定义

本节解决“只知道函数名却不知道怎么落笔”的缺口。标为完整文件的代码可作为该文件的当前参考；
标为成员片段的代码必须放到指定类/namespace 内。步骤中的未完成函数不能用空 return 或假成功占位。
本节细化已接受 ADR-013，不新增队列/配置管理协议；生产源码仍由维护者修改。

### 11.1 第一处：先清理等级头，再恢复 Config 的值语义

当前 include/qlog/log_level.hpp 末尾还有一行未完成的 `[[nodiscard]] constexpr RecordValidationPolicy`，
先删除这一残留行；保留六等级与 valid_level。不要在 public 等级头再定义一份 policy：
make_producer_policy 已在 detail/producer_policy.hpp，类型在 detail/record_types.hpp。
本轮首次语法检查正是在此失败，这不是 CallGate 本身的错误。生产文件需由维护者修正。


文件 include/qlog/async_logger.hpp。删除 LoggerConfig 末尾这四个 deleted 声明：

```cpp
LoggerConfig(const LoggerConfig&) = delete;
LoggerConfig& operator=(const LoggerConfig&) = delete;
LoggerConfig(LoggerConfig&&) = delete;
LoggerConfig& operator=(LoggerConfig&) = delete;
```

不需要再补特殊成员，保留纯字段 struct，让编译器生成默认构造、复制和移动。
当前 normalize_logger_config 中的 LoggerConfig result=input 需要复制，Impl 的按值参数和 std::move 需要可用的转移/复制；
用户声明构造函数还会抑制隐式默认构造。不要为了避开错误把 normalize 改为借用调用者输入。
AsyncLogger 自己的 deleted 复制/移动声明保留。Config 的 vector/string 可分配；它不包含真实 Ring。

### 11.2 完整 CallGate 类型与正确成员声明位置

新增 include/qlog/detail/call_gate.hpp，完整参考：

<!-- check:call_gate -->
```cpp
#pragma once
#include <cstdint>

namespace qlog::detail {
enum class CallGate : std::uint8_t {
    proceed,
    filtered,
    invalid_level,
    invalid_category,
};
}  // namespace qlog::detail
```

| 枚举 | 意味着什么 | 下一步 |
|---|---|---|
| proceed | 本次 level/category 合法且一次过滤通过 | 才能取得 TLS context，不代表已入队 |
| filtered | 输入合法，但当前过滤关闭 | 返回 LogStatus::filtered，不 TLS/measure/reserve/clock |
| invalid_level | level 不在 0～5 | validation 失败，不做位移，不访问类别开关 |
| invalid_category | level 合法，category 超界 | validation 失败，不索引数组 |

不在此加入 full/resource_exhausted/registration_closed；这些发生在后面的不同阶段。
若 level 与 category 同时非法，本参考优先返回 invalid_level，使测试有确定优先级。

async_logger.hpp 顶部新增直接 include：call_gate.hpp、context_result.hpp、argument_traits.hpp，
保留 record_limits/log_level/log_result/log_format，移除 producer_handle.hpp。ContextResult 先按下一节完成。
将 try_log 模板声明移入 AsyncLogger 的 public；当前 namespace 级带 const 的声明整段删除。
在 private、class Impl 前添加以下两个成员声明：

```cpp
[[nodiscard]] detail::CallGate classify_call(
    std::uint32_t category_id, LogLevel level) const noexcept;

[[nodiscard]] detail::ContextResult acquire_context_for_thread() const noexcept;
```

函数返回类型属于 detail，不等于函数本身属于 detail。这里两个函数的主人都是 AsyncLogger。
只有非静态成员函数才能使用末尾 const；自由函数末尾 const 没有对象可约束。

### 11.3 classify_call 完整定义：使用已有 FilterState，不创建局部表

文件 src/async_logger.cpp。删除末尾 namespace detail 内当前两个错误自由函数。
在完整 AsyncLogger::Impl 定义之后、namespace qlog 内放置：

<!-- check:classify_call -->
```cpp
detail::CallGate AsyncLogger::classify_call(
    std::uint32_t category_id, LogLevel level) const noexcept {
    if (!valid_level(level)) {
        return detail::CallGate::invalid_level;
    }

    const auto& filter = impl_->filter;
    if (category_id >= filter.category_count()) {
        return detail::CallGate::invalid_category;
    }

    if (!filter.allows_unchecked(
            category_id, static_cast<std::uint8_t>(level))) {
        return detail::CallGate::filtered;
    }

    return detail::CallGate::proceed;
}
```

当前 FilterState state{} 不可成立：没有无参构造，即使创建新表也读不到 Logger 的真实动态配置。
正确依赖是 impl_->filter；它已由 Logger 构造建立且稳定存在。本函数没有任何新存储所有权。
先 valid_level 再 category 检查，最后才调用 unchecked；assert 不是 Release 的输入检查替代品。
allows_unchecked 只调用一次，不在 TLS 获取后重新过滤，避免配置更新时一条调用被两次不同快照重新分类。
此处不检查 format 内容、不取时、不读 TLS、不分配；只有配置标量 load。
没有 invalid_logger 分支，Logger 对象有效与不并发析构是调用前置条件。

第一处检查点：仅 CallGate/成员声明/本函数即可做静态编译；不需要 Context 已经运行。
运行验收覆盖 level=6/255、category=count、全关、通过四结果，检查非法输入不进入 unchecked。

### 11.4 FormatView 完整 runtime 参考：三个字段足够

文件 include/qlog/log_format.hpp。当前 byte_ 与 uint64_t* pointer_ 重复且后者类型不对；删除 pointer_，
只保留 const byte 地址、长度、按值 hash。类定义末尾必须有分号。
下面是当前 runtime 切片完整文件，literal 扩展按本节最后步骤，不开放自填 hash：

<!-- check:format_view -->
```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace qlog {
class FormatView final {
public:
    [[nodiscard]] const std::byte* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::uint64_t stored_hash() const noexcept { return stored_hash_; }

private:
    friend FormatView runtime_format(const char*, std::size_t) noexcept;
    friend FormatView runtime_format(const char8_t*, std::size_t) noexcept;

    FormatView(const std::byte* data, std::size_t size) noexcept
        : data_(data), size_(size), stored_hash_(0U) {}

    const std::byte* data_;
    std::size_t size_;
    std::uint64_t stored_hash_;
};

[[nodiscard]] inline FormatView runtime_format(
    const char* data, std::size_t size) noexcept {
    return FormatView(reinterpret_cast<const std::byte*>(data), size);
}

[[nodiscard]] inline FormatView runtime_format(
    const char8_t* data, std::size_t size) noexcept {
    return FormatView(reinterpret_cast<const std::byte*>(data), size);
}

[[nodiscard]] inline FormatView runtime_format(std::string_view text) noexcept {
    return runtime_format(text.data(), text.size());
}

[[nodiscard]] inline FormatView runtime_format(std::u8string_view text) noexcept {
    return runtime_format(text.data(), text.size());
}
}  // namespace qlog
```

data/size/stored_hash 只读，读取不扫描。空指针测试用明确类型指针，避免 char/char8_t 两重载的 nullptr 歧义。
运行期 format 的生命周期到 try_log 返回；三个字段均初始化。不要为了 constexpr 在 C++20 常量求值中强行 reinterpret_cast。
try_log 过滤通过后用 detail::FormatInput{format.data(), format.size(), format.stored_hash()} 适配。
literal 后续补：BasicFixedFormat 复制 char/char8_t 静态字节→NTTP 静态存储→hash_literal_stored→受控 literal 工厂设置非零 hash。
增加三参数私有构造时只授权可信 literal 工厂，不能增加 public hash setter；runtime 始终固定 0。
当前块仅 runtime，不宣称已经完成 literal 实现。

### 11.5 ContextError / ContextResult 完整定义及结果映射

从 log_result.hpp 删除当前 public struct ContextResult。它引用了未声明的 ProducerContext/ContextError，
而且没有保证成功/失败互斥；这两个是内部接入类型，放独立 detail 头。
LogStatus 的 identify_exhausted 更名 identity_exhausted；删除旧 invalid_handle。其余已实现 LogResult 工厂保留。
新增 include/qlog/detail/context_result.hpp，完整参考：

<!-- check:context_result -->
```cpp
#pragma once
#include <cstdint>
#include <optional>

namespace qlog::detail {
class ProducerContext;

enum class ContextError : std::uint8_t {
    allocation_failed,
    tls_capacity_exhausted,
    registration_closed,
    producer_token_exhausted,
};

class ContextResult final {
public:
    [[nodiscard]] static ContextResult success(ProducerContext& context) noexcept {
        return ContextResult(&context, std::nullopt);
    }
    [[nodiscard]] static ContextResult failure(ContextError error) noexcept {
        return ContextResult(nullptr, error);
    }
    [[nodiscard]] ProducerContext* context() const noexcept { return context_; }
    [[nodiscard]] const ContextError* error() const noexcept {
        return error_ ? &*error_ : nullptr;
    }

private:
    ContextResult(ProducerContext* context, std::optional<ContextError> error) noexcept
        : context_(context), error_(error) {}
    ProducerContext* context_;
    std::optional<ContextError> error_;
};
}  // namespace qlog::detail
```

success 取引用而非可空指针，前置条件是节点有效且已发布；结果不拥有节点，析构不 delete。
无默认构造，避免“两个都没有”；失败时 context=nullptr。只有查询到 error 非空才能解引用。
当前 helper 边界可用此内部静态工厂，不把结果暴露为业务绑定 API。

| ContextError | LogStatus | FailureReason | stage/index/bytes |
|---|---|---|---|
| allocation_failed | resource_exhausted | context_allocation_failed | context / 0xFF / 0 |
| tls_capacity_exhausted | resource_exhausted | tls_capacity_exhausted | 同上 |
| registration_closed | registration_closed | registration_closed | 同上 |
| producer_token_exhausted | identity_exhausted | producer_token_exhausted | 同上 |

map_context_error(ContextError) 返回 LogResult：穷尽 switch，用现有 LogResultAccess::make_failed 构造上述组合。
它不回滚、不释放节点、不统计，回滚在接入函数，计数在主调用路径；非法内部 enum 值应按内部不变量处理，不伪装 full。

### 11.6 先补哪些字段：身份 → 冷依赖 → Channel → Context → RegistryView → Impl

这部分是相互配套的字段/构造声明方案。按顺序建立完整类型；不要把所有内容都塞回 log_result.hpp。

**第一步，detail/producer_identity.hpp：** 直接用 std::uint64_t 声明身份值，不定义别名，0 表示无效。头文件只声明分配函数，不定义普通全局变量。
声明 std::uint64_t allocate_logger_id()（构造失败可抛）与 optional<std::uint64_t> allocate_producer_token() noexcept。
计数器只在一个 cpp 中定义，两种身份各一份，不要各个 TU 一份；两个域都不回绕，内存序 relaxed 足够用于唯一编号分配。

**第二步，detail/channel.hpp：** include 身份、Ring、filter/clock/record_types/hash、string/vector。
在 qlog::detail 内依次定义下列三个类型。字段名在后续步骤固定使用：

| 类型 | 成员（按声明顺序） | 谁拥有它 |
|---|---|---|
| ChannelDependencies | const FilterState& filter；const ClockDescriptor& clock；const RecordValidationPolicy& policy；const FormatHashDispatch& hash_dispatch；const string& logger_name；const vector<string>& category_names | Impl 持有依赖对象，结构仅借用；运行期间不可替换其引用目标 |
| ChannelCold | std::uint64_t logger_id；std::uint64_t producer_token；uint64_t os_thread_id{0}；string thread_name；size_t payload_quota；ChannelDependencies dependencies | Context/Channel 按值持有冷元数据，名字自己拥有；显示 TID=0/空名表示暂不可用，不影响 token |
| Channel | ChannelCold cold_；SpscRingBuffer ring_ | ProducerContext 内嵌拥有 |

Channel 声明构造 Channel(ChannelCold cold_data, SpscRingBufferConfig ring_config)，禁复制/移动，默认析构。
本批按 §12.3 在头内定义，构造初始化列表先 cold_(std::move(cold_data))，再 ring_(ring_config)；不加 noexcept（Ring 分配可失败），
不创建第二份 Logger、不调用 TLS、不发布。make_context 必须令 cold.payload_quota 与 ring_config.max_payload_bytes 相等。

**第三步，detail/producer_context.hpp：** include Channel、context_result、atomic；定义 class ProducerContext final，
内部实现字段可 public，按顺序为 ProducerContext* published_next{nullptr}、Channel channel_。
构造 ProducerContext(ChannelCold cold, SpscRingBufferConfig config) 直接初始化 channel_(std::move(cold), config)，
禁复制/移动。没有 LoggerContext 第二类型，不把 TLS 的 last_context 写成按值的 ProducerContext。

**第四步，同一 context 头补同步调用用的 RegistryView：**

```cpp
struct ContextRegistryView {
    std::uint64_t logger_id;
    SpscRingBufferConfig ring_config;
    ChannelDependencies dependencies;
    std::atomic<ProducerContext*>& head;
    const std::atomic<bool>& registration_open;
};

[[nodiscard]] ContextResult acquire_thread_context(
    const ContextRegistryView& registry) noexcept;
```

这个 view 自身可为局部对象，acquire_thread_context 不保留其地址；新节点按值复制 dependencies 中稳定引用。
head/open 引用属于 Impl，TLS 不保存它们；这样 src/producer_context.cpp 不需要知道 private Impl 的布局。

**第五步，Impl 字段：** 在已持有的 config 后增加 const std::uint64_t logger_id，在 filter/clock/policy/hash_dispatch 后增加
ChannelDependencies dependencies，然后 atomic<ProducerContext*> published_head{nullptr}、既有 atomic<bool> registration_open{true}。
初始化依次是 config → allocate_logger_id → filter → probe clock → policy → automatic dispatch →
dependencies{filter, clock, policy, hash_dispatch, config.name, config.category_names}。
Logger 身份分配若失败，构造失败自动清理已有 config；节点尚未发布。依赖必须声明在节点拥有状态之前。

对照：const 成员的初始化只能在构造列表完成；引用成员必须从有效 owner 初始化，不能先默认构造后赋值。
以上字段没有 Appender 运行对象、配置命令队列或 V2 频率字段，不改变本次 I2 范围。

### 11.7 acquire_context_for_thread：成员桥接代码与真正实现位置

在 src/async_logger.cpp 完整 Impl 之后，namespace qlog 内加入如下成员定义（先完成 §11.6）：

```cpp
detail::ContextResult AsyncLogger::acquire_context_for_thread() const noexcept {
    const detail::ContextRegistryView registry{
        impl_->logger_id,
        impl_->config.ring,
        impl_->dependencies,
        impl_->published_head,
        impl_->registration_open};
    return detail::acquire_thread_context(registry);
}
```

这段只连接字段，不执行一份重复 TLS 算法。真正 TLS/分配/CAS 函数是 src/producer_context.cpp 中的
qlog::detail::acquire_thread_context(const ContextRegistryView&) noexcept；它是自由函数，没有末尾 const。
名称刻意区分成员 for_thread 和内部 thread_context，避免误写递归调用自己。
此桥接没分配，但被调用冷路径可分配并把 bad_alloc 转成 ContextResult；已有 context 命中不分配。

### 11.8 ThreadRegistry 与每个冷 helper：先类型，再 thread_local 对象

src/producer_context.cpp 先 include 自己的完整内部头，直接 include atomic/vector/optional/memory/limits/new/utility。
在 namespace qlog::detail 内的匿名 namespace 写类型，然后写对象：

```cpp
struct Entry {
    std::uint64_t logger_id{0};
    ProducerContext* context{nullptr};
};

struct ThreadRegistry {
    std::uint64_t last_logger_id{0};
    ProducerContext* last_context{nullptr};
    std::uint64_t producer_token{0};
    std::vector<Entry> entries;
};

thread_local ThreadRegistry thread_registry;
```

早期 thread_local 类型定义问题已修；当前应保留先定义 struct 再定义 thread_local 对象的顺序。
TLS 初始只含空 vector，不创建 Ring；last_context 是指针，不能是 LoggerContext 值。
TLS 默认析构只销毁 vector，不访问 pointer 指向对象，不注销/删除发布节点。

以下 helper 依次写在同一匿名 namespace；参数名/返回形状是本轮明确的实施建议：

| helper | 必需步骤、失败和返回 |
|---|---|
| optional<uint64_t> take_id(atomic<uint64_t>& next) noexcept | next 初始 1；读 expected，等于 UINT64_MAX 返回 nullopt；否则 CAS 从 expected 到 expected+1，成功返回旧值；失败用更新的 expected 重试。永远不做溢出加法；0 不发放 |
| ProducerContext* find_context(ThreadRegistry&, std::uint64_t) noexcept | 先比较 last id，匹配才返回 last 指针；否则按 const Entry& 遍历 entries，比 id 后才读取 context；命中更新 last；miss 返回 nullptr。不要复制旧 entry 再解引用已释放节点取 id |
| optional<size_t> prepare_tls_slot(ThreadRegistry&) | size==max_size 返回 nullopt；否则 emplace 一个默认 Entry，可能 bad_alloc；成功返回最后下标；std::vector 提供此平凡 Entry 下的失败不变保证；这一步就是容量准备，发布后绝不再 push_back |
| bool ensure_thread_token(ThreadRegistry&) noexcept | token 非零立即 true；否则调用 take_id 的 token 域；耗尽 false；成功缓存，号码不会因本次 Ring 分配失败而复用 |
| unique_ptr<ProducerContext> make_context(view, token) | 构造 ChannelCold：view.logger_id/token、显示 TID/名暂缺时 0/空串、quota=view.ring_config.max_payload_bytes、dependencies=view.dependencies；make_unique Context。分配失败自然抛 bad_alloc，不在 helper 内打印 |
| ProducerContext* publish_context(unique_ptr<ProducerContext>& node, atomic<ProducerContext*>& head) noexcept | 完整节点按 §5.3 CAS 插入；成功 release unique_ptr 并返回 raw；没有后续分配，没有读旧节点内容 |
| void install_tls_slot(ThreadRegistry&, size_t index, std::uint64_t, ProducerContext*) noexcept | 断言下标有效/节点非空；给已存在槽的 id/context 赋值，更新 last；不 emplace，不分配 |
| void rollback_tls_slot(ThreadRegistry&, size_t index) noexcept | 断言这是本次追加的最后空槽，pop_back；只在发布前失败时调用；不动旧 entries/last，不 delete 已发布对象 |

allocate_logger_id/allocate_producer_token 的对外内部定义放匿名 namespace 外，分别调用两个静态原子域的 take_id；
Logger 身份耗尽抛 overflow_error（加 stdexcept），token 耗尽返回 nullopt。原子域不定义在头文件函数局部导致多个独立域。
选用的 atomic<uint64_t>/atomic<ProducerContext*> 要验证平台 lock-free；只代表编号/发布算法，不包括 new。

acquire_thread_context 主函数按下列顺序写，不能只放 return：

1. 取 TLS registry，find_context；命中返回 ContextResult::success(*context)，零分配。
2. miss 时读 registration_open，false 返回 registration_closed；既有关闭合同要求已无并发接入，不把本次 load 当竞态关闭证明。
3. try 内执行 prepare_tls_slot；容量已达上限返回 tls_capacity_exhausted，尚无槽就不 pop。
4. 槽成功后记录 index；ensure_thread_token 失败则 rollback 槽并返回 producer_token_exhausted。
5. make_context；若 bad_alloc，rollback 已存在空槽并返回 allocation_failed。prepare_tls_slot 自身抛出时没有新增槽，不能错误 pop 旧 entry。
6. publish_context；从这个位置开始只调用 noexcept 的 install_tls_slot 与 success 包装；不再有资源准备。
7. 返回 ContextResult::success(*raw)。成功无需额外把节点塞进 owner vector，链已是 Logger 的唯一拥有集合。

可用 optional<size_t> slot_index 区分异常时是否真的创建了空槽：默认空，prepare 成功后赋值；catch bad_alloc 只在它有值时 rollback。
容量用 vector.max_size 检查，不以不可恢复超长 reserve 请求触发 length_error；不 catch 所有异常伪装资源不足。
上述流程禁止本线程接入重入，不通过日志报告分配失败；发布后的任何 return 都不能再释放 raw。

### 11.9 析构不能继续只默认：加入节点统一回收

当 published_head 真的拥有 Context 后，在 Impl 内增加析构定义；AsyncLogger::~AsyncLogger 仍可在 cpp 中默认，
由 unique_ptr 触发完整 Impl 析构。前置条件：所有日志/注册/管理活动结束、测试 reader 或 I3 Backend 已退出。
不能边遍历删除边让 Backend 继续使用。

析构算法：head.load(relaxed) 取得首节点；每轮先保存 next=node->published_next，再 delete node，最后 node=next。
不可 delete 后读取 next；Context 的内嵌 Channel/Ring 自动释放；最后 head 可置空（对象随后也销毁）。
不遍历每个线程的 TLS、不删除 TLS entry 指向的节点第二次、不为释放引入 shared_ptr。
依赖成员在 Impl 析构体执行期间仍存活；析构体清完节点后，成员才按逆序销毁。
此操作不是完整 shutdown：排空/I/O/Backend join 仍须 I3 实现，I2 测试先满足无读写者前提。

### 11.10 try_log：四个 Gate 分支与后续函数的实际位置

新文件 detail/async_logger_impl.hpp 由 async_logger.hpp 在类定义结束、namespace 关闭后 include。
定义写在 namespace qlog 内，函数名必须是 AsyncLogger::try_log，模板约束与类声明逐字保持一致。
实现头直接 include utility、完整 producer_context、record_measure/record_encoder 与内部错误映射头（若拆分）。

| 主函数局部顺序 | 如何写出结果/下一步 |
|---|---|
| gate=classify_call(...) | switch 四值：invalid_level/category 调 make_failed 对应 validation、index=0xFF、byte_count=0；filtered 调 make_filtered；proceed 才跳出 switch 继续 |
| acquired=acquire_context_for_thread() | context() 为空则按 §11.5 穷尽映射 *error() 返回；非空取得 ctx 引用，Ring 位置为 ctx.channel_.ring_ |
| measured=measure_record(input, ctx.channel_.cold_.payload_quota, forward<Args>(args)...) | measured.succeeded() 假时映射 *failure()；真时 const auto& prepared=*measured.prepared()，不从临时结果借用 |
| auto write=ring.try_reserve(prepared.payload_size()) | 用 bool/status 分类；full 与内部 reserve 错误分开；失败无当前 reservation，因此不 abort、不读 clock |
| timestamp/metadata | dependencies.clock 取时；metadata 的四项依次 time_value/category_id/uint8 level/flags；取时失败本身不早退 |
| encoded=encode_v1(...) | 传 write.data()/write.size()/prepared/metadata/dependencies.policy/dependencies.hash_dispatch；当前 EncodeResult failure() 返回 EncodeError 指针 |
| encoded 失败 | ring.abort(write) 一次，然后 map_encode_error(*encoded.failure(), write.size())；映射 helper 不自己 abort |
| encoded 成功 | ring.commit(write)，最后返回 make_accepted；中间不复制完整 Record 或重新扫描 cstr |

对应 helper 的定义要先于头内模板调用可见；可放 detail/producer_result_map.hpp，全部 inline，命名/参数明确如下：

- map_context_error(ContextError) → LogResult：使用 §11.5 表。
- map_measure_failure(const MeasureFailure&) → LogResult：invalid_limits→internal_error；两个 metadata 原因→invalid_input；其余六项长度/溢出/额度原因→too_large（MeasureError 共九项）；index/byte_count 原样保留。
- map_reserve_error(ReserveStatus, size_t requested) → LogResult：仅处理 full/payload_too_large/reservation_pending；后两者 internal_error，stage=reserve，index=0xFF；ok 不进入错误映射。
- map_encode_error(EncodeError, size_t target_size) → LogResult：七种 encoder 原因逐项映射 internal_error/encode，保留具体 reason，不 static_cast 假定枚举序号对应。

LogResultAccess::make_failed 必须传完整 LogFailure{stage,reason,index,bytes}；不写 return;，不默认构造假成功。
先完成 runtime 两整数闭环；literal 是下一步，不把零实现的 literal_format 当成已支持。诊断接入按 §7，classify_call 本身不要重复加 calls。

### 11.11 每一批的结束条件与未完成符号怎么处理

| 批次 | 按顺序完成 | 结束条件 |
|---|---|---|
| 1 | 修 LoggerConfig；完成 CallGate/ContextResult/FormatView；移 try_log 到 public、声明两个 private 成员；classify_call 定义 | 类型能自包含解析，classify_call 编译；acquire/try_log 尚未定义时不写假函数体，也不调用它们做链接成功声明 |
| 2 | 身份头/计数器、ChannelDependencies/Cold/Channel、ProducerContext/RegistryView、Impl 新字段、桥接成员 | 全部依赖类型完整，成员构造顺序成立；Context/Ring 可独立冷构造 |
| 3 | ThreadRegistry、各 helper、acquire_thread_context、Impl 节点析构 | 首次/重复/双 Logger、失败槽回滚、CAS 发布和两种退出顺序可运行验证 |
| 4 | 结果映射、完整 try_log/runtime、literal | 真实 Producer→Ring→decoder；不是仅 public 头能解析 |
| 5 | 诊断宏、CMake、完整验收 | Debug/Release/ASan/UBSan 等真实证据，首用与稳态分配、Release 代码生成与基线 |

本轮会对独立类型/格式/Gate 参考片段做语法核验；它不能证明整套 TLS、生产模板或并发协议已验收。
生产文件修改完成后，按完成的 cpp 逐项接根 qlog target；只有声明没有定义的函数应作为本批明确待办，不用成功空壳掩盖。


本轮片段核验结果见 [详细指南文档验证](./I2_DETAILED_GUIDE_VALIDATION_20260915_CHS.md)：临时副本移除等级头残留声明后，GCC/Clang 片段语法检查通过；生产文件未修复，生产实现与运行验收仍未执行。


<a id="i2-current-repair"></a>

## 12. 当前源码复核与下一批完整落笔说明（2026-09-15 再次更新）

本节以本次重新读取的生产文件为准，覆盖 §11 中描述“当前尚未写”的旧快照；§11 的接口合同仍可参考。
这里只提供修正指南，未替维护者修改生产源码。下面完整参考文件只覆盖上下文接入这一批，不代表 try_log、Backend 或 I2 已验收。

### 12.1 已完成项与必须先修的实际位置

| 文件 / 搜索锚点 | 当前判断 | 修正动作 |
|---|---|---|
| async_logger.hpp / Config、try_log、classify_call | Config 和入口声明保留；classify_call 函数体正确 | 两个桥接移 private，直接 include context_result.hpp |
| detail/context_result.hpp / error() | 独立结果头和 const 指针均已完成 | 保留，不重复旧修正 |
| qlog/log_result.hpp | 文件已不在当前目录，公共结果类型缺失 | 按 §12.10 恢复，内部 ContextResult 保留在 detail |
| producer_identity.hpp | 名称和两函数声明正确，头内仍定义 logger_id/token_ | 删除两个变量定义，完整文件见 §12.2；不使用身份类型别名 |
| channel.hpp / ChannelCold | producer_token 仍使用已取消的旧别名 | 改为 std::uint64_t producer_token |
| channel.hpp / category_names | 仍是 const vector 值 | 加 &，依赖仅借用 Impl 的分类名 |
| channel.hpp / ContextRegistryView | 与 producer_context.hpp 中定义重复，且反向依赖 Context | 删除 channel.hpp 中整段 RegistryView 和 acquire_thread_context 声明；只保留在 producer_context.hpp |
| Channel / ProducerContext 成员 | cold_/ring_/channel_ 已存在；Channel 分号/default 析构已正确 | 保留当前下划线命名，指南同步采用，不为命名重写工作 |
| Impl / 初始化、析构 | 身份初始化已正确；dependencies 内和尾部有多余逗号；delete *node 错 | 使用完整六项初始化且尾部不加逗号；delete node |
| ThreadRegistry / find_context | last_logger_id 仍32位、last_context仍值对象；索引命中已补 last 更新 | 改 uint64_t 与指针；快命中 return registry.last_context，不取地址 |
| take_id | 单次 CAS 失败直接 nullopt | 按 §12.4 while 重试，§12.9 给执行示例 |
| prepare_tls_slot / make_context | emplace_back 和下标已修；容量判断和 make_unique 仍错 | 用 max_size；view 改 const&；正确调用 make_unique |
| install_tls_slot | 槽已经整体赋值，又重复写槽，未更新 last | 保留整体赋值，后两句改为 last_logger_id/last_context 更新 |
| acquire_thread_context | 主函数已写，但 slot 始终为空；rollback 少传 index | try 开头补 prepare_tls_slot 和容量失败分支，之后才能 *slot |
| publish_context / rollback / ensure_thread_token | 当前函数体对应先准备后发布协议 | 保留；依赖调用顺序修好后再验收 |

源码锚点比易漂移的行号更适合逐项修改。完成某一行后在本地编译验证，不把表里的“保留”误读成整库验收通过。

### 12.2 身份值和分配器：先修类型，再写 TLS

完整文件 include/qlog/detail/producer_identity.hpp：

<!-- check:identity-v2 -->
```cpp
#pragma once
#include <cstdint>
#include <optional>

namespace qlog::detail {

[[nodiscard]] std::uint64_t allocate_logger_id();
[[nodiscard]] std::optional<std::uint64_t> allocate_producer_token() noexcept;
}  // namespace qlog::detail
```

logger_id 是一个 Logger 创建后不变的号码；producer_token 是线程首次接入时缓存的号码，两者直接使用 std::uint64_t，同线程写不同 Logger 复用 token，Context/Ring 仍各自独立。
真正的两个原子计数器只定义在一个 cpp 内，起始 1，0 表示 TLS 尚未分配，UINT64_MAX 表示耗尽，不发放也不回绕。
先前 load+store 不是原子递增；本次源码已改成单次 CAS，但竞争失败后没有重试，仍需修正。两个原子计数器现在已经正确地放在 cpp 匿名 namespace。
身份字段直接用 std::uint64_t，不增加别名；用 logger_id/producer_token 字段名表达不同含义，直接比较或赋值。
这里的唯一性以单一 QLog runtime 的计数器域为前提；不能把这份 cpp 分别链接进多个插件再宣称跨副本全局唯一。

### 12.3 依赖、拥有关系与完整头文件

以下沿用当前源码的内部 public cold_/ring_/channel_，简化 Producer 模板接线；它们位于 detail，并非公共业务 API。
若自行保留私有成员，必须同时提供访问器并统一所有调用处；本指南不混用两套命名。

完整文件 include/qlog/detail/channel.hpp：

<!-- check:channel-v2 -->
```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "qlog/detail/admission_clock.hpp"
#include "qlog/detail/filter_state.hpp"
#include "qlog/detail/format_hash.hpp"
#include "qlog/detail/producer_identity.hpp"
#include "qlog/detail/record_types.hpp"
#include "qlog/detail/spsc_ring_buffer.hpp"

namespace qlog::detail {
struct ChannelDependencies final {
    const FilterState& filter;
    const ClockDescriptor& clock;
    const RecordValidationPolicy& policy;
    const FormatHashDispatch& hash_dispatch;
    const std::string& logger_name;
    const std::vector<std::string>& category_names;
};
struct ChannelCold final {
    std::uint64_t logger_id;
    std::uint64_t producer_token;
    std::uint64_t os_thread_id{0};
    std::string thread_name;
    std::size_t payload_quota;
    ChannelDependencies dependencies;
};
class Channel final {
public:
    Channel(ChannelCold cold_data, SpscRingBufferConfig ring_config)
        : cold_(std::move(cold_data)), ring_(ring_config) {}
    ~Channel() = default;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    Channel(Channel&&) = delete;
    Channel& operator=(Channel&&) = delete;

    ChannelCold cold_;
    SpscRingBuffer ring_;
};
}  // namespace qlog::detail
```

ChannelCold 保存显示元数据、身份、额度和依赖视图；Ring 真正拥有缓存。ChannelDependencies 仅借用 Impl 的稳定成员。
logger_name/category_names 不可借用构造函数临时 prepared_config，也不可借用 make_context 的局部 vector。
Impl 的 config 初始化后不能移动或扩容这些被引用对象来改变生命周期合同；配置更新按 FilterState 的既有发布接口做。
这些字段在节点发布后不得再作为普通可变共享字段修改。thread_name 的空串和 os_thread_id=0 只是当前未采集显示信息，不是身份来源。
本方案全部 Channel 函数头内定义，因此 src/channel.cpp 不需要额外定义，也无需为一个空文件强行添加构建条目。

完整文件 include/qlog/detail/producer_context.hpp（先按 §11.5 建好 context_result.hpp）：

<!-- check:context-header-v2 -->
```cpp
#pragma once
#include <atomic>
#include <utility>
#include "qlog/detail/channel.hpp"
#include "qlog/detail/context_result.hpp"
#include "qlog/detail/producer_identity.hpp"

namespace qlog::detail {
class ProducerContext final {
public:
    ProducerContext(ChannelCold cold, SpscRingBufferConfig config)
        : channel_(std::move(cold), config) {}
    ProducerContext(const ProducerContext&) = delete;
    ProducerContext& operator=(const ProducerContext&) = delete;
    ProducerContext(ProducerContext&&) = delete;
    ProducerContext& operator=(ProducerContext&&) = delete;

    ProducerContext* published_next{nullptr};
    Channel channel_;
};
struct ContextRegistryView final {
    std::uint64_t logger_id;
    SpscRingBufferConfig ring_config;
    ChannelDependencies dependencies;
    std::atomic<ProducerContext*>& head;
    const std::atomic<bool>& registration_open;
};
[[nodiscard]] ContextResult acquire_thread_context(
    const ContextRegistryView& registry) noexcept;
}  // namespace qlog::detail
```

ContextResult 的头只需 forward declare ProducerContext，不 include producer_context.hpp；否则又绕回循环依赖。
ContextRegistryView 是调用期间的借用视图，ring_config 按值复制很小，dependencies 只复制引用，head/open 引用 Impl 的原子。
发布前 unique_ptr 拥有节点；发布成功后 Impl 链拥有节点；TLS 的 context 指针永不 delete。

### 12.4 producer_context.cpp：按依赖顺序给出闭合参考

下面提供整个文件供对照，重点是把每个函数的输入、输出和失败边界连起来；不要与当前残缺 helper 重复粘贴定义。
无需 std::thread，也不把 C++ 关键字 thread_local 当成线程 ID 实参。

<!-- check:context-source-v2 -->
```cpp
#include "qlog/detail/producer_context.hpp"
#include <cassert>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <vector>

namespace qlog::detail {
namespace {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<ProducerContext*>::is_always_lock_free);
std::atomic<std::uint64_t> next_logger_id{1};
std::atomic<std::uint64_t> next_producer_token{1};

struct Entry {
    std::uint64_t logger_id{0};
    ProducerContext* context{nullptr};
};
struct ThreadRegistry {
    std::uint64_t last_logger_id{0};
    ProducerContext* last_context{nullptr};
    std::uint64_t producer_token{0};
    std::vector<Entry> entries;
};
thread_local ThreadRegistry thread_registry;

std::optional<std::uint64_t> take_id(std::atomic<std::uint64_t>& next) noexcept {
    auto expected = next.load(std::memory_order_relaxed);
    const auto exhausted = std::numeric_limits<std::uint64_t>::max();
    while (expected != exhausted) {
        if (next.compare_exchange_weak(expected, expected + 1U,
                                       std::memory_order_relaxed,
                                       std::memory_order_relaxed)) {
            return expected;
        }
        // CAS 失败会更新 expected；下一轮先检查是否已经耗尽。
    }
    return std::nullopt;
}

ProducerContext* find_context(ThreadRegistry& registry, std::uint64_t logger_id) noexcept {
    assert(logger_id != 0);
    if (registry.last_logger_id == logger_id) {
        return registry.last_context;
    }
    for (const auto& entry : registry.entries) {
        if (entry.logger_id == logger_id) {
            registry.last_logger_id = logger_id;
            registry.last_context = entry.context;
            return entry.context;
        }
    }
    return nullptr;
}

std::optional<std::size_t> prepare_tls_slot(ThreadRegistry& registry) {
    if (registry.entries.size() == registry.entries.max_size()) {
        return std::nullopt;
    }
    registry.entries.emplace_back();
    return registry.entries.size() - 1U;
}

bool ensure_thread_token(ThreadRegistry& registry) noexcept {
    if (registry.producer_token != 0) {
        return true;
    }
    const auto token = allocate_producer_token();
    if (!token) {
        return false;
    }
    registry.producer_token = *token;
    return true;
}

std::unique_ptr<ProducerContext> make_context(const ContextRegistryView& view,
                                             std::uint64_t token) {
    ChannelCold cold{view.logger_id, token, 0, std::string{},
                     view.ring_config.max_payload_bytes, view.dependencies};
    return std::make_unique<ProducerContext>(std::move(cold), view.ring_config);
}

ProducerContext* publish_context(std::unique_ptr<ProducerContext>& node,
                                 std::atomic<ProducerContext*>& head) noexcept {
    auto* raw = node.get();
    assert(raw != nullptr);
    auto* expected = head.load(std::memory_order_relaxed);
    do {
        raw->published_next = expected;
    } while (!head.compare_exchange_weak(expected, raw,
                                         std::memory_order_release,
                                         std::memory_order_relaxed));
    return node.release();
}

void install_tls_slot(ThreadRegistry& registry, std::size_t index,
                      std::uint64_t logger_id, ProducerContext* context) noexcept {
    assert(index < registry.entries.size());
    assert(registry.entries[index].context == nullptr);
    assert(context != nullptr);
    registry.entries[index] = Entry{logger_id, context};
    registry.last_logger_id = logger_id;
    registry.last_context = context;
}

void rollback_tls_slot(ThreadRegistry& registry, std::size_t index) noexcept {
    assert(registry.entries.size() == index + 1U);
    assert(registry.entries[index].context == nullptr);
    registry.entries.pop_back();
}
}  // namespace

std::uint64_t allocate_logger_id() {
    const auto id = take_id(next_logger_id);
    if (!id) {
        throw std::overflow_error("QLog logger identity exhausted");
    }
    return *id;
}

std::optional<std::uint64_t> allocate_producer_token() noexcept {
    return take_id(next_producer_token);
}

ContextResult acquire_thread_context(const ContextRegistryView& view) noexcept {
    auto& registry = thread_registry;
    if (auto* existing = find_context(registry, view.logger_id)) {
        return ContextResult::success(*existing);
    }
    if (!view.registration_open.load(std::memory_order_acquire)) {
        return ContextResult::failure(ContextError::registration_closed);
    }

    std::optional<std::size_t> slot;
    try {
        slot = prepare_tls_slot(registry);
        if (!slot) {
            return ContextResult::failure(ContextError::tls_capacity_exhausted);
        }
        if (!ensure_thread_token(registry)) {
            rollback_tls_slot(registry, *slot);
            return ContextResult::failure(ContextError::producer_token_exhausted);
        }
        auto node = make_context(view, registry.producer_token);
        auto* published = publish_context(node, view.head);
        install_tls_slot(registry, *slot, view.logger_id, published);
        return ContextResult::success(*published);
    } catch (const std::bad_alloc&) {
        if (slot) {
            rollback_tls_slot(registry, *slot);
        }
        return ContextResult::failure(ContextError::allocation_failed);
    }
}
}  // namespace qlog::detail
```

### 12.5 每个函数为什么这样写、写完怎么检查

1. **take_id**：当前源码只执行一次 compare_exchange_strong；即使 strong 没有伪失败，也可能因另一个线程先递增而正常失败。此时必须使用 CAS 更新后的 expected 重试，不能返回耗尽。上面的 while 把“尚未耗尽”直接写在循环条件里；成功从循环体 return，耗尽从循环后 return。每轮先判断最大值再计算 expected+1，不会回绕。relaxed 只保证号码分配，不发布节点。单线程等待时间无固定上限，不能把 lock-free 称为 wait-free。执行过程见 §12.9。
2. **find_context**：先比 last_id，再线性查 TLS entries。命中慢索引必须同时更新 last_id 与 last_context，否则 A→B→A 后下一次 A 可能拿到 B 的 Ring。只比较 entry 保存的 id，不能解引用旧 Context 来查 ID；Logger 已销毁时这些指针可能失效。Logger 身份不复用且不允许并发析构保证当前命中节点仍活着。
3. **prepare_tls_slot**：size 不可能大于 size_t 最大值，真正上限是 vector.max_size()。用 emplace_back()，不是缺 position 的 emplace，也不把其返回对象当 bool。返回下标是新 size-1。容量申请失败时 vector 不增加元素，外层 slot 仍为空，因此不允许 pop 旧 entry。
4. **ensure_thread_token**：TLS 存的是已分配 token，不是计数器。第一次从全局 token 域取号，检查 optional 后必须赋值。后续 Node/Ring 分配失败不回收号码，线程下次继续使用该 token。
5. **make_context**：按照 ChannelCold 字段声明顺序填完全部六项；局部 quota/dependencies 必须真正传进 cold，不能仅声明。make_unique 的模板参数是类型，圆括号内才是构造实参；返回 unique_ptr，由异常路径自动销毁半成品。
6. **publish_context**：head 类型必须是 atomic<ProducerContext*>，不是 atomic<ProducerContext>。先构造全部字段，再把 raw->next 设置为观察到的头，CAS 失败后用更新的 expected 重设 next。成功 CAS 是所有权转移点，随后 release 只取消本地 unique_ptr 的销毁责任。不得在发布后改 next，也不解引用旧 head。消费者以 acquire 读取 head；并发插入均为 RMW，读取者沿只增链访问已发布节点。实际 Ring 数据另由 Ring 协议同步。
7. **install_tls_slot**：只写已经存在的槽和两个 last 字段，不 push、不 reserve、不创建字符串。这里必须 noexcept，因为节点已发布，此后不能以抛异常为由删掉节点或回滚链。
8. **rollback_tls_slot**：只能撤销本次追加的最后空槽。它不是注销 Context，也不负责撤销已发布节点。断言帮助查重入/顺序错误，Release 正确性仍由调用顺序保证。
9. **allocate_logger_id/allocate_producer_token**：定义放匿名 namespace 外才匹配头文件中的外部符号；内部两个 counter/helper 留在匿名 namespace。Logger 构造失败直接抛 overflow_error；日志调用的 token 耗尽转换为 ContextError。文件尾要分别关闭两个 namespace，不能只有当前的一枚右括号。
10. **acquire_thread_context**：接入主流程把上述 helper 串起来。先命中缓存，miss 才查 open、准备槽、取 token、创建节点；try 内保存 slot 以区分 vector 自己分配失败和 Node/Ring 分配失败。只捕获预期 bad_alloc。Ring 配置必须已经由 Logger 构造验证，不能任意构造不合法 view 然后指望 noexcept 主函数吞掉 invalid_argument。

close_registration 只禁止新的上下文；上述命中优先的写法不把已有缓存改成 registration_closed。它不是停止生产/排空函数。
现行前提仍是 close/析构前停止并协调所有调用；单次 acquire load 不能解决 close 与 CAS 并发的竞态，不能据此开放并发关闭。
同线程注册不允许重入，分配器回调/异常报告不能递归写这个日志系统。内存分配本身可能使用库内锁；本方案保证注册代码无 mutex/spinlock，不宣称整个首次调用严格 lock-free。

### 12.6 Impl 和两个桥接成员如何收口

位置 src/async_logger.cpp：删除未使用的 mutex include；直接 include atomic、producer_identity.hpp、producer_context.hpp；修复双斜杠 include。
身份初始化和 const std::uint64_t 字段已完成；保留。当前必须修 dependencies 内 filter 后的多逗号，以及整个 dependencies 初始化后的尾逗号。对照以下写法：

```cpp
// 初始化列表：config 后，filter 前。
logger_id(detail::allocate_logger_id()),
// 初始化列表最后一项：删除 filter 后和整个 } 后的多余逗号，随后直接接构造函数体 {}。
dependencies{filter, clock, policy, hash_dispatch, config.name, config.category_names}
// 字段声明：config 后，filter 前。
const std::uint64_t logger_id;
```

字段声明顺序必须是 config、logger_id、filter、clock、policy、hash_dispatch、dependencies、published_context、registration_open。
保留当前发布头名称 published_context 即可，它与旧参考的 published_head 是同一角色，不需要保留两个原子头。
现有 acquire_context_for_thread 的五项聚合初始化和转发已经正确；修复 RegistryView 头后即可继续使用。

当前已写析构草稿，将其中 delete *node 改成 delete node；完整对照如下。AsyncLogger 自身的 cpp 析构仍可 `= default`：

```cpp
~Impl() {
    auto* node = published_context.load(std::memory_order_relaxed);
    while (node != nullptr) {
        auto* next = node->published_next;
        delete node;
        node = next;
    }
}
```

此前必须停止所有生产调用，结束测试消费者；I3 则还需先完成 Backend 的排空/join。这个析构不会代替那些步骤。
析构体先释放节点，随后成员逆序析构，所以 Channel 借用的配置/时钟/过滤仍活着。不要访问其他线程 TLS，也不要清 TLS 时重复释放节点。

位置 include/qlog/async_logger.hpp：直接 include detail/context_result.hpp；classify_call 和 acquire_context_for_thread 两个声明移到 private，try_log 留 public。
不要为了访问私有 Impl 而把 Impl 定义搬到头文件；模板只调用两个桥接并使用 Context 的完整内部定义。

### 12.7 接入层修好后，剩余函数按什么顺序完成

先完成本节 12.2→12.3→12.4→12.6，交接一个可编译链接的“取得 Context”批次；不要同时开始 Backend/Appender 虚函数/V2 队列切换。
之后按以下顺序实现尚无函数体的 Producer 部分，具体错误枚举映射沿用 §11.10，不留成功空壳：

| 顺序 / 文件锚点 | 必须落笔的内容 | 结束条件 |
|---|---|---|
| 1. detail/producer_result_map.hpp / map_context_error | inline 函数，switch 穷尽四种 ContextError；allocation_failed/tls_capacity_exhausted→resource_exhausted；closed→registration_closed；token exhausted→identity_exhausted；统一 stage=context，reason 保留区别 | 四输入分别得到正确 status/stage/reason，失败不能映射 full |
| 2. 同文件 / map_measure_failure | 按 §11.10 的九枚举分类，用原 failure.argument_index/byte_count 构造 LogFailure | 不重复 measure，不丢参数位置，不按枚举数字强转 |
| 3. 同文件 / map_reserve_error | full→full；payload_too_large/reservation_pending→internal_error；stage=reserve，bytes=requested，index=0xFF | 只接收失败 status；ok 不能伪造失败 reason，可断言并终止不可达分支 |
| 4. 同文件 / map_encode_error | 七种 encoder 原因逐项转换，status=internal_error、stage=encode、bytes=target_size | helper 只映射结果，abort 由调用者执行 |
| 5. detail/async_logger_impl.hpp / AsyncLogger::try_log | 模板声明约束原样复制；四 Gate 分支先结束或继续；取得 ctx 后构造 FormatInput{format.data(),format.size(),format.stored_hash()}；measure→reserve→clock→encode→commit/abort | 同线程同 Logger 已接入后无需重新分配；过滤路径不触碰 TLS |
| 6. log_format.hpp / literal 工厂 | 按既有 I1 哈希合同增加受控 literal 工厂，保留 runtime hash=0；非零预计算值不能由普通调用者任意填写 | runtime 两整数闭环通过后，再验证 literal 一致性、字符串与错误路径 |
| 7. 诊断与 CMake | 根 qlog target 加入真正有实现的 async_logger.cpp/producer_context.cpp 与现有 filter/clock 源；PUBLIC 诊断宏保持消费方一致；模板头通过真实调用实例化 | 最小消费方不再有 undefined reference；Debug/Release 都完成本批真实构建 |

模板使用的对象路径统一为 ctx.channel_.cold_、ctx.channel_.ring_；其他旧示例中不带下划线的同角色成员也须同步，不能混用。
reserve 成功前不取时间；encode 失败执行一次 abort，成功执行一次 commit；失败的 reserve 没有可 abort 的当前 reservation。
MeasureResult 局部变量必须活到 encode 完成，prepared 引用不得来自临时返回值；不得重扫 cstr 或把输入参数另拷成完整 Record。
新实现头在 async_logger.hpp 的类和 namespace 定义都结束后 include；mapper 的定义必须先于模板使用可见。

### 12.8 本批自查与后续验收范围

先逐头自包含编译，再链接接入实现，最后才运行路径测试；语法修好不等于并发语义已经成立。

| 场景 | 预期结果 / 能抓住的问题 |
|---|---|
| 同线程同 Logger 连续取得两次 | Context 指针相同，发布节点数不增加 |
| 同线程 A→B→A→A | A/B Context 不同；后两次都是 A；两 Context 的线程 token 相同，Logger 身份不同 |
| 多线程同时首次写同一 Logger | 每线程独立 Context/Ring，发布链节点不丢失，token 不重复；在线消费者按 acquire 读头 |
| vector 追加失败 | 返回 allocation_failed；已有 entries/last 不变，无误 pop，无新发布节点 |
| Node 或 Ring 分配失败 | 撤销本次空槽，保留线程 token；发布链无新增；重试可成功 |
| ID 域设在最大值附近的专用测试 | 最后合法值只发一次；之后持续耗尽，绝不发 0；CAS 失败更新为最大值后不溢出 |
| miss 且 registration_open=false | registration_closed；不追加槽、不创建节点；不拿这个测试证明并发 close 安全 |
| 线程先退出 / Logger 先销毁且无并发访问 | Logger 回收所有节点一次；TLS 析构不解引用或 delete；同地址重建新 Logger 不命中旧 ID |
| 后续 filtered / full / encode failure | 分别无 TLS、无 clock、恰好一次 abort；成功恰好一次 commit |

错误注入、并发、分配统计与生命周期测试由生产批次完成后交接验证；不得仅凭文档参考编译宣布这些场景已通过。
本轮循环与参考核验见 [身份循环指南核验](./I2_ID_LOOP_GUIDE_VALIDATION_20260915_CHS.md)；先前记录保留于 [上下文指南核验](./I2_CONTEXT_GUIDE_VALIDATION_20260915_CHS.md)。



### 12.9 本次疑问：循环如何退出，以及源码的最小修正顺序

`for (;;)` 没有条件退出，但函数内的 `return` 会退出整个函数，当然也会退出循环。旧完整例子的成功分支在循环内部，并不等待执行到循环之后。
所以旧版不是必然死循环。为方便阅读，§12.4 已改成 `while (expected != exhausted)`，成功在循环内返回，耗尽在循环后返回；CAS 协议不变。

| 本地 expected / 情况 | 执行过程 |
|---|---|
| expected=1，无竞争 | 条件成立；CAS 把 next 从1改成2；返回1，整个函数结束 |
| expected=1，其他线程先改 next 为2 | CAS 失败并把 expected 更新成2；下一轮尝试2→3；成功返回2 |
| expected=max-1，无竞争 | CAS 把 next 改成max；返回max-1，这是最后一个合法编号 |
| expected=max-1，其他线程先取走最后编号 | CAS 失败并更新 expected=max；下一轮条件为假，循环后返回 nullopt |
| 一开始 expected=max | 不进入 while，直接返回 nullopt |

compare_exchange_weak 还允许伪失败，因此必须放在重试循环中；strong 只是不允许伪失败，仍可能竞争失败，也不能删掉重试。
不要每轮把 expected 重设成第一次加载值；CAS 的 expected 参数是可被更新的引用。
不改成无检查 fetch_add：最大值处递增会回绕。也不加固定重试次数后返回“耗尽”，因为竞争与编号空间耗尽是两种情况。

按以下顺序修本次实际代码，每步都对照 §12.4 完整文件：

1. producer_identity.hpp 删除 `std::uint64_t logger_id{};` 和 `std::uint64_t token_;`。头文件只留函数声明。普通全局变量定义放在头里会让多个翻译单元重复定义；这里也不需要这两个变量。实际计数器 next_logger_id/next_producer_token 已经在 cpp。
2. channel.hpp 删除整段 ContextRegistryView 与 acquire_thread_context 声明，只在 producer_context.hpp 保留一份。修 producer_token 类型与 category_names 引用，补直接 `<utility>` include。
3. ThreadRegistry 改为 `std::uint64_t last_logger_id{0};`、`ProducerContext* last_context{nullptr};`；find_context 快路径改成 `return registry.last_context;`。
4. take_id 替换成 §12.4 的 while 版本。prepare_tls_slot 首条件改成 `registry.entries.size() == registry.entries.max_size()`，已写好的 emplace_back 和 size-1 保留。
5. make_context 参数改 `const ContextRegistryView& view`，末句改 `return std::make_unique<ProducerContext>(std::move(cold), view.ring_config);`。模板括号里填类型，圆括号里填构造实参，不出现 `& ProducerContext(...)`。
6. install_tls_slot 保留 `registry.entries[index] = Entry{logger_id, context};`，后面两句必须是 `registry.last_logger_id = logger_id;` 和 `registry.last_context = context;`，不是再次写 entries[index]。
7. acquire_thread_context 的 try 开头先写下面代码，再调用 ensure_thread_token。token 失败分支使用 `rollback_tls_slot(registry, *slot);`。此前空 optional 不能解引用，未追加槽不能回滚。

```cpp
slot = prepare_tls_slot(registry);
if (!slot) {
    return ContextResult::failure(ContextError::tls_capacity_exhausted);
}
```

8. producer_context.cpp 直接 include `<stdexcept>`，去掉不使用的 `<thread>`。现有 CAS 发布函数和回滚函数保留。发布成功之后只能安装已有槽并返回，不能追加分配。
9. Impl 的 dependencies 写成 `dependencies{filter, clock, policy, hash_dispatch, config.name, config.category_names} {}`，后面的 `{}` 是构造函数体，中间没有逗号；析构改 `delete node;`，不是 `delete *node;`，delete 接收拥有对象的指针。
10. async_logger.hpp 直接 include context_result.hpp，并把两个桥接声明移入 private。恢复下面的公共结果头后，再检查头文件和接入 cpp 的真实编译链接。

### 12.10 恢复公共 LogResult，不能只保留 ContextResult

本次实读 include/qlog/log_result.hpp 已不存在，include/src 内也未找到 LogResult/LogStatus 定义；async_logger.hpp 仍 include 它，所以这是一项真实的编译阻塞。
拆分内部 ContextResult 时，只应把内部四种错误和 ContextResult 类移到 detail/context_result.hpp；公共结果头仍需存在。
恢复 include/qlog/log_result.hpp，保留以下职责：

- LogStatus：accepted、filtered、invalid_level、invalid_category、invalid_input、too_large、full、internal_error、resource_exhausted、registration_closed、identity_exhausted；不恢复 invalid_handle。
- FailureStage：validation、measure、reserve、encode、context。
- FailureReason：保留 §11.10 的测量/预留/编码原因及 validation 的 invalid_level/invalid_category；接入新增 context_allocation_failed、tls_capacity_exhausted、registration_closed、producer_token_exhausted。
- LogFailure：stage、reason、argument_index（默认0xFF）、byte_count（默认0）。
- LogResult：status()/accepted()/failure() 三个只读查询；内部保存 LogStatus 与 optional<LogFailure>；私有构造函数由 detail::LogResultAccess 访问。
- LogResultAccess：make_accepted() 构造 accepted/nullopt；make_filtered() 构造 filtered/nullopt；make_failed(status, failure) 构造失败并断言 status 非 accepted/filtered。返回类型都是 LogResult，不留空 return。

公共结果头直接 include cassert/cstddef/cstdint/optional；先前置声明 detail::LogResultAccess，再定义公共类，最后定义工厂。
该头不定义 ContextError/ContextResult，也不 include 完整 ProducerContext。业务用 LogResult，内部接入用 ContextResult，由 map_context_error 转换。
完成后最小消费方只 include async_logger.hpp 就应能解析所有公开声明；函数体/链接验证仍需后续接入完成。

## 13. 下一批实施入口

见 [runtime 下一步指南](./MILESTONE2_I2_RUNTIME_NEXT_GUIDE_CHS.md)：最新剩余修正 → 恢复 LogResult → 四个 mapper → runtime try_log → 根 CMake 接线。
该切片不改变 ADR-013；Appender 动态配置与 V2 切换仍待对应阶段商榷。
