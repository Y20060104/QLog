# I2 Producer / Channel 设计讨论历史（已由正式计划取代）

> 2026-09-13：用户已确认冻结。以下是讨论历史，不再作为执行合同；当前以 [ADR-011](./ADR-011-v1-producer-channel.md)、[正式计划](./MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md) 和 [动手指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md) 为准。

日期：2026-09-13。审阅基准：35570da。
状态：部分方向已确定；2026-09-13 用户选择及第 7 节优先于早期候选。剩余 public API、生命周期和更新一致性细节仍待讨论。
实际仓库：/home/qq344/QLog；Windows 入口见根 AGENTS.md。

## 1. 当前结论与依据

- SPSC Ring 已完成本地开发；I1 的 measure/hash/types/encoder/decoder 和 I1-D 已按 WSL2 开发范围阶段收口。
- 本轮读取了最新验收报告、随提交 JSON 摘要，并确认 build/validation/i1d-acceptance 证据目录存在；没有重新运行代码门禁。
- 当前 include/src 中没有 Channel、ProducerHandle、AsyncLogger 或 Backend 的生产实现，下一阶段是 I2。
- 原生 Linux 发布前复核与自动 CI 待补，依最新验收报告不阻塞当前后续开发。
- 证据：[I1-D 验收](./I1D_ACCEPTANCE_20260913_CHS.md)、[随提交摘要](./I1D_ACCEPTANCE_20260913_SUMMARY.json)。
- 总体顺序沿用 I2 Producer/Channel → I3 Backend/NullSink → I4 Text/benchmark，不增加顶层里程碑。

## 2. 已冻结且继续沿用

每个 (生产线程, AsyncLogger) 独立固定容量 SPSC；ProducerHandle 稳态缓存 Channel 地址。
过滤早于 measure；精确 measure → 一次 reserve → admission timestamp → encode 到 Ring → commit。
失败不得发布半条 Record，reserve 后不能完成编码则 abort；满时 drop_new。
时钟遵循 ADR-008：COARSE → REALTIME → unavailable，失败记录仍可提交。
不改变 32B Header、packed tags、深拷贝、一次 cstr strlen、hash identity 或 Release decoder 检查。
I1 保持独立，不反向依赖 Channel/Logger；Backend 使用同一 RecordValidationPolicy 与 decode_v1。

阅读依据：Record 实现指南第 3、9、10、14、16 节；ADR-008；V1 决策 46。

## 3. 必须商量的设计

### Q1 public LogLevel 与日志入口

候选：trace=0/debug=1/info=2/warn=3/error=4/critical=5；off 仅用于过滤配置，不写入 Record。
severity 越大越严重；policy 接受上述六个 wire 值，不以当前过滤阈值构造，否则后续阈值变化可能拒绝已入队记录。
这会固定 public 枚举与 wire 解释，必须确认后才能写入正式合同。
建议先提供显式 ProducerHandle 的模板 try_log 入口和可检查结果；便利宏/TLS 入口是否纳入 I2 待确认。
还须确认：参数表达式在 C++ 函数调用前求值；内部过滤只能避免 measure/strlen/hash/clock，
不能宣称自动跳过调用方昂贵表达式。若要宏级惰性求值，另行确认 API 语义。
最终签名、FormatInput 字面量/runtime 适配、错误枚举在确认后一起给出，不先创建占位生产 API。

### Q2 注册、句柄与回收

推荐显式冷路径绑定，业务线程持有 ProducerHandle；同一线程同一 Logger 重复绑定复用同一 Channel。
候选：Logger 持有稳定地址 Channel，句柄为线程绑定的非拥有引用，不跨线程写入，
Channel 保留至 Producer 停止、Backend 排空并退出后释放。
需确认是否允许运行中加入新 Producer、是否要求线程退出即回收，以及句柄复制/移动合同。
运行中注册若支持，需要明确冷路径注册锁、Backend 获取新 Channel 的同步方式；禁止并发遍历正在变化的普通容器。
若保留至 shutdown，线程频繁创建/退出会累积 Channel 内存，必须确定 Channel 数量上限和容量配置。
建议固定单 Channel 容量、显式配置总 Channel 上限，达到上限在绑定时返回错误；默认数值待确认。

### Q3 category 与过滤配置

已确定：按 BQLog 对齐 level 位图/category 开关与运行时配置重置；下段只读建议已被第 7 节取代。

既有“每条 Record 动态 category/level”不等于“运行中修改过滤配置”。
推荐先启动前登记 category 名称和阈值，运行期间只读；category=0 为默认，名称存活至排空。
若需要热更新，应明确 atomic 读取或不可变配置发布方式、旧配置生命周期和可见性，不能普通变量并发读写。
需确认无效 category/level 的拒绝顺序、阈值组合规则以及 category 名称/数量上限。

