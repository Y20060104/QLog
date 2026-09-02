# 读侧 R1 实验记录：pending 校验仅保留在 Debug（未通过）

> 状态：2026-09-02 完成验收，实验未达到性能门槛并已回退；本文仅作为实验记录保留。

- 状态：当前唯一生产代码任务
- 日期：2026-09-02
- 所属里程碑：里程碑一最终性能收口
- 生产代码负责人：你
- 测试、汇编、benchmark 和保留/回退决策：Codex
- 依据：ADR-005、ADR-006、V1 决策 27/35/36

## 目标

只验证一个假设：`read_pending_` 的每条记录检查、置位和清零是否构成可测的 Release
读侧开销。

本轮不追求一次消除全部 18% 差距，也不修改批量回收策略。实验完成前不进入
`RecordHeader`，不加入 spdlog async benchmark。

## 为什么先做这个实验

当前 1 MiB Ring、64B payload、7 次重复的 WSL2 开发矩阵中：

```text
prefilled-read：QLog 37.74M/s，BQLog 46.15M/s
```

Release 对象的代码体积：

```text
QLog try_read      194B
QLog release       138B
BQLog read_chunk   122B
BQLog return        62B
```

两边 ReadHandle 都是 16B，四个函数都没有函数调用、锁或原子 RMW。QLog 成功路径额外
执行：

```text
try_read：检查 read_pending_，成功后写 true
release：写 false
```

这些操作只用于发现 API 误用，不参与 SPSC 的 payload 可见性和游标发布。Producer 依赖
`reservation_pending_` 防止覆盖尚未提交的写预留，因此写侧 pending 不是本次实验对象。

## 实现前浏览顺序

按以下顺序重新读一遍，每个文件只看指定内容：

1. `include/qlog/detail/spsc_ring_buffer.hpp`
   - `ReadHandle`；
   - `ReaderState`；
   - `ReadStatus::read_pending`。
2. `src/build_config.hpp`
   - 确认 Debug 为 `QLOG_ENABLE_RING_VALIDATION=1`；
   - Release 为 `0`。
3. `src/spsc_ring_buffer.cpp`
   - `abandon()`；
   - `try_read()`；
   - `release()`。
4. BQLog `siso_ring_buffer.cpp`
   - `read_chunk()`；
   - `return_read_chunk()`；
   - 观察其 waiting-for-return 检查只存在于 Debug 宏中。

读完后应能回答：为什么 `read_pending_` 可以是 Debug 契约检查，而
`reservation_pending_` 目前仍必须保留在正常写入语义中？

## 只修改一个生产文件

```text
src/spsc_ring_buffer.cpp
```

不要修改头文件、Handle 布局、状态枚举或测试。

把以下三类操作放进已有的 `#if QLOG_ENABLE_RING_VALIDATION` 条件：

1. `try_read()` 开头的 `reader_state_.read_pending_` 检查和
   `ReadStatus::read_pending` 返回；
2. `try_read()` 成功路径设置 `reader_state_.read_pending_ = true`；
3. `release()` 与 `abandon()` 中清除 `reader_state_.read_pending_`。

`ReaderState::read_pending_` 字段继续保留。这样 Debug 仍能发现重复读取，Release
不为误用检查付费，也不会改变四缓存行布局。

## Release 契约变化

修改后：

- Debug 中第二次未归还的 `try_read()` 仍返回 `read_pending`；
- Release 中，成功 Handle 仍然只能逻辑终结一次；
- Release 中重复 `try_read/release`、重复终结、跨 Ring 终结继续属于底层契约违例，
  不保证返回 `read_pending` 或自动修复状态；
- `ReadStatus::read_pending` 继续保留，服务 Debug 和稳定的枚举定义。

这不是删除 SPSC 同步。Producer release-store、Consumer acquire-load、当前读游标、缓存
写游标和回收游标发布全部保持不变。

## 严禁顺手修改

- `FrameHeader` 或 `FrameLayout`；
- `ReadHandle` 的 16B 布局；
- `records_since_publish_` / `bytes_since_publish_`；
- 32 条、4 KiB、快照耗尽三个发布条件；
- `current_read_cursor_` / `cached_write_cursor_`；
- acquire/release 内存序；
- `publish_reclaimed()`；
- Writer 的 `reservation_pending_`；
- branch hint、prefetch、强制内联或新的状态字段。

本轮若同时修改这些内容，就无法判断收益来自哪里。

## 你完成后的交付

完成生产文件后只需要通知 Codex，不要修改测试，也不要先提交。

请同时用自己的话回答三点：

1. 为什么 Release 省略 `read_pending_` 不会破坏 payload 的跨线程可见性？
2. 为什么不能同时删除 Writer 的 `reservation_pending_`？
3. 这项改动牺牲了哪一种错误检测能力？

## Codex 验收

Codex 负责：

