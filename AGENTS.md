# QLog 工作约定

- 实际生产源码、测试、构建与权威设计文档位于 /home/qq344/QLog。
- Windows 入口：\\wsl.localhost\Ubuntu\home\qq344\QLog。
- E:\VisualStudioProject\BqLog 是 BQLog 参考和历史暂存目录；不得将其中的 QLog 快照当作当前生产仓库。
- 执行前检查实际仓库路径、git status 和最新验收报告；设计/计划中的旧进度不能覆盖较新的明确验收决定。
- 对未明确或相互冲突的设计，先给出问题、建议、语义/生命周期/成本影响，与用户商量后再冻结；草案不能作为已接受合同。
- 用户仅要求浏览文档与制定计划时，不据此开始生产代码开发。
- 当前阶段以 docs/decisions/I1D_ACCEPTANCE_20260913_CHS.md 为准：I1 已按 WSL2 开发范围收口，原生 Linux 发布复核与自动 CI 尚未完成。
- 当前 I2 Producer 合同：docs/decisions/ADR-013-v1-automatic-producer-context.md；覆盖 ADR-011 的显式绑定与注册锁方案。
- 执行计划：docs/decisions/MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md；动手指南：docs/decisions/MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md。
- I2 原草案仅保留讨论历史；不要重新采用旧 move-only、只读过滤或 Release 常驻统计候选。
- I2 生产骨架已开始，尚未完成；文档或骨架编译通过不等于生产实现/验收完成。

- 多 Appender 合同见 docs/decisions/ADR-012-v1-multi-appender.md；文件 reset 的 I/O 失败策略在 I4 前先讨论。

- 2026-09-15 用户接受 AsyncLogger::try_log + TLS 自动 ProducerContext；首条通过过滤可分配，稳态不分配。注册无 mutex/spinlock，CAS 只增链，停止全部使用者后回收。
- public BindResult/ProducerHandle 不再扩展；当前实施仅看新版主指南，显式绑定归档不是生效步骤。
- Appender 无锁配置命令交接仍待商榷，不把单管理线程/SPSC 命令候选当成用户已确认。

## 2026-09-16 V1 收尾覆盖决定

- 用户授权源码比较后直接决定最终方案，无需再确认三项候选；以 docs/decisions/ADR-015-v1-backend-control-and-output.md 为准。
- 单管理线程、单槽无锁请求/响应邮箱；Backend 独占活动 Appender；配置冷准备、兼容复用、明确文件故障与关停结果。
- 当前完整实施入口 docs/decisions/V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md；上文管理协议/文件故障“待商榷”已被此决定覆盖。
- 生产仍由维护者实现，本轮只交付文档且不运行测试；实现补齐后集中验收，不标记 V1 已验证完成。

## 2026-09-16 R2：第2/3项对齐BQLog

- 用户明确要求文件故障自动恢复、默认共享/可选独立worker、低空间/失败唤醒对齐BQLog，并明确采用mutex/CV；不采用futex候选。
- 允许worker等待/唤醒的mutex；低空间/full Producer分支可能exchange并短暂拿锁。此前全路径无锁/RMW/通知的绝对表述被覆盖，Ring与Context注册仍保持原协议。
- 第1项单管理线程/单槽邮箱保留；共享Logger关停采用排空并确认Session解绑，不join公共worker。
- 文件ENOSPC保留后缀并周期重试；其他永久write错误报告缓存损失并开新编号恢复文件。详见ADR-015 R2与同一V1收尾指南；本轮仍只写文档、不测试。

## 2026-09-17：format优先对齐BQLog当前UTF-8实现

- 用户本轮明确要求先看BQLog源码、format对齐且冲突以对齐优先；格式唯一合同为 docs/decisions/ADR-016-v1-bqlog-worker-format.md。
- Producer只保存原始格式字节并copy/hash，不解析brace；worker按BQLog UTF-8状态机处理，零参数原样。旧strict FormatPlan/FormatCache与严格字段数量规则退出V1必做项。
- 保留I1 wire/hash、ADR-013 TLS、ADR-014数组入口及ADR-015共享/独立worker、管理邮箱、mutex/CV和文件恢复；数值/容量安全适配按ADR-016明确边界。
- 当前总入口 docs/decisions/V1_REMAINING_IMPLEMENTATION_GUIDE_CHS.md，详细实施仍见已同步的V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md。
- 当前诊断代码部分写入且有三处静态接线问题；本轮只改文档，不修改生产或运行测试，不把历史配置验证当当前全树验收。

## 2026-09-19：正文formatter已实现并验证

- 用户已明确授权“你来补全优化”；此前日期段落的“本轮只写文档/不测试”描述其历史轮次，不限制本次授权实现。
- src/text_formatter.cpp的render_message_utf8与匿名scanner/spec/15种tag/padding已补齐，CMake与formatter测试已接入。FormatSpec命名已统一。
- 当前B0三处历史编译问题已修复，完整Debug/Release/ASan+UBSan回归及正文专项验证见docs/decisions/V1_FORMATTER_IMPLEMENTATION_REPORT_20260919_CHS.md。
- 正文验证限WSL2；compose_line、实际worker/Appender与管理闭环仍待完成。不得把本次结果写成整个V1已实现或已获端到端性能验收。

