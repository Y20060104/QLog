# 最新官方 BQLog 与 QLog：WSL2 性能记录

## 版本与源码

用户要求先 pull 最新 BQLog 再测试。官方 Tencent/BqLog main 与本地 HEAD 均为 **60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9**，pull 返回 Already up to date；网络与拉取记录见 `docs/validation/bqlog-comparison/upstream-pull-result.txt`。

原 `/home/qq344/BqLog` 有用户未提交修改，包括 MISO head 对齐、调试统计及构建文件。本次保留这些修改，使用 `git archive HEAD` 导出的干净官方快照构建，不使用旧静态库，也不把本地修改版当成官方版。构建位于 QLog/build，未修改 BQLog 生产源码。

QLog 使用当前 V1 Release、诊断 OFF；生产 src/include 文件 SHA256 与刚完成验收并构建的 Release 版本逐个核对一致。新建比较专用入口，把计时终点从 shutdown 改为 request_drain(buffered) 完成，旧性能报告不用于这次表格。

## 方法

- 同机 i7-9750H、WSL2 Ubuntu、GCC Release；两个进程继承同一个 8 逻辑 CPU 亲和池。
- 六场景，每库三轮，共 **36 次运行**；逐场景顺序执行，第二轮交换库的先后顺序，无并行跑分。
- 相同混合正文：两个整数、一个 double、16 字节字符串；相同投递次数与 Producer 数量；每线程暖机 1000 条，排空后开始计时；每 67 次采样一次前台调用。
- QLog 每 Producer Ring 为 1 MiB；BQLog 配置 log.buffer_size=1048576，buffer_policy_when_full=discard，recovery=false，保留默认自适应队列策略。两者写缓存配置均为 256 KiB。
- QLog 终点为停止 Producer 后 request_drain(buffered) 完成，BQLog 为停止 Producer 后 force_flush 返回；均为 buffered 文本写出，不测逐条 durable，也不将进程析构算入。
- accepted 吞吐包括结束阶段排空时间。逐目标核对全部成功调用的 ID、数量、重复、正文和各 Producer 顺序，**36 次全部通过**。

## 吞吐中位数

单位：百万 accepted 记录/s；双文件场景每条 accepted 记录写两份。P99 仅统计成功调用，单位 ns。

| 场景 | QLog | BQLog | QLog/BQLog 观测比值 | accepted P99 QLog / BQLog |
|---|---:|---:|---:|---:|
| 共享 / 1 Producer / 不限速 | 1.308 | 1.274 | 1.03 | 305 / 297 |
| 共享 / 4 Producer / 不限速 | 1.212 | 0.623 | 1.95 | 496 / 397 |
| 独立 / 4 Producer / 不限速 | 1.246 | 0.571 | 2.18 | 403 / 397 |
| 共享 / 4 Producer / 双文件 / 不限速 | 0.791 | 0.323 | 2.45 | 601 / 496 |
| 共享 / 1 Producer / 限速 50 万/s | 0.499 | 0.500 | 1.00 | 892 / 803 |
| 共享 / 4 Producer / 合计限速 60 万/s | 0.597 | 0.600 | 1.00 | 1186 / 1129 |

限速场景由输入速率决定吞吐，不能用接近 1 的比值证明后台服务上限相同。不限速场景是固定次数短时过载，不能把尝试吞吐或大量快速拒绝算作成功日志。

## 波动、拒绝率、尾延迟与文件字节

