# QLog

QLog 是参考 BQLog 源码重建的 C++ 日志库，提供同步、共享异步与独立 Worker 模式，以及 Console、TextFile、CompressedFile 输出。

## Benchmark

测试日期：2026-10-15。入口位于 `benchmark/cpp`，按 BQLog 官方 C++ benchmark 对齐消息、参数、默认配置和计时范围。

| 项目 | 本轮配置 |
| --- | --- |
| QLog | `ee3ac945` 加本轮 SISO 命名、工程宏与 RSA 对齐变更 |
| BQLog | `086f2fd831af86fcfa40e90015f89626d498d3d7` |
| 环境 | WSL2 / Ubuntu 24.04，Intel Core i7-9750H，6 核 12 线程，GCC 13.3.0 |
| 构建 | 两库 Release；QLog C++20，BQLog 官方 benchmark C++17 |
| 输出 | WSL ext4 文件系统 |
| 并发与重复 | 1、4 个生产者，各 3 轮，Q/BQ、BQ/Q、Q/BQ 交替顺序 |
| 负载 | 每生产者、每吞吐场景 2,000,000 次调用，`BENCH_POOL_SIZE=50000` |
| 日志配置 | async，初始 Buffer 64 KiB，满时 block，HP 阈值 1000 条/秒，recovery=false |

普通接口包含 13 个吞吐场景；BQLog 额外运行 6 个 FAST 场景并单独列出。两程序共完成 12 次进程运行、9.6 亿次名义吞吐调用，另有预热及写侧延迟测量。

耗时沿用上游 `system_clock` 毫秒计时，包含线程创建、join 和库的全局 flush。下表为三轮中位数，方括号为最小–最大值；吞吐按调用次数计算。`Q/BQ = BQ 耗时 / Q 耗时`，大于 1 表示该场景 QLog 更快。测量未绑核。

### 普通接口：1 个生产者

| 场景 | QLog ms [范围] | QLog 百万次/秒 | BQLog ms [范围] | BQLog 百万次/秒 | Q/BQ |
| --- | ---: | ---: | ---: | ---: | ---: |
| ASCII UTF-8 压缩 | 562 [494–562] | 3.559 | 431 [419–549] | 4.640 | 0.767 |
| ASCII UTF-16 压缩 | 1050 [765–2343] | 1.905 | 1070 [933–1085] | 1.869 | 1.019 |
| 中文 UTF-16 压缩 | 1172 [866–2217] | 1.706 | 1200 [1169–1249] | 1.667 | 1.024 |
| 混合 UTF-16 压缩 | 1101 [796–1570] | 1.817 | 1235 [1222–1355] | 1.619 | 1.122 |
| 压缩，4 参数 | 323 [270–569] | 6.192 | 174 [166–228] | 11.494 | 0.539 |
| 加密压缩，4 参数 | 300 [285–352] | 6.667 | 204 [180–218] | 9.804 | 0.680 |
| Text，4 参数 | 1317 [1144–1379] | 1.519 | 723 [658–732] | 2.766 | 0.549 |
| 压缩，无参数 | 238 [189–272] | 8.403 | 103 [103–104] | 19.417 | 0.433 |
| 加密压缩，无参数 | 191 [188–196] | 10.471 | 111 [109–128] | 18.018 | 0.581 |
| Text，无参数 | 462 [457–528] | 4.329 | 368 [263–415] | 5.435 | 0.797 |
| 多格式：单模板 | 287 [228–393] | 6.969 | 182 [169–187] | 10.989 | 0.634 |
| 多格式：2048 模板轮转 | 257 [245–258] | 7.782 | 212 [192–216] | 9.434 | 0.825 |
| 多格式：8 模板热点窗口 | 243 [231–251] | 8.230 | 179 [166–227] | 11.173 | 0.737 |

### 普通接口：4 个生产者

| 场景 | QLog ms [范围] | QLog 百万次/秒 | BQLog ms [范围] | BQLog 百万次/秒 | Q/BQ |
| --- | ---: | ---: | ---: | ---: | ---: |
| ASCII UTF-8 压缩 | 788 [734–852] | 10.152 | 796 [778–842] | 10.050 | 1.010 |
| ASCII UTF-16 压缩 | 1150 [1063–1174] | 6.957 | 1078 [1060–1204] | 7.421 | 0.937 |
| 中文 UTF-16 压缩 | 1194 [1100–1225] | 6.700 | 1186 [1166–1205] | 6.745 | 0.993 |
| 混合 UTF-16 压缩 | 1173 [1161–1200] | 6.820 | 1181 [1161–1265] | 6.774 | 1.007 |
| 压缩，4 参数 | 760 [718–778] | 10.526 | 681 [680–805] | 11.747 | 0.896 |
| 加密压缩，4 参数 | 799 [765–809] | 10.013 | 854 [796–886] | 9.368 | 1.069 |
| Text，4 参数 | 4615 [4462–4705] | 1.733 | 3112 [2895–3308] | 2.571 | 0.674 |
| 压缩，无参数 | 417 [396–516] | 19.185 | 454 [413–471] | 17.621 | 1.089 |
| 加密压缩，无参数 | 418 [387–454] | 19.139 | 589 [364–634] | 13.582 | 1.409 |
| Text，无参数 | 1746 [1666–1813] | 4.582 | 1627 [1553–1870] | 4.917 | 0.932 |
| 多格式：单模板 | 581 [537–734] | 13.769 | 1025 [705–1141] | 7.805 | 1.764 |
| 多格式：2048 模板轮转 | 678 [647–688] | 11.799 | 663 [604–851] | 12.066 | 0.978 |
| 多格式：8 模板热点窗口 | 652 [516–742] | 12.270 | 826 [746–889] | 9.685 | 1.267 |

