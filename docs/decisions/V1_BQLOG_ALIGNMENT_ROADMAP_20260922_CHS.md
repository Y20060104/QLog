# QLog V1 后续开发路线：统一先对齐 BQLog

> 2026-09-23接续更新：SpscRingBuffer已迁入buffer模块，第一、二批已有草稿，第三、四批尚未完成。当前动手顺序及已有/缺失指南见[补全指南第0节](./R0_POST_CLEANUP_IMPLEMENTATION_GUIDE_CHS.md)。R0/R2是模块归属编号，不阻止当前先完成Ring；不要把本路线当作MISO/TLS等模块已经具备完整逐函数实现稿。


> **模块与进度更新（2026-09-23）：** 用户已写utility函数及部分Ring结构草稿；目标buffer/utility分模块，hpp/cpp归属见 [block指南第0节](./R0_BQLOG_BLOCK_RING_GUIDE_CHS.md)。当前迁移与生产Ring实现尚未完成，不能把新增草稿当作旧文件再次清理。

> **2026-09-23 Ring补充决定：旧字节Ring/配置及相关测试基准、过期Ring文档已删除。** 当前库仅版本基础；新SISO按8字节block、外部内存、32位游标重建，见 [Ring实现指南](./R0_BQLOG_BLOCK_RING_GUIDE_CHS.md) 与 [清理记录](./R0_RING_CONFIG_CLEANUP_20260923_CHS.md)。此前47项测试只是本次Ring删除前的历史结果。


> **2026-09-22 最新确认：完整 Layout 与 Record 一起对齐 BQLog。** 已实现/冻结部分不能限制对齐；旧 32 字节头、纳秒时间、旧 tag/DecodedArg、六参数正文函数不再是当前合同。当前 R0 入口为 [完整重建指南](./R0_BQLOG_LAYOUT_REBUILD_GUIDE_CHS.md)，详见 [ADR-015](./ADR-015-bqlog-layout-record-alignment.md)。以下冲突内容只保留为历史。


- 日期：2026-09-22；修订：按用户最新“其余建议也先对齐 BQLog”统一替换首版路线。
- 状态：对齐方向已确定；路线交维护者检查，未开始生产代码替换。
- 权威开发目录：`/home/qq344/QLog`，Windows 入口 `\\wsl.localhost\Ubuntu\home\qq344\QLog`。
- 起点：`feat/spsc-ring-opt@94fefc0f29b21a1dbdd258fdb5725aadf84d770e`。
- 参考源码：`E:\VisualStudioProject\BqLog@60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`。
- 当前 R0 动手入口：[清理后的补全实现指南](./R0_POST_CLEANUP_IMPLEMENTATION_GUIDE_CHS.md) → [完整记录/Layout 算法指南](./R0_BQLOG_LAYOUT_REBUILD_GUIDE_CHS.md)。旧正文抽取指南与冲突实现已删除，见 [清理记录](./R0_CONFLICT_CLEANUP_20260923_CHS.md)。
- 决定：[ADR-014](./ADR-014-v1-bqlog-first-rebaseline.md)；旧文档效力：[作废清单](./V1_DESIGN_SUPERSESSION_20260922_CHS.md)。

## 1. 唯一设计原则

本轮后续 Logger 设计统一先对齐 BQLog。正文与完整行、Producer/Channel、Logger 状态、Manager/Worker、配置、Appender、动态 reset、文件 I/O、flush、退出均适用。

不再保留上一轮“其余按推荐”的 QLog 专用差异：正文共享、固定 SPSC 到 V1 结束、单槽管理命令、提前公平调度、强制释放 Frame 后 I/O、失败必保留旧配置、有限提交快照 flush、结构化持久化成功承诺、空配置自动 Console，都不是当前基线要求。

顺序是：查清参考调用链和实际行为 → 建立对应模块 → 实现并验证对齐基线 → 再提出有依据的优化。对齐不等于将疑似缺陷写成正确性保证；未查清的分支标记待核查，不自行补成另一种 QLog 设计。

