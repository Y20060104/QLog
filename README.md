# QLog

QLog 是参考 BQLog 源码重建的 C++ 日志库，提供同步、共享异步与独立 Worker 模式，以及 Console、TextFile、CompressedFile 输出。

## Benchmark

测试日期：2026-10-16。入口位于 `benchmark/cpp`，按 BQLog 官方 C++ benchmark 对齐消息、参数、默认配置和计时范围。

| 项目 | 本轮配置 |
| --- | --- |
| QLog | `f643d62f` 加当前工作树：复制/hash 融合、SISO 通知与十进制对齐 |
| BQLog | `086f2fd831af86fcfa40e90015f89626d498d3d7`，固定官方 Release 产物 |
| 环境 | WSL2 / Ubuntu 24.04，Intel Core i7-9750H，6 核 12 线程，GCC 13.3.0 |
| 构建 | Release；QLog C++20、`-O3 -DNDEBUG -g -fno-omit-frame-pointer`，BQLog 官方调用方 C++17；计时时无采样器 |
| 输出 | WSL ext4 文件系统 |
| 并发与重复 | 1、4 个生产者，各 3 轮，Q/BQ、BQ/Q、Q/BQ 交替顺序 |
| 负载 | 每生产者、每吞吐场景 2,000,000 次调用，`BENCH_POOL_SIZE=50000` |
| 日志配置 | async，初始 Buffer 64 KiB，满时 block，HP 阈值 1000 条/秒，recovery=false |

普通接口包含 13 个吞吐场景；BQLog 额外运行 6 个 FAST 场景并单独列出。两程序共完成 12 次进程运行、9.6 亿次名义吞吐调用，另有预热及写侧延迟测量。

耗时沿用上游 `system_clock` 毫秒计时，包含线程创建、join 和库的全局 flush。下表为三轮中位数，方括号为最小–最大值；吞吐按调用次数计算。`Q/BQ = BQ 耗时 / Q 耗时`，大于 1 表示该场景 QLog 更快。测量未绑核。

### 普通接口：1 个生产者

| 场景 | QLog ms [范围] | QLog 百万次/秒 | BQLog ms [范围] | BQLog 百万次/秒 | Q/BQ |
| --- | ---: | ---: | ---: | ---: | ---: |
| ASCII UTF-8 压缩 | 700 [696–717] | 2.857 | 541 [482–559] | 3.697 | 0.773 |
| ASCII UTF-16 压缩 | 1195 [1151–1869] | 1.674 | 1014 [910–1060] | 1.972 | 0.849 |
| 中文 UTF-16 压缩 | 1486 [1121–1627] | 1.346 | 1191 [1174–1285] | 1.679 | 0.801 |
| 混合 UTF-16 压缩 | 1412 [1296–1485] | 1.416 | 1338 [1337–1564] | 1.495 | 0.948 |
| 压缩，4 参数 | 303 [280–332] | 6.601 | 198 [197–198] | 10.101 | 0.653 |
| 加密压缩，4 参数 | 353 [330–426] | 5.666 | 206 [196–229] | 9.709 | 0.584 |
| Text，4 参数 | 1003 [824–1235] | 1.994 | 802 [775–827] | 2.494 | 0.800 |
| 压缩，无参数 | 200 [180–221] | 10.000 | 112 [105–113] | 17.857 | 0.560 |
| 加密压缩，无参数 | 199 [176–238] | 10.050 | 117 [107–147] | 17.094 | 0.588 |
| Text，无参数 | 468 [389–480] | 4.274 | 373 [310–449] | 5.362 | 0.797 |
| 多格式：单模板 | 354 [311–407] | 5.650 | 249 [183–251] | 8.032 | 0.703 |
| 多格式：2048 模板轮转 | 281 [253–354] | 7.117 | 210 [178–210] | 9.524 | 0.747 |
| 多格式：8 模板热点窗口 | 268 [255–365] | 7.463 | 170 [159–201] | 11.765 | 0.634 |

### 普通接口：4 个生产者

| 场景 | QLog ms [范围] | QLog 百万次/秒 | BQLog ms [范围] | BQLog 百万次/秒 | Q/BQ |
| --- | ---: | ---: | ---: | ---: | ---: |
| ASCII UTF-8 压缩 | 960 [807–973] | 8.333 | 1024 [914–1112] | 7.812 | 1.067 |
| ASCII UTF-16 压缩 | 1225 [999–1293] | 6.531 | 1252 [1142–1260] | 6.390 | 1.022 |
| 中文 UTF-16 压缩 | 1327 [1269–1328] | 6.029 | 1323 [1290–1330] | 6.047 | 0.997 |
| 混合 UTF-16 压缩 | 1239 [1191–1336] | 6.457 | 1208 [1160–1249] | 6.623 | 0.975 |
| 压缩，4 参数 | 889 [718–987] | 8.999 | 977 [811–1083] | 8.188 | 1.099 |
| 加密压缩，4 参数 | 819 [814–927] | 9.768 | 968 [920–987] | 8.264 | 1.182 |
| Text，4 参数 | 4044 [3671–4092] | 1.978 | 4015 [3959–4573] | 1.993 | 0.993 |
| 压缩，无参数 | 471 [393–500] | 16.985 | 549 [436–586] | 14.572 | 1.166 |
| 加密压缩，无参数 | 464 [450–467] | 17.241 | 490 [473–508] | 16.327 | 1.056 |
| Text，无参数 | 2101 [1908–2213] | 3.808 | 2053 [1951–2057] | 3.897 | 0.977 |
| 多格式：单模板 | 667 [640–671] | 11.994 | 1114 [752–1243] | 7.181 | 1.670 |
| 多格式：2048 模板轮转 | 742 [737–814] | 10.782 | 949 [824–1279] | 8.430 | 1.279 |
| 多格式：8 模板热点窗口 | 680 [668–751] | 11.765 | 1212 [879–1825] | 6.601 | 1.782 |

