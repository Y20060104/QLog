# QLog I1-B hash 与 I1-C 公共类型/encoder 修复验证记录

日期：2026-09-10。实际仓库：`/home/qq344/QLog`。
本轮用户明确授权 Codex 完善生产修复和测试；本报告记录实际生产源码上的结果。

## 1. 结论与边界

- I1-B 已形成可编译、可链接、数值与内存边界测试通过的 hash 基线。
- `record_types.hpp` 与 `record_encoder.hpp` 已通过本报告所列行为验证。
- I1-A measure 与 32B Header、packed tagged wire 均保留；已有测试随本轮回归。
- **I1 尚未整体完成。** `record_decoder.hpp` 与 `src/record_decoder.cpp` 尚未实现；本轮 codec target 仅包含 types/encoder。
- 未完成或未执行：decoder golden/corruption/所有截断点、完整 compile-fail driver、独立 property target、长时 fuzz/corpus replay、coverage，以及 Record Core benchmark/codegen 性能验收。
- 本轮运行环境是 WSL2 Ubuntu x86-64；不将其写成额外 native Linux runner 的验证证据。

## 2. 生产修复

| 文件/函数 | 修复与保留的合同 |
|---|---|
| `format_hash_reference.hpp::hash_raw_ref` | 短输入先分段；只有 >=32B 才做块减法；整块结束不再重复尾块；修正字面量命名空间与 offset 遮蔽 |
| `src/format_hash_core.hpp::hash_core` | 补齐 1..3、4..7、8..15、16..31B 分支；长输入仍保留 ADR-010 的重叠尾窗 |
| `src/format_hash_x86_crc32c.cpp` | 定义统一为 dispatch 声明的 `x86_hash_raw` / `x86_copy_raw`，消除真实链接失败 |
| `record_encoder.hpp` | 单参数 helper 名称统一；UTF-8 写 tag/u32 length/bytes，正确推进 cursor；删除旧 format 写入片段；保留 preflight、raw→stored、Header 最后写 |
| 根 CMake | 增加默认 ON 的 `QLOG_ENABLE_X86_CRC32C`，允许完整构建关闭硬件后端；`-msse4.2` 仅作用于硬件 TU |

公共类型只作格式整理并新增行为测试；既有 policy/metadata/结果构造修订已通过验证。
`automatic()` 单一定义、CPU 检测、constexpr 软件表和 checked 归一化沿用维护者已有修订。

测试接线另修复了多标签在 GoogleTest discovery 中被展开的问题：
`tests/record_core_labels.cmake` 在注册后设置完整标签列表，`-L format_hash/codec/arguments` 均实际选中对应测试。
Clang `-Werror` 还发现两处既有 SPSC 测试未使用句柄，已补读取 empty 与成功 reserve 的状态断言。

## 3. 实际验证矩阵

编译器：GCC 13.3.0、Clang 18.1.3；C++20 与 `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`。
每组都编译整个项目并运行全量 CTest，测试总数为 88（原有 63 + 本轮新增 25）。

| 配置 | 通过 | 预期跳过 | 失败 |
|---|---:|---:|---:|
| `gcc-debug` | 88 | 0 | 0 |
| `gcc-release` | 87 | 1 | 0 |
| `clang-debug` | 88 | 0 | 0 |
| `clang-release` | 87 | 1 | 0 |
| `clang-asan-ubsan` | 88 | 0 | 0 |
| `gcc-software-release` | 82 | 6 | 0 |

Release 的一个跳过是原有 `SpscRingBufferRead.CorruptedFrameDoesNotAdvanceReader`（Debug-only Ring 校验）。
禁用硬件的 Release 另有 5 个 forced hardware case 按能力跳过；software 与 automatic 均实际运行并通过。
启用硬件的所有配置中，forced hardware case 均实际执行，无硬件跳过。
软件回退构建证明“未编入硬件”分支可用；没有在缺少 SSE4.2 的另一台实体 CPU 上运行。