### Q4 统计语义与观测

已确定：Debug 诊断计数、常规 Release 编译移除这组统计；下段常驻计数建议已被第 7 节取代。守恒保留为验收语义。

详细指南第 16 节已有：
calls = filtered + rejected_pre_admission + attempted
attempted = accepted + dropped_full
总体计划仍保留更早的 calls = filtered + attempted，需在冻结统计合同后统一。

缺口：上式未覆盖 reserve 返回非 full 异常状态或 reserve 后 encode 失败并 abort。
候选补充：
attempted = accepted + dropped_full + failed_internal
failed_internal 细分 reserve 非 full 失败和 encode_abort；这些属于诊断，不混入 full。
编译期拒绝的调用不计运行时 counters；运行时非法/过大计 rejected_pre_admission 的互斥子类。
内部不可恢复 terminate 与可返回诊断的边界必须单独写清，不承诺进程已终止后的守恒。

推荐 V1 首先提供 Producer 停止、Backend join 后的精确聚合；每侧单写普通计数与 Ring 发布游标隔离。
如需要运行中快照，应另外设计合法同步与快照一致性；不得跨线程直接读取正在写的非 atomic 计数。
I3 中 processed 建议表示已完成处理并释放的 Record（包括 decode/format/sink 失败），不等于写盘成功。

### Q5 shutdown 与 I2/I3 分界

较早指南明确“业务线程全部 join 后 shutdown”；较新测试条目写“shutdown 与满队列等并发”，表述需要收敛。
推荐 V1 要求调用者停止并 join Producer 后 shutdown；Backend 可在关停中处理已接受记录及错误。
若需要 shutdown 与业务日志调用真正并发，必须额外设计拒绝新调用、在途调用和生命周期握手，评估热路径成本。
I2 只冻结生命周期合同与冷路径接口；实际后台线程、排空及 shutdown 完整实现/验收留给 I3。
I2 测试可用直接 Ring consumer 驱动验证，不能因此声称 Backend 或 shutdown 已完成。

## 4. 确认后的实施顺序

| 顺序 | 工作范围 | 退出条件 |
|---|---|---|
| I2 设计收敛 | 一次整理 Q1-Q5、公有签名、状态/所有权表、错误顺序；更新决策日志与冲突条文 | 用户确认后标记冻结；没有关键语义空白 |
| I2 基础与冷路径 | LogLevel、Channel、ProducerHandle、AsyncLogger 绑定/配置、policy/dispatch 冷路径构造 | 地址稳定；同线程双 Logger 隔离；重复绑定和容量失败明确 |
| I2 Producer 闭环 | filter → measure → reserve → clock → encode → commit/abort；统计 | 失败路径不污染 Ring；filtered/invalid/too-large/full 不取时；无二次 reserve |
| I2 验收 | 集成、故障注入、分配检查、编译矩阵和生产路径 benchmark/codegen | GCC/Clang Debug/Release、ASan/UBSan；逐路径计数；稳态无 map/分配/锁/阻塞/共享 RMW |
| 后续 I3 | 公平扫描、32 槽 decode、NullSink、错误隔离、排空 | 单 Channel FIFO，1-128 Channel 不饥饿，停止后 accepted=processed |

以上为同一个 I2 的内部工作顺序，不为每个 helper 单独拆讨论轮次。
生产实现归属按用户下一步授权执行；测试、构建、benchmark 和验收报告由 Codex 负责。

## 5. 文件与首个有界切片

已有函数入口：
- include/qlog/detail/record_measure.hpp:396 — measure_record。
- include/qlog/detail/record_encoder.hpp:122 — encode_v1。
- include/qlog/detail/spsc_ring_buffer.hpp:155 — try_reserve 及 commit/abort。
- include/qlog/detail/record_types.hpp:22 — RecordValidationPolicy。
- include/qlog/detail/record_decoder.hpp — decode_v1；供 I2 集成测试 consumer 和后续 I3 使用。

候选新增文件（命名随确认后的接口设计落定）：
include/qlog/log_level.hpp、include/qlog/producer_handle.hpp、include/qlog/async_logger.hpp、
include/qlog/detail/channel.hpp、src/channel.cpp、src/async_logger.cpp、src/admission_clock.cpp；
以及 tests/producer_integration_test.cpp、tests/channel_lifecycle_test.cpp 和必要 CMake 接线。

首个生产切片在设计确认后开始：LogLevel/policy + 稳定 Channel 所有权 + 显式绑定。
验收是同线程双 Logger、同 Logger 重复绑定、绑定失败、地址稳定和正确 policy；
随后在同一 I2 工作包完成 Producer 闭环。当前不生成实现，不冻结上述候选设计。