## 2026-09-19：Producer诊断专项收口与完整V1后续范围

- 用户授权完成诊断专项，并要求后续保持完整V1功能，已接受WSL2开发验收与原生Linux发布验证分别记录及基于代表负载的性能验收条件。
- B专项已完成：Debug/Release × AUTO/ON/OFF六配置、八个返回点、内部故障分支、跨TU、并发计数守恒、关闭时字段/符号/调用移除；证据见docs/decisions/V1_DIAGNOSTICS_ACCEPTANCE_20260919_CHS.md。
- 后续按docs/decisions/V1_FULL_COMPLETION_EXECUTION_PLAN_20260919_CHS.md覆盖完整行、输出、管理、共享/独立worker、生命周期和完整验收；不缩成仅compose_line或演示版，不重复确认已接受架构。
- 现有诊断/full探针为无消费者测试；接worker时迁为受控fixture，同时增加真实消费和安全shutdown测试。


## 2026-09-19：五项确认及flush最新更正

- 用户要求详细、代码级、按依赖顺序的完整V1指南；当前唯一后续编码入口为docs/decisions/V1_REMAINING_CODE_IMPLEMENTATION_GUIDE_CHS.md。旧收尾/剩余/执行计划只保留历史参考，不混用旧接口。
- 五项先按推荐接受，随后用户明确要求flush对齐BQLog；以ADR-017-v1-output-and-completion.md为准：一次flush内持续短写、write EINTR重试，无调用/字节预算或跨轮FlushCursor；Linux fdatasync一次，write零进展保留后缀并如实报告未完成。
- 允许额外的后端共用Console输出mutex；保留文件/Console durable差异、同路径多写入者限制、当前操作错误与历史损失分离。
- 本次只交付合同和代码级指南，不把文档或参考算法检查宣称为Backend生产实现、线程验证或V1性能完成。

## 2026-09-20：Appender 输出层实现更新

- 用户明确授权检查当前修改，对指南模糊导致的未实现内容直接补齐代码。
- OutputBatch/IoReport/Appender/ConsoleAppender/TextFileAppender/冷工厂已实现并接入 CMake；函数体不再含聊天占位 helper。write 命名与 BQLog 对齐的 flush 循环保留。
- 当前完整行 compose_line 尚未进入生产；不要修改 render_message_utf8 六参数正文 API。下一步看 docs/decisions/V1_APPENDER_NEXT_IMPLEMENTATION_GUIDE_20260920_CHS.md，含完整可追加参考代码。
- Session 每次目标操作后 capture_first_error 到同一个历史汇总，closed 后 merge_history_into；后台周期 report 与管理请求 report 分开。
- 本轮证据见 docs/decisions/V1_APPENDER_IMPLEMENTATION_REPORT_20260920_CHS.md；单线程人工驱动输出验收不等于真实 worker/公共管理 API/生命周期或 V1 端到端性能完成。

## 完整行与 OS tid 复核更新

- compose_line 已进入生产；修复头声明 std::yte 拼写和 make_context 重复定义，保留冷路径 gettid。新增 tests/compose_line_test.cpp。
- 下一步为 docs/decisions/V1_CONTROL_PREPARATION_NEXT_GUIDE_20260920_CHS.md；不要重复粘贴旧行层参考代码，不提前启动 worker。
- 复核证据见 docs/decisions/V1_COMPOSE_REVIEW_REPORT_20260920_CHS.md；不视为异步端到端或 V1 已完成。

## 2026-09-20：非 worker 实现与用户分工

- 用户要求 Codex 实现剩余非 worker 部分，worker 线程交给用户；不得擅自把 manual test driver 作为生产线程。
- PreparedReset、ControlMailbox、ManagementController、BackendSession 与 AsyncLogger 管理/shutdown 已接线；真实 WorkerProvider/Attachment 实现仍待用户完成，未安装 provider 时构造明确抛错。
- 当前唯一入口仍为 V1_REMAINING_CODE_IMPLEMENTATION_GUIDE_CHS.md，内容已按实际接口重写；worker 细节见 V1_WORKER_HANDOFF_20260920_CHS.md。历史 PRE_SESSION_ARCHIVE 不作编码依据。
- Debug/ASan+UBSan 各 204 通过，Release 203 通过+1 跳过，六诊断配置通过；这是受控驱动验收，不等于真实异步或 V1 性能完成。

## 当前授权更新：完整 V1

用户已明确改为由 Codex 补全整个 V1 并最终性能测试，覆盖此前 worker 保留给用户的分工。继续实现真实线程、验收与性能测试，不再受历史交接边界限制。

## V1 Runtime 已进入生产

- 真实共享/独立 worker 位于 src/backend_runtime.cpp，默认 provider 自动安装；不再要求用户完成 worker。
- Runtime 节点 CAS 发布，worker 私有活动链，节点在 Engine join 后回收；等待 mutex 不保护登记链或 Appender 配置。
- 当前入口 README.md 与 V1_REMAINING_CODE_IMPLEMENTATION_GUIDE_CHS.md；最终验收/性能文档分别记录 WSL2 和原生发布证据，不把 CI 配置文件当远程运行成功。
