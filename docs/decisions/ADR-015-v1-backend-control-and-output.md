# ADR-015：V1 高性能收尾——Backend、管理交接与输出故障

> **2026-09-19 当前覆盖：** [ADR-017](./ADR-017-v1-output-and-completion.md)补充Console输出锁、BQLog对齐flush、durable范围、同路径及当前/历史错误；编码统一看[新代码级指南](./V1_REMAINING_CODE_IMPLEMENTATION_GUIDE_CHS.md)。本ADR的日期段落与旧实施状态保留历史；配置helper实际已位于`include/qlog/logger_config.hpp`、`namespace qlog`，不执行旧R4回迁。

> 2026-09-17 format覆盖见[ADR-016](./ADR-016-v1-bqlog-worker-format.md)：worker直接扫描BQLog UTF-8格式，正文一次，无V1解析缓存；管理/恢复/唤醒合同保留。

- 日期：2026-09-16；R2状态：用户要求第2/3项对齐BQLog，并追加明确采用其mutex/CV。第1项管理交接保持不变。
- 本轮交付是设计与指南；没有修改生产代码，没有构建、测试或性能测量。
- 实施入口：[V1 完整收尾指南](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md)。
- 覆盖 ADR-011/012/013 中尚未确定的管理交接、文件失败与后台等待部分；保留 ADR-014 的数组入口。

## 1. 比较依据与能证明的范围

BQLog：本地源码提交 `60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`。
spdlog：官方 `v1.15.3` 固定版本源码；不是宣称该版本为当前最新版。
比较的是以下具体路径的成本与合同，不是整个库的跑分。

| 主题 | BQLog 源码事实 | spdlog 源码事实 | QLog 决定及代价 |
|---|---|---|---|
| reset | log_imp::reset_config 在 scoped_spin_lock 内刷新旧缓存、重组/复用 Appender | sink formatter 更新由具体 sink 保护；不提供与 QLog 完全同义的整表事务 reset | 管理线程准备；Backend 独占活动对象，在无 Frame 借用时切换；不把锁移成自旋锁 |
| 队列与管理 | BQLog 已有自己的路由与配置管理体系，不能简单视为逐条加 reset 锁 | 异步 MPMC 队列入队/出队使用 mutex/CV，discard_new 也拿队列锁 | Producer 原 SPSC 不变；管理采用单槽 SPSC 请求/响应邮箱，Release Producer 不读取邮箱 |
| 唤醒 | worker timed wait 的 process_interval_ms=66；低空间/分配失败路径会 awake，awake 含 exchange 和条件通知 | 入队后通知消费者，消费者在队列 CV 上等待 | 定时等待66ms，低空间/full触发waiting.exchange及mutex/CV通知；普通低占用不通知，唤醒分支有额外同步成本 |
| Worker | 默认 async 可共享 public worker，independent 使用独立 worker | thread_pool 可共享，线程数可配 | 默认async共享public worker、可选independent专属worker；Session与worker分离，共享资源但慢I/O可相互影响 |
| 消息格式化 | BQLog 延后处理其 Record 与布局 | 常规 logger::log_ 在入队前 vformat_to，后台再处理 sink pattern | QLog Producer 只编码；Backend按BQLog UTF-8顺序扫描，正文每 Record 只渲染一次 |
| 文件错误 | 文件缓存保留未写部分；ENOSPC 拒绝新条目并在后续 flush 尝试恢复；其他错误可能重新开文件 | file_helper 使用 stdio，write/fflush/fsync 错误向上报告 | ENOSPC保存后缀并周期重试；其他永久write错误报告缓存损失并自动开新编号文件；恢复自动进行 |
| 资源语义 | reset 不是完整外部 I/O 回滚事务 | file open 有重试等不同合同 | 准备失败旧配置保留；切换阶段旧 I/O 失败仍继续，返回 applied_with_io_error；不承诺文件回滚 |