## 6. 2026-09-13 源码比较补充：性能优先，仍待冻结

用户要求根据 BQLog/spdlog 的实际做法选择更高性能方案，而非直接接受先前默认选项。
本轮仅源码审阅，未实施 I2 或执行同口径性能测量；以下为成本推断，不是实测排行榜。
BQLog 基准：60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9（本地生产源码无修改）。
spdlog：2026-09-13 在线读取官方 v1.x 源码，属于移动分支；正式 benchmark 必须固定 commit。

### Producer 获取

- BQLog log_buffer.h:348 的 get_buffer_info：TLS 最近 buffer id 命中直接返回；切换 Logger 查线程内 map，首次使用分配。log_manager.h:45 的 get_log_by_id 是 id XOR 后还原指针，不是每条全局 map 查找。
- BQLog log_buffer.cpp:162 还根据频率选择独立 SISO 或共享 MISO，不能把它的整个 Producer 成本归因于 TLS。
- spdlog 缓存 logger 指针后不查 registry；每条调用 get(name) 才有注册表 mutex/map。async_logger::sink_it_ 有 weak_ptr::lock/shared_from_this；共享 MPMC 入队有 mutex，discard_new 同样要取得队列锁。模板日志正文在 Producer 格式化。
- 推荐 QLog 显式预绑定 Channel* 作为最小成本核心入口；TLS 便利适配若需要可在以后单独提供。TLS 不必然很慢，但一般需要 TLS 获取/命中判断，多 Logger 路由还需额外处理。
- 仅“取得缓存对象”不能声称 QLog 必然胜过缓存 spdlog 指针；完整异步 Producer 则 QLog 设计避免了上述格式化、共享队列锁和引用计数路径，仍须实测量化。

### 过滤配置

- BQLog misc/bq_log_impl.h:186 普通读取 level bitmap 和 category mask；log_imp.cpp:218 reset_config 支持配置重置。检查到 bitmap/category 的这些读写不是 C++ atomic；写方 appender 锁不能自动同步未加锁的 Producer 普通读取，因此不能把这种写法移植为 QLog 的 C++20 并发热更新保证。
- spdlog logger.h:247 默认 atomic level relaxed load；logger-inl.h:57 set_level 用 store。单阈值与 QLog Logger/category 组合功能不同，不按源码指令条数直接排名。
- 性能优先候选仍是启动前构造只读的有效过滤表，热路径直接判断。若只支持阈值/开关，可在冷路径合成每 category 有效阈值；若接受任意 level 集合，用位图。功能语义需先确认，不能因实现简单删掉已有过滤能力。
- relaxed load 不等于锁或共享 RMW；稳定配置下可能接近普通 load 的实际成本。没有测量，不能声称禁用热更新必然带来显著收益。

### 统计观测

- BQLog log_buffer_defs.h:26 在非 NDEBUG 下定义 BQ_LOG_BUFFER_DEBUG；SISO header:103 / MISO header:124 的结果和总字节 atomic 统计及更新均受该宏控制。常规 Release 无这组统计；不由此声称 BQLog 没有任何运行计数，频率路由的 update_times 仍存在。
- take_snapshot 是近期日志内容快照，有独立 buffer/copy/format 和锁，不等于 accepted/dropped 的跨线程计数快照。
- 只比较这组统计的额外成本：关闭统计为零；保留单写普通计数并在停止后读，避免运行中发布/读取同步；运行中快照还需发布或原子读写，成本取决于周期与一致性要求。
- 推荐 QLog 保留既有数量守恒要求，采用每 Channel 单写普通计数，停止 Producer、排空并 join Backend 后精确聚合。运行中不读这些正在写的非 atomic 值。
- 如果用户希望像 BQLog 常规 Release 一样移除该组统计，必须明确修改 QLog 统计/验收合同，不能以“最高性能”为由默认删除 accepted/dropped 观测。

### 后续测量门槛

在实现合同确认后，同一 Producer 核心只改变一个变量：显式/TLS；只读/relaxed 动态过滤；无统计/本地统计/周期发布。
覆盖单 Logger 连续、双 Logger 交替、过滤命中/拒绝、1/多 Producer、full 路径；分开首用成本与稳态。
实测 Producer 延迟与吞吐、accepted/dropped，报告同机重复 median/MAD；没有达到可分辨收益时保留更简单的基线。
此处原建议已被第 7 节取代：常规 Release 移除这组诊断统计，验收仍需独立观测。与 spdlog/BQLog 全链路比较另按统一消息、容量、溢出和 Sink 语义执行。