### 写侧延迟

每线程 10000 组，每组 20 次普通调用，组间忙等约 2000–2200 µs。统计对象是每组耗时除以 20 后的批次平均值，单位 ns/次。P50/P99/P99.9 取三轮对应分位数的中位数，最大值取三轮最大批次平均值。

| 写线程 | 接口 | P50 | P99 | P99.9 | 最大批次平均值 |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | QLog info | 95.0 | 2055.0 | 3322.5 | 18035.0 |
| 1 | BQLog info | 70.0 | 2115.0 | 2865.0 | 9880.0 |
| 1 | BQLog FAST | 20.0 | 95.0 | 155.0 | 6485.0 |
| 4 | QLog info | 110.0 | 1570.0 | 2270.0 | 32280.0 |
| 4 | BQLog info | 75.0 | 1515.0 | 2035.0 | 19205.0 |
| 4 | BQLog FAST | 20.0 | 55.0 | 90.0 | 9085.0 |

### BQLog FAST

BQLog 使用官方 `BQ_LOG_FAST_INFO` 路径。QLog 当前未提供对应接口。

| 生产者 | 场景 | BQLog ms [范围] | BQLog 百万次/秒 | QLog |
| ---: | --- | ---: | ---: | --- |
| 1 | 压缩，4 参数 | 139 [134–179] | 14.388 | 未支持 |
| 1 | 加密压缩，4 参数 | 150 [129–157] | 13.333 | 未支持 |
| 1 | Text，4 参数 | 805 [703–1602] | 2.484 | 未支持 |
| 1 | 压缩，无参数 | 88 [83–161] | 22.727 | 未支持 |
| 1 | 加密压缩，无参数 | 87 [87–94] | 22.989 | 未支持 |
| 1 | Text，无参数 | 458 [385–881] | 4.367 | 未支持 |
| 4 | 压缩，4 参数 | 818 [814–909] | 9.780 | 未支持 |
| 4 | 加密压缩，4 参数 | 804 [761–857] | 9.950 | 未支持 |
| 4 | Text，4 参数 | 4039 [3891–4064] | 1.981 | 未支持 |
| 4 | 压缩，无参数 | 465 [462–588] | 17.204 | 未支持 |
| 4 | 加密压缩，无参数 | 412 [394–461] | 19.417 | 未支持 |
| 4 | Text，无参数 | 2213 [1829–2724] | 3.615 | 未支持 |


## 构建与复现

以下命令均在 QLog 项目根目录执行；参考源码、构建目录及运行产物统一放在项目内的 `build/benchmark-repro` 目录，并固定 BQLog 版本：

```bash
mkdir -p build/benchmark-repro
BQLOG_SOURCE=build/benchmark-repro/BqLog
git clone --filter=blob:none --sparse https://github.com/Tencent/BqLog.git "$BQLOG_SOURCE"
git -C "$BQLOG_SOURCE" sparse-checkout set src include benchmark
git -C "$BQLOG_SOURCE" checkout 086f2fd831af86fcfa40e90015f89626d498d3d7

QLOG_BENCH_BUILD=build/benchmark-repro/qlog-release
BQLOG_BENCH_BUILD=build/benchmark-repro/bqlog-release
cmake -S . -B "$QLOG_BENCH_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DQLOG_BUILD_BQLOG_BENCHMARK=ON \
  -DCMAKE_CXX_FLAGS="-Werror -g -fno-omit-frame-pointer"
cmake --build "$QLOG_BENCH_BUILD" --parallel 4
ctest --test-dir "$QLOG_BENCH_BUILD" --output-on-failure
cmake -S "$BQLOG_SOURCE/benchmark/cpp" \
  -B "$BQLOG_BENCH_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTARGET_PLATFORM=linux
cmake --build "$BQLOG_BENCH_BUILD" --parallel 4
python3 benchmark/cpp/run_benchmark.py \
  --qlog-exe "$QLOG_BENCH_BUILD/benchmark/cpp/QLogBenchmark" \
  --bqlog-exe "$BQLOG_SOURCE/artifacts/benchmark/linux/Release/BqLogBenchmark" \
  --bqlog-source "$BQLOG_SOURCE" \
  --data-root build/benchmark-repro/data \
  --evidence-dir build/benchmark-repro/measured \
  --threads 1 4 --repeats 3 --min-host-free-gib 20 --cleanup-output
```

运行器使用 Linux/Python 3，每轮使用新输出目录，并检查磁盘余量与计时时钟。`--cleanup-output` 在记录输出文件哈希后清理该进程生成的目录；原始计时和环境证据保留。

## 结果与源码说明

- [本轮机器结果](docs/validation/BQLOG_BENCHMARK_20261016_RESULTS.json)
- [延迟、组件开销与加密场景的定位报告](docs/validation/PERFORMANCE_INVESTIGATION_20261016_CHS.md)
- [独立组件与连续/间歇负载测试工具](benchmark/diagnostics/README.md)
- [SISO 通知和十进制对齐决定](docs/decisions/SISO_DECIMAL_PERFORMANCE_ALIGNMENT_20261016_CHS.md)
- [生产实现对齐现状](docs/decisions/BQLOG_ALIGNMENT_GAPS_20261015_CHS.md)
- [2026-10-15 历史测量](docs/validation/BQLOG_BENCHMARK_20261015_RERUN_RESULTS.json)
- [BQLog 来源与许可](NOTICE-BQLog.md)