1. 调整测试分层：Debug 验证 `read_pending`，Release 不依赖误用检测；
2. 运行格式、Debug、Release、ASan/UBSan；TSan 仍在原生 Linux/CI 补跑；
3. 检查 Release 汇编，确认 pending 的比较和两次写入消失；
4. 重跑相同 `prefilled-read` 与 64B `transfer-retry`，报告中位数和 MAD；
5. 检查其他 Geometry 和数量守恒没有回归。

## 保留或回退标准

只有满足全部条件才保留 R1：

- Debug 仍能抓住第二次未归还读取；
- 正确使用路径的全部测试和 Sanitizer 通过；
- `prefilled-read` 中位数改善至少 3%，且改善大于噪声/MAD；
- `transfer-retry` 没有超过 3% 或合并 MAD 范围的退化；
- Release 汇编确实删除目标指令，没有通过别的路径重新引入。

若改善小于 3% 或不可稳定复现，就回退 R1。为了不足 3% 的收益削弱 Release 诊断不值得。

## 2026-09-02 验收结果

### 正确性与工具门禁

- `format --check` 与 `git diff --check` 通过；
- Debug：47/47 通过，仍能检测第二次未归还读取；
- Release：44 项通过，3 项仅 Debug 的误用/损坏检测按实验设计跳过；
- ASan/UBSan：47/47 通过，无内存错误、泄漏或未定义行为报告；
- TSan 不在 WSL2 上作为有效门禁，仍需原生 Linux/CI 补跑。

### Release 汇编

- `try_read()` 从 194B 减少到 172B；
- `release()` 从 138B 减少到 130B；
- 两个函数均不再访问原 `read_pending_` 所在偏移 `0x118`。

这证明目标指令确实被删除，但代码尺寸变小本身不能替代吞吐量验收。

### 性能结果

环境为 WSL2、Intel i7-9750H、GCC 13.3.0；QLog 与 BQLog 工作区均为 dirty，
因此跨时段结果标记为 `NON_REPRODUCIBLE`。

完整 7 轮 after 测量：

- `prefilled-read`：QLog 55.47M/s，MAD 0.54M；BQLog 62.45M/s，MAD 1.88M；
- 64KiB/64B `transfer-retry`：QLog 13.98M/s，MAD 1.40M；BQLog 12.49M/s，MAD 0.53M；
- transfer 的 14 个样本全部有效，`dropped_full`、`unexpected_failures`、
  `validation_failures` 均为 0。

由于跨时段绝对频率漂移很大，又构建了一个只恢复 Release `read_pending_` 三处热路径操作的
临时 before 版本，使用相同 benchmark 做同场复核：

- 64KiB/64B transfer：before 13.94M/s（MAD 0.47M），after 15.00M/s
  （MAD 2.61M）。方向为 +7.6%，但差值处在合并 MAD 内；结论只能是没有发现回归，
  不能声称获得稳定收益；
- 1MiB/64B 纯读按 before/after 成对交替 7 次：before 50.13M/s（MAD 2.93M），
  after 50.51M/s（MAD 4.88M）。改善只有 **0.77%**，低于 3%，并且完全落在噪声内。

### 决策

R1 **未通过** `prefilled-read` 的预设门槛，因此不保留。下一步应恢复 Release 中：

1. `try_read()` 的 pending 前置检查；
2. 成功读取后的 `read_pending_ = true`；
3. `release()` 与 `abandon()` 的 `read_pending_ = false`。

完成回退后，Release 测试也应重新要求 pending 契约，而不是跳过对应测试。随后重新跑
Debug、Release 与 ASan/UBSan，确认回到实验前基线。R1 回退完成之前，不启动 R2。

### 回退收口结果

2026-09-02 已完成回退与最终门禁：

- `read_pending_` 的检查、成功置位以及 `release/abandon` 清零均恢复为 Debug/Release 生效；
- Debug 47/47 通过；Release 46 项通过，仅故意损坏 Frame 的 Debug-only 测试跳过；
- ASan/UBSan 47/47 通过；
- Release 汇编恢复为 `try_read()` 194B、`release()` 138B，并包含 `0x118` 上的 pending
  比较、置位和清零；完整 Header/Geometry 损坏校验仍未进入 Release；
- quick benchmark 的 10 个样本全部有效，`unexpected_failures` 与
  `validation_failures` 均为 0；非零 dropped 仅来自预期的 `drop_new` 场景。

R1 至此关闭，不再继续 R2。SPSC RingBuffer 的本地开发实现与门禁完成；原生 Linux
TSan 和 clean commit 正式矩阵保留为发布门禁。

## R1 之后

如果 R1 保留但读侧仍明显落后，下一轮才商讨 R2：批量回收判定的单计数器/发布预算
实验。R2 必须同时观察纯读与并发传输，因为 BQLog 每条发布读游标虽然纯读代码更短，
却可能增加 Producer/Consumer 间的缓存行争用。

R1、R2 都结束并完成原生 Linux 门禁后，才冻结 Ring V1 性能并进入 `RecordHeader`。