### 写侧延迟

沿用上游测量：每线程 10000 组，每组 20 次调用，组间忙等约 2000–2200 µs。指标是每组耗时除以 20 后所得批次平均值的分位数，单位为 ns/次。P50/P99/P99.9 取三轮各自分位数的中位数，最大值取三轮最大批次平均值。

| 写线程 | 接口 | P50 | P99 | P99.9 | 最大批次平均值 |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | QLog info | 1140.0 | 1727.5 | 2425.0 | 6220.0 |
| 1 | BQLog info | 60.0 | 1535.0 | 2095.0 | 35080.0 |
| 1 | BQLog FAST | 15.0 | 60.0 | 115.0 | 6200.0 |
| 4 | QLog info | 220.0 | 1795.0 | 2595.0 | 177800.0 |
| 4 | BQLog info | 85.0 | 1400.0 | 1795.0 | 13970.0 |
| 4 | BQLog FAST | 20.0 | 40.0 | 80.0 | 7205.0 |

### BQLog FAST

BQLog 使用官方 `BQ_LOG_FAST_INFO` 路径。QLog 当前尚无对应接口。

| 生产者 | 场景 | BQLog ms [范围] | BQLog 百万次/秒 | QLog |
| ---: | --- | ---: | ---: | --- |
| 1 | 压缩，4 参数 | 142 [120–144] | 14.085 | 未支持 |
| 1 | 加密压缩，4 参数 | 146 [142–235] | 13.699 | 未支持 |
| 1 | Text，4 参数 | 739 [609–810] | 2.706 | 未支持 |
| 1 | 压缩，无参数 | 82 [75–90] | 24.390 | 未支持 |
| 1 | 加密压缩，无参数 | 82 [80–85] | 24.390 | 未支持 |
| 1 | Text，无参数 | 430 [350–485] | 4.651 | 未支持 |
| 4 | 压缩，4 参数 | 641 [554–662] | 12.480 | 未支持 |
| 4 | 加密压缩，4 参数 | 629 [626–693] | 12.719 | 未支持 |
| 4 | Text，4 参数 | 3375 [3004–3536] | 2.370 | 未支持 |
| 4 | 压缩，无参数 | 448 [422–610] | 17.857 | 未支持 |
| 4 | 加密压缩，无参数 | 395 [358–399] | 20.253 | 未支持 |
| 4 | Text，无参数 | 1556 [1489–3482] | 5.141 | 未支持 |

## 构建与复现

将 BQLog 放在 QLog 同级的独立参考目录，并固定版本：

```bash
git clone --filter=blob:none --sparse https://github.com/Tencent/BqLog.git /home/qq344/BqLog-benchmark-20261015
git -C /home/qq344/BqLog-benchmark-20261015 sparse-checkout set src include benchmark
git -C /home/qq344/BqLog-benchmark-20261015 checkout 086f2fd831af86fcfa40e90015f89626d498d3d7

QLOG_BENCH_BUILD=/mnt/d/qq344/Documents/qlog-benchmark-repro/qlog-release
BQLOG_BENCH_BUILD=/mnt/d/qq344/Documents/qlog-benchmark-repro/bqlog-release
cmake -S /home/qq344/QLog -B "$QLOG_BENCH_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DQLOG_BUILD_BQLOG_BENCHMARK=ON -DCMAKE_CXX_FLAGS=-Werror
cmake --build "$QLOG_BENCH_BUILD" --parallel 4
ctest --test-dir "$QLOG_BENCH_BUILD" --output-on-failure
cmake -S /home/qq344/BqLog-benchmark-20261015/benchmark/cpp \
  -B "$BQLOG_BENCH_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTARGET_PLATFORM=linux
cmake --build "$BQLOG_BENCH_BUILD" --parallel 4
python3 /home/qq344/QLog/benchmark/cpp/run_benchmark.py \
  --qlog-exe "$QLOG_BENCH_BUILD/benchmark/cpp/QLogBenchmark" \
  --bqlog-exe /home/qq344/BqLog-benchmark-20261015/artifacts/benchmark/linux/Release/BqLogBenchmark \
  --bqlog-source /home/qq344/BqLog-benchmark-20261015 \
  --data-root /home/qq344/QLogBenchmarkData-repro \
  --evidence-dir /mnt/d/qq344/Documents/qlog-benchmark-repro/measured \
  --threads 1 4 --repeats 3 --min-host-free-gib 20
```

运行器使用 Linux/Python 3，每轮使用新输出目录。预留足够的输出空间，运行器会检查磁盘余量与计时时钟。

## 结果与源码说明

- [本轮机器结果](/home/qq344/QLog/docs/validation/BQLOG_BENCHMARK_20261015_RERUN_RESULTS.json)
- [构建、测试与测量记录](/home/qq344/QLog/docs/validation/BQLOG_NAMING_RSA_BENCHMARK_20261015_CHS.md)
- [生产实现对齐现状](/home/qq344/QLog/docs/decisions/BQLOG_ALIGNMENT_GAPS_20261015_CHS.md)
- [BQLog 来源与许可](/home/qq344/QLog/NOTICE-BQLog.md)
