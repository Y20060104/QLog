# I1-D 继续验收报告：WSL 开发环境收口

日期：2026-09-13。实际项目 `/home/qq344/QLog`，Windows 入口 `\\wsl.localhost\Ubuntu\home\qq344\QLog`。
基准 commit：`c108301bacb0fa943299b14adbaa8c513bb2fa0e`；本轮源码以包含此报告的 Git 提交为准。
环境：WSL2 Ubuntu，Linux 6.6.87.2-microsoft-standard-WSL2，Intel Core i7-9750H。

## 结论与边界

本轮由 Codex 实际补充测试并执行验收。六配置编译/测试、覆盖率数值与关键分支审计、
Decoder 连续至少一小时 fuzz、另外三个 target 连续至少十分钟 fuzz、完整性能场景与 Release 汇编证据均已完成。
**WSL 开发环境验收通过；不宣布原生 Linux Tier 1 发布验收完成。**
当前没有原生 Linux runner 的执行证据，自动 CI 调度也尚未新增；可复现脚本已在源码树内。
本报告更新上一份阶段报告的未完成项，保留旧报告作为历史证据。

## 修改及其理由

- `src/format_hash_software.cpp`：CRC 表构造从 constexpr 改为 consteval，明确只能在编译期执行。
  没有改变 CRC 算法、wire、ABI 或生命周期；GCC/Clang 前后指令、重定位与只读常量完全一致。
  不将表改成运行时生成，也没有用覆盖率过滤规则删去生产源码。
- `tests/record_decoder_boundary_test.cpp`：独立全部 15 tag golden；逐字节截断 × 32 种起始对齐，
  同时测试原始长度和修复外层 args_bytes 后的输入；精确堆分配与两端保护页检查真实可读范围。
- `tests/i1d_boundaries.cpp`：运行时空字面量，以及独立 GF(2)/CRC32C 求解的非空 raw-zero 固定向量，
  验证 raw=0、stored=1；加入永久语料。固定向量为
  `00000000d43a705800000000923ba15b00000000000000000000000000000000`。
- `tests/record_allocation_test.cpp`：空、两整数、混合长字符串、32 个长 cstr，各运行 1000 次
  measure/encode/decode；监控区间 C++ new/new[]（含 aligned）调用数不增加。
  该检查不声称拦截任意直接 malloc；配合生产汇编审查使用。
- benchmark 补齐真实对齐、轮转工作集、cstr 首中尾/数量、exact quota/拒绝路径和延迟统计。
  测试、脚本与必要 CMake 接线属于 Record Core 范围，没有加入 Ring/Logger/Sink 实现。

## 最终构建矩阵

GCC 13.3 / Clang 18.1.3，C++20、严格 warnings 与 -Werror。
每配置 123 个 CTest entry；property executable 内为 43 个 GoogleTest 用例，包含 10000 组固定种子性质测试。
23 对 compile-fail/pass 在矩阵中实际编译，合法对照成功、非法调用失败。

| 配置 | 通过 | 跳过 | 失败 |
|---|---:|---:|---:|
| clang-asan-ubsan | 123 | 0 | 0 |
| clang-debug | 123 | 0 | 0 |
| clang-release | 122 | 1 | 0 |
| gcc-debug | 123 | 0 | 0 |
| gcc-release | 122 | 1 | 0 |
| gcc-software-release | 117 | 6 | 0 |

Release 的一个 skip 是既有 Ring 的 Debug-only corruption case；软件-only 另有 5 个硬件能力 skip。
I1 本轮新增测试没有跳过。ASan/UBSan 启用 no-recover、frame pointer 和 leak 检查，零错误报告。
首次 GCC 构建发现新增保护页测试的宏 dangling-else warning，补充花括号后重跑完整矩阵；
失败日志保留在 `matrix-before-brace-fix/`，未以 flaky retry 掩盖问题。
最终源码格式与 git diff 空白检查通过。

## Fuzz 连续执行