范围是当前 V1 日志主链。现有 Ring/Record 保留可复用基础，但不能用旧集成合同阻止对齐；wire 不会因文档更新自动改变。raw/compressed、mmap 恢复、snapshot、跨语言和崩溃信号处理列入范围外能力清单，不自动全量移植。若主链依赖其中某机制，先说明依赖再确定拆分范围。

## 2. 回退时的历史基线

2026-09-23 当前状态：旧codec、Logger/Channel/Producer、六参数格式器及关联测试已删除；当前库仅Ring/version。下列发现描述删除前的回退起点，不是现在的文件清单。

已核对 reflog reset 到 `94fefc0`；开始本轮时已跟踪文件无修改，未跟踪 `docs/validation/`、`tools/` 保留原状，不能证明当前 V1 已完成。

现存 Ring、Record codec/hash、FilterState、ProducerContext 骨架和文本格式化代码；没有后期 Appender/Worker/BackendSession。

已发现：`AsyncLogger::try_log` 尚只有声明；`TimeZoneConfig` 缺失；主库 CMake 未包含 async_logger、producer_context、filter_state、admission_clock、text_formatter 等新文件。对 `text_formatter.cpp` 的无产物语法检查实际报 `TimeZoneConfig does not name a type`。本轮未运行完整构建/测试/性能矩阵。

R0 改为完整记录/Layout 基础重建：BQLog 式 40 字节记录头、参数编码/视图、毫秒时间、TimeZone、拥有缓冲的完整 Layout 一起落地。旧六参数正文抽取方案撤销；见 R0_BQLOG_LAYOUT_REBUILD_GUIDE_CHS.md 与 ADR-015。旧 I1 验收仅说明旧协议，不能作为新记录/Layout 通过证据。

## 3. 模块对照与源码入口

下列路径相对 BQLog 根目录；行号对应参考提交。表中“对齐路线”是后续目标，不表示现有 QLog 已实现。

| 模块 | BQLog 已核实机制与入口 | 对齐路线 |
|---|---|---|
| 配置入口 | `src/bq_log/log/log_manager.cpp:41,123`：配置文本解析为 property_value；`log_imp.cpp:193,230`：按名字读取 appenders_config 对象 | 采用配置树/按名字的目标模型和 type 派发；旧扁平 AppenderConfig 不作为冻结 public 合同 |
| TLS | `src/bq_log/types/buffer/log_buffer.h:348`：最近 buffer ID 快缓存＋TLS map | 业务无需显式绑定；按 Buffer 身份访问线程上下文 |
| Buffer 路由 | `log_buffer.cpp:162`：频率统计；高频专有 SISO，低频共享 MISO | V1 纳入 LP/HP 路由；已有 SPSC 可作专有缓冲基础，新增真正共享 MPSC |
| 顺序/回收 | 同文件 `read_chunk_full_impl`、`verify_context`、`deregister_seq`、块遍历与退休 | 搬清序号、旧块、退出、消费者回收的完整职责；不先做 QLog 简化协议 |
| 满策略 | `log_buffer_defs.h:95`、`log_buffer.cpp:198`、`src/bq_log/api/bq_log_api.cpp:227` | discard/block/expand，默认 block；按参考 API 等待重试与唤醒，不保持旧“始终非阻塞”宣称 |
| Logger 状态 | `src/bq_log/log/log_imp.h:122`、`.cpp:495` | LoggerImpl 对应 log_imp：队列、目标、配置、处理、私有 layout/worker；无独立 Session 层 |
| Manager | `src/bq_log/log/log_manager.h:77`、`.cpp:163` | 拥有 Logger 集合、公共 Worker/Layout，协调创建/reset/处理/退出；不另建第二套所有权 |
| Worker | `src/bq_log/log/log_worker.cpp`、`log_imp.cpp:137` | async 公共、independent 专属、sync 调用线程三模式均进入对齐路线；先公共异步贯通，再补齐其余模式 |
| Appender | `src/bq_log/log/appender/appender_base.h`、`.cpp:79` | 非虚 init/reset/log 组织公共行为，虚 *_impl 扩展；配置数据不继承运行对象 |
| 文本 | `src/bq_log/log/layout.cpp:326`、`appender_file_text.cpp:17`、console 的 log_impl | 每个选中目标各自完整 layout；共享工作区不是共享正文；不引入跨目标文本缓存 |
| 文件层 | `appender_file_base.h/.cpp`、`appender_file_text.cpp` | 对齐文件公共层＋文本派生类、缓存、打开/切换、大小/时间滚动、清理职责；不先压成 QLog 单层设计 |
| reset | `log_manager.cpp:123` → `log_imp.cpp:218` → Appender::reset/add | 管理锁协调的同步修改；按名字复用/替换，刷新旧缓存和 I/O；不加 mailbox 和强事务保证 |
| flush | `log_manager.cpp:181,211` → `log_imp.cpp:495,561,576` | 强制消费/缓存刷新与文件同步分层，按参考调用关系实现；不加提交快照屏障 |
| 退出 | `log_manager.cpp:233`、`log_imp.cpp:120,334`、`appender_file_base.cpp:25` | 对齐 phase、cancel/awake/join、对象清理和析构刷新；不把 uninit 自动解释为可靠排空 |

