# ADR-008：Unix Epoch 纳秒与 admission timestamp

- 状态：已接受（冻结 D3）
- 日期：2026-09-02
- 影响范围：QLog V1 `time_value`、`flags`、Channel 时钟描述、Producer 取时位置
- 前置决策：[ADR-007：32B 自包含 RecordHeader 与统一格式记录](./ADR-007-self-contained-record-header.md)
- 不改变：32B RecordHeader、每 `(线程, AsyncLogger)` 独立 SPSC、`drop_new`、Null/Text Sink

## 背景

`RecordHeader` 已为时间保存一个 64 位 `time_value`，并在尾部保留一个 8 位 `flags`。
D3 需要冻结四件事：时间值表示什么、Producer 从哪里取时、何时取时，以及取时失败如何编码。

QLog V1 面向人类可读的服务日志，不承诺跨线程严格全序，也不把日志时间戳定位为性能计时器。
因此，V1 优先选择直接可格式化为墙钟时间、无需后台校准且可由 Linux vDSO 加速的时钟路线，
不在本阶段自行实现 TSC 到墙钟时间的转换。

## 决策

### 1. `time_value` 的语义

`time_value` 固定表示：

```text
自 Unix Epoch 起的纳秒数
```

对应逻辑类型为无符号 64 位整数：

```text
domain = unix_epoch
unit = nanosecond
storage = uint64_t
```

“纳秒”是编码单位，不是精度承诺。默认时钟可能在多个连续事件中返回相同值，实际分辨率由
运行内核和 `clock_getres()` 的结果决定。

`time_value` 仅是一次墙钟观测值：

- 不用于计算请求耗时、代码段耗时、超时或性能指标；
- 需要计算耗时时，调用方必须使用单调时钟并把结果作为普通参数记录；
- 不保证相邻 Record 的数值严格递增；
- 不用于推导跨线程全局时间顺序；
- 每个 Channel 的 SPSC FIFO 才是线程内顺序权威。

V1 不把 `std::chrono::system_clock::duration::count()` 或其他实现相关 tick 直接写入 ABI。
生产实现必须明确调用 Linux clock id，并显式转换为 Unix Epoch 纳秒。

### 2. 默认时钟与 fallback

V1 默认策略为：

```text
primary  = clock_gettime(CLOCK_REALTIME_COARSE)
fallback = clock_gettime(CLOCK_REALTIME)
```

两者均属于 Unix realtime 域，并统一转换为 epoch nanoseconds，所以 fallback 不会改变
`time_value` 的单位或解释方式，也不需要校准点。

默认选择 `CLOCK_REALTIME_COARSE` 是面向文本服务日志的吞吐取舍。它通常比精细 realtime
读取更便宜，但有效分辨率由平台决定。V1 不承诺它能区分同一毫秒内的事件。

调用必须经 libc 的 `clock_gettime()`，不直接解析 vDSO，也不假定所有部署环境一定命中 vDSO；
是否发生 syscall fallback 只属于部署诊断和 benchmark 结果，不改变 ABI。

### 3. admission timestamp

D3 将时间采样点命名并冻结为：

```text
admission timestamp
```

其精确定义是：

```text
`try_reserve(exact_record_bytes)` 成功之后，开始写 Record payload 之前取得的时间值。
```

因此：

- filtered 事件不读时钟；
- metadata/类型/长度校验失败的事件不读时钟；
- too-large 事件不读时钟；
- `drop_new`/full 事件不读时钟；
- 只有已经获得 Ring 空间的 Record 才执行正常取时。

这个时间不表示日志宏开始执行的时刻，也不表示 `commit()` 完成的时刻。它表示该事件已经通过
前置准入并成功获得本 Channel Ring 空间的时刻。

### 4. `flags` 的 timestamp status

V1 使用 `flags` 的低两位保存 timestamp status，不使用 C++ bit-field：

| `flags & 0x03` | 名称 | `time_value` 语义 |
|---:|---|---|
| `0` | `primary_valid` | primary 成功所得的 Unix Epoch 纳秒 |
| `1` | `fallback_valid` | fallback 成功所得的 Unix Epoch 纳秒 |
| `2` | `time_unavailable` | 必须为 0；Record 其余内容仍可处理 |
| `3` | `reserved` | V1 Producer 不得生成；不承担 Record invalid 语义 |

常量约束：

