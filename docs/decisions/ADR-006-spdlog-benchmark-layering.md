# ADR-006：spdlog 对照的 benchmark 分层

- 状态：已接受
- 日期：2026-09-02
- 影响范围：Ring 微基准、异步日志系统 benchmark、对外性能表述
- 不改变：QLog Ring 实现、两里程碑结构、BQLog SISO 对照口径

## 背景

QLog 需要引入 spdlog 作为主流异步日志系统对照，但不能把不同职责的队列混为同一
SPSC 排行榜。

当前本地 spdlog `v1.17.0-41-gf5f173a1` 的源码关系为：

```text
thread_pool
  -> mpmc_blocking_queue<async_msg>
       -> mutex + condition_variable
       -> circular_q<async_msg>
```

`circular_q` 只有普通的 `head_`、`tail_` 和 `vector<T>`，线程安全由外层
`mpmc_blocking_queue` 的互斥锁提供。它不是可以脱离外层锁供两个线程并发访问的 SPSC
实现。spdlog 入队完整 `async_msg`，容量单位是对象槽位；QLog/BQLog Ring 则先取得可写
字节区域，再原地填充并发布，容量单位是可用字节。

## 决策

### 1. Ring 微基准保持同职责对照

现有 `qlog_spsc_benchmark` 只包含：

```text
QLog SpscRingBuffer
BQLog siso_ring_buffer
```

两者都提供单生产者、单消费者、固定字节容量和直接写入 Ring 存储的两阶段接口。
不把 spdlog `circular_q` 或 `mpmc_blocking_queue` 接入现有 adapter runner。

### 2. 不制造伪 SPSC adapter

禁止以下做法：

- 两个线程无锁并发调用 `circular_q`；
- 在 adapter 内增加 staging record，再在 `commit` 中复制进 spdlog 队列；
- 把 `enqueue_if_have_room` 的提交后失败伪装成 reserve 前失败；
- 只按名义 payload 比较，却隐藏 `async_msg`、mutex/CV 和对象槽位内存成本；
- 把 spdlog `overrun_oldest` 与 QLog `drop_new` 放在同一丢弃语义结果中。

这些做法分别会引入数据竞争、额外复制、不同成功判定时刻或不同过载语义。

### 3. spdlog 加入系统级异步日志对照

等 QLog 里程碑二具备 `AsyncLogger + NullSink` 后，新增独立 benchmark target，至少测试：

```text
1 Producer + 1 Backend
N Producers + 1 Backend
```

其中：

- QLog 使用每 Producer 一个 SPSC Channel，spdlog 使用其真实 thread pool；
- 主要过载对照为 QLog `drop_new` 与 spdlog `discard_new`；
- spdlog `block` 和 `overrun_oldest` 单列，不能合并；
- NullSink、后台格式化、普通文件写入和 durable flush 分层计时；
- 报告 attempted、accepted、dropped、processed、吞吐、Producer P50/P99/P99.9、
  Backend CPU、总 CPU、RSS 和队列实际内存；
- shutdown 后验证 `accepted == processed`，并记录计时边界是否包含排空。

### 4. 可选 MPMC 诊断不进入排名

如需解释 spdlog 系统结果，可以增加 `mpmc_blocking_queue` 的单 Producer 负载诊断，
但输出文件和结果表必须标注 `diagnostic_mpmc`，不能与 Ring 微基准计算领先百分比。

## 后果

- 当前 Ring 性能结论不会被不公平的锁、复制和容量语义污染。
- spdlog 仍会以真实用户路径进入最终项目对照，比单独抽取内部容器更有工程意义。
- 里程碑二需要新增可选 `QLOG_BENCH_WITH_SPDLOG` 和 `QLOG_BENCH_SPDLOG_ROOT`，并在
  输出元数据中记录 spdlog commit、dirty 状态、版本、overflow policy 和队列槽位数。
- QLog 不复用 spdlog 队列实现；spdlog 只作为 benchmark 依赖。