## 4. 归属、调用流与生命周期

```text
业务 Logger 入口 / 配置文本
          ↓
LogManager（LoggerImpl 集合、公共 Worker、公共 Layout、管理同步）
          ├─ LoggerImpl A（LogBuffer、配置、Appender 列表）
          └─ LoggerImpl B（LogBuffer、配置、Appender 列表、独立 Worker/Layout）

异步写入：过滤 → TLS → LogBuffer LP/HP 分配 → 编码/提交
异步消费：Worker → Manager → LoggerImpl::process → 借用记录
          → Logger category → 逐 Appender 过滤
          → 该目标完整 layout → Console 输出 / 文件缓存（期间可有 I/O）
          → 归还记录 → 周期/强制刷新缓存

同步模式：调用线程同步缓冲 → sync_process → 同样的目标链
```

BQLog Manager 独占持有 log_imp；Logger 的轻量业务入口不是另一份运行态所有者。QLog 按此重新梳理 AsyncLogger 的角色，不在现有 unique_ptr<Impl> 外再套 Session。具体公有类名/签名随后按接口对照表落地，不从旧接口名称反推不相容行为。

公共 Layout 跟随公共处理路径；独立 Logger 使用自己的 Layout。管理线程可能在强制 flush 时取得排他权帮助消费，因此“永远只有固定 Worker 线程才能读”不是对齐合同；必须保证任一时刻同一 Buffer 只有一个消费者。

不沿用此前“销毁每个业务 Logger 立即注销运行态”的 QLog RAII 提案。参考的 Manager 生命周期和对象清理路径先查全，尤其 TLS 先退/后退、Buffer destruction_mark 与锁协调，之后才制定等价释放规则。

## 5. 配置与过滤

以 BQLog 配置树为基础：log 节点控制线程模式、缓冲策略、类别等，appenders_config 是 name→配置对象的映射，type 创建具体派生对象。引入解析/值模型的工作列入路线；具体 C++ 容器或解析器不是已冻结决定，不能再把“无需配置文本解析”当作限制。

当前 BQLog 核心：appenders_config 不是对象会失败；有效空对象不会自动创建 Console。旧 QLog 的“空列表规范化默认 Console”撤销为生效规则。可以提供显式含 Console 的示例配置，但不把示例当作核心隐式 fallback。

Producer 粗等级位图来自所有 Appender levels 的 OR，不排除 disabled 目标、不合并每个 Appender category；Logger category 也参与过滤。后台再次检查 Logger category 和各目标自己的 category/levels/enabled；积压按处理时配置处理。

2026-10-07 用户确认：保留参考当前两套类别匹配。Logger/Snapshot 调用 log_utils::get_categories_mask_by_config，按相等或紧接点号的子路径匹配，*default 另外允许索引0，*无通配语义；Appender::set_basic_configs 使用普通前缀或*，不特判*default。两套都只收集字符串mask，不trim/不折叠大小写；没有字符串mask时全部允许。后台 Logger 类别拒绝会在分发前返回，Appender/Snapshot 的局部允许不能绕过该拒绝。不要把 helper 直接替换 Appender 逻辑或新增统一匹配策略。源码与完整指导见 [过滤实现指南](CONFIG_FILTER_IMPLEMENTATION_GUIDE_20261007_CHS.md)。

