# QLog 剩余 V1 实现指南：BQLog format 对齐后继续完成

> **2026-09-19 编码入口已替换：** 本文保留历史设计/进度，不再作为剩余 V1 的逐步编码指令。请按 [剩余代码级实施指南](./V1_REMAINING_CODE_IMPLEMENTATION_GUIDE_CHS.md) 的依赖顺序和唯一接口实施；五项决定及用户最新 flush 更正见 [ADR-017](./ADR-017-v1-output-and-completion.md)。旧接口片段不得与新指南混用。flush 对齐 BQLog：单次持续短写、write EINTR重试；取消暂存预算/跨轮续刷方案。历史验收结果的原始范围不变。

日期：2026-09-19（正文formatter实现更新；其余后端设计沿用9月17日）。权威仓库 `/home/qq344/QLog`，Windows入口 `\\wsl.localhost\Ubuntu\home\qq344\QLog`。BQLog参考目录 `E:\VisualStudioProject\BqLog`。

2026-09-19已按用户授权补全正文formatter、接入构建并执行验证；详见[formatter实现与验证报告](./V1_FORMATTER_IMPLEMENTATION_REPORT_20260919_CHS.md)。本次证据覆盖独立正文格式化及现有工程回归，不代表Backend、Appender、管理闭环或整个V1已完成。历史I1验收仍按其原有范围解读。

唯一 format 合同：[ADR-016](./ADR-016-v1-bqlog-worker-format.md)。配置/控制/输出的完整逐函数说明：[V1收尾指南](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md)。实施时先读本文的起点和顺序，再按链接进入对应细节，不再从旧严格 FormatPlan 开始。

## 1. 当前已经有什么，缺什么

QLog HEAD为 `58b6948c33bb2f4b94db7c3e3d77b228eaf5afd7`，但现状包含大量未提交修改；不能只用commit号复现本次工作树。BQLog格式参考固定为 `60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`，相关源码未修改。

| 模块 | 本次检查结果 | 后续边界 |
|---|---|---|
| Ring / Record Core / hash / decoder | 已有真实实现；I1按WSL2范围验收，见I1D报告 | 保留wire、tag、32槽decoder及单次reserve协议 |
| Filter / admission clock / TLS Context | 已有实现 | 不按旧9月14日骨架重写，不恢复显式绑定 |
| format入口和Producer | 已有数组+FormatView，measure不解析brace，encoder原样copy/hash | format方向已对齐；不要新增前台parser |
| 配置模块A | logger_config.cpp三个共享函数已实现，构造已调用 | reset复用，不再重复迁移 |
| 诊断B | 已完成六种宏配置、八个返回点、跨TU接线与并发计数守恒专项 | 见[诊断验收](./V1_DIAGNOSTICS_ACCEPTANCE_20260919_CHS.md)，接worker后保留受控Producer fixture |
| formatter正文 | FormatSpec / render_message_utf8已实现；scanner、15种tag、padding、安全适配及CTest已接入 | 正文已验证；compose_line、目标前缀和worker消费接线仍待完成 |
| Appender / batch / mailbox | 无生产实现 | D/E完整实现，不用返回success占位 |
| Session / worker / runtime / wakeup | 无生产实现 | F实现实际消费与共享/独立生命周期 |
| reset / flush / drain / shutdown | AsyncLogger只有声明；当前析构直接回收Context | G闭合后才能成为可用异步Logger |

源代码起点：`include/qlog/async_logger_impl.hpp`，`include/qlog/detail/record_measure.hpp`，`include/qlog/detail/record_encoder.hpp`，`src/producer_context.cpp`，`src/logger_config.cpp`，`src/async_logger.cpp`。上述文件本轮均保持原样。

## 2. format统一后的不变量

