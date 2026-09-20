# QLog SPSC Ring 微基准

本目录只测固定容量 SPSC Ring，不包含 `fmt`、日志参数编码、后台调度或文件 I/O。
这里的结果不能与 BQLog README 中的完整日志系统耗时直接比较。

## 对照边界

BQLog 只作为 benchmark 的可选同级源码依赖：

```text
/home/qq344/
├── QLog/
└── BqLog/
```

构建系统只单独编译：

```text
BqLog/src/bq_log/types/buffer/siso_ring_buffer.cpp
```

BQLog 头文件不会进入 QLog 生产库，也不会执行 BQLog 自己的 CMake。benchmark
输出会记录两个仓库的 commit 与 dirty 状态。任何仓库为 dirty 时，结果标记为
`NON_REPRODUCIBLE`，只能用于开发期分析，不能用于简历中的正式性能结论。

### 为什么本层不加入 spdlog

当前检查的 spdlog `v1.17.0-41-gf5f173a1` 没有并发 SPSC Ring：异步
`thread_pool` 的队列类型是 `mpmc_blocking_queue<async_msg>`，其入队和出队使用
`std::mutex`、`std::condition_variable` 与非线程安全的 `circular_q`。因此：

- 不能让 Producer/Consumer 直接并发使用 `circular_q`，否则产生数据竞争；
- 不能把带锁、按对象槽位计容量的 MPMC 队列放进字节型 SPSC Ring 排名；
- 不能为适配现有 `reserve -> fill -> commit` runner 而增加临时 staging buffer，
  因为这会改变复制次数和成功判定时刻。

spdlog 会在异步日志 V1 层作为真实系统对照，而不是伪装成第三个 SPSC 实现。
详细边界见 ADR-006。

## 首版场景

### `transfer_retry`

真实 1 Producer + 1 Consumer 饱和传输。Producer 遇到 full 后重试同一条逻辑
消息，衡量 Ring 的有效传输上限：

```text
reserve_calls == accepted + full_retries
attempted_messages == accepted
accepted == consumed
dropped_full == 0
```

### `drop_new`

Producer 遇到 full 后立即丢弃该消息。Consumer 会扫描完整 payload 并计算
FNV 校验，故意构造慢于 Producer 的后台工作，用于观察过载行为，不作为 Ring
峰值吞吐主成绩：

```text
reserve_calls == attempted_messages
attempted_messages == accepted + dropped_full
accepted == consumed
```

### `full_fast_fail`

在计时区外填满 Ring，不启动 Consumer；计时区只测满队列失败返回：

```text
attempted_messages == dropped_full
accepted == consumed == 0
```

这个数字是 drop 路径成本，不能称为日志吞吐量。

## 单侧隔离场景

这两个场景用于回答“端到端差距来自写侧、读侧，还是跨线程协作”，不代表完整日志系统吞吐。
两边都使用两个固定线程并绑到不同 CPU；Ring、线程和 payload source 均在计时前创建。

### `write_success`

每个周期由 Producer 先在计时外写入少量 0B padding，再在计时区精确执行 N 次：

```text
reserve -> 完整 payload memcpy/sequence/canary -> ring.commit
```

Consumer 在计时外按相同顺序读空并校验。计时区不包含 padding、barrier、排空、empty 探测或
terminal full 探测。Handle 是平凡可析构的 16B 被动令牌，循环中的普通作用域结束仍保留在
真实生成代码中，但不存在自定义析构或自动终结 Ring 的成本。
主指标是 `accepted_records / active_elapsed`。

### `prefilled_read`

Producer 在计时外预填 N 条目标记录和必要 padding；Consumer 在计时区精确执行 N 次：

```text
ring.try_read -> 长度、sequence、canary 轻量校验 -> ring.release
```

padding 的排空和 terminal empty 探测都在停表后执行。主指标是
`consumed_records / active_elapsed`，不会错误地沿用写入速率。

### 周期公平性

基准先在独立 Ring 上校准 `target_records_per_cycle` 和
`padding_records_per_cycle`，并验证“目标记录在前”和“padding 在前”两种顺序都恰好填满且可完整
读空。正式循环每周期推进一整圈，因此下一周期回到相同物理 offset；padding 不计入业务 counters
或主吞吐。这样既不会每批重建 Ring，也不会把一次失败的 full/empty 探测混进成功路径。

JSONL 会同时输出：

- `rate_metric`：`accepted_records`、`read_records` 或 `full_calls`；
- `primary_per_second` 与 `nanoseconds_per_operation`；
- `cycle_count`、每周期目标记录数和 padding 数；
- `read_calls` 以及原有 accepted/consumed/full/drop/validation counters。

`ring-v1` 预设对两个隔离场景使用 1 MiB Ring 和 64B payload，减少计时外 phase barrier
相对于计时区的占比。单独运行：

```bash
./build/benchmark/release/benchmarks/qlog_spsc_benchmark \
    --impl all --scenario write-success \
    --capacity 65536 --payload 64 \
    --warmup-ms 2000 --duration-ms 5000 --repeat 7 --format jsonl

./build/benchmark/release/benchmarks/qlog_spsc_benchmark \
    --impl all --scenario prefilled-read \
    --capacity 65536 --payload 64 \
    --warmup-ms 2000 --duration-ms 5000 --repeat 7 --format jsonl
```

诊断时记 `Wq/Wb` 为 QLog/BQLog 纯写吞吐，`Rq/Rb` 为纯读吞吐，`Tq/Tb` 为
`transfer_retry` 吞吐：

