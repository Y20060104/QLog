# QLog V1主链实现与WSL性能报告（2026-10-14）

线程/CV、记录入口、Logger/Manager/Worker、Console/TextFile和Snapshot已补齐并接实际库；GNU/Linux与原生MSVC x64 Debug/Release均严格构建并执行31/31 CTest。性能主测使用WSL/Linux Release，Windows不计入性能结论。主测54次、156,000,000次调用；有效日志144,000,000条全部接收并完成输出，等级过滤12,000,000次明确返回false。文本60,000,000条完成数量、正文、重复与每生产者顺序核对。不是服务器、SLO、断电持久化或全部继承边界的安全验收。

证据：D:/qq344/Documents/qlog-v1-mainline-performance-20261014-01。measured-long/environment.json包含源文件/二进制SHA-256、Git状态与全部环境；raw-results.json包含每次命令和指标；summary.json包含3次中位数及范围。measured保存短轮54次，benchmark-dry-results.json保存调用方预检查，本报告表格仅使用扩大负载的measured-long。

## 实现与修正

实际采用QLog feat/spsc-ring-opt@c2faba4032c145de534d0a3d70cc05af58b523b4加当前工作树；BQLog固定78cbfbef4f87e7558335f0c04bf6dd6894b46451，通过git archive独立导出。原参考工作树有本地MISO/构建修改，未用于比较、未被修改。

修正条件变量误放src、POSIX中混入Windows定义及缺失命名空间；保留已有构造/internal_run/CR并补线程生命周期和两个CV后端。新增原log_imp/log_manager/log_worker/log_snapshot与Appender链、扩展原全局生命周期、公开qlog::log和begin/finish记录运输、两系统堆栈支持。已有Record/UTF/Layout/Config/HashMap直接复用。

明确差异：标准tuple/string用std::sort替代原qsort；短写重叠搬移用memmove；失败分配先检查结果再形成地址，空堆栈不调用空地址memcpy。当前格式拷贝及CRC哈希为memcpy后get_hash_64，未融合为BQ的一次处理；QLog Layout采用已确认软件路径，参考保留官方构建和CPU分派。普通UTF32使用既有UTF16协议，custom四字节字符仍未决。raw/compressed/Decoder/跨语言/自动崩溃信号处理未扩项。详见[实现决定](../decisions/V1_RUNTIME_MAINLINE_COMPLETION_20261014_CHS.md)。

## 环境与测量合同

- CPU：Intel Core i7-9750H，6核12线程；进程允许CPU 0–11，未固定单核亲和性或关闭宿主其他任务。
- 系统：Linux-6.6.87.2-microsoft-standard-WSL2-x86_64-with-glibc2.39；GCC版本：g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0.
- 主测时间：2026-10-14T15:09:30.798900+00:00 至 2026-10-14T15:13:20.502349+00:00；代码/二进制在测量期间未改变。
- QLog C++20、Release -O3 -DNDEBUG；BQLog按其官方CMake的C++17、Release、禁异常/RTTI规则构建。benchmark调用方统一C++20，同一源码、消息、sink和计时边界；非逐编译选项完全相同的内核实验。
- 字面格式record p={} seq={} a={} text={}；参数为生产者索引、uint64序号、uint64 123456及32字节ASCII字符串。Console为相同的计数回调，包含每条2次原子fetch_add，没有终端I/O；TextFile写/home/qq344的Linux文件系统，不写/mnt/d。
- LP/文件缓存均64KiB，满策略block；LP阈值0禁用频率切换；HP阈值1并在同一生产线程预热、等待1.1秒。未在热路径增加LP/HP统计计数器，HP是该配置/预热组合，不称每条都已独立观测。
- 单生产者每次2,000,000条，四生产者每线程1,000,000条；每场景每库3次，顺序Q/BQ、BQ/Q、Q/BQ。报告中位数、min/max，3次重复不是统计置信区间。
- producer时间包含开始屏障、全部生产者完成及其TLS退休；drained时间再包含force_flush完成。force_flush将Appender用户缓存写到OS，不等同fdatasync/断电持久化；文件内容核对在计时区间之外。
- 每256次调用采样一次延迟；单生产者约7,813个样本，四生产者约15,628个。P99.9和max只描述样本，固定周期可能与批次耦合，max不是全部调用最大值。

## 排空完成吞吐

单位M条/s；正常场景accepted与attempted相等、reject为0。比值为QLog/BQLog。括号给3次min–max。