| Target | 进程墙钟秒 | 结果 |
|---|---:|---|
| record_decode | 3688.27 | 退出 0；1112004346 runs，libFuzzer 自报 3603 秒 |
| record_roundtrip | 617.14 | 退出 0 |
| format_hash_equivalence | 617.23 | 退出 0 |
| cstr_length_cache | 617.35 | 退出 0 |

四个 target 均无 crash、timeout、OOM 或 sanitizer 错误。输入上限 65536B。
Decoder 是一次连续运行，不是把短运行时间相加；外部墙钟包含启动、调度与结束清理，区别于工具自报计时。
Decoder 长时运行期间其生产实现、依赖解析逻辑与 harness 未修改；CRC 表的声明修改不参与 decoder 执行。
其余三个 target 使用该声明修改后的独立构建。最终源码与四个二进制 SHA-256 已归档。

## 覆盖率及关键分支

Clang 18，-O0 coverage，合并五个测试 executable 和 QEMU 软件回退 profile；保留全部 13 个 I1 源文件。

| 指标 | 原始结果 | 门槛 |
|---|---:|---:|
| Function | 95/95，100% | 100% |
| Line | 861/889，96.85% | >=95% |
| Branch | 351/362，96.96% | >=90% |

`gate.json` 和 `branch-audit.json` 均 passed=true；这是源码函数合并口径，不声称所有模板实例全覆盖。
49 条 LLVM mismatch 均实查为 hash=0 的无运行计数内联映射，详见 `mismatch-diagnostic.txt`；不隐藏警告。

分支审计书面解释五处不可达防御：encoder/decoder 的 metadata 枚举 default、
encoder 穷尽合法 tag 后的防御、PreparedRecord 规范化后的无效 encoded_size、
以及 64 位 size_t 上最多 32 个 u32 长度参数不可能造成的累加溢出。
checked arithmetic 本身的溢出测试已覆盖。

CPU 回退另有两种真实执行证据：QEMU `max,-sse4.2` 运行实际生产库和测试，5 pass、2 hardware capability skip；
QEMU `max,level=0` 使用无 libc 启动的 probe 连接实际 hash 源文件，CPUID 失败后软件回退出口返回 0。
后者没有 coverage profile，仅作为外部关键分支证据，审计脚本核对实际源码 SHA-256。
这两项是 WSL 上的 CPU 模拟，不是旧款实体 CPU 实测。

## 性能与 Release 汇编

每个编译器的 throughput/latency CSV 各 414 条记录，共四份。
覆盖 18 个 hash 长度（额外包含 8191/8193）、SW/HW、aligned/offset-1、hot/rotating、hash/copy-hash，
以及 21 类 record × 6 种操作。轮转缓冲区每个约 65 MiB；短输入只访问其中部分字节，不能称保证冷缓存。
固定 CPU 0，Release 无 LTO；吞吐 median/MAD 为 9×20000 批次，延迟 P99 为 101×128 批次的 nearest-rank。
延迟是 batch 摊销 ns/op，不是单事件尾延迟；吞吐模式不从九个样本伪造 P99。
本轮 fuzz 与构建矩阵已结束后才开始性能计时。

| 编译器 | 场景 | Record bytes | median ns/op | MAD ns/op |
|---|---|---:|---:|---:|
| g++ | two_int_runtime/measure_encode | 47 | 20.378 | 0.335 |
| g++ | two_int_runtime/decode | 47 | 26.258 | 0.010 |
| g++ | two_int_literal/measure_encode | 47 | 19.344 | 0.005 |
| g++ | mixed/measure_encode | 73 | 37.194 | 0.515 |
| clang++-18 | two_int_runtime/measure_encode | 47 | 7.197 | 0.020 |
| clang++-18 | two_int_runtime/decode | 47 | 14.767 | 0.739 |
| clang++-18 | two_int_literal/measure_encode | 47 | 5.439 | 0.020 |
| clang++-18 | mixed/measure_encode | 73 | 21.880 | 0.490 |

