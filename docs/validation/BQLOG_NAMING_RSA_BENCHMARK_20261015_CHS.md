# 命名、RSA 与官方 benchmark 复测记录（2026-10-15）

QLog 实际起点 `ee3ac945397e8a6628192ebef88b4e79e8ec2cd2`，已包含本轮未提交命名、宏及 RSA 改动。生产参考固定 78cbfbef；benchmark 独立参考固定 `086f2fd831af86fcfa40e90015f89626d498d3d7`，其跟踪源码干净。实现差异见[决定](../decisions/BQLOG_ALIGNMENT_GAPS_20261015_CHS.md)，正常运行方式与完整结果表见[README](../../README.md)。

## 实际源码保护

开始时保存 408 个跟踪文件的 SHA-256 和逐字节副本。改动前后核查，生产变动属于已记录的类型/路径/宏/Worker名以及 RSA 两个零整数分支；SISO 的头/cpp按命名映射归一化后与改动前完全相同，CR 均保留。原 `benchmarks/adapters/qlog_spsc_adapter.hpp` 字节完全保留，未接回旧 Ring。

本轮 RSA 按用户单独确认恢复原 `n=1` 后范围复制的越界行为，撤回两条零输入安全断言；正常密钥/非零回转测试保留。下列 CTest 结果不赋予全零/空输入确定结果或越界安全保证。此前压缩互通的 40 文件/24008 记录是旧源码的历史结果，本轮未重跑该解码互通矩阵。

## 四配置构建和回归

| 环境 | 库 cpp | 全部 TU | Debug | Release |
| --- | ---: | ---: | --- | --- |
| GNU/Linux | 57 | 69 | 严格构建、40/40 CTest | 严格构建、40/40 CTest |
| 原生 MSVC x64 | 58 | 70 | 严格构建、40/40 CTest | 严格构建、40/40 CTest |

这四个构建均包含当前库、常驻测试、`QLogBenchmark` 和 `qlog_runtime_benchmark`。逐 TU 核对 `QLOG_DEBUG` 只在 Debug 定义，`NDEBUG` 只在 Release 定义。GNU 使用项目警告与 `-Werror`；MSVC 使用 `/W4 /WX /permissive- /utf-8 /EHsc`，仅沿用 C4324/C4146 排除，没有新增警告豁免。Windows 本轮实际运行的是 CTest，不把此前官方 benchmark 的 Windows 运行结果当成本轮复测。

构建/CTest证据：`/mnt/d/qq344/Documents/qlog-naming-rsa-actual-20261015-01` 中 `linux-{Debug,Release}-*`、`native-{Debug,Release}-*` 和编译命令；`resume-audit.json` 记录命名与 CR 核查。

## 新 benchmark 批次

采用目录：`/mnt/d/qq344/Documents/qlog-alignment-benchmark-20261015-03/measured`；输出目录：`/home/qq344/QLogBenchmarkData-20261015-05`。按原每生产者每吞吐场景 200 万次，1/4 生产者各三轮，Q/BQ、BQ/Q、Q/BQ 顺序。12 个进程完整完成；普通接口共 7.8 亿次、FAST 额外 1.8 亿次，名义吞吐调用合计 9.6 亿次，预热与延迟另计。FAST 未在 QLog 上模拟。

沿用两库 Release 配置，消息、参数、默认 LP/HP 策略与毫秒 `system_clock` 计时不改。QLog benchmark C++20，BQLog 原入口 C++17，库分别保留现有项目编译选项。未绑核。每条耗时按三轮中位数/范围报告，不挑单次最优作为结论。写侧延迟是每 20 次调用的批次平均值分位数，三轮分位数取中位数、worst取三轮最大值。

源码清单指纹：`92a4feba711bc2c0eae39cdf8e5838316f3bba0e25bed3a277c1dd1b55755c3c`。两个二进制 SHA-256：

```json
{
  "qlog": "451f05f60dc309575322934c36228a17a67f2c60b023d84c81aa05f589a19d58",
  "bqlog": "2090e98362de3722b5d9306c3187259d7e53637348eda01276367e619ba15dbc"
}
```

测量前后 QLog 和 BQLog 源清单均无漂移。完整 source/reference manifest、机器环境及编译器见 `environment.json`，12 份原始 stdout、逐轮明细与汇总分别在同目录。机器摘录另存 `BQLOG_BENCHMARK_20261015_RERUN_RESULTS.json`；此前结果文件保持原样。

## 环境诊断归档

本轮首次负载运行在 `qlog-alignment-benchmark-20261015-02/measured` 因墙上/单调时钟偏移变化超过 50ms 被中止；随后一次空闲预检查失败，另一次预检查通过但负载阶段再次触发校验。相关失败批次均未用于本轮结果，日志与 `invalid-run.json` 保留。

实际采用批次临时停止 `systemd-timesyncd.service`，仍保留 benchmark 原 `system_clock` 计时；两库均以 qq344 用户运行，结束后在 finally 中恢复 active。前、中、后服务状态及预检查记录位于 `/mnt/d/qq344/Documents/qlog-alignment-benchmark-20261015-03/time-service.json`。这项实验消除了本批次的时钟校验失败，未修改系统日期或重启 Ubuntu；不能据此断言此前跳变的所有根因均已查清。

从旧 README 移出的上一批历史经过仍保留在原证据：初批 C 盘满/WSL I/O，以及 334613ms 大于进程单调墙时 90.5s 的异常批次已全部排除。用户授权恢复后，266 个既有文件核查除当轮 CMake 外一致；80 个旧输出、17353660396 字节逐文件 SHA 校验备份到 D 盘再清理。恢复时 C 盘约 50GiB 可用，执行 guest fstrim，未实际执行离线 VHD 压缩。这些属于历史环境记录，见 `qlog-bqlog-benchmark-20261015-01` 的 `storage-incident.json`、`storage-recovery.json` 与 `initial-data-backup-manifest.json`。

## 测量边界

官方 benchmark 按调用次数统计，未记录每次返回值。配置含文件保留清理，末尾文件不能代替全量记录计数或压缩比验收。flush 被计时不等于每条 fdatasync；本次不计断电、恢复损坏、TSAN、其他平台性能或新参考解码互通。不同负载不合成整体胜率，容器/UTF/锁的耗时归因需要单独 profile。