类别掩码、等级名称、默认值、时间区字符串等先按 BQLog parser/helper 逐项核对；不拿现有 QLog 的固定 category 位图 public API、固定 offset_minutes 类型替代参考的配置能力。每种错误输入实际是拒绝、默认还是记录诊断，必须出对照测试。

## 6. Producer / Channel / Buffer

2026-10-07 接续：等级位图/过滤已由用户填写，当前仍有三处编译/链接问题，见 [实际过滤审查](../validation/FILTER_CODE_REVIEW_20261007_CHS.md)。下一组先建立Buffer配置数据与恢复身份校验、配置树映射，不提前创建Logger/Manager或完整LogBuffer。转换保留参考默认block、非对象/错误类型的默认、uint32容量转换、recovery请求与平台支持、策略不trim、0频率转UINT64_MAX；容量规范化留Buffer实际初始化。完整源码出处与逐函数指导见 [BUFFER_CONFIG_IMPLEMENTATION_GUIDE_20261007_CHS.md](BUFFER_CONFIG_IMPLEMENTATION_GUIDE_20261007_CHS.md)。候选验证不计生产Buffer/并发/恢复验收。

LP 是低频线程共享多生产者缓冲，HP 是高频线程独立缓冲；阈值和降级、旧块退休、消费序号共同工作。参考默认阈值 1000 次/秒，检查间隔 1000ms；先保留参考基线，再测是否有优化需求。

discard、block、expand 都纳入，默认 block。旧 try_log “必不阻塞”与此不相容，后续公开入口按 BQLog 行为重新整理，不能保留旧注释而让实现阻塞。共享 MPSC 允许正常路径所需原子竞争；扩容/路由转换和冷接入的分配也按实际路径描述，撤销整个 Producer 路径无共享 RMW/无分配/无锁的全局承诺。

必须成套覆盖：LP→HP、HP→LP、块替换、线程退出、Logger/Buffer 析构交错、消费者校验/推进序号、退休回收、等待重试与唤醒。BQLog 具有 oversize 外部缓冲，纳入 Buffer 机制核查，不用旧“超过固定 Ring 就拒绝”代替参考；QLog Record 安全长度检查不会凭空取消。

Record 按 ADR-015 重建为 BQLog 对应头、tag、参数布局、扩展信息和毫秒时间；旧 codec 不再是限制。BQLog 路由 header 的 context/序号仍属运输元数据，不混入 40 字节日志头。新旧进程内协议不混用，不承诺旧持久化文件兼容。

BQLog API 在分配前获取供 Buffer/记录使用的时间；不再保留上一版“路由额外取时、Record 仍按旧 admission 时点”的自定适配。时间时点、单位及 process_log_chunk 的非递减修正一并核对。保留旧时钟/Record 工具只代表可复用，不冻结原取时顺序。

## 7. Worker 与文本目标

async 默认公共 Worker，independent 专属 Worker，sync 不启动异步消费线程。公共 Worker 通过 Manager 遍历异步 Logger，各 log_imp::process 读到 empty；不先引入每 Logger N 条预算或新的公平轮转算法。

BQLog Worker 有 66ms 等待周期、低空间触发唤醒、等待请求处理与退出检查；文件缓存 process 路径使用约 100ms 刷新条件。它们是实现参数，不是端到端延迟保证。先按源码对齐通知/等待次序，再测空闲 CPU、延迟和繁忙 Logger 对其他实例的影响。

Appender 通过公共非虚 log 完成过滤，然后 log_impl。Console 与 TextFile 各自调用 do_layout：本次前缀和本次正文都生成，随后实际终端输出或写入文件缓存。现有 render_message_utf8/compose_line 可作为底层工具，但不能在 fanout 外只算一次正文。