源码链接：[BQ reset](https://github.com/Tencent/BqLog/blob/60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9/src/bq_log/log/log_imp.cpp)、[BQ worker](https://github.com/Tencent/BqLog/blob/60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9/src/bq_log/log/log_worker.h)、[BQ API 唤醒](https://github.com/Tencent/BqLog/blob/60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9/src/bq_log/api/bq_log_api.cpp)、[BQ 文件故障](https://github.com/Tencent/BqLog/blob/60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9/src/bq_log/log/appender/appender_file_base.cpp)。

spdlog 对照：[队列](https://github.com/gabime/spdlog/blob/v1.15.3/include/spdlog/details/mpmc_blocking_q.h)、[线程池](https://github.com/gabime/spdlog/blob/v1.15.3/include/spdlog/details/thread_pool-inl.h)、[Producer 格式化](https://github.com/gabime/spdlog/blob/v1.15.3/include/spdlog/logger.h)、[sink 保护](https://github.com/gabime/spdlog/blob/v1.15.3/include/spdlog/sinks/base_sink-inl.h)、[文件辅助](https://github.com/gabime/spdlog/blob/v1.15.3/include/spdlog/details/file_helper-inl.h)。

判断：QLog保留SPSC与Producer只编码，参考BQLog的压力唤醒/公共后台/批处理。低占用不进入worker mutex，高压力可进入；第1项配置交接仍不用对象管理锁。这是成本来源分析。不能推出 QLog 吞吐一定更高：BQLog 的自适应队列可节省低频线程内存，共享 worker 可节省线程，已有成熟转换/批输出实现。QLog 仍要复制 format/arguments、扫描多个 SPSC，后台及磁盘可能成为瓶颈。

## 2. 管理交接最终方案

单管理线程负责 reset/完成结果读取。业务 try_log 不绑定线程，仍按 ADR-013 自动 TLS。
V1 使用一个未完成请求的 mailbox，而不是实现通用 MPSC 或多槽队列：empty → pending → completed → empty。
该选择适合低频配置；邮箱忙立即返回 busy，已完成但尚未领取也不能覆盖。无需无界请求表、future、共享引用计数或运行期节点回收。
管理线程在发布前校验、分配新batch、打开新文件、准备目标映射与结果存储；失败不改变生效配置。提交后调用负责worker的awake；邮箱仍无锁，允许等待/唤醒mutex不等于配置列表改用锁。
Backend 无持有 Frame 时接收所有权；兼容目标复用，过滤/时区调整不重开文件、不清空缓存。
请求编号用 std::uint64_t，0 无效，耗尽拒绝，不添加类型别名。完成响应包含 applied 与分阶段 I/O 错误，随后由管理线程回收已关闭的退休对象。
管理方法不可重入；控制权只可在外部同步后转交其他线程。不会靠一个忙标志把多线程调用伪装成 SPSC。

## 3. 文件故障按BQLog分类自动恢复

用户本轮明确要求第2项对齐BQLog，覆盖上一版“永久错误后必须手动reset”。
ENOSPC：保留batch未写后缀、拒绝新行、每个恢复周期重试原fd；完全写清后恢复接收。QLog将EDQUOT也归容量耗尽组，这是额外明确的扩展。
其他永久write错误：BQLog会调用open_new_indexed_file_by_name；本地实现先close并clean_cache_write，所以不能宣称这类错误仍保留全部缓存。QLog同样报告并清理剩余字节，自动尝试新编号恢复文件；已write前缀不重放，旧文件可能有部分行尾。
新文件使用base_path.recovery.index，O_EXCL避免覆盖；每次最多16个重名尝试，失败后100ms恢复周期再试。命名未复制BQLog日期/保留策略，V1不因此扩展完整滚动归档/mmap恢复。
EAGAIN/EWOULDBLOCK使用同fd延后重试。sync失败单独报告durability_uncertain，不禁止普通write，也不据此清缓存或换文件；下一次显式durable请求重试sync。
第1项reset冷准备/兼容复用/应用反馈仍有效；兼容更新保留恢复状态与缓存。recreate_all可主动替换，自动恢复不依赖它。
retire/shutdown最多一次最终flush过程，不等磁盘修好，不创建新恢复文件；丢弃字节明确报告。默认正常flush与recovery检查间隔100ms，flush不等于fdatasync，成功close不等于durable。
所有输出/重开都在release Frame并publish回收之后。固定batch防止故障扩容，慢I/O仍会影响同worker其他目标。

## 4. 后台对齐共享/独立、定时等待、低空间唤醒

用户本轮要求第3项对齐BQLog，替换上一版“每Logger专属线程+50µs轮询+Producer永不通知”。
新增ThreadMode::async（默认公共worker）/independent（私有worker）；一个Logger仍按线程分配SPSC，不在V1引入共享MPSC或同步日志模式。
拆分BackendSession（每Logger的目标/管理/消费状态）、BackendWorker（线程/解码工作区/scratch）、BackendRuntime（public worker与稳定注册节点）。管理邮箱仍每Logger单槽，Logger shutdown在共享模式只排空并detach，不能停止public worker。
Context和Session只有worker明确放下借用、发布detach或独立worker join后才能回收。Runtime注册节点CAS只增，附着/移除由worker私有活动链处理，Runtime节点在全局worker退出后释放。
无积压时默认66ms定时等待；低空间、full、管理/注册/stop事件可提前唤醒。各Session按有限配额轮转，仍有积压继续工作，避免一个Logger独占共享worker。
BQLog low_space参考值是近似半容量；QLog在commit后做本地近似占用比较，达到一半则notify；full时notify后仍返回full，不循环reserve。
正常低占用路径只多本地比较；高占用/full路径调用BQLog风格awake：waiting.exchange(false,relaxed)，原值true才取得worker mutex并通知CV。该路径允许共享RMW和短暂等待mutex，明确修订旧“任何Release Producer路径都无锁/无共享RMW”的绝对承诺。
用户追加明确要求同步原语也对齐BQLog：采用std::mutex+std::condition_variable和waiting标志，不采用futex候选。等待mutex只用于worker等待/唤醒；另按ADR-017允许后端共用Console输出mutex。两者均不覆盖Ring、Context注册、Appender配置交接、格式化和文件I/O。
worker在同一mutex下置waiting=true并进入wait_for，awake命中true后持同一mutex通知，避免置标志到实际等待窗口丢唤醒；在置标志前的通知由有限周期tick兜底，不承诺即时响应。default Runtime冷初始化与系统调用不声称严格lock-free。
定时参数是参考默认，真实延迟取决于tick/调度/I/O。共享worker省线程/工作区，慢目标可拖慢同worker其他Logger；独立模式隔离worker但不隔离磁盘。

## 5. 保留合同、差异与实施状态

第1项管理方案不变；Appender虚基类、Config独立、空配置Console、多目标处理时过滤、固定SPSC/自包含Record均保留。
每worker拥有32槽解码工作区及正文/完整行各64KiB工作区，不设FormatCache；每Record正文一次渲染，按目标时区生成完整行。时间戳与输出大小合同不变。
Debug内部计数，普通Release移除统计更新；恢复/唤醒功能字段Release保留。BackendWorkspace不存跨release的Ring视图。
不复制BQLog的管理锁、默认block/expand、同步模式、信号强制flush、线程watchdog重启和完整文件保留策略；V1仍drop_new、无NullAppender、无V2自适应。用户允许等待/唤醒锁及ADR-017的后端Console输出锁，不能把第1项管理协议改成锁保护。
源码对齐事实：BQLog ENOSPC保缓存；非ENOSPC重开可能清缓存；默认async共享worker与independent模式；mutex/CV、waiting标志、66mswait、100ms刷新检查；低空间/失败唤醒。QLog保留适配：单槽管理、有界公平配额、单调deadline、简化恢复文件命名与明确错误结果。
详细字段、函数、握手、detach和故障分支见[V1收尾指南](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md)。本轮仅文档重规划，没有生产修改、构建、测试或性能验证。


## 2026-09-16 R3（替换修订）：平铺配置 + 枚举选择 + 运行期继承

用户最终确认采用平铺 AppenderConfig：name/type/enabled/filter/text/console/file，替换前次 variant 配置组合方案。
保留 AppenderType::Console/TextFile；不引入 AppenderCommonConfig/AppenderTargetConfig 或动态 property_value 树。配置值不使用继承、不持有运行资源。
公共字段始终校验；type 为 Console 时仅校验并使用 console，忽略 file；type 为 TextFile 时仅校验并使用 file，忽略 console；非法 type 在准备阶段拒绝。
未选中字段允许保留配置值，不产生资源，也不影响创建或兼容判断。类型与专用字段的有效组合由校验和解析规则保证。
Appender 保留抽象虚基类、虚析构、公共非虚控制入口和受保护虚输出扩展点；ConsoleAppender/TextFileAppender 继承 Appender，BackendSession 以 vector<unique_ptr<Appender>> 独占持有。FileAppenderBase 仅在复用文件行为需要时引入。
工厂在冷路径按 type 创建派生运行对象；reset 按 name 匹配，比较 type、text.batch_bytes 和对应 file.path/console.stream 决定兼容复用；未选中字段不参与比较。过滤/时区/周期等兼容更新沿用原有规则。
空列表规范化为 name="console"、type=AppenderType::Console，其余字段使用默认值；等级合并读取 filter.levels，disabled 仍参与。
完整类型、示例及实施步骤见 [V1收尾指南§1.1](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md#11-appenderconfig)。生产字段补齐和调用点修正仍由维护者实施。本次仅替换文档，不构建、不测试。

## 2026-09-16 R4：配置模块目录与冷路径复用

按用户要求，内部配置声明固定放 `include/qlog/detail/logger_config.hpp`，唯一实现放 `src/logger_config.cpp`；本轮新增头文件不放src。LoggerConfig仍在 `include/qlog/async_logger.hpp`，内部声明头只前置声明它。
选择普通非模板函数的声明/实现分离：构造与reset共用qlog::detail接口及同一套原地规范化/校验；各值准备入口仅复制原始输入一次，后续按既定所有权准备必要快照。
BQLog依据：appender_base::init/reset共用set_basic_configs；log_imp::reset_config按名字复用；refresh_merged_log_level_bitmap冷路径预合并；log::is_enable_for逐条只检查位图与类别。QLog沿用这些机制，保留单槽邮箱与Backend独占，不引入BQLog的管理锁。
构造与reset各合并一次等级位图，Backend应用时只发布准备值；Producer不调用配置helper。头文件位置/inline不是吞吐保证，性能仍按集中验收实测。
当前src/logger_config.cpp与src/async_logger.cpp内容相同，尚未完成拆分；维护者必须先清除前者的重复Logger实现、修改调用点，再接入CMake，不能把文件存在当成实现完成。
详细签名、文件责任、调用链、失败边界及顺序见[收尾指南§1.4](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md#config-module)。本次仅文档修订，不修改生产源码、不构建、不测试。