perf 实测 cycles/instructions/branches/branch-misses，原始文件为 `*-pmu.csv`。
这是整个 benchmark 进程的用户态总量，包含 setup、输出和计时开销；不除以某一个场景迭代数冒充 cycles/op。
WSL 调度、频率与宿主机干扰仍限制数字的可推广性。此次是扩展场景后的基线，
没有 PreparedRecord/dispatch 运行时优化候选，不对不同测试布局的旧数值应用 3% before/after 优化结论。

Release 汇编显示 literal probe 没有运行时 reference hash；runtime 通过 fused copy-hash 函数指针入口，
没有完整 hash 后再次复制 format 的双遍路径。固定宽度读写内联，没有逐字段外部 memcpy helper。
Clang 的通用 encoder 保留可变长度 memcpy 与部分 encoder/helper 调用；不据此盲目添加 always_inline。
正常路径未见分配、锁、shared RMW、locale/formatter；Clang 留有 personality addrsig 符号，
未产生该 probe 的异常展开 LSDA 表，非法内部状态的 terminate 防御仍保留。
Decoder 源码继续使用 memcpy 定宽加载与减法优先边界检查，保护页和 sanitizer 实证覆盖非对齐输入。

## 复现及证据位置

```bash
python3 scripts/run_i1d_matrix.py
python3 scripts/run_i1d_cpuid_leaf0.py
python3 scripts/run_i1d_coverage.py
python3 scripts/diagnose_i1d_coverage.py
python3 scripts/audit_i1d_coverage.py
python3 scripts/verify_i1d_codegen.py
python3 scripts/run_i1d_fuzz.py --targets record_decode --seconds 3600 --output acceptance-decoder-hour
python3 scripts/run_i1d_fuzz.py --targets record_roundtrip format_hash_equivalence cstr_length_cache --seconds 600 --build-dir i1d-acceptance-clang-fuzz --output acceptance-nightly
# 等待上面全部结束后，独立运行性能基线
python3 scripts/run_i1d_benchmark.py
bash scripts/format.sh --check
git diff --check
```

工具依赖：Ninja、GCC、Clang/LLVM 18、sanitizer/fuzzer runtime、项目既有 GTest 来源；PMU 使用可用 perf。
本机 QEMU 采用 build 内解包，不安装系统包：在 `build/tools/qemu` 下载 qemu-user/libcapstone4/liburing2，
用 dpkg-deb 解包到 root；脚本设置本地 LD_LIBRARY_PATH。其他发行版应使用自身可用 QEMU 包，不能照搬库 ABI。
先完成矩阵和 QEMU 工具准备，再执行 CPUID probe 与 coverage；没有 QEMU 时外部分支审计不会被伪造通过。

主要证据目录 `build/validation/i1d-acceptance/`：matrix、coverage（含 HTML）、cpu-fallback、cpuid-leaf0、
codegen、benchmark、provenance.json。长时 fuzz 保留在 `build/validation/i1d/acceptance-decoder-hour/`
和 `build/validation/i1d/acceptance-nightly/`。build 内证据不会自动随 Git 提交，应在发布时随源码 SHA 归档。

## 阶段收口决定

2026-09-13，用户确认当前开发环境为 Windows + WSL2，原生 Linux 复核暂缓。
**Record Core（I1）按 WSL2 开发范围完成阶段收口，可以进入后续模块开发。**
原生 Linux 发布前复核与自动 CI 仍是后续事项，不作为当前开发阶段的阻塞项，也不计为已验证。
验收结果摘要随提交保存于 `I1D_ACCEPTANCE_20260913_SUMMARY.json`；完整运行日志仍位于上述 build 目录。

## 下一步

当前无需为通过测试继续改 decoder 或改变 wire。正式发布前，在原生 Linux x86-64 runner 对同一源码
重跑矩阵、fuzz、coverage 与性能基线，并归档 runner 环境；测试执行仍由 Codex 负责。
若之后提出内联/表示/dispatch 优化，先建立同机交替 before/after 数据，再应用 3% 与合并 MAD 门禁。
自动 CI 调度目前待补，不能把本次手动脚本执行写成已有持续门禁。
