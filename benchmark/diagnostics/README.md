# Component and latency diagnostics

这组工具用于区分算法成本、Buffer 成本和异步日志流水线成本，独立于 `benchmark/cpp` 的官方场景。默认不进入主库构建。

## 构建

从 QLog 根目录运行：

```bash
cmake -S benchmark/diagnostics -B build/cost -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DQLOG_BQLOG_SOURCE_DIR=build/benchmark-repro/BqLog \
  -DQLOG_BQLOG_LIBRARY=build/benchmark-repro/BqLog/artifacts/static_lib/lib/Release/libBqLog.a
cmake --build build/cost --parallel 4
```

不传两个 `QLOG_BQLOG_*` 参数时只构建 QLog 工具。启用参考工具时必须使用与静态库对应的固定源码和 Release 配置；本轮参考为 `086f2fd831af86fcfa40e90015f89626d498d3d7`。两库组件调用方均使用 C++20，库保留各自构建配置。QLog 的 Debug 宏继续由库目标 PUBLIC 传播。

## 单独测量组件

```bash
./build/cost/QLogComponentBenchmark all
./build/cost/BQLogComponentBenchmark all
```

也可以将 `all` 改为 `copy`、`vernam`、`ring` 或 `layout`。输出为每项一行 JSON，包含操作次数、ns/op 和校验值。

| 项目 | 计入的工作 | 用途 |
| --- | --- | --- |
| copy | 不同长度和偏移的复制后 hash、融合复制/hash | 判断融合在特定长度和热缓存条件下的收益；不预设融合一定更快 |
| vernam | 固定 4096 字节密钥、不同块长、自动硬件分派 | 比较加密核心；先按逐字节 XOR 期望值检查，再验证偶数次调用恢复原输入 |
| ring | 同一线程依次 alloc/commit/read/return，核对序号 | 度量 SISO 原语的顺序往返成本；不作为并发吞吐结果 |
| layout | 固定前缀的实际 Record，递增序号，计数器和四参数格式 | 排除 Buffer、文件 I/O 和 Worker 调度，判断数字转换和 Layout 成本 |

Layout 循环复用对象、缓存和输出缓冲；`OUTPUT` 是最后一条实际文本，可以核对两库及修改前后文本是否相同。该用例固定时间戳，不能用于推导真实每毫秒刷新成本。组件时间不可直接相加预测异步吞吐。

## 连续和间歇负载

参数顺序：

```text
QLogLatencyProbe burst|compressed [groups=10000] [wait_us=2000] [hp_threshold=1000] [csv_path]
```

每组 20 次普通 `info`，组间忙等 `wait_us` 到 `wait_us+200` 微秒；`wait_us=0` 表示连续调用。`burst` 使用文本输出，`compressed` 使用压缩输出。阈值 `0` 按库的真实配置规则禁用 HP，`1` 用于观察尽早进入 HP 的行为。阈值仅用于受控对照，官方 benchmark 仍保留默认 1000。

```bash
mkdir -p build/cost/runs/qlog build/cost/runs/bqlog
(
  cd build/cost/runs/qlog
  ../../QLogLatencyProbe burst 10000 2000 1000 qlog-batches.csv
)
(
  cd build/cost/runs/bqlog
  ../../BQLogLatencyProbe burst 10000 2000 1000 bqlog-batches.csv
)
```

CSV 保存每批耗时除以 20 的值；P50/P95/P99/max 为这些批次平均值的分位数，不是逐条独立调用延迟。CSV 在计时区间之外写入。工具同时检查 API 接受数是否等于调用数；该检查不等同于逐条文件持久化验收。这是诊断负载，不能替换官方 benchmark 的整套场景或拼接到官方统计表。

## 找瓶颈的顺序

1. 固定源码、Release 选项、输出目录和参数，先跑未插桩版本。
2. 只改变一个实现或配置；保留所有重复，交替运行顺序。
3. 对照连续/间歇、默认/禁用 HP，判断差距是否与路由、占用或唤醒相关。
4. 在独立诊断构建记录 HP/LP、低空间提示、真实通知和重试次数，按批次与延迟关联。分段计时需要校准探针开销；不要把插桩绝对值当成正常接口性能。
5. 对纯 Layout 或加密负载采样，避免组间忙等淹没热点。WSL 可使用已安装的 `/usr/lib/linux-tools/<version>/perf`；不能仅因 `/usr/bin/perf` 包装器版本不匹配就判定内核不支持。
6. 最后跑 `benchmark/cpp` 完整套件，区分组件 A/B 收益与整条流水线结果。

2026-10-16 的实际方法和证据见 [性能定位报告](../../docs/validation/PERFORMANCE_INVESTIGATION_20261016_CHS.md)。