```text
timestamp_status_mask = 0x03
known_flags_mask      = 0x03
flags bits 2..7       = 0
```

值 `3` 只为未来协议保留。V1 Decoder 遇到它时返回独立的
`reserved_timestamp_status`/unsupported 结果并安全结束当前 Record；不得把 `3` 定义或报告成
“Record invalid”标志。bits 2～7 中任何非零位属于 `unknown_flags`，与 reserved status 分开计数。

其他规则：

- `time_unavailable` 要求 `time_value == 0`；
- `fallback_valid` 只有在 Channel 描述了 fallback source 时才可接受；
- 静态/动态 format、字符串类型和截断状态不占用 D3 flags；
- Producer 正常 primary 路径写入的 `flags` 值为 0。

为保持 I1 Record Decoder 不依赖 Channel 类型，I2 从 Channel 时钟描述构造只读
`RecordValidationPolicy::fallback_timestamp_allowed`，I1 仅查询该纯值。`fallback_valid` 与 policy 不符时
返回独立的 `fallback_timestamp_not_configured`；policy 不携带 Channel 指针、回调或动态容器。

### 5. 取时算法

正常流程为：

```text
完成过滤、类型检查和精确长度计算
  -> try_reserve
  -> full: drop_new，不读时钟
  -> success:
       primary clock_gettime
       -> success: primary_valid
       -> failure: fallback clock_gettime
            -> success: fallback_valid
            -> failure: time_unavailable + time_value = 0
  -> 在栈上构造 RecordHeader，复制 format、编码 arguments，成功路径最后复制 Header
  -> commit
```

两级取时都失败时仍提交这条日志，不因为缺少时间而丢失其 category、level、format 或参数。
Text Sink 对该状态输出稳定占位文本，例如 `time-unavailable`，不得把数值 0 格式化成一条看似
真实的 1970 年日志。

下一条日志重新尝试 Channel 的 primary source。V1 不维护“永久切换到 fallback”的共享模式，
因此不需要 Producer 每条读取共享 generation，也不引入共享原子 RMW。

### 6. `timespec` 转换

只有同时满足以下条件才算一次有效取时：

```text
clock_gettime 返回 0
tv_sec >= 0
0 <= tv_nsec < 1'000'000'000
tv_sec * 1'000'000'000 + tv_nsec 可表示为 uint64_t
```

转换必须使用 checked multiply/add：

```text
time_value = checked_add(
    checked_mul(uint64(tv_sec), 1'000'000'000),
    uint64(tv_nsec))
```

primary 返回错误、非法 `timespec` 或转换溢出都进入 fallback；fallback 同类失败则进入
`time_unavailable`。

### 7. Channel 时钟描述

Channel 保存冷且不可变的逻辑描述；本 ADR 冻结字段语义，不要求立即冻结其 C++ 物理布局：

```text
clock_descriptor_version
domain = unix_epoch
unit = nanosecond
primary_source
fallback_source
primary_resolution_ns
fallback_resolution_ns
calibration_kind = none
sampling_point = admission_timestamp
```

Logger/Channel 冷路径使用 `clock_getres()` 探测和记录分辨率：

- coarse 与 realtime 均可用：primary=coarse，fallback=realtime；
- coarse 不可用但 realtime 可用：primary=realtime，fallback=none；
- 两者均不可用：配置为 unavailable-only，保留日志但不在热路径重复调用已知不可用的 clock id，
  并发出非递归冷路径诊断。

一个 Channel 生命周期内不修改 domain、unit、source 或 sampling point。所有 source 都产生同一
domain/unit，因此 Channel 不存在需要逐 Record 解释的混合时间域。

### 8. 墙钟回退与输出顺序

`CLOCK_REALTIME_COARSE` 和 `CLOCK_REALTIME` 会受到系统校时影响。QLog 必须保留实际取得的值：

- Producer 不 clamp；
- Backend 不改写 Record；
- Text Sink 不把较小值提升到上一个时间值；
- Backend 可以用 Consumer 私有状态统计单 Channel 的 `clock_regression_observed`；
- 多 Channel 输出采用 Backend 扫描顺序，不按 `time_value` 排序。

因此，文本中的时间可能相同、回退或在不同线程间交错。这不违反 V1 的 FIFO 契约，也不形成
跨线程全局时间顺序承诺。

### 9. ClockPolicy 边界

