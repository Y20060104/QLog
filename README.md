# QLog

QLog 是按 BQLog 源码逐模块重建的 C++ 日志库。本次加入与官方 C++ benchmark 对应的入口，并记录当前实际工作区与最新拉取参考的测试结果。

## 本轮范围与参考

- QLog 权威目录：`/home/qq344/QLog`，本次 HEAD 为 `c2faba4032c145de534d0a3d70cc05af58b523b4`；存在未提交实现，不能用 HEAD 单独复现被测代码。
- 新参考位于同级 `/home/qq344/BqLog-benchmark-20261015`。2026-10-15 拉取时固定为 `086f2fd831af86fcfa40e90015f89626d498d3d7`，提交日期 2026-10-07；原 `/home/qq344/BqLog` 的脏工作树保留。
- [上游 benchmark 原文](https://github.com/Tencent/BqLog/blob/086f2fd831af86fcfa40e90015f89626d498d3d7/benchmark/cpp/main.cpp)，原始文件哈希记录在 `/home/qq344/QLog/benchmark/cpp/UPSTREAM.json`。
- 新入口为 `/home/qq344/QLog/benchmark/cpp`，目标 `QLogBenchmark`，构建选项 `QLOG_BUILD_BQLOG_BENCHMARK=ON`；旧 `/home/qq344/QLog/benchmarks` 保留独立用途。
- 本轮只新增 benchmark、构建入口和记录。库的 `include/`、`src/` 及既有测试保持原样；没有将库整体升级到新 BQLog 提交。
- 依用户确认：普通 `log.info()` 互相比较；BQLog 的 `BQ_LOG_FAST_INFO` 单列，QLog 标为未支持，未用普通接口冒充 FAST。

## 工作负载与统计口径

- 保留上游每生产者、每吞吐场景 **2,000,000 次调用**；测试 1、4 个生产者，各 3 轮。每轮使用新输出目录，执行顺序为 Q/BQ、BQ/Q、Q/BQ。
- 保留 `character_pool_size=8*1024*1024`、`BENCH_POOL_SIZE=50000`、输入选择公式、四种字符场景、六种文件/参数组合，以及单模板、2048 模板轮转、8 模板热点窗口（burst=64）。两种多模板场景保留原 2048 次预热。
- 原消息、参数和普通调用顺序保留（时间戳、线程 ID、程序名自然不同，文件不是逐字节相同）；BQ 容器/字符串适配为现有 std 类型，flush 调用适配到 `force_flush_all()`。BQLog 原 suite 还执行 6 个 FAST 场景，因此两程序的完整进程工作量及多模板场景之前的运行历史不同。
- 保留默认配置：async、64 KiB 初始 Buffer、满时 block、HP 阈值 1000 条/秒、recovery=false。没有手工强制固定 LP/HP 路径。
- 吞吐沿用上游 `system_clock` 毫秒计时，包含线程创建、join 和库的全局 flush；没有改为仅统计入队，也没有改变 C++ 计时源。
- 表中耗时为三轮中位数，方括号为最小–最大值。名义吞吐 = `2,000,000 × 生产者数 / 中位耗时`；`Q/BQ` = `BQ 耗时 / Q 耗时`，大于 1 表示 QLog 此项更快。
- 普通可比吞吐场景共 7.8 亿次调用，BQLog FAST 单列 1.8 亿次，共 9.6 亿次；此外还有预热及写侧延迟调用。调用返回值按原 benchmark 未统计，不能据此声称全部成功写入或零丢失。
- 原配置含 `capacity_limit=1`，存在文件保留/清理规则；不能用末尾留存文件的条数或总字节数推断全量丢失、压缩比或两库写入数据量相同。

## 环境与验证

- WSL2 Ubuntu 24.04，Linux 6.6.87.2，Intel Core i7-9750H（6 核 12 线程），GCC 13.3.0，Release。输出位于 WSL ext4：`/home/qq344/QLogBenchmarkData-20261015-02`；构建和证据位于 D 盘。
- 未绑核，允许 CPU 0–11；未控制睿频、温度、宿主后台任务及设备缓存。三轮中位数不是置信区间或稳定性保证。
- QLog benchmark 为 `-O3 -DNDEBUG -std=gnu++20`，库沿用项目 C++20；BQLog 原 benchmark 为 `-O3 -DNDEBUG -std=gnu++17`，库保留上游选项（含其 RTTI/异常设置）。这不是所有编译参数完全相同的内核比较。
- GNU/Linux 严格构建、实际链接通过；重启恢复后 **40/40 CTest** 通过。Release 的 68 个编译单元均有 NDEBUG、均无 QLOG_DEBUG。
- 原生 MSVC x64 Release 严格构建和 benchmark 单线程整套运行通过；13 个普通吞吐场景及写侧延迟入口均完成。Windows 的结果仅作运行验证，不与 WSL 数据混合。未在本轮构建或测量 Windows BQLog。
- MSVC 使用项目 `/W4 /WX /permissive- /utf-8` 及原有 C4324/C4146 排除；入口单独定义 NOMINMAX，并适配 `_dupenv_s`。59 个编译单元均有 NDEBUG、均无 QLOG_DEBUG。
- 两个 Linux 二进制 SHA-256、完整源码清单和前后无漂移检查保存在证据中。本次源码清单指纹：`8a1690754ed559601ecc0227727b80175cc67610f2f8f1e1f4a782cf740fcfd9`。

## 普通接口结果

### 1 个生产者

| 场景 | QLog ms [范围] | QLog 百万次/秒 | BQLog ms [范围] | BQLog 百万次/秒 | Q/BQ |
| --- | ---: | ---: | ---: | ---: | ---: |
| ASCII UTF-8 压缩 | 554 [494–1315] | 3.610 | 638 [535–703] | 3.135 | 1.152 |
| ASCII UTF-16 压缩 | 889 [761–934] | 2.250 | 1177 [1143–1205] | 1.699 | 1.324 |
| 中文 UTF-16 压缩 | 842 [802–1015] | 2.375 | 1052 [992–1373] | 1.901 | 1.249 |
| 混合 UTF-16 压缩 | 876 [832–975] | 2.283 | 1728 [1503–2397] | 1.157 | 1.973 |
| 压缩，4 参数 | 271 [256–301] | 7.380 | 180 [169–239] | 11.111 | 0.664 |
| 加密压缩，4 参数 | 297 [293–311] | 6.734 | 199 [196–269] | 10.050 | 0.670 |
| Text，4 参数 | 1121 [1062–1324] | 1.784 | 712 [686–983] | 2.809 | 0.635 |
| 压缩，无参数 | 183 [181–188] | 10.929 | 110 [107–122] | 18.182 | 0.601 |
| 加密压缩，无参数 | 195 [184–195] | 10.256 | 130 [105–141] | 15.385 | 0.667 |
| Text，无参数 | 453 [438–498] | 4.415 | 348 [337–439] | 5.747 | 0.768 |
| 多格式：单模板 | 225 [221–230] | 8.889 | 192 [191–257] | 10.417 | 0.853 |
| 多格式：2048 模板轮转 | 241 [241–243] | 8.299 | 207 [166–248] | 9.662 | 0.859 |
| 多格式：8 模板热点窗口 | 231 [226–234] | 8.658 | 180 [144–249] | 11.111 | 0.779 |

### 4 个生产者

| 场景 | QLog ms [范围] | QLog 百万次/秒 | BQLog ms [范围] | BQLog 百万次/秒 | Q/BQ |
| --- | ---: | ---: | ---: | ---: | ---: |
| ASCII UTF-8 压缩 | 820 [754–846] | 9.756 | 871 [784–1034] | 9.185 | 1.062 |
| ASCII UTF-16 压缩 | 1029 [971–1143] | 7.775 | 1092 [946–1162] | 7.326 | 1.061 |
| 中文 UTF-16 压缩 | 1136 [1086–1197] | 7.042 | 1272 [1054–1384] | 6.289 | 1.120 |
| 混合 UTF-16 压缩 | 1215 [1208–1254] | 6.584 | 1312 [1003–1322] | 6.098 | 1.080 |
| 压缩，4 参数 | 662 [586–703] | 12.085 | 703 [540–789] | 11.380 | 1.062 |
| 加密压缩，4 参数 | 709 [640–809] | 11.283 | 681 [635–944] | 11.747 | 0.961 |
| Text，4 参数 | 4526 [4208–4682] | 1.768 | 3294 [3141–4442] | 2.429 | 0.728 |
| 压缩，无参数 | 371 [357–387] | 21.563 | 511 [507–528] | 15.656 | 1.377 |
| 加密压缩，无参数 | 378 [370–542] | 21.164 | 609 [355–640] | 13.136 | 1.611 |
| Text，无参数 | 1549 [1372–1809] | 5.165 | 1399 [1252–1434] | 5.718 | 0.903 |
| 多格式：单模板 | 494 [472–666] | 16.194 | 947 [527–1180] | 8.448 | 1.917 |
| 多格式：2048 模板轮转 | 722 [610–734] | 11.080 | 679 [659–816] | 11.782 | 0.940 |
| 多格式：8 模板热点窗口 | 570 [534–620] | 14.035 | 587 [575–728] | 13.629 | 1.030 |

## 写侧延迟：批次平均值的分位数

沿用上游：每线程 10000 组，每组 20 次调用；组间忙等约 2000–2200 µs。每组计时后除以 20，再计算分位数。该指标不是单次调用延迟，也不是端到端文件持久化延迟。

以下单位为 ns/次：P50/P99/P99.9 各取三轮对应分位数的中位数，worst 取三轮最大的批次平均值，没有把三轮样本合并计算新分位数。选择输入线程数与实际写线程数相同的测量；4 线程 suite 中额外的 1 线程测量保留在原始 JSON。

| 写线程 | 接口 | P50 | P99 | P99.9 | 最大批次平均值 |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | QLog info | 1128.5 | 1793.0 | 2604.9 | 34722.2 |
| 1 | BQLog info | 70.5 | 1552.7 | 2212.2 | 16391.9 |
| 1 | BQLog FAST（QLog 未支持） | 74.9 | 135.9 | 492.6 | 7919.6 |
| 4 | QLog info | 245.6 | 1791.2 | 2604.8 | 34983.5 |
| 4 | BQLog info | 110.7 | 1394.5 | 1692.1 | 12200.0 |
| 4 | BQLog FAST（QLog 未支持） | 85.6 | 156.1 | 811.7 | 32443.1 |

## BQLog FAST 吞吐单列

这些是官方参考程序的原始 FAST 路径。QLog 当前无对应 API，下面不计算 Q/BQ 比值。

| 生产者 | 场景 | BQLog ms [范围] | BQLog 百万次/秒 | QLog |
| ---: | --- | ---: | ---: | --- |
| 1 | 压缩，4 参数 | 155 [135–170] | 12.903 | 未支持 |
| 1 | 加密压缩，4 参数 | 160 [140–285] | 12.500 | 未支持 |
| 1 | Text，4 参数 | 698 [695–825] | 2.865 | 未支持 |
| 1 | 压缩，无参数 | 93 [80–109] | 21.505 | 未支持 |
| 1 | 加密压缩，无参数 | 94 [81–115] | 21.277 | 未支持 |
| 1 | Text，无参数 | 406 [366–499] | 4.926 | 未支持 |
| 4 | 压缩，4 参数 | 837 [606–884] | 9.558 | 未支持 |
| 4 | 加密压缩，4 参数 | 685 [551–867] | 11.679 | 未支持 |
| 4 | Text，4 参数 | 3286 [3265–3361] | 2.435 | 未支持 |
| 4 | 压缩，无参数 | 561 [464–566] | 14.260 | 未支持 |
| 4 | 加密压缩，无参数 | 512 [451–532] | 15.625 | 未支持 |
| 4 | Text，无参数 | 1416 [1384–3692] | 5.650 | 未支持 |

## 如何复现

先在 QLog 同级建立新的参考目录，避免覆盖原参考中的 MISO/CMake 草稿；固定本次 SHA：

```bash
git clone --filter=blob:none --sparse https://github.com/Tencent/BqLog.git /home/qq344/BqLog-benchmark-20261015
git -C /home/qq344/BqLog-benchmark-20261015 sparse-checkout set src include benchmark
git -C /home/qq344/BqLog-benchmark-20261015 checkout 086f2fd831af86fcfa40e90015f89626d498d3d7
```

在 WSL 的新构建目录中构建。以下目录应不存在；已经存在本次取证目录时，请换新后缀：

```bash
QLOG_BENCH_BUILD=/mnt/d/qq344/Documents/qlog-benchmark-repro/qlog-release
BQLOG_BENCH_BUILD=/mnt/d/qq344/Documents/qlog-benchmark-repro/bqlog-release
cmake -S /home/qq344/QLog -B "$QLOG_BENCH_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DQLOG_BUILD_BQLOG_BENCHMARK=ON -DCMAKE_CXX_FLAGS=-Werror \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$QLOG_BENCH_BUILD" --parallel 4
ctest --test-dir "$QLOG_BENCH_BUILD" --output-on-failure
cmake -S /home/qq344/BqLog-benchmark-20261015/benchmark/cpp \
  -B "$BQLOG_BENCH_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTARGET_PLATFORM=linux -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$BQLOG_BENCH_BUILD" --parallel 4
python3 /home/qq344/QLog/benchmark/cpp/run_benchmark.py \
  --qlog-exe "$QLOG_BENCH_BUILD/benchmark/cpp/QLogBenchmark" \
  --bqlog-exe /home/qq344/BqLog-benchmark-20261015/artifacts/benchmark/linux/Release/BqLogBenchmark \
  --bqlog-source /home/qq344/BqLog-benchmark-20261015 \
  --data-root /home/qq344/QLogBenchmarkData-repro \
  --evidence-dir /mnt/d/qq344/Documents/qlog-benchmark-repro/measured \
  --threads 1 4 --repeats 3 --min-host-free-gib 20
```

运行器只面向 Linux/Python 3，两个输出目录必须是新目录；它不自动删除既有测试数据。完整运行保留的数据约数十 GiB，开始前还应核对 Windows 宿主盘实际余量，不能只看 WSL `df` 的虚拟容量（[Microsoft WSL 磁盘说明](https://learn.microsoft.com/en-us/windows/wsl/disk-space)）。运行期间每秒检查宿主盘至少 20 GiB、guest 至少 8 GiB，发现库报错或 system_clock 相对单调时钟偏移超过 50 ms 就终止本轮。

## 证据与边界

- 最终采用：`/mnt/d/qq344/Documents/qlog-bqlog-benchmark-20261015-01/measured-final/` 中的 environment.json、raw-results.json、summary.json、12 份进程日志。仓库内摘录为 `/home/qq344/QLog/docs/validation/BQLOG_BENCHMARK_20261015_RESULTS.json`。
- 构建、重启后 CTest、MSVC 日志和 Windows 运行验证位于同一取证目录；`native-execution-check.json` 是本轮 Windows 验证边界。
- 初次测试发生 C 盘空间耗尽/WSL I/O 错误，且一项墙上时钟耗时 334613 ms 大于整个进程单调时钟 90.5 秒。该批次全部排除，没有挑取其中较快结果填表。
- 用户授权后暂停并重启 Ubuntu；266 个既有文件哈希核对除本轮 CMake 接入外均一致。80 个旧输出文件、17353660396 字节已逐文件 SHA-256 校验备份到 D 盘，再仅清理本轮旧输出目录；QLog 源码另有 tar.gz 备份。
- 恢复后 C 盘已有约 50 GiB 可用空间，完成了 guest fstrim；没有实际执行 Windows 离线 VHD 压缩。fstrim 返回的是虚拟磁盘丢弃范围，不能当作 C 盘实际回收量。恢复记录见 storage-incident.json、storage-recovery.json 和 initial-data-backup-manifest.json。
- 本次结果是当前实现与固定上游版本在本机的场景对比；不证明断电耐久性、完整日志零丢失、新版本解码互通、恢复/损坏容错、TSAN 或其他平台性能。库的 flush 调用被计时，不等于逐条同步落盘。
- 文件/字符/参数场景负载不同，未把它们合成一个整体胜率或峰值结论；不同场景中的差距需要另用 profiler 定位，不能直接归因于某个容器、UTF 软件路径或锁。

移植及许可说明见 `/home/qq344/QLog/NOTICE-BQLog.md`，BQLog 原始许可保留在 `/home/qq344/QLog/LICENSES/BQLog-APACHE-2.0.txt`。
