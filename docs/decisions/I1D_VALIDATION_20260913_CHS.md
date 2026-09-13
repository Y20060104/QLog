# I1-D 测试实施与阶段验收报告

> 后续状态见 [继续验收报告](./I1D_ACCEPTANCE_20260913_CHS.md)：已补齐 WSL 覆盖率门槛、完整性能场景及连续一小时 decoder fuzz。本报告保留为前一阶段记录。

日期：2026-09-13（工作从 9 月 12 日开始）。实际仓库 `/home/qq344/QLog`，WSL2 Ubuntu。
基准 commit：`3750e6531e75d4504509be792bc4e109097266ec`。工作树有既有及本轮未提交内容，准确源码以 build 目录的 SHA-256 清单为准。

## 结论

本轮由 Codex 编写并执行测试支持代码。生产 hash/measure/encoder/decoder 算法未修改。
六配置回归、23 对 compile-fail/pass、固定种子性质测试、永久语料回放、四个 fuzz target、
原始覆盖率报告和首版 GCC/Clang Record Core 性能基线均已执行。
**I1-D 尚未整体关闭**：原始覆盖率未满足全部数值门槛，完整性能场景与发布候选环境仍有缺口。
本报告不把执行了测试等同于所有 DoD 已通过。

## 测试交付

- `tests/compile_fail` / `compile_pass`：23 对真实 API 调用；GCC/Clang 均验证合法成功、非法失败及 QLog constraint token。
- `tests/cmake/check_compile_case.cmake`：独立 driver，不使用 WILL_FAIL；另用不存在的编译器验证 driver 正确拒绝环境失败。
- `tests/record_property_test.cpp`：seed=0x20260912，10000 组输入，独立 scalar/string 模型、长度一致、位模式、强失败保证、hash 一致和 cstr 长度缓存。
- `tests/i1d_boundaries.cpp`：checked add/mul、u32/size_t 边界及各个 MeasureError 的命名测试。
- `tests/i1d_error_names.cpp`：所有 7 个 EncodeError 和 19 个 DecodeError 的直接命名测试与定位断言。
- property executable 内共 41 个 GoogleTest 用例，作为一个 CTest entry 注册；不是只有一个断言。
- `tests/corpus/record_core`：独立构造的全部 tag、截断和空/32 参数 seed；常规测试逐文件重放。
- `tests/fuzz`：四个 libFuzzer 入口，真实生产对象与 harness 都有 ASan/UBSan 插桩。

## 最终编译矩阵

GCC 13.3 / Clang 18.1；C++20，严格 warning 和 -Werror；sanitizer 使用 no-recover 与 frame pointer。
120 个 CTest entry = 既有 96 + 23 个 compile-fail driver + 1 个 property executable。

| 配置 | 通过 | 跳过 | 失败 |
|---|---:|---:|---:|
| clang-asan-ubsan | 120 | 0 | 0 |
| clang-debug | 120 | 0 | 0 |
| clang-release | 119 | 1 | 0 |
| gcc-debug | 120 | 0 | 0 |
| gcc-release | 119 | 1 | 0 |
| gcc-software-release | 114 | 6 | 0 |

Release 的一个跳过为原有 Debug-only Ring corruption；软件-only 配置另跳过 5 个 forced hardware hash case。
I1 新增测试没有跳过。ASan/UBSan 没有错误、泄漏报告。最终 format.sh --check 和 git diff --check 通过。

## Fuzz 执行记录

第一轮各配置 600 秒预算，libFuzzer 的整数计时导致进程墙钟约 599 秒；最终版本另运行 60 秒预算
并加 2 秒余量，各约 62 秒。每个 target 本轮累计超过 10 分钟，下面分别列出，不冒充一段连续的 1 小时验证。
输入上限 65536B，超限输入由 harness 明确忽略。没有 crash input，不存在待修 crash corpus。
第一轮与追加轮之间只修改诊断上下文及构建保障，生产算法没有变更。

| Target | 首轮墙钟秒 | 最终追加秒 | 首轮 runs |
|---|---:|---:|---:|
| record_decode | 599.63 | 62.72 | 125931791 |
| record_roundtrip | 599.39 | 62.5 | 28357177 |
| format_hash_equivalence | 599.49 | 62.56 | 18727414 |
| cstr_length_cache | 599.68 | 62.79 | 52305024 |

四个 target 均退出 0，无 sanitizer、timeout、OOM 或不变量失败。原始日志分别位于 `fuzz/` 和 `fuzz-final/`。
这不替代 release-candidate decoder 连续至少 1 小时的要求。

## Coverage：保留原始数据，不降低门槛

本次统计 I1 first-party 源码与头文件，排除测试和 GTest。报告合并四个测试 executable。

| 指标 | 原始结果 | 规范门槛 |
|---|---:|---:|
| Function | 98.96%（95/96） | 100% |
| Line | 94.58%（855/904） | >=95% |
| Branch | 96.69% | >=90% |