Clang sanitizer 配置启用 `-fsanitize=address,undefined -fno-omit-frame-pointer`，
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`、`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`，无报告。

标签注册实查：`record_core` 38、`arguments` 13、`format_hash` 16、`codec` 9。
通过 `compile_commands.json` 检查全部配置，ISA 编译选项没有泄漏到普通 TU。
项目 `scripts/format.sh --check` 通过；交付前 `git diff --check` 与新文件尾空白检查通过。

## 4. 测试覆盖

`tests/format_hash_test.cpp`：

- 8 个冻结 raw 向量，expected 使用规范中的固定常量；包含 32B/64B 整块和 33B 尾窗。
- `char`/`char8_t` constexpr、空串、内嵌 NUL、高位字节；空 raw=0/stored=1。
- 135 个长度（0..128、255/256/257、8191/8192/8193），source 与 destination offset 各 0..31 的完整笛卡尔积。
- 三种 dispatch（software/automatic/hardware）与 hash-only/copy 路径，对照独立逐 bit reference；source 不变、精确复制、两侧 canary 和额外 capacity 字节不变。
- 固定种子随机输入 128 组、长度最高 16384B，精确大小 heap allocation 配合 ASan 检查边界。
- 长度 1..257，分别贴近可读写区开头/末尾，前后 `PROT_NONE` 页检测越界读写；canary 不作为越界读的唯一依据。
- checked metadata/容量错误及优先级、失败目标不变、`nullptr/0`、automatic 与编译/运行时能力一致性。

`tests/record_types_test.cpp` 与 `tests/record_encoder_test.cpp`：

- 256×256 level 位图检查；全部 256 flags × fallback 开关 × time 为零/非零，并检查错误优先级。
- 结果 success/failure 互斥、失败写入 0B、Header 按值保存及视图借用；封闭构造静态断言。
- 全部 15 个 tag 的独立 golden bytes，涵盖空字符串、NullUtf8、内嵌 NUL、浮点位模式、UTF-8、未对齐 destination。
- 每类 recoverable encode 错误下整段 destination 不变，hash/copy 回调均未调用。
- 0/32 参数、scalar snapshot、8192B format 和 65537B string 的精确长度。
- precomputed 不调用 hash；runtime 非空 format 只调用一次 copy-and-hash；raw zero stored 归一化。
- 四个新增 detail header 均有自身作为首个 include 的独立翻译单元。

## 5. 复现与原始证据

构建目录：`build/test/i1-20260910-<配置名>`。
日志、JUnit XML、汇总 JSON 和源码 SHA-256 清单位于 `build/validation/i1-20260910/`（本地 build 产物）。
`run_validation.py` 保存本轮六组 configure/build/CTest 命令，复用已存在的 GoogleTest 源码，不修改生产文件。

```bash
cd /home/qq344/QLog
cmake --build build/test/i1-20260910-gcc-debug --parallel
ctest --test-dir build/test/i1-20260910-gcc-debug -L format_hash --output-on-failure --no-tests=error
ctest --test-dir build/test/i1-20260910-gcc-debug -L codec --output-on-failure --no-tests=error
ctest --test-dir build/test/i1-20260910-gcc-debug --output-on-failure --no-tests=error
```

从新的 checkout 配置示例（依照项目现有 CMake 获取 GoogleTest）：

```bash
cmake -S . -B build/test/i1-check -G Ninja   -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++   -DCMAKE_CXX_FLAGS=-Werror -DBUILD_TESTING=ON
cmake --build build/test/i1-check --parallel
ctest --test-dir build/test/i1-check --output-on-failure --no-tests=error
```

软件构建加 `-DQLOG_ENABLE_X86_CRC32C=OFF`。Clang 配置用 `-DCMAKE_CXX_COMPILER=clang++`。
sanitizer 需给所有参与链接的对象启用上述 sanitizer flags，运行时保留本报告的环境变量。

## 6. 下一步

直接进入 [I1CD 指南 D 章](./MILESTONE2_I1CD_HANDS_ON_GUIDE_CHS.md#record-decoder)：
新增 decoder 头/源、实现 Header/区域/metadata/workspace 检查，再实现所有 tag、错误定位与借用视图，加入 qlog target。
随后补独立 decoder golden/corruption/截断点测试，并继续 I1-D 与性能验收。
无需重新实现已经验证的 hash/types/encoder；不可把当前 codec 测试通过写成 decoder 或整个 I1 完成。