| 场景 | QLog中位数（范围） | BQLog中位数（范围） | Q/BQ |
| --- | ---: | ---: | ---: |
| async / Console回调 / 1生产者 / LP | 1.901 (1.875–1.991) | 2.004 (1.970–2.312) | 0.949 |
| async / Console回调 / 4生产者 / LP | 1.823 (1.759–1.849) | 1.838 (1.822–1.848) | 0.992 |
| independent / Console回调 / 1生产者 / HP预热 | 2.106 (2.034–2.481) | 2.733 (2.385–2.960) | 0.771 |
| independent / Console回调 / 4生产者 / HP预热 | 2.063 (2.014–2.199) | 2.452 (2.294–2.547) | 0.842 |
| sync / Console回调 / 1生产者 | 2.418 (2.414–2.621) | 2.131 (2.039–2.322) | 1.135 |
| async / TextFile / 1生产者 / LP | 1.507 (1.480–1.584) | 1.406 (1.344–1.507) | 1.072 |
| async / TextFile / 4生产者 / LP | 1.199 (1.185–1.230) | 1.290 (1.272–1.297) | 0.929 |
| independent / TextFile / 4生产者 / HP预热 | 1.387 (1.184–1.432) | 1.367 (1.365–1.395) | 1.014 |

本机主测中，async Console四生产者约为参考99.2%；independent Console单/四生产者约为77.1%/84.2%，是明确需要继续定位的差距。sync中位数领先13.5%，async Text单生产者领先7.2%，independent Text四生产者约相近；文本和部分Console重复范围重叠，短轮与长轮结果也有变化，不据此声称普遍优于BQLog。

等级过滤单独是微探针：QLog中位数266.666M调用/s，BQLog464.906M调用/s，全部因等级禁用返回false，没有产生日志；区间仍较短、计时粒度可见，不与正文吞吐或discard满策略混算。

## 调用端延迟

以下为3次各自样本分位数的中位数，单位微秒。最后两列是所有重复中的样本最大值，而不是中位数。

| 场景 | Q P50 | Q P99 | Q P99.9 | BQ P50 | BQ P99 | BQ P99.9 | Q最大样本 | BQ最大样本 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| async / Console回调 / 1生产者 / LP | 0.204 | 0.511 | 0.714 | 0.205 | 0.513 | 0.616 | 340.645 | 125.968 |
| async / Console回调 / 4生产者 / LP | 0.302 | 37.873 | 67.496 | 0.302 | 39.808 | 66.305 | 343.503 | 744.399 |
| independent / Console回调 / 1生产者 / HP预热 | 0.200 | 0.501 | 86.480 | 0.101 | 0.501 | 54.574 | 157.125 | 160.755 |
| independent / Console回调 / 4生产者 / HP预热 | 0.202 | 0.606 | 578.836 | 0.196 | 0.504 | 320.449 | 900.569 | 894.375 |
| sync / Console回调 / 1生产者 | 0.391 | 1.103 | 1.203 | 0.401 | 1.103 | 1.203 | 15.359 | 33.128 |
| async / TextFile / 1生产者 / LP | 0.195 | 0.472 | 0.754 | 0.201 | 0.502 | 0.692 | 29.936 | 23.507 |
| async / TextFile / 4生产者 / LP | 0.301 | 117.293 | 200.252 | 0.303 | 112.874 | 201.700 | 7738.418 | 10925.602 |
| independent / TextFile / 4生产者 / HP预热 | 0.198 | 0.500 | 1041.892 | 0.199 | 0.502 | 166.696 | 2223.118 | 2837.393 |

HP预热组合的P99较低，但P99.9并不始终较低：QLog independent/Text四生产者P99约0.500µs，P99.9约1.042ms，参考约0.167ms。不能只看P99就认为尾部已经收口；block策略下的等待、回收和调度需要单独剖析。当前没有profiler证据，不能把差距全部归因于SIMD、哈希或某条原子操作。

## CPU、内存与输出

CPU为进程各线程user+system秒/排空墙钟秒，即平均占用核心数；不是整机百分比。RSS为getrusage报告的进程峰值，包含整个进程并受子进程启动方式影响，不能解释为Logger增量堆内存。