| 场景 | 库 | 三轮吞吐范围 百万/s | 未接受率 | accepted P50 / P99 / P99.9 ns | 最终排空 ms | 平均文件字节/行（含暖机） |
|---|---|---:|---:|---:|---:|---:|
| 共享 / 1 Producer / 不限速 | qlog | 1.275–1.442 | 93.21% | 100 / 305 / 2482 | 6.02 | 127.8 |
| 共享 / 1 Producer / 不限速 | bqlog | 1.086–1.407 | 91.64% | 100 / 297 / 412 | 2.83 | 124.8 |
| 共享 / 4 Producer / 不限速 | qlog | 1.180–1.216 | 96.44% | 198 / 496 / 4865 | 10.63 | 127.4 |
| 共享 / 4 Producer / 不限速 | bqlog | 0.617–0.647 | 97.95% | 198 / 397 / 2532 | 11.28 | 124.2 |
| 独立 / 4 Producer / 不限速 | qlog | 1.159–1.273 | 96.48% | 198 / 403 / 2720 | 11.01 | 127.4 |
| 独立 / 4 Producer / 不限速 | bqlog | 0.571–0.633 | 97.87% | 198 / 397 / 2577 | 4.43 | 124.2 |
| 共享 / 4 Producer / 双文件 / 不限速 | qlog | 0.693–0.827 | 97.45% | 198 / 601 / 3569 | 38.91 | 126.8 |
| 共享 / 4 Producer / 双文件 / 不限速 | bqlog | 0.302–0.379 | 98.91% | 198 / 496 / 2814 | 19.02 | 122.9 |
| 共享 / 1 Producer / 限速 50 万/s | qlog | 0.499–0.500 | 0.00% | 100 / 892 / 26628 | 2.38 | 126.9 |
| 共享 / 1 Producer / 限速 50 万/s | bqlog | 0.500–0.500 | 0.00% | 101 / 803 / 29226 | 1.03 | 123.9 |
| 共享 / 4 Producer / 合计限速 60 万/s | qlog | 0.597–0.600 | 0.00% | 99 / 1186 / 24268 | 10.51 | 126.5 |
| 共享 / 4 Producer / 合计限速 60 万/s | bqlog | 0.599–0.600 | 0.00% | 205 / 1129 / 29066 | 1.33 | 123.5 |

BQLog 公共 info API 只返回 bool，false 在结果中记录为 rejected；本负载有效且使用 discard，但 API 无法进一步区分所有失败原因，所以不声称 false 全部是 full。QLog full 单独由状态码识别。原始结果含 CPU、RSS、write 次数和各 Producer 接受数；RSS 包含基准本身的位图和采样容器，不是库净占用。

## 不能忽略的差异

1. **并非相同原始输出字节。** QLog 使用纳秒时间、logger/category、Linux tid 的前缀；BQLog 使用自己的毫秒时间、线程 ID/线程名和等级前缀。正文相同，实际每行字节数见表，不将结果称为严格等字节算法排名。
2. **相同 buffer_size 数字不等于完全相同总内存预算。** QLog 固定每 Producer SPSC；BQLog 默认带 MISO/SISO 自适应及分组存储。保留官方默认高频切换阈值，未强制改造成 QLog 队列拓扑；千条暖机不保证所有内部迁移都已结束。
3. **结束阶段执行者不同。** 本版本 BQLog force_flush 通过 log_manager::force_flush 调用 process(true)，可能由调用线程完成尾部消费；QLog drain 仍由 worker 消费，调用线程轮询结果。本表是完整管线到 buffered 完成的观测，不是隔离 worker 的纯服务率。
4. **持久化没有对照。** 本版本 BQLog process(true) 冲写缓存，不等价于 QLog durable/fdatasync；因此未将两者放入同一 durable 排名。BQLog 文件轮转和退出还可能有额外 I/O，本次大文件配置避免轮转，进程退出在计时区外。
5. **数据范围有限。** 这是单机 WSL2、指定正文和固定次数下三轮观察，时钟约百纳秒量化、宿主调度和 CPU 频率都会影响结果。不能据此宣布 QLog 普遍快于 BQLog；原生 Linux、长时间稳态、严格统一输出字节和更多负载需另行测试。

## 复现与证据

- 运行工具：`tools/run_bqlog_comparison.py`；运行前先在同级 BqLog 执行 git pull，再在 QLog 根目录运行该脚本。
- 适配器：`benchmarks/bqlog_async_benchmark.cpp`、`benchmarks/qlog_comparison_benchmark.cpp`。
- 官方库从干净快照重新构建；构建命令见 `docs/validation/bqlog-comparison/build-commands.json`。
- 原始数据：[results.json](../validation/bqlog-comparison/results.json)；汇总：[summary.json](../validation/bqlog-comparison/summary.json)；环境：[environment.json](../validation/bqlog-comparison/environment.json)。
- 源码版本、本地修改备份、源码及可执行文件 SHA256、每轮 stdout/stderr、实际输出路径均记录在同目录。原始大文件保留在 QLog/build/qlog-bqlog-comparison-*，不作为仓库源码提交。

本轮没有改动 QLog 或 BQLog 的生产实现，没有提交 Git 或发布。