文件缓存分配可触发 flush，文件准备/滚动也可能在记录处理链里进行；Console 可直接输出。撤销“必须先 release Frame 才允许 I/O”和强制每 Console 自有 batch 的先验设计。归还记录的时机按 BQLog 的 scoped read handle 生命周期建立对应；任何被保存的数据仍必须拥有有效存储，不能留下悬空 Ring/scratch 指针。

同一目标过滤/输出失败不阻止后续目标分发；共享线程的慢 I/O 会拖慢其他 Logger/目标，不宣称吞吐隔离。原字符串长度、格式语法、时区、线程名、换行/前缀等通过参考输出逐项核对，不预置自定义解析缓存。

## 8. 动态 reset：按参考顺序与真实返回语义

参考调用链：Manager 解析配置 → 获取 logs_lock_ 写锁 → 找到 log_imp → reset_config 获取实例保护 → 刷新旧缓存与文件 I/O → 移出旧列表 → 按名字寻找可 reset 对象 → 清理未复用对象 → 创建剩余目标 → 重算合并等级位图 → 更新类别等配置。

没有单槽 mailbox、固定唯一管理线程、busy 提交背压或等待 Worker 接受命令的前置结构。先对齐同步管理锁及嵌套关系；实际锁序统一从外层 Manager 到实例/资源，不仅查看某个局部锁就认定整个流程安全。

必须如实呈现参考行为：

- appender_base::reset 对类型作检查后可能清理并重设基本状态，再调用 reset_impl；返回 false 不等于旧对象完全没变。
- log_imp::reset_config 会先处理并移除旧对象，再创建新对象；不是先把全部新文件准备成功才交换。
- add_appender 可失败，而调用点不一定把失败聚合成整个 reset 失败；不可宣称“所有目标成功应用”。
- Manager::reset_config 找到 Logger 后返回 true，即使内部 reset_config 返回 false；内部成功时才更新保存的配置文本。该 bool 不是事务成功证明。
- 文件 init 并不保证已经成功打开最终输出文件，实际打开可延后到处理记录时。

因此撤销普通 reset 失败必保留旧配置/旧对象、不排空就拒绝切换、提交后结构化清理失败结果等 QLog 专用提案。先建立与参考一致的部分失败状态和测试；若之后要改良返回语义，另提设计，不藏进本次“对齐”。

## 9. 文件 I/O 与故障

以 appender_file_base 管理文件和缓存，appender_file_text 负责文本布局/拷贝；先对齐 init/reset、缓存分配/归还/完成游标、flush、按大小/时间切换、过期/容量清理。不要提前替换为另一个 QLog 文件状态机。

`flush_write_cache` 按实际写入字节调整未写缓存。ENOSPC 路径设置 disk_full_drop_，保留待写数据并在后续 flush 重试；新记录在该目标门口被拒绝，其他目标独立处理。成功刷新并排空后解除该标志，期间未接收记录不会自动补发。

不能推广为“所有错误保留后缀”：参考非磁盘满写错误会尝试 open_new_indexed_file_by_name；该函数先关闭旧文件、clean_cache_write，再准备新文件。这样可能丢弃未写缓存。轮转也经过有关路径，应测试真实剩余数据行为，不添加默认无损承诺。

ENOSPC、平台磁盘满、其他错误、部分写、打开失败、flush_file 失败各走哪条分支，以平台层返回值为准；EDQUOT 不自动等同 ENOSPC，EAGAIN/EINTR 也不能直接套用回退前 QLog 状态机。文件同步失败参考会记录诊断；没有据此证明 public 调用获得结构化持久化结果。

代码中疑似缺陷（例如重叠复制或异常分支）应单列证据、复现和是否修复的决定；不能为了逐字移植制造未定义行为，也不能在路线中悄悄声称参考拥有更强保证。

## 10. flush、退出与可观察结果

force_flush/force_flush_all 在 Manager 排他协调下调用 process(true)；process 读取到 empty，再刷新 Appender 缓存。它不是“调用瞬间已提交集合”的快照屏障，持续生产下不承诺固定等待上界。sync 模式的强制 flush 分支与异步不同，按参考单独验证。

flush_appenders_cache 与 flush_appenders_io 是不同函数，force_flush 的调用链不能直接等同文件同步。撤销前一版有限提交截止点、durable 选项和新 FlushResult 的预设设计；公有返回值首先按 BQLog 对照，不把入队/目标接收/写入/同步混为一谈。

