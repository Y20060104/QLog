# QLog 完整 V1 收尾执行计划

> **2026-09-19 编码入口已替换：** 本文保留历史设计/进度，不再作为剩余 V1 的逐步编码指令。请按 [剩余代码级实施指南](./V1_REMAINING_CODE_IMPLEMENTATION_GUIDE_CHS.md) 的依赖顺序和唯一接口实施；五项决定及用户最新 flush 更正见 [ADR-017](./ADR-017-v1-output-and-completion.md)。旧接口片段不得与新指南混用。flush 对齐 BQLog：单次持续短写、write EINTR重试；取消暂存预算/跨轮续刷方案。历史验收结果的原始范围不变。

日期：2026-09-19。用户已同意验收条件并要求保持V1功能完整；当前先完成诊断专项，再确认后续开发范围。本文确认的是完整V1范围，内部按依赖顺序交付，不把范围缩减为只做compose_line或仅做独立worker演示。

## 起点和完成定义

Record Core、Ring、配置准备、Producer入口和正文formatter已有实现；正文专项已验证，Producer诊断B也已通过本次专项。不得重写已验收wire、恢复strict FormatPlan/FormatCache，或把前台花括号解析加回来。

本阶段的最终结果是：业务线程try_log → Ring → worker解码/一次正文格式化 → 每目标行组装 → batch → Console/TextFile真实输出，支持默认共享与可选独立worker、运行期Appender reset、flush/drain、故障恢复和安全shutdown。

验收分开报告：

1. WSL2开发验收：完整功能、单元/串联/并发/故障测试、适用的sanitizer及分层性能结果。
2. 原生Linux发布验证：在目标或可用原生Linux环境复核构建、并发、I/O和性能；不能用WSL2结果替代或提前打勾。
3. 性能以固定机器和代表负载衡量，记录前台分位数、后台持续服务率、drop、输出延迟和资源成本；性能目标在实际运行后结合目标负载确定，不预设“所有场景超过BQLog”。

## 交付1：完整日志行与输出基础

文件：`include/qlog/detail/text_formatter.hpp`、`src/text_formatter.cpp`；新增 `io_result.hpp`、`output_batch.hpp`、`appender.hpp`、`console_appender.hpp`、`text_file_appender.hpp` 及相应 src 实现。

- 实现CalendarCache和compose_line：固定前缀、每目标固定时区、9位纳秒、不可用时间、时间倒退、原样名字字节、最终换行。
- 对前缀+正文+换行执行64KiB总限制。正文失败影响本条所有目标；单目标前缀/容量失败只影响该目标。
- 固定容量batch，区分接收、write、durable。支持部分写入、EINTR、延后重试；不重放已成功写入前缀。
- Console/TextFile真实输出；ENOSPC/EDQUOT保留未写后缀并拒绝新行，恢复后重新接收；其他永久write错误按ADR-015记录丢失并尝试新编号恢复文件。
- open/write/sync/close均有明确结果；sync失败不等同普通write失败。关闭阶段不等待磁盘无限恢复，不创建新恢复文件。

验收：字节级完整行、时区/倒退/容量边界、Console捕获、真实临时文件、系统I/O故障注入及batch字节守恒。

## 交付2：管理邮箱与资源准备

文件：新增 `include/qlog/detail/control_mailbox.hpp`、`src/control_mailbox.cpp`、`src/appender_prepare.cpp`；复用已实现logger_config共享准备函数。

- 单管理线程、单槽请求/响应：empty → pending → completed → empty；未领取结果也占槽，busy不覆盖。
- uint64请求编号、耗尽处理、明确提交与完成状态。
- 冷路径校验/分配/打开目标；准备失败不替换活动配置。
- reuse_compatible保留兼容目标batch与恢复状态；recreate_all明确重建。
- worker只在无Frame借用时切换；退休对象有明确回收方；applied_with_io_error不伪装成完整回滚。

验收：busy、未领取、编号边界、准备失败、兼容复用、应用I/O失败和退休资源回收。

## 交付3：worker运行和完整生命周期