- 只有写侧明显落后：分析 `try_reserve/commit`、Handle 返回值与 Frame Geometry；
- 只有读侧明显落后：优先分析 `try_read/release`、Header 解码与批量回收；
- 两个隔离路径都接近、并发传输仍明显落后：分析游标发布、full retry 和缓存一致性；
- 两侧都按近似固定比例落后：共同的 16B Handle 返回或跨翻译单元调用更可疑。

## Payload 与计时

每条成功消息都完整 `memcpy` 到 Ring。前 8B 写入 sequence，末 8B 写入
canary；Consumer 校验长度、顺序和 canary。每个正式样本前还会执行一次不计时
的全 payload round-trip 自检。

吞吐模式不会逐条读取时钟。Ring、payload source 和线程都在计时前创建；线程
先完成绑核，再通过 barrier 同时开始。Producer 停止后 Consumer 必须排空全部
accepted 消息，程序分别报告 active time 与 drain time。

补充：sequence/canary 校验适用于至少 16B 的 payload；1–15B 的自定义 payload
逐字节校验固定内容，但因空间不足而不承担顺序校验。

同一组 geometry 下，两种实现按 repetition 交替先后运行，避免固定先跑完 QLog、再跑完
BQLog 带来的温度、睿频和系统时间漂移偏差。

`ring-v1` 预设：

- 主容量 64 KiB，payload 为 0/16/64/256/1024/8192B；
- 固定 64B payload，再测 16 KiB、64 KiB、1 MiB；
- 预热 2 秒，正式测量 5 秒，重复 7 次；
- 使用中位数和 MAD，不选择最好的一次。

0B 仅用于固定成本诊断，不代表真实日志记录。

## 构建

```bash
./scripts/build_benchmark.sh
```

默认从 `../BqLog` 读取 BQLog。也可显式指定：

```bash
QLOG_BENCH_BQLOG_ROOT=/path/to/BqLog ./scripts/build_benchmark.sh
```

只构建 QLog：

```bash
QLOG_BENCH_WITH_BQLOG=OFF ./scripts/build_benchmark.sh
```

## 运行

快速自检：

```bash
./build/benchmark/release/benchmarks/qlog_spsc_benchmark --preset quick
```

正式矩阵：

```bash
./build/benchmark/release/benchmarks/qlog_spsc_benchmark \
    --preset ring-v1 \
    --producer-cpu 2 \
    --consumer-cpu 4 \
    --format jsonl \
    --output build/benchmark/ring-v1.jsonl
```

指定单场景：

```bash
./build/benchmark/release/benchmarks/qlog_spsc_benchmark \
    --impl all \
    --scenario transfer-retry \
    --capacity 65536 \
    --payload 64 \
    --warmup-ms 2000 \
    --duration-ms 5000 \
    --repeat 7
```

未显式指定 CPU 时，程序会读取 Linux sysfs topology，优先选择不同物理核；只有拓扑信息不可用时，
才退化为两个不同逻辑 CPU。正式运行仍应显式记录并检查 CPU 组合。

正式运行应选择同一 NUMA 节点、不同物理核且不是 SMT sibling 的两个 CPU。
WSL2 结果用于开发期回归；公开“达到 BQLog 90%”前，应在稳定的原生 Linux
环境复测并完整披露硬件、内核、编译器、commit、dirty 状态和计时边界。

## 后续而非首版

下一小步再加入：

- Producer latency 的独立采样模式与 P50/P99/P99.9；
- normal/header-at-tail/wrapped 三种布局的受控测试；
- BQLog `batch_read()` 最佳生产路径对照；
- `perf stat` 的 cycles、instructions、branches 与 cache misses。

等 QLog 的 `AsyncLogger + NullSink` 可用后，新增独立的 async-system benchmark：

- 单 Producer + 单 Backend：QLog 每线程 SPSC 对 spdlog 单 Producer 负载下的 MPMC；
- 多 Producer + 单 Backend：比较完整系统扩展性，不声称是 Ring 微基准；
- QLog `drop_new` 对 spdlog `discard_new`；`overrun_oldest` 与 `block` 单独报告；
- 同时报告 attempted/accepted/dropped/processed、Producer P50/P99/P99.9、吞吐、CPU 和 RSS；
- spdlog 容量按对象槽位配置，QLog 按字节配置，因此同时披露记录数容量和实际内存占用。

这些项目不能混入首版吞吐热循环后再声称口径相同。

## V1 异步文本对照

`tools/run_bqlog_comparison.py` 从同级 BqLog 的 HEAD 导出干净源码并构建，与 QLog 比较同正文的 buffered 管线。先 git pull，再从 QLog 根目录运行脚本。实际结果与不可完全对齐的边界见 [对照报告](../docs/decisions/V1_BQLOG_COMPARISON_20260920_CHS.md)。本节的完整日志基准与上文 Ring 微基准分别解释。

## 原生默认配置的产品工作负载评测

新的 [production 套件](production/README_CHS.md) 用各库原生实现和默认资源策略完成同类文本日志业务，不强制相同的缓存、队列或满策略。预定方案包含 8 个主场景、2 个固定到达率场景和持续负载附加项；结果、配对置信区间、完整证据及冻结源码复现入口见 [2026-09-20 报告](../docs/decisions/V1_NATIVE_PRODUCTION_PERFORMANCE_20260920_CHS.md)。

本报告可以作为明确限定 WSL2 环境的可复现基线；不能外推为原生 Linux 发布性能验收，也不替代上文 Ring 微基准的独立解释。
