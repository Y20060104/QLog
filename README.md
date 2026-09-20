# QLog

QLog 是 C++20 异步日志库，当前 V1 开发目标为 Linux。Producer 将原始格式串与自包含参数写入每线程 SPSC Ring；后台线程解码、按 BQLog UTF-8 规则格式化，并写入 Console/TextFile 批次。

默认共享 worker，可选择每 logger 独立 worker。内置 Runtime 自动启动，无需安装 provider。当前包含动态 Appender reset、flush/drain、文件错误恢复、安全 shutdown；格式支持和限制见 [ADR-016](docs/decisions/ADR-016-v1-bqlog-worker-format.md)，输出合同见 [ADR-017](docs/decisions/ADR-017-v1-output-and-completion.md)。

## 构建和运行

```sh
cmake -S . -B build/v1 -DCMAKE_BUILD_TYPE=Release -DQLOG_BUILD_EXAMPLES=ON
cmake --build build/v1 -j 4
ctest --test-dir build/v1 --output-on-failure
./build/v1/qlog_v1_logging_demo /tmp/qlog-demo.log
./build/v1/qlog_v1_logging_demo /tmp/qlog-independent.log independent
```

测试使用 GoogleTest，首次配置需要下载依赖，或者指定已有源码路径 `-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/googletest`。仅构建库可以加 `-DBUILD_TESTING=OFF`。项目内通过 `QLog::qlog` 链接，线程依赖和诊断宏随 target 传递。

```cpp
#include <qlog/async_logger.hpp>

qlog::LoggerConfig config;
config.name = "app";
qlog::AppenderConfig file;
file.name = "file";
file.type = qlog::AppenderType::TextFile;
file.file.path = "/tmp/app.log";
config.appenders = {file};
// config.backend.thread_mode = qlog::ThreadMode::independent;
qlog::AsyncLogger logger(config);
auto admitted = logger.try_log(0, qlog::LogLevel::info, "value={}", 42);
// accepted 仅表示成功入队。full 按 drop_new 返回，不自动重试。
// 停止并 join 所有使用 logger 的业务线程，结束其他管理调用后：
const auto& result = logger.shutdown(qlog::FlushMode::durable);
```

完整例子见 [examples/v1_logging_demo.cpp](examples/v1_logging_demo.cpp)，包含多输出、管理完成轮询、reset 和 shutdown 错误检查。

## 使用契约

- 格式串在 Producer 原样复制，后台解析；不是完整 `std::format` 语义。零参数原样输出。动态格式串使用带显式长度的 `runtime_format`，不要把裸指针当成数组入口。
- 每个 logger 限定一个管理调用者。提交返回 submitted 后，轮询对应 request_id 并领取完成结果；未领取时邮箱仍 busy。
- `request_flush_batches` 只处理已经进入输出批次的内容；`request_drain` 调用前停止 Producer，并保持停止到完成结果领取。
- shutdown 前停止并 join 所有业务使用者；`close_registration` 只关闭新 Context 注册。重复 shutdown 返回稳定结果。
- buffered 表示普通写出；durable 对 TextFile 执行一次 fdatasync，不是每条日志同步。Console 不提供磁盘持久化保证。
- 默认 66 ms 兜底扫描、100 ms 批次刷新/文件重试。稀疏日志不会因每次入队立即唤醒；低延迟需求可调整刷新周期，代价是更多 I/O。
- 同路径多 logger/多目标写入不提供跨写入者顺序和恢复协调；为独立写入者选择不同路径。
- 故障目标可跳过后续选中投递；检查 shutdown 的错误、历史事件和 lost_bytes。accepted 不等于成功写出或持久化。
- 共享模式的同步慢 I/O 会影响同线程其他 logger；独立模式隔离 worker 调度，不隔离磁盘。
- Runtime 登记节点在 worker 退出后回收；频繁构造销毁共享 logger 会保留少量节点到 Runtime 结束，Ring/Session/输出批次在 logger 关闭后释放。

## 验证与性能