uninit 路径：管理协调、解除 console callback、phase 变为 uninitialized、公共 Worker cancel/awake/join、独立 Worker cancel/awake/join。log_imp 清理目标和缓冲，文件 Appender 析构按开关尝试刷新缓存。不能仅由这些函数推导“uninit 自动排空所有 Ring”或“全部文件已持久化”。

正常结束的示例应明确停止业务日志调用后使用参考支持的 flush/退出入口；不得替参考补上不存在的可靠 shutdown 合同。生产者仍在 block 重试时退出、静态析构顺序、TLS 退出、回调重入等列入生命周期专项核查，先记录实际限制，再确定是否存在必须单独修复的问题。

诊断同样先学习 BQLog 的诊断开关、错误输出和返回路径。QLog 原计数公式可用作外部验收观察，但不据此新增生产 public statistics API、复杂 per-target 结果协议或让诊断参与正确性。

## 11. 分期路线

| 阶段 | 工作内容 | 验收与边界 |
|---|---|---|
| R0 记录/Layout 基础 | 重建 Record/编码/视图、TimeZone、完整 Layout 对象和缓冲；列主链接口映射 | 结构断言、记录往返、完整字节差分、实际库链接；撤销旧正文抽取验收 |
| R1 配置与运行态骨架 | 配置树/解析入口、Manager/LoggerImpl 归属、类型工厂 | 默认值/空配置/无效配置与参考对照；没有 Session/双重所有权 |
| R2 Producer / Buffer | 先按ADR-016重建8字节block SISO，再补缓存行block MISO、TLS、LP/HP、频率、顺序、块退休、满策略、oversize | 切换顺序、失败/退出、竞争、block 唤醒与 expand；以测试消费者先验证 |
| R3 Worker 与模式 | 公共异步贯通，随后 independent/sync；处理、等待、管理协调 | 单消费者与 layout 互斥；按参考调度行为；不先优化公平性 |
| R4 Appender/layout 接入/文件 | 公共基类、文件基类、Console/TextFile，接入 R0 Layout、缓存/滚动/故障 | 真实字节对照、时区/过滤、多目标；持有记录期间 I/O 与参考一致；故障真实行为可复现 |
| R5 reset/flush/退出 | 完整动态替换、部分失败、强制处理、取消/join/析构关系 | 返回 bool 与实际状态分开核对；不额外声称原子 reset、有限 flush 或可靠退出 |
| R6 V1 对齐验收 | 示例、文档、测试矩阵、语义匹配基准 | 记录一致/不同/待查项；不同项必须有明确依据，不能用性能理由先改语义 |
| R7 后续优化 | 正文共享、缓存、管理无锁、调度公平性、增强错误报告等 | 每项另提合同和 A/B 证据，检查后再开发 |

reset/flush/退出的接口及锁序在 R1/R3 就设计，R5 是完整功能和故障收口。各阶段先读源码对照表，再写逐函数/成员/状态转移指南，不把本文路线当作可直接照抄的最终并发实现。

测试先覆盖参考真实行为，不强行套用已撤销的强保证。性能报告分开冷接入、LP/HP/切换、三种满策略、三种线程模式、多目标重复 layout、TextFile 缓存/write/同步；列尝试/接纳/拒绝/未开始、延迟、CPU、内存、输出核验、重复数据和源码身份。旧回退前数字仅作历史参考。

## 12. 本次交付与下一步检查

此次已更新路线、ADR、项目约定和作废标记；未修改生产源码、未提交 git commit、未运行完整生产回归。

维护者检查重点：本路线是否完整体现“所有当前 V1 主链模块先对齐 BQLog”，以及表中实现顺序、范围外能力是否合适。不再逐条要求重新接受此前的 QLog 优化建议。

检查后进入 R0/R1 的源码到类/函数/成员详细对照，记录仍需查证的 parser、平台 I/O、退出与异常分支；任何为了安全正确性必须偏离参考的修改单列讨论，任何性能优化推迟到基线之后。