decoder 单文件行覆盖率 97.52%、分支覆盖率 98.86%；checked add/mul 的逻辑分支全部覆盖。
这些是源码函数合并口径，不宣称每个模板实例的每个分支均覆盖。

未覆盖函数为 `make_crc_table`：表通过 constexpr 初始化，该函数不执行运行时调用。
不能为了抬高覆盖率把表改成运行时生成。若只作解释性计算，排除该函数 15 行和 1 个函数后，
可执行函数为 100%、行约 96.18%；这不是本轮擅自替换规范的正式验收数字。

其他缺口包括：软件回退/CPUID 失败在本硬件 coverage 配置未触发（软件-only 正确性另已通过）、
encoder/decoder metadata 枚举穷尽后的终止防御、有效 PreparedRecord 不可产生的非法 tag、
以及受参数数目和 u32 单值上限保护的 args 累加溢出路径。reference literal 的 raw-zero 运行时分支
也尚未由这组 runtime literal case 触发；原有 constexpr 空串断言和 raw/stored 空输入测试另已通过。

LLVM 输出 32 个 hash-mismatch 警告。`--dump` 实查全部是 hash=0 的无运行时计数内联映射，
不是已发现的生产行为错误；完整诊断保留于 `coverage/mismatch-diagnostic.txt`。
仍保留警告和原始数值，不把它们隐藏后宣布 coverage 全绿。

## 性能与汇编证据

独立 target `qlog_record_core_benchmark`，不依赖 Ring 或 BQLog checkout。
GCC/Clang Release、固定 CPU 0、热输入复用；计时前完成分配和输入准备，输出通过编译器屏障保留。
吞吐为 9 个 20000 次批次的 median/MAD；延迟为 101 个 128 次批次，报告摊销 batch ns/op，
不是单条事件的 P99。没有 PMU 实测，不报告 CPU cycles。
测量时本轮 fuzz/编译已结束，但仍属于 WSL 开发机数据，不是隔离 native Linux 发布结果。

部分吞吐结果如下，完整 CSV 包含 hash/copy-hash 的长度/对齐/SW/HW，以及 9 类 record 场景。

| 编译器文件 | 路径 | Record bytes | median ns/op | MAD ns/op |
|---|---|---:|---:|---:|
| clang++-18-throughput.csv | two_int_runtime/measure_encode | 47 | 5.279 | 0.000 |
| clang++-18-throughput.csv | two_int_runtime/decode | 47 | 13.716 | 0.360 |
| clang++-18-throughput.csv | two_int_literal/measure_encode | 47 | 4.959 | 0.010 |
| clang++-18-throughput.csv | mixed/measure_encode | 73 | 21.068 | 0.247 |
| g++-throughput.csv | two_int_runtime/measure_encode | 47 | 19.261 | 0.182 |
| g++-throughput.csv | two_int_runtime/decode | 47 | 25.325 | 0.039 |
| g++-throughput.csv | two_int_literal/measure_encode | 47 | 18.411 | 0.555 |
| g++-throughput.csv | mixed/measure_encode | 73 | 35.653 | 0.604 |

`record_core_codegen_probe.cpp` 对 literal 使用显式 constexpr stored hash：constexpr 函数本身不强制
每个普通调用都在编译期求值。最终 GCC/Clang literal probe 均不调用 reference hash；runtime 路径
通过 raw copy 函数入口。Clang 在此无 LTO probe 中保留 encoder/helper 调用，GCC 消除了更多层级；
这只是后续组合性能审查线索，不能据此直接更改 PreparedRecord 或加 always_inline。

首版基线尚未覆盖轮转工作集、所有 cstr 首/中/尾组合、容量拒绝路径，以及完整优化前后交替比较。
首轮没有生产优化候选，因此没有宣称通过 3% before/after 回归门禁，也不与完整 logger 比较。

## 复现

```bash
python3 scripts/run_i1d_matrix.py
python3 scripts/run_i1d_fuzz.py --seconds 600
python3 scripts/run_i1d_coverage.py
python3 scripts/run_i1d_benchmark.py
```

脚本优先复用现有 GoogleTest 源码；新 checkout 没有缓存时走项目既有 FetchContent。
需要 Ninja、GCC/Clang 18、LLVM 18 coverage 工具及 Clang sanitizer/fuzzer runtime。
所有日志在 `build/validation/i1d/`。脚本已入源码树，当前未新增自动调度 CI。

## 尚未关闭的验收项

1. 原始 coverage 的 function/line 门槛及不可达/编译期统计说明收口；关键逻辑分支仍逐项审查。
2. 完整性能矩阵、轮转工作集、可信 native Linux 与可用 PMU 证据。
3. 发布候选 decoder 连续至少 1 小时 fuzz；本轮仅完成约 11 分钟累计/target。
4. CI 调度与最终 I1 DoD 审核。生产正确性回归通过，不代表这些工作自动完成。