[V1 验收报告](docs/decisions/V1_FINAL_ACCEPTANCE_20260920_CHS.md) 和 [性能报告](docs/decisions/V1_PERFORMANCE_20260920_CHS.md) 记录实际环境、测试结果和局限。WSL2 开发证据与原生 Linux 发布复核分开；仓库包含 [Linux CI](.github/workflows/v1-linux.yml)，工作流尚未在远程运行不能算发布验收通过。

### 实测结果摘要（WSL2 开发环境，不作为原生 Linux 发布结论）

环境：Intel Core i7-9750H、WSL2 Ubuntu、GCC 13.3.0、Release、诊断 OFF，进程固定亲和池 [0,1,2,3,4,5,6,7]；每 Producer 1 MiB Ring、每目标 256 KiB 批次，真实 TextFile 输出，三轮串行取中位数。

| 场景 | accepted 百万条/s | full/drop | accepted P50 | P99 | P99.9 | shutdown |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 共享 worker，1 Producer，不限速 | 1.450 | 92.31% | 100 ns | 299 ns | 803 ns | 5.03 ms |
| 共享 worker，4 Producer，不限速 | 1.205 | 96.50% | 199 ns | 494 ns | 2670 ns | 14.64 ms |
| 独立 worker，4 Producer，不限速 | 1.205 | 96.49% | 199 ns | 498 ns | 2670 ns | 19.49 ms |
| 共享 worker，4 Producer，双文件，不限速 | 0.814 | 97.38% | 199 ns | 499 ns | 2708 ns | 33.50 ms |
| 共享 worker，1 Producer，限速 50 万/s | 0.500 | 0.00% | 100 ns | 988 ns | 27.3 μs | 0.45 ms |
| 共享 worker，4 Producer，各限速 15 万/s | 0.599 | 0.00% | 197 ns | 1185 ns | 25.5 μs | 2.02 ms |
| 共享 worker，1 Producer，限速 + durable | 0.473 | 0.00% | 101 ns | 1009 ns | 26.8 μs | 113.61 ms |

不限速场景是故意过载：full 快速返回远快于后台完整格式化/输出，高 drop 不计入成功吞吐；限速场景三轮均 0 drop 且全部完成目标输出，证明固定负载可被持续处理，不代表所有负载优于任何对照库。

与 BQLog 原生默认配置的配对对照（8 个饱和负载、102 次正式试验，WSL2 同上）：QLog/BQLog 成功输出吞吐指数 **0.855×**，95% 经验 bootstrap 区间 [0.793, 0.921]。QLog 保留默认满队列拒绝、BQLog 保留默认阻塞，报告同时公开拒绝比例、CPU 成本与 API 延迟，不把队列吸收率当成功写出率。复现与完整口径见 [BQLog 对照报告](docs/decisions/V1_BQLOG_COMPARISON_20260920_CHS.md)。

```sh
cmake -S . -B build/v1-perf -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DQLOG_ENABLE_DIAGNOSTICS=OFF -DQLOG_BUILD_BENCHMARKS=ON -DQLOG_BENCH_WITH_BQLOG=OFF
cmake --build build/v1-perf -j 4
python3 tools/run_v1_performance.py --evidence docs/validation/local-performance
```

脚本固定可用 CPU 的前 8 个组成亲和池，串行运行三次；输出保留在 build 目录，逐条核对每目标 accepted ID、重复和顺序。报告分别列出成功吞吐、full、前台分位延迟和最终 durable 边界，不把队列吸收率当成功写出率。

最新官方 BQLog 的 WSL2 对照实测见 [BQLog 对照报告](docs/decisions/V1_BQLOG_COMPARISON_20260920_CHS.md)，包含版本、36 次运行结果及两库配置差异。

原生默认配置的产品工作负载评测见 [原生性能报告](docs/decisions/V1_NATIVE_PRODUCTION_PERFORMANCE_20260920_CHS.md) 与 [复现说明](benchmarks/production/README_CHS.md)。该套件保留 QLog 的拒绝策略和 BQLog 的阻塞策略，分别报告成功输出吞吐、交付比例、API 延迟和 CPU 成本；总体倍数仅适用于报告声明的 WSL2 环境与固定场景集合。