| 场景 | Q CPU核 | BQ CPU核 | Q RSS MiB | BQ RSS MiB | 单次文件MiB |
| --- | ---: | ---: | ---: | ---: | ---: |
| async / Console回调 / 1生产者 / LP | 1.95 | 1.95 | 19.10 | 19.10 | 0.00 |
| async / Console回调 / 4生产者 / LP | 4.95 | 4.94 | 19.10 | 19.10 | 0.00 |
| independent / Console回调 / 1生产者 / HP预热 | 2.00 | 2.00 | 19.10 | 19.10 | 0.00 |
| independent / Console回调 / 4生产者 / HP预热 | 4.38 | 3.90 | 19.10 | 19.10 | 0.00 |
| sync / Console回调 / 1生产者 | 1.01 | 1.00 | 19.10 | 19.10 | 0.00 |
| async / TextFile / 1生产者 / LP | 2.04 | 1.99 | 20.72 | 20.72 | 258.34 |
| async / TextFile / 4生产者 / LP | 4.96 | 5.00 | 22.59 | 22.59 | 514.56 |
| independent / TextFile / 4生产者 / HP预热 | 4.62 | 3.81 | 22.59 | 22.59 | 514.56 |

两库Text同场景的文件字节数相等；除线程ID、时间等运行信息外，正文逐项验证固定字符串与生产者/序号。主测累计输出8,100,001,860字节文本（约7.54GiB），60,000,000条。原始JSON及输出证据保留；记录计数无缺失、重复或生产者逆序。

## 构建和回归证据

| 环境 | 库cpp数 | Debug | Release |
| --- | ---: | --- | --- |
| GNU/Linux | 50 | 严格构建，31/31实际CTest | 严格构建，31/31实际CTest |
| 原生MSVC x64 | 51 | 严格构建，31/31实际CTest | 严格构建，31/31实际CTest |

GNU使用-Werror及根库警告；MSVC仅沿用外部验证/wd4324 /wd4146，生产警告未放宽。PUBLIC QLOG_DEBUG仅Debug定义，NDEBUG仍控制标准assert。新增11项验证涵盖线程/条件等待、三模式、参数序列化、不同类别规则、reset、四生产者顺序、文件缓存/滚动、堆栈/Console缓冲、Oversize、满策略、Snapshot迁移。库/调用方都来自实际源码，不使用候选覆盖或替身。旧47项已删除Ring测试不计入。

本轮早期失败包括新源码迁移限定名/标准容器转换、Windows min/max与DbgHelp包含顺序；均已修正。文件滚动测试按已写入文件大小修正，测试解析用from_chars满足原生严格编译。最后四配置31/31，不以早期失败或旧日志代替最终结果。

2026-10-15收口复核：final-validation-matrix.json记录四组实际编译命令逐翻译单元的宏核对（Linux读取Ninja命令，Windows读取compile_commands.json），全部通过；主测清单中的140个生产源码文件仍与当前工作区逐项SHA-256一致。git diff --check通过，qlog-final.json、bqlog-final.json保存最终Git快照；参考HEAD及脏状态未改变。本轮未暂存、提交或推送。原始文本保留在/home/qq344/QLogPerfData-20261014-01/measured-long，主测约7.54GiB输出未删除。

## 复现

```sh
cmake -S /home/qq344/QLog -B /tmp/qlog-runtime-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_CXX_FLAGS=-Werror -DQLOG_BUILD_RUNTIME_BENCHMARK=ON
cmake --build /tmp/qlog-runtime-release -j 6
ctest --test-dir /tmp/qlog-runtime-release --output-on-failure
python3 /home/qq344/QLog/tools/run_runtime_benchmark.py \
  --qlog-exe /tmp/qlog-runtime-release/qlog_runtime_benchmark \
  --bqlog-exe /mnt/d/qq344/Documents/qlog-v1-mainline-performance-20261014-01/bqlog-release/bqlog_runtime_benchmark \
  --evidence-dir /tmp/qlog-runtime-reproduce-evidence \
  --data-root /home/qq344/QLogPerfData-reproduce \
  --records-one 2000000 --records-many 1000000 --repeats 3
```

复现data-root须为新的路径，脚本拒绝覆盖旧数据；参考的固定源码与comparison-project已保存在证据目录。

## 当前结论及下一验证边界

目前主链已经可运行，常规功能和这次负载数据成立；无需回退旧Logger接口。下一性能工作应从independent Console吞吐与HP P99.9差距定位：分离序列化/格式复制哈希、运输、Layout与回调的成本，增加持续负载、采样策略和CPU剖析，再决定融合复制哈希或SIMD。该顺序是后续建议，本轮未把推测当优化结果。

仍未完成全部BQ差分、异常/短写故障注入、进程崩溃恢复、回绕/极端容量、TSAN与长期稳定性验收；沿用参考的已知原子、typed访问、普通字段/无锁读取等边界见决定文档。未决配置越界两方案和custom UTF32不因本轮自动选定。