文件：新增 BackendSession/BackendWorker/BackendRuntime/WorkerWakeup 对应 detail头与src；修改 ChannelDependencies、ProducerContext接线、Ring的producer近似低空间查询和AsyncLogger构造/析构。

- 每Logger一个Session；默认共享public worker，可选独立worker。worker拥有32槽decode workspace及message/line scratch。
- 发现新Channel并按记录数/字节配额轮转；一个Logger/Channel不能持续独占公共worker。
- 每条Record只渲染一次正文；完成必要复制、release Frame并publish回收之后才进入潜在慢I/O。
- 默认66ms等待、100ms常规flush/retry检查沿用既定合同；低空间/full和管理/注册/stop提前通知。
- 普通低占用只做本地检查；压力唤醒可exchange并短暂使用mutex/CV；full不循环reserve。
- 在启用消费线程的同一次集成中完成shutdown必需的停止/排空/解绑/join/回收路径，不能保留当前直接delete Context的析构而启动worker。
- 共享Logger关闭只解绑自己的Session；独立Logger join自己的worker。worker失败后不无限等待正常完成信号，必须确认借用已释放再清理。

验收：单/多Channel、持续注册、共享/独立、多Logger干扰、公平性、唤醒窗口、虚假唤醒、失败退出、反复构造/关闭和析构安全。

## 交付4：公开API与真实示例

实现现有 request_reset_appenders、request_flush_batches、request_drain、poll_management、shutdown，统一FilterConfigAccess定义；构造发布顺序保证不暴露半对象。

- flush_batches仅冲已有batch，不作为跨Producer屏障。
- drain要求调用方先停止Producer，不堵住共享worker对其他Session的服务。
- shutdown完成既有管理请求、停止注册/唤醒、最终排空和flush/sync/close，重复调用返回保存结果；错误包含backend_failed/drain_incomplete等既定状态。
- 新增 `examples/v1_logging_demo.cpp` 和构建选项，真实展示Console+文件、共享/独立、运行期reset、停止Producer、drain/shutdown及结果检查。
- CMake纳入全部真实cpp并PUBLIC链接Threads::Threads，保留诊断宏跨TU一致性。

验收：完整程序实际输出文件，可重复运行和安全退出；所有公开声明有真实实现和链接覆盖。

## 交付5：完整验收和性能修正

- 保留Record/formatter/diagnostics既有回归。接worker时把当前无消费者full探针迁成可控fixture，同时新增真实消费者测试。
- Debug/Release、诊断模式、ASan/UBSan、适用环境的TSan，分别记录结果；TSan启动失败不能写成竞态检查通过。
- 故障注入覆盖short write、EINTR、EAGAIN、ENOSPC/EDQUOT、其他write/open/sync/close错误。
- 正常停止后核对Producer、processed、decode_failed、no_destination、dispatched及selected delivery等计数守恒。
- 分别测Producer、formatter、worker、单/多Appender、普通write与durable；报告accepted/full、持续processed、p50/p99/p99.9、输出延迟、CPU/RSS和write次数。
- BQLog比较固定版本、机器、输入输出字节、线程数、队列预算、worker模式、丢弃策略和持久化边界。浮点安全扩展单列，不把不同工作量放到同一结论。
- 热路径或服务率出现问题时先定位，再做有语义回归保护的优化；不以改变输出、漏记drop或暂存队列吸收率掩盖性能不足。
- 最后进行原生Linux发布复核并更新最终证据、限制和使用文档。

## 开发推进方式与需要用户介入的边界

交付1至5属于同一个完整V1收尾范围，内部按依赖拆分不是缩减功能。已确定的共享/独立拓扑、单槽邮箱、mutex/CV、自动恢复和format合同不重复请求许可。

真正出现与活动ADR冲突的语义/生命周期选择时，先提供具体冲突、选项与影响再讨论。目标机器/代表负载不足只影响最终性能门槛和原生发布验证，不阻塞行组装、输出与worker实现；在需要外部环境时明确列出缺口。

当前诊断专项已完成；紧接着开始交付1，并沿上述范围持续推进。本文是实施计划，未把尚未编写的模块标为完成。细节继续以 [V1收尾指南](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md)、ADR-015和ADR-016为准。
