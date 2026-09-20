# ADR-017：V1 输出与完成结果（flush 对齐 BQLog）

- 日期：2026-09-19。
- 状态：已接受。用户先确认“五项按推荐”，随后明确更正“flush需要更正和BQLog对齐”。本修正版撤销暂存草案的flush预算。
- 范围：补充并覆盖 ADR-015 的锁范围、flush 循环与完成结果边界；不改变 ADR-016 的格式语义。
- 唯一后续编码入口：[V1 剩余代码级实施指南](./V1_REMAINING_CODE_IMPLEMENTATION_GUIDE_CHS.md)。
- 本次是合同与指南交付，不表示 Backend 已实现或性能验收已通过。

## 1. Console 输出锁

允许所有 QLog ConsoleAppender 共用一个后端输出 mutex。它只覆盖一次Console输出操作，不覆盖 Record 借用、格式化、文件 I/O、配置邮箱；Producer 不获取此锁。worker 等待/唤醒 mutex 的既定例外保留。

使用明确的指针与字节长度输出，包含 NUL；不照搬 BQLog 的 C 字符串长度逻辑。锁能串行化 QLog 的单次 Console 输出服务，但不保证跨短写调用、错误、其他进程或外部写入者的整行原子性。慢 Console 仍会阻塞其他 Console worker。

## 2. flush 按 BQLog 一次调用推进

缓存flush内部持续推进短写，write遇EINTR立即重试，直到写完、实际非EINTR错误或write返回0。取消调用次数/字节预算、yielded和跨轮FlushCursor；消费配额和恢复文件名搜索上限仍保留。

write返回0且有后缀时保留后缀并报告incomplete，system_error=0、unwritten_bytes>0；不伪造errno，不声称完成，不因此换文件。Linux持久化同步按BQLog flush_file执行一次fdatasync，失败包含EINTR均报告，下一次显式durable可重试。buffered/cache flush不等于durable。

retire/shutdown调用一次最终flush过程，内部仍按上述循环；实际失败/零进展后不等待磁盘恢复、不创建恢复文件，报告并丢弃实际剩余后缀。没有“健康输出达到预算也丢弃”的规则。阻塞I/O/持续write EINTR不受硬时限约束，不允许超时后分离仍借用Session的线程。

## 3. durable 的目标范围

TextFile 的 buffered 表示本次目标字节完成 write；durable 还要求本次成功 fdatasync。Console 两种模式均只要求输出调用完成，不承诺持久化；重定向 stdout 也不改变 Console 类型语义。混合目标 durable 成功表示 Console 输出完成、TextFile 同步成功，不因存在默认 Console 而返回“不支持”。

## 4. 同路径多个写入者

继续允许不同 Appender 名称使用同一路径。V1 不增加全局文件注册表、共享 FileSink 或跨 worker 文件锁。独立 Appender、不同 worker 和外部进程并发写入时不保证整行不交错；同一 worker 的不同批次也不因此获得全局日志顺序。

需要单一逻辑记录写入边界时，使用一个 Appender 写入所有者，并排除外部并发写入者。保留每个 Producer Channel 的 FIFO 消费，不承诺跨 Producer 时间戳排序；多个目标的实际输出完成顺序还受过滤和 I/O 恢复影响。

## 5. 当前操作与历史错误

ManagementCompletion.errors 包含本请求期间遇到的错误，以及本请求无法完成时仍未解除的目标故障。已恢复的旧错误不污染后续成功请求。同一请求内发生后恢复的错误仍属于本请求，不能抹去。

历史累计事件数、实际丢弃字节与首错单独保存；目标退休时合并，最终 shutdown 合并所有剩余活动目标，放入已有 retired_io。重复 ENOSPC 重试可以产生多个实际错误事件，但相同保留后缀不是多次丢失；lost_bytes 只在实际丢弃时增加一次。write零进展不是操作系统错误事件。

错误记录采用每目标、每阶段固定槽，冷路径预分配字符串和容器；不递归写日志、不调用任意用户回调、不保存无界错误历史。

## 6. 源码参考及取舍

BQLog 固定本地提交：`60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`，不声称为远端最新版本。

| BQLog 源码锚点 | 本次取舍 |
|---|---|
| `src/bq_common/utils/util.cpp::_default_console_output`，桌面分支 `console_mutex_` | 参考共用 Console 锁；保留 QLog 明确字节长度 |
| `src/bq_common/platform/posix_misc.cpp::write_file` | 对齐短写推进、write EINTR重试和write返回0退出；不增加flush预算 |
| `src/bq_log/log/appender/appender_file_base.cpp::flush_write_cache` | 保留 ENOSPC 后缀及永久错误恢复思路，明确损失和最终关闭 |
| `src/bq_log/log/log_imp.cpp::flush_appenders_io` | 参考文件目标执行持久化同步，明确 Console 完成边界 |

同路径写入和错误历史是 QLog 明确补充的合同，不宣称 BQLog 提供同样的公开结果模型。