V1 保留可注入的 ClockPolicy 以测试所有分支。生产默认策略固定为上述 Linux realtime 策略，
不提供运行中切换 source 的公共配置。测试策略可以返回确定的 primary、fallback、unavailable
和非法 `timespec` 结果。

ClockPolicy 的热路径调用必须：

- `noexcept`；
- 不分配、不加锁、不阻塞；
- 不进行共享原子 RMW；
- 返回 `{time_value, timestamp_status}` 一类小型按值结果；
- 不拥有或引用 Ring payload。

## 不选择的方案

### 每条 `CLOCK_REALTIME`

它保留为 fallback 和 benchmark 对照。如果后续真实 workload 证明 coarse 分辨率不足，可以通过
新的决策把它提升为 primary；`time_value` 的 domain/unit 无需改变。

### `CLOCK_MONOTONIC`/`CLOCK_MONOTONIC_RAW` + 后台墙钟校准

该路线需要维护校准点历史并定义系统时间 step 时如何映射 backlog Record。V1 的文本日志不要求
用这份复杂度换取单调值，耗时计算也不使用 `time_value`。

### RDTSC/RDTSCP + 后台校准

该路线还需处理 TSC 能力探测、跨核/多路同步器偏移、线程迁移、指令排序、休眠、CPU hotplug、
虚拟化和 live migration。直接使用 Linux 时钟可以复用内核的 timekeeping/vDSO 机制，V1 不自行
重写这套转换。只有 benchmark 证明取时是 Producer P99 的主要瓶颈后，才允许建立独立实验 ADR。

### Backend clamp 或跨线程按时间排序

clamp 会掩盖真实墙钟调整，排序会改变 Backend/内存成本并扩大 V1 顺序承诺，因此均不采用。

## 测试与性能门禁

### 正确性

- FakeClock 覆盖 primary、fallback、unavailable、非法 `timespec` 和溢出；
- `primary_valid`、`fallback_valid`、`time_unavailable` 的 golden bytes；
- status `3` 只返回 reserved/unsupported，不被命名为 Record invalid；
- bits 2～7 非零返回独立 `unknown_flags`；
- `time_unavailable` 与非零 `time_value` 的组合被拒绝；
- filtered、invalid metadata、unsupported、too-large、full-drop 的取时次数为 0；
- accepted primary 路径恰好一次 primary 调用；fallback 路径至多再调用一次 fallback；
- 人工注入时间回退，确认数值不改写且 Channel FIFO 不变；
- Text Sink 对 unavailable 输出稳定占位符；
- Debug/Release、ASan/UBSan 和原生 Linux TSan 通过。

### 性能

- 分别测 FakeClock、`CLOCK_REALTIME_COARSE`、`CLOCK_REALTIME` 的纯取时 P50/P99/P99.9；
- 分别测无时间戳基线与 admission timestamp 后的 accepted Producer 成本；
- 证明 filtered/too-large/full-drop 没有取时调用；
- pinned/unpinned 与 1/2/4/8/16/32/64/128 Producer 分开；
- 记录目标机器的 kernel、glibc、clocksource、`clock_getres()` 与 vDSO/syscall 诊断；
- 不使用 WSL2 数据作为原生 Linux 发布性能结论；
- 继续验证统计守恒和 Ring 高水位。

## 参考

- [Linux `clock_gettime(2)`](https://man7.org/linux/man-pages/man2/clock_gettime.2.html)
- [Linux `vdso(7)`](https://man7.org/linux/man-pages/man7/vdso.7.html)
- [Linux x86/KVM timekeeping](https://docs.kernel.org/virt/kvm/x86/timekeeping.html)
- [BQLog Linux `CLOCK_REALTIME_COARSE` 实现](https://github.com/Tencent/BqLog/blob/main/src/bq_common/platform/linux_misc.cpp)

## 后续入口

D3 至此冻结；D4～D6 后续已由
[ADR-009：V1 参数类型、字符串与 packed tagged arguments](./ADR-009-v1-packed-tagged-arguments.md)
冻结。`known_flags_mask == 0x03` 和 bits 2～7 为 0 的约束保持不变。hash policy、自研
`c20_format`、format cache 与 Text Sink 工作量边界已经由 [ADR-010](./ADR-010-v1-backend-c20-format.md)
冻结；下一实现入口为独立 Record Core，执行合同见
[I1 Record Core 企业级开发规范](./MILESTONE2_I1_RECORD_CORE_DEVELOPMENT_GUIDE_CHS.md)。