官方 spdlog 依据：
- https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/details/registry-inl.h
- https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/logger.h
- https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/logger-inl.h
- https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/async_logger-inl.h
- https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/details/mpmc_blocking_q.h

## 7. 2026-09-13 用户选择：过滤/统计对齐 BQLog，入口兼顾 V2

本节优先于第 3、6 节早期建议。用户明确选择“2 和 3 与 BQLog 对齐”，
并授权第 1 项考虑未来 V2 MPSC 选择更优方向。当前只更新设计文档，未实施生产代码或测得性能排名。

### 7.1 核心入口：显式预绑定写入端

选择显式 ProducerHandle 作为 V1 核心入口，绑定后直接访问专属 SPSC Channel。
它不要求未来每个 Producer 都拥有独立队列：V2 可研究多个线程各持线程私有上下文，
共同指向同一个或分片的 MPSC 队列。绑定方式、队列拓扑、自动升降级是三个独立维度。
既有 SPSC 句柄仍不能直接被多线程共用；V2 不能把其写游标访问原样搬到 MPSC。

同一 MPSC 算法下，预绑定可避免重复 TLS 获取/id 匹配/map 路由；不声称编译后的差距一定可测。
BQLog log_buffer.cpp:162 的频率判断、SISO/MISO 路由可改善大量低频线程的缓冲资源利用，
但不证明 TLS 获取本身比预绑定更快。其 miso_ring_buffer.cpp:128 起还使用线程私有读游标缓存，
共享写侧通过 fetch_add 等竞争空间；预绑定不会消除 MPSC 队列本身的竞争。

V1 不为了未来兼容预先增加虚函数、函数指针派发、频率计数、MPSC 分支或固定“仅一个指针”的永久 public ABI 承诺。
V2 的共享/分片拓扑、接入 API、显式/自动路由和迁移顺序保证届时单独测量决定。
本次选定的是核心入口方向，不是对未来全部负载宣称全局最优。
线程频繁创建/退出场景需额外比较注册成本、RSS、后台扫描成本；不能只看稳态单次调用。

### 7.2 过滤：对齐 BQLog 功能，保持 C++20 并发正确性

已确定运行期允许重置 level 位图/category 开关，取代“运行中只读过滤配置”建议。
热路径使用稳定存储的 level bitmap 和 category mask，先过滤再 measure/reserve/clock。
category 名称表是否启动后扩展仍未决定，不能由“过滤热更新”推导出容器可并发扩容。
不照搬非 atomic 并发读写；最低成本候选为各独立标量 atomic load/store。
是否允许单次日志观察 level/category 的混合新旧值，还是要求整组配置一致切换，仍需商量后冻结。
若前者可接受可评估 relaxed 标量；若后者必须成立则设计不可变快照发布及回收。
更新过滤不改变 RecordValidationPolicy 的合法 wire level 集合，也不使已经接受的记录失效。

### 7.3 统计：Debug 诊断，常规 Release 移除

用户选择对齐 BQLog 的条件编译诊断统计：Debug 启用对应结果/字节/数量诊断，
常规 Release 编译移除这组字段与逐条更新，不提供伪造零值的完整运行时统计接口。
这取代原先“常驻普通计数、停止后公开精确聚合”的建议；Debug 统计是否需运行中读取及同步机制仍待设计。
仅移除诊断计数；不得移除队列同步/回收/关停协议所需状态、错误返回、Release decoder 校验。
不引入 BQLog 近期日志内容 snapshot；那是另一项功能。

数量守恒继续是语义和验收要求，不再要求常规 Release 内置维护同名 counters。
Debug/独立诊断构建做逐路径守恒；Release 用测试驱动的调用结果、记录 ID 和测试 consumer 独立核对接受/丢弃/处理。
验收覆盖 filtered、invalid、too-large、full、abort，不能靠取消计数隐藏丢失或重复。
关停必须依据 Producer 已停止且队列排空的协议完成，不能依赖 Release 已删除的 accepted/processed 计数。
生产无统计性能构建和诊断构建必须分别标识；基准外部计数成本与计时边界明确，不能把有无观测的成绩混合。

### 7.4 剩余讨论边界

public LogLevel 数值/入口返回类型、整组过滤更新一致性、category 注册与容量、
运行中注册/回收、句柄复制移动、shutdown 并发语义仍待确认；本节不隐式冻结这些设计。


## 8. 冻结结果索引

用户确认过滤独立标量更新、运行中注册且 Producer 停止后关停；接口对齐 BQLog，采用 verbose/debug/info/warning/error/fatal 与轻量可复制句柄，副本仍限绑定线程。早期 trace/move-only/只读过滤/Release 常驻统计建议均失效。