1. 业务线程仅校验地址/长度/参数白名单/配额，复制原始format和参数；不能因`{`、`}`、字段与参数数目不符拒绝日志。
2. Producer顺序保持 Gate → Context → measure一次 → reserve一次 → admission clock → encode → commit/abort。过滤和full不执行hash/clock；数组包装只有末尾NUL判断。
3. worker只处理QLog已支持的UTF-8 format和15个有效ArgumentTag；32是参数上限，不是brace出现次数上限。
4. 零参数正文全部原样；有参数采用BQLog UTF-8 scanner。参数耗尽后仍扫描`}}`。Bool/null/pointer/float等可见文本规则按ADR-016覆盖旧规则。
5. 不建立V1 FormatPlan/FormatCache；保留已有Record hash但worker不补算无用hash。正文最多一次，目标只重复前缀与完整行复制。
6. 保留8192B format、32参数、65536B完整行、pointer+length、固定scratch、固定batch。解码使用memcpy/bit_cast，不移植BQLog未对齐读取或不安全容量算术。
7. 共享worker省线程和工作区，慢I/O仍会影响同worker其他Logger。低占用Producer无worker锁，低空间/full允许exchange与短暂mutex/CV通知；不能再宣称全部Producer路径无锁。

## 3. B0/B：先收齐当前诊断接线

以下是9月17日记录的三处B0问题。9月19日检查当前工作树时均已修复，且本次Debug/Release构建通过；保留此表用于解释历史，不要求重复修改。诊断计数专项现已通过，见[诊断验收](./V1_DIAGNOSTICS_ACCEPTANCE_20260919_CHS.md)；下表仅保留历史问题：

| 文件/位置 | 修改方法 | 原因 |
|---|---|---|
| 根CMakeLists.txt:33，target在:54才创建 | AUTO/ON/OFF选项与值计算保留；把target_compile_definitions移到add_library之后，继续PUBLIC导出0/1 | target必须存在；模板与库宏必须一致 |
| async_logger_impl.hpp:24 | `detail::LogResultAccess::make_failed(...)` 与 `0U` 之间补逗号，核对整个invalid_level分支括号 | Debug/ON分支当前语法不完整 |
| src/async_logger.cpp:60使用ProducerDiagnostics | include `qlog/detail/producer_diagnostics.hpp` | 类型不能依赖未引入的声明 |

