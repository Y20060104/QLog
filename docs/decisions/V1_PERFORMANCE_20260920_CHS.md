# QLog V1 性能实测

本报告只采用最终 CAS Runtime 版本的 `docs/validation/v1-final/performance-release/` 数据。`performance/` 与 `performance-final/` 是迭代过程数据，不作为最终结论。

## 环境与口径

- Intel Core i7-9750H，WSL2 Ubuntu，宿主暴露 12 个逻辑 CPU，进程固定使用 [0, 1, 2, 3, 4, 5, 6, 7] 的亲和池；这不等于专用隔离核心。
- c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0；Release，诊断 OFF；每 Producer 1 MiB Ring，每目标 256 KiB batch，默认 64 records/256 KiB Channel 配额。
- 固定整数、double、16 字节字符串混合正文，完整时间/等级/logger/category/tid 前缀，真实 TextFile 输出。每 Producer 先暖机 1000 条并 drain，再计时。
- 三轮串行测试，期间不并行编译或运行回归；表中为各轮指标的中位数，不是将全部调用混在一起重新计算分位数。
- accepted 吞吐 = accepted 条数 / 从开始投递到最终 shutdown 完成的时间。每目标逐条核对 accepted ID、重复、格式和每 Producer 顺序。双目标行数为 accepted 的两倍。
- 前台延迟每 67 次采样一次，与每 32 条限速批次互质，避免只测 sleep 后第一条。结果未扣除计时器开销；时钟连续读的 P50=0 ns、P99=99 ns，约百纳秒量化限制不能忽略。
- buffered 终点是 write/close；durable 场景只在最终 shutdown fdatasync 一次，不是每条同步。WSL 虚拟磁盘 fdatasync 不作为真实设备断电持久性证明。

## 成功吞吐与前台延迟

| 场景 | accepted 百万条/s | full/drop | accepted P50 ns | P99 ns | P99.9 ns | shutdown ms |
|---|---:|---:|---:|---:|---:|---:|
| 共享，1 Producer，不限速 | 1.450 | 92.31% | 100 | 299 | 803 | 5.03 |
| 共享，4 Producer，不限速 | 1.205 | 96.50% | 199 | 494 | 2670 | 14.64 |
| 独立，4 Producer，不限速 | 1.205 | 96.49% | 199 | 498 | 2670 | 19.49 |
| 共享，4 Producer，双文件，不限速 | 0.814 | 97.38% | 199 | 499 | 2708 | 33.50 |
| 共享，1 Producer，限速 50 万/s | 0.500 | 0.00% | 100 | 988 | 27256 | 0.45 |
| 共享，4 Producer，各限速 15 万/s | 0.599 | 0.00% | 197 | 1185 | 25478 | 2.02 |
| 共享，1 Producer，限速 50 万/s，最终 durable | 0.473 | 0.00% | 101 | 1009 | 26840 | 113.61 |

不限速场景是故意过载：Producer 的 full 快速返回远快于后台完整格式化/输出。高 drop 并未计入成功吞吐。限速场景三轮均 0 drop，且 accepted 全部完成目标输出。这证明这些固定负载在本机可以持续处理；没有预设或证明所有负载优于 BQLog。

独立 4 Producer 场景仍是一个 logger 的一个独立 worker，不是四个 worker。共享/独立差值包括调度噪声，不能解释为普遍性能优劣。单目标与双目标的实际输出字节不同，不用于相同工作量排名。

## 波动、资源与公平性

| 场景 | 三轮成功吞吐范围 百万/s | CPU 秒 | 峰值 RSS MiB | write 次数 | fdatasync 次数 | 各 Producer accepted 最小/最大 |
|---|---:|---:|---:|---:|---:|---:|
| 共享，1 Producer，不限速 | 1.333–1.485 | 1.100 | 17.6 | 500 | 0 | 1.000 |
| 共享，4 Producer，不限速 | 1.062–1.264 | 2.970 | 29.1 | 455 | 0 | 0.971 |
| 独立，4 Producer，不限速 | 1.119–1.226 | 3.048 | 29.4 | 456 | 0 | 0.973 |
| 共享，4 Producer，双文件，不限速 | 0.734–0.884 | 1.714 | 20.8 | 408 | 0 | 0.966 |
| 共享，1 Producer，限速 50 万/s | 0.500–0.500 | 1.167 | 17.6 | 646 | 0 | 1.000 |
| 共享，4 Producer，各限速 15 万/s | 0.597–0.600 | 1.916 | 17.6 | 773 | 0 | 1.000 |
| 共享，1 Producer，限速 50 万/s，最终 durable | 0.470–0.475 | 1.347 | 17.6 | 646 | 1 | 1.000 |

CPU、RSS 为整个进程，包含 benchmark 的 accepted 位图、采样数组、线程栈及 QLog；不是库自身净占用。RSS 在计时结束、文件逐条验证之前读取；文件校验的内存不计入这个值。write/fdatasync 使用链接包装计数，每次系统调用有一个原子计数开销。原始 JSON 同时保留尝试速率、每 Producer accepted、文件字节和全部采样量。

饱和场景中各 Producer 接受数量存在调度偏差，配额提供访问机会，不承诺每个 Producer 获得相同成功率。公平性专项验证另有持续忙 logger 与稀疏 logger 同时工作场景。

## 稀疏日志可见延迟与分层测量

- 默认 100 ms 刷新，60 个样本：文件大小可见 P50 **99.07 ms**，P99 **99.42 ms**，最大 **99.45 ms**。以 fstat 每 100 µs 轮询观察，包含轮询与调度误差，不等于 durable 延迟。
- 混合正文 `render_message_utf8`：**194.57 ns/条**；仅正文，不含解码、完整行和 I/O。
- 被过滤的 try_log：**2.68 ns/调用**；不含时间戳、编码、Ring 和输出，不能拿来代替成功日志成本。
- 两个微基准均为 9 组、每组 50 万次调用的平均值取中位数，不是单次 P50/P99。checksum 与可观察结果检查用于避免无效循环。
- 真实共享/独立线程的暖机后 4096 条日志及 shutdown，经 C++ new/new[]/aligned/nothrow 探针确认零分配；不宣称拦截了全部 libc 内部分配。

默认配置强调批次效率；前台约微秒量级 P99 与约 100 ms 稀疏文件可见延迟可以同时存在。应用如果要求更快可见，应明确调整 flush_interval_us 并复测系统调用与吞吐代价。

## 复现与边界

构建/运行命令见根 README 和 `tools/run_v1_performance.py`。完整环境、编译参数、亲和池、可执行文件 SHA256、每轮命令与逐条校验结果保存在 [原始证据](../validation/v1-final/performance-release/results.json) 和 [环境元数据](../validation/v1-final/performance-release/environment.json)。

当前工作树包含用户既有未提交内容；本轮没有提交或清理它。文件 SHA256 清单记录在 `../validation/v1-final/source-manifest.json`，便于冻结开发快照；这不是干净发布 commit。

这是 WSL2 开发性能证据。尚无原生 Linux 目标主机、远程 CI 执行或同口径 BQLog/spdlog 端到端对照结果，因此不作发布硬件性能保证、领先倍数或全平台结论。BQLog 在本轮作为实现参考，原有 Ring 微基准也不能替代整套日志系统对比。