已完成并验证[收尾指南§2](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md#2-debug-诊断先完成返回路径记账)的七项计数和八个返回位置；数组重载不计第二次。关闭宏时类型、字段、桥接调用和fetch_add一并消失；IO恢复状态和错误结果仍是功能字段，不能跟着关闭。

函数入口：`AsyncLogger::record_call_result(const LogResult&, std::size_t) const noexcept`。当前accepted_bytes未使用，本轮计划仍只要求七项守恒，不宣称有字节统计或运行期快照API。

完成标准：后续Debug/Release、AUTO/ON/OFF真实编译及公共模板消费方链接，停止Producer后核对calls/attempted守恒。发生问题优先修正此切片，不撤销配置A或已验收I1。当前`i2_runtime_public_probe.cpp`已接入CTest，仍假定没有消费者；接worker前把“最终full”的部分迁入受控Producer fixture，另建端到端测试。

## 4. C：独立完成worker formatter，再接线程

### C1 类型与边界

- `include/qlog/detail/format_spec.hpp` 已使用FormatSpec、FormatError、FormatFailure、FormatResult；当前命名已同步，不再重命名或恢复严格FormatPlan。
- `render_message_utf8` 已在 `src/text_formatter.cpp` 实现，头文件已写明容量、输入有效期和失败约定；scanner、spec、数值和writer辅助函数位于匿名命名空间。下一步补 `compose_line`，复用正文输出，不重复格式化。
- text_formatter.cpp已加入qlog target，两个formatter测试目标已注册；后续新增源文件仍须即时接线。接口保持pointer+length，不新增std::span。

```cpp
[[nodiscard]] FormatResult render_message_utf8(
    const std::byte* format, std::size_t format_size,
    const DecodedArg* args, std::size_t arg_count,
    std::byte* output, std::size_t capacity) noexcept;
```

FormatResult含输出size及optional<FormatFailure>；失败size=0，scratch允许被改写但一律不得交给batch。成功空正文同样size=0，靠error是否存在区分成功。FormatFailure含error、format byte_offset、argument_index，非参数位置为0xFF。

### C2 扫描与转换

先实现CheckedTextWriter的append_bytes/append_fill/有界移动；null+nonzero和capacity先检查。输入format、args及字符串存储在调用期间有效，输出与输入不重叠。生产调用只接decode_v1成功的DecodedArg。

按ADR-016§4实现零参数raw分支、有参数20B字段前瞻、10索引spec窗口与顺序消耗；每一条路径都推进cursor，禁止无限重试同一个`{`。保留未知spec/default路径，不增严格错误枚举。

然后移植支持tag的转换：先Char/Bool/UTF8/null，再整数/Pointer64，再F32/F64与padding。普通浮点兼容路径保留7/15位逐位输出；NaN/Inf、整数转换范围、负宽度/移动索引异常按ADR-016§5处理。不要一开始全部委托std::format，也不要把to_chars默认舍入当BQLog截断。

普通run采用一次扫描并批量复制。SIMD是同一语义的后续优化：基线可以先scalar；证明无越界并有测量收益后，再加入硬件派发。编译器/目标CPU未保证指令集时不得在通用TU无条件使用AVX2。

### C3 正文复用和完整行

worker持有堆上message_scratch与line_scratch，各65536B；不在逐条栈上申请128KiB。一次正文成功后，各选中健康目标用自己的时区和CalendarCache组装前缀、正文和换行；完整行超限只使该目标失败。没有选中目标时跳过正文；所有选中目标都处于恢复拒收状态时可跳过正文但必须正确记录delivery_failed。

`compose_line` 的前缀和时间合同保留[收尾指南§4.2](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md#42-一次正文多目标前缀)。OS_TID当前为0，要在Context冷创建时获取一次；不能逐条系统调用，也不能冒用producer_token。

完成标准：ADR-016示例、全部tag/spec关键组合、初始0/1/32参数、缺参后`}}`、20B/10索引边界、内嵌NUL、65535/65536/65537输出、数值安全分支和前缀计长。源码推导向量须再经实际BQLog对照；UB输入只测试QLog安全结果，不拿BQLog崩溃/随机输出作oracle。

## 5. D：固定batch和真实Appender输出

按[收尾指南§5](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md#5-appenderbatch-与-bqlog-风格文件恢复)完成 `io_result.hpp`、`output_batch.hpp`、`appender.hpp`、`console_appender.hpp`、`text_file_appender.hpp` 及各cpp。

Appender非虚公共流程控制过滤/容量/状态，受保护虚方法实现输出。Producer不调用Appender；BackendSession独占实例。`accept_line`只复制完整行到自有固定batch，绝不在持Frame期间flush。

实现次序：batch边界 → short-write/EINTR推进 → Console真实write与SIGPIPE策略 → TextFile open/fstat/append → ENOSPC/EDQUOT保留未写后缀 → 周期重试 → 其他永久write错误清理并报告后缀损失、开新编号恢复文件 → buffered/durable分离。

故障时固定内存，不为堆积扩容；已写前缀不重放。EAGAIN延后同fd重试，sync失败独立标durability_uncertain。控制线程冷准备错误结果的字符串和容量，Backend的noexcept路径不临时分配错误文案。

完成标准：故障注入覆盖短写、零进展、EINTR、容量耗尽、恢复、重开失败、sync/close失败；IO字节损失和Record/delivery计数区分。每个目标失败独立，不能阻塞其他目标的逻辑分发；真实阻塞write仍可能拖延共享worker，这是性能限制。

## 6. E：单管理线程与单槽邮箱

按[收尾指南§6](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md#6-配置准备与单槽无锁命令交接)实现 `control_mailbox.hpp/.cpp` 与 `appender_prepare.cpp`。正常状态链为empty → pending → completed → empty；未领取完成结果也占槽，忙立即返回busy。提交前拒绝failed/exited的worker；发布后才失败时，poll须等exited(acquire)确认无借用，再以backend_failed终结请求并安全回收，不能永久pending。

`prepare_reset`复用已完成 `qlog::prepare_appender_configs` 和 `merge_appender_levels`。配置值与资源冷准备成功后才发布；新目标/影子/结果/退休回收均有明确拥有者。兼容性以稳定base_path/类型/资源配置判断，不以运行中的recovery文件路径判断。

worker在无Frame时应用，兼容目标保留batch与恢复状态；旧IO失败可返回applied_with_io_error且新配置已经生效。poll移出完成值后管理侧回收退休对象；禁止假装reset是文件系统可回滚事务。构造失败或尚未发布请求失败，活动配置不变。

完成标准：busy/未领取、ticket耗尽、准备失败、兼容复用、退休错误报告、发布/应答可见性；管理方法的SPSC前提由外部同步保证，不能以一个busy标志承诺多管理线程安全。

## 7. F：worker、Session与Ring回收

实现文件：`detail/backend_session.hpp`、`backend_worker.hpp`、`backend_runtime.hpp`、`worker_wakeup.hpp` 与对应cpp。完整方法与所有权见[收尾指南§7](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md#7-bqlog-风格后台共享独立-worker定时等待与低空间唤醒)。

先有Session消费一步，再有轮转调度，再加线程等待/注册生命周期；一开始用单Session验证真实Ring→decoder→正文→batch。每Record作用域清理器保证恰好release一次；外层Frame损坏不能猜下一个边界，要标consumer_faulted/drain_incomplete；内层Record坏但外层可信才可跳过该Frame继续。

Session按有限Channel快照和记录/字节双配额轮转；worker再轮转Session。持续新注册不能饿死旧链尾。Frame释放后及时publish_reclaimed，在任何write/open/reopen/sync/close/等待之前公布回收；不能只release本地游标而不发布，然后进入慢IO。

默认async使用公共worker，independent使用独立worker；worker持有scratch、Session持有Appender与控制状态。Runtime节点CAS只增，Session脱离必须由worker确认；构造等待attached也须检查worker failed/exited，退出确认后回收未附着Session，已发布节点仍留给Runtime，不死等或删除可见节点。Session不得保存跨轮次的scratch指针或跨release的Ring视图。

最后接入Producer唤醒：ChannelDependencies持有稳定WorkerWakeup引用；Ring新增仅producer使用的近似半容量判断；commit发布后低空间notify，full notify一次后返回，不重试reserve。CV默认66ms定时等待，常规flush/retry检查默认100ms；不是硬实时上界。awake的exchange+mutex只在既定分支进入；worker与所有通知者之间必须满足关闭寿命。

完成标准：一个/多个Channel、持续注册、公平性、共享/独立、低流量tick、压力唤醒、虚假唤醒、停止窗口、worker失败和Session回收；线程异常必须发布失败并放下借用，不能detach线程后释放对象。

## 8. G/H：公共API闭环、示例与构建

构造：配置A → stable metadata/filter/clock/hash → worker及wakeup → ChannelDependencies/Session/Appender/邮箱 → 完整构造后发布Session或启动独立worker。首次打开文件/worker启动失败必须返回失败，不能留下可见半对象。

实现现有声明的四个管理成员及shutdown（共五个）；`FilterConfigAccess`迁到单一共享头；不要在多个cpp复制类定义。`request_flush_batches`只冲已有batch，不能宣传为Producer全局屏障。`request_drain`要求调用方先停Producer，在本Session按轮次排空后答复，不占住公共worker等待本Logger结束。

shutdown：调用方停止/join本Logger的Producer与并发管理 → 完成既有请求 → close_registration → stop/wake → drain并最终flush/sync/close → 共享模式等Session detach / 独立模式join → 回收Context/Session。已有TLS Context不受close_registration拦截，故它不能代替join。关停不等待设备无限恢复，也不为最终关闭新建恢复文件。

若worker已失败，不再无限等待pending完成或正常detach；先acquire观察exited，确认线程放下全部Session/Frame借用，再执行失败清理。独立模式仍join自己的线程；共享模式不得竞争join公共线程。结果记录backend_failed与drain_incomplete。

当前default析构必须在这一切片替换；只接worker而仍保留直接delete Context会导致悬空访问。公共Runtime仅在所有Logger结束后统一停线程；共享Logger不能join公共worker。重复shutdown返回保存结果。

CMake逐个加入真实cpp，链接PUBLIC Threads::Threads，保持诊断宏跨TU一致；示例 `examples/v1_logging_demo.cpp` 覆盖Console+文件、多个Logger的共享/独立模式、运行期reset、Producer停止、drain/shutdown及错误结果检查。实际文件输出完成才可称全链路示例。

## 9. 高性能成立条件和验收顺序

本方案具备实现高性能的结构条件：前台不解释文本、单次reserve、格式复制与hash合并、动态字符串长度复用、worker有界扫描、每Record正文一次、batch摊薄系统调用、释放Ring后再IO。这是源码和设计成本分析，**不能保证整机吞吐已经提高或超过BQLog**；解析工作仍由worker承担，服务率不足仍会full/drop。

| 层次 | 要证明什么 | 必须报告 |
|---|---|---|
| Producer | 稳态零分配、没有parser；低占用/压力唤醒分开 | 每调用p50/p99/p99.9、calls/accepted/full、CPU、首次TLS成本 |
| Formatter | scanner+转换有界；热格式/动态format和零参/浮点负载 | 每秒实际输出字节、每Record耗时、分配次数、输出一致性 |
| worker | Channel/Session公平与足够消费能力 | 长时accepted/processed、drop、排队和输出延迟、共享干扰 |
| Appender | 单目标/多目标与batch收益 | delivery_accepted/failed、字节、write次数、CPU/RSS |
| 持久化 | 普通write与durable分别正确 | flush/sync完成时间、IO错误、未写字节/不确定持久性 |

BQLog对照统一UTF-8输入、输出文本、编译器优化、CPU绑定、Producer数、队列预算、discard策略、worker模式、设备与持久化边界；比较bool/float前先确保语义一致。QLog有安全扩展的值单独评估。不能只以accepted计算分母而隐藏full，也不能用短时Ring吸收速率代替持续吞吐。

合理的性能验收先要求：Release普通低占用路径没有新增parser/分配/诊断RMW；健康worker formatter/batch无稳态分配；代表负载下消费能持续跟上目标入流且drop/延迟可接受。绝对吞吐和p99门槛须从本机与目标部署机器实测确定，不在指南虚构数字。若热模板扫描确为瓶颈，再提交兼容scanner语义的缓存或SIMD优化；不得回退到旧严格语义换取跑分。

集中验收顺序：Debug/Release构建与单元 → 原始格式/兼容输出向量 → ASan/UBSan与随机byte-range → 真实多线程与TSan适用环境 → IO故障与生命周期 → 长时公平/drop与分层benchmark → 原生Linux发布复核。WSL2和原生Linux结果分开记录。

正常排空且计数未溢出时同时检查：

```text
calls = filtered + rejected_pre_admission + attempted
attempted = accepted + dropped_full + failed_after_attempt
accepted = processed
processed = decode_failed + no_destination + dispatched
selected_deliveries = delivery_accepted + delivery_failed
```

正文因输出/安全转换失败时，本条所有selected delivery失败；目标前缀或接收失败只影响该目标。格式宽松恢复不是strict parse failure。accepted只表示commit，delivery_accepted只表示进入目标batch，二者均不等于落盘或durable。

当前下一个可执行切片是 **C的compose_line/目标前缀 → D批量与Appender输出**；正文render_message_utf8已经完成，不重复移植。其后按E→F→G/H继续，本文及收尾指南已经给出剩余模块的接口归属、所有权、失败边界和验收范围，无需重新冻结已接受的worker/管理/恢复方案。

2026-09-19后续范围已确认：按[完整V1收尾执行计划](./V1_FULL_COMPLETION_EXECUTION_PLAN_20260919_CHS.md)继续覆盖C行组装、D输出、E管理、F/G生命周期和H/最终验收；B诊断专项已完成，不重复列为阻塞项。
