# QLog 正文 formatter 实现与验证记录

日期：2026-09-19。权威实现：`/home/qq344/QLog`。本次依据用户“你来补全优化”的授权，补全生产代码并执行验证。

**已完成独立正文 `render_message_utf8`，已接入构建与测试。** 这不包含 `compose_line`、BackendWorker、Appender、flush/drain/shutdown，也不代表整个 V1 已实现或已通过端到端性能验收。格式语义继续由 [ADR-016](./ADR-016-v1-bqlog-worker-format.md) 约束。

## 1. 本次交付

| 文件 | 结果 |
|---|---|
| `src/text_formatter.cpp` | 保留已写好的宽松 spec parser，补全常量基数整数转换、浮点兼容/安全扩展、类型分派、padding、正文主扫描 |
| `include/qlog/detail/text_formatter.hpp` | 保持 pointer + length 签名和 `render_message_utf8` 命名；补齐容量、输入有效期、禁止重叠和失败输出约定 |
| `include/qlog/detail/format_spec.hpp` | 使用现有 `FormatSpec` / `FormatResult` / `FormatError`，本次没有增加另一套类型或修改其布局 |
| 根 `CMakeLists.txt` | 将 `src/text_formatter.cpp` 纳入 qlog 静态库 |
| `tests/text_formatter_test.cpp` | 15 项语义、边界、随机与 codec 串联测试 |
| `tests/text_formatter_allocation_test.cpp` | 独立 C++ 分配拦截探针，包含首次调用、普通转换、超范围 fixed 分支和失败路径 |
| `tests/text_formatter_bq_differential.cpp` | 可选的真实 BQLog 差分程序；不加入默认构建，不让 QLog 依赖 BQLog |
| `tests/CMakeLists.txt` | 注册 formatter 语义目标和 Unix 分配目标，统一 `formatter` 标签 |
| `third_party/licenses/BQLog-Apache-2.0.txt` | 保存参考代码许可；formatter 文件头保留来源和改动说明 |

没有修改 Producer 的 format copy/hash、Record wire、Tag、decoder、Ring、配置实现或线程拓扑。工作树原有其他修改保持原样；本次未提交 Git commit。

## 2. 当前代码的调用顺序

```text
render_message_utf8
  ├─ 元数据预检：format <= 8192、args <= 32、地址/长度一致
  ├─ valid_argument：检查全部参数，包括最终未使用的多余参数
  ├─ 初始 arg_count == 0：原样复制全部 format
  └─ 有参数的扫描循环
      ├─ copy_literal_run：定位连续普通字节，一次 memcpy
      ├─ 处理单个 } 和 }}，参数耗尽后继续执行
      ├─ { 后最多向前查看 20 字节，遇嵌套 { 放弃本候选
      ├─ parse_format_spec：保留 BQLog 内部索引窗口和数字收尾
      ├─ render_argument
      │   ├─ bool / char / UTF-8 bytes / null：直接有界复制
      │   └─ 数值：固定 512 B 栈暂存，完成转换后复制最终字节
      └─ apply_padding：容量检查后执行有界移动/填充
```

数值辅助函数的顺序是 `write_digits<Base>` → `render_magnitude` → `render_unsigned/render_signed` → `render_float` → `render_argument`。主函数不需要理解各个数值类型的内部转换细节。

`render_signed` 固定默认十进制，再由 spec 的 b/x/o 覆盖；只有 pointer 分派需要给 `render_unsigned` 传默认基数 16。因此 signed helper 没有额外 default_base 参数。

## 3. 特别保留的兼容行为

- `FormatSpec` 等内部符号不加 bq/q 前缀。
- `{name}`、`{0}` 与 `{}` 都顺序消耗参数；缺参后的 `{}` 保留，多参忽略输出但仍检查参数元数据。
- 初始零参不折叠双花括号；初始有参时，即使参数已经耗尽，`}}` 仍折叠。
- 第二个对齐字符先成为 fill；首次 align 是 `<` 或 `^` 时，再把 fill 改为空格。不会补上对 `>` 的同样处理。
- 不擅自增加 O/D 类型；未知字符可能成为 fill。内部扫描窗口退出仍执行数字收尾，不能直接按严格 parser 报错。
- Bool 为 `TRUE/FALSE`；Char 从 `bits` 取低一字节；UTF-8 字符串按显式长度复制，包含 NUL，不按 precision 截断。
- `#` 只增加二进制/十六进制前缀；八进制没有此前缀。负整数输出负号与 magnitude，`INT64_MIN` 不直接取负。
- 无符号零不输出 `+`，有符号零可以输出 `+`。指针零为 `null`，非零先输出 `0x`，保留 spec 对后续数字的影响。
- 普通浮点保持 BQLog 的逐位截断、7/15 默认小数位和历史符号行为，例如 `-0.5` 的普通路径丢负号。
- 右对齐非空格 fill 的特殊符号/prefix 移动保留，包括文本参数中的符号；居中补齐的奇数余量放左侧。

具体例子由默认 CTest 固定：`{:05}` 配 `"a-b"` 得到 `a0a0b`；`{:8.2e}` 配 `123.5` 得到 `1.23e+02`；这些不按标准格式库语义修正。

## 4. 安全边界

所有输出 copy/fill/move 先检查剩余容量，正文上限是 `min(capacity, 65536)`；不追加 NUL 或换行。null + 0 合法，不在零长度路径对空指针做算术或 memcpy。

标量从 decoder 的 bits 按各自宽度 `bit_cast`，不移植 BQLog 未对齐的 typed-pointer 读取。输入与输出不能重叠；这是头文件中的调用约定，caller 负责输入有效期。

NaN/Inf 在整数强转前处理为 `nan/inf/-inf`。普通浮点转换先用精确的 `2^64` / `-2^63` 判断范围；超范围有限值采用 fixed `to_chars` 和 512 B 暂存，然后只做整体 padding。安全扩展不应用 spec 的正号、进制、前缀或科学计数变换。

科学计数截断、负宽度、前缀越过字段起点等危险状态返回 `number_conversion_failed`；输出容量不足返回 `text_output_limit_exceeded`，两类错误分开。`FormatResult.size` 在失败时为 0，包含参数的错误记录格式串位置与参数索引。caller 必须丢弃失败 scratch，不能把已写前半段送入 batch。

数值暂存还解决一个容量问题：科学计数的中间小数串可能长于最终文本，不能因为中间长度超出 caller 的精确最终容量而提前失败。测试分别使用“恰好容纳最终结果”和较大容量验证输出。

## 5. 优化与证据边界

已保留的实现方式：编译期基数 2/8/10/16；64 B 倒序数字缓冲后一次复制，不额外反转；连续普通文本一次 memcpy；适用的 padding 使用 memmove/memset；数值转换使用固定栈空间，无缓存、堆容器、locale、锁或 worker hash 查询。

`value % Base` 的 Base 是编译期常量，不能按“运行期除法一定昂贵”来判断。GCC 13.3、`-O3` 编译的最终 formatter object 经反汇编检查，整数 `div/idiv` 指令数为 0。这个观察仅对记录的编译配置成立。

曾试验 200 B 双位十进制查表：同进程、同输入、同 `-O3`、交替顺序、9 轮各 50 万次 formatter 调用取中位数，并先比较两版输出。该候选没有稳定收益，部分负载回退，已撤回；最终生产代码与已验证的常量基数版本 SHA256 一致。该实验不是相对 BQLog 的性能基准，也不是整个日志管线的吞吐/尾延迟结论。实验原始结果留在 evidence 目录中，不据此宣传提速百分比。

分配探针执行 20,000 次调用（10,000 成功 + 10,000 容量失败），包含首次调用和超范围 fixed 浮点路径，观察到 **零 C++ new/new[]/aligned-new 分配**。它不等于对所有动态库内部 malloc 的通用拦截证明；源代码路径同时没有直接 malloc/realloc 调用。

## 6. 实际验证结果

环境为 Ubuntu WSL2，GCC 13.3 和 Clang 18；不是原生 Linux 发布验收。BQLog 基准为本地 `60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`。QLog HEAD 仍为 `58b6948c33bb2f4b94db7c3e3d77b228eaf5afd7`，有未提交修改，必须结合 [源码 SHA256 清单](../validation/formatter_20260919/source-sha256.json) 识别本次实现。

| 验证 | 结果 |
|---|---|
| GCC Debug 全套回归（新增分配目标前） | 139 项全部通过 |
| GCC Release 全套回归（新增分配目标前） | 138 项通过，1 项按原有 ring validation 配置跳过，0 失败 |
| Clang Debug + ASan/UBSan 全套回归（新增分配目标前） | 139 项全部通过 |
| 最终 formatter 专项，含分配目标 | Debug / Release / ASan+UBSan 各 16 项全部通过 |
| BQLog 实际 UTF-8 formatter 差分 | 23,180 组相同字节，0 差异；174 组被 QLog 安全检查拒绝，未送入 BQLog |
| 整数随机参考 | 2,000 组随机值，覆盖二/八/十/十六进制及有符号十进制，对照独立 to_chars |
| 随机格式与容量 | 10,000 组，双初始填充值、输出护栏和结果确定性检查，纳入 sanitizer 运行 |
| codec 串联 | measure → encode → decode → render，验证原始 format 保留及解码参数输出 |
| 严格编译 | formatter 单独通过 GCC/Clang 的 `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` |
| 差异检查 | `git diff --check` 通过 |

Release 原有 `src/producer_context.cpp:116` 的未使用 `index` 参数告警仍存在；本次未改该函数，不把上述 formatter 的 -Werror 结果扩大为全仓无告警。

BQ 差分使用其 `test_python_style_format_content_simd` 包装调用生产 scanner，fixture 先用零参 raw-copy 预留 4096 B：单独调用正文包装没有正常日志前缀准备，而 BQ 的 padding 自身不扩容。只比较 QLog 接受的有界输入，不把 NaN/Inf/超范围浮点安全扩展作为 BQ 相等样本。23,180 组样本一致不是对任意字节组合的形式化完整兼容证明。

原始记录位于 [validation/formatter_20260919](../validation/formatter_20260919)，其中包含三组全套日志、三组最终专项日志、差分输出、分配/反汇编结果和 SHA256 清单。

## 7. 复现

以下命令在 `/home/qq344/QLog` 执行；已有 GoogleTest 缓存可用 `-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/home/qq344/QLog/build/_deps/googletest-src` 避免重新下载。

```sh
cmake -S . -B build/formatter-gcc-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/formatter-gcc-debug -j 8
ctest --test-dir build/formatter-gcc-debug --output-on-failure

cmake -S . -B build/formatter-release -DCMAKE_BUILD_TYPE=Release
cmake --build build/formatter-release -j 8
ctest --test-dir build/formatter-release --output-on-failure

cmake -S . -B build/formatter-san -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/formatter-san -j 8
ctest --test-dir build/formatter-san -L formatter --output-on-failure
```

可选 BQ 差分不依赖网络；下面的参考路径须指向上述本地 BQLog 版本。BQ 的构建脚本会在参考仓库 artifacts/install 下生成构建产物，不修改参考源码：

```sh
cmake -S /mnt/e/VisualStudioProject/BqLog/src -B build/formatter-bq-reference \
  -DTARGET_PLATFORM=linux -DBUILD_LIB_TYPE=static_lib \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-DBQ_UNIT_TEST
cmake --build build/formatter-bq-reference -j 8
g++ -std=c++20 -O2 -DBQ_UNIT_TEST -Iinclude \
  -I/mnt/e/VisualStudioProject/BqLog/include \
  -I/mnt/e/VisualStudioProject/BqLog/src \
  tests/text_formatter_bq_differential.cpp build/formatter-release/libqlog.a \
  /mnt/e/VisualStudioProject/BqLog/artifacts/static_lib/lib/Release/libBqLog.a \
  -pthread -o build/formatter-release/bq_differential
build/formatter-release/bq_differential
```

## 8. 剩余 V1 的下一个实现切片

1. 补齐诊断计数守恒与宏配置专项验收。B0三处历史编译问题在当前树已修复，不要照旧指南重复改。
2. 按既有合同补 `compose_line`：正文只渲染一次；每个目标增加时间/级别/category等前缀与换行；对“前缀 + 正文 + 换行”整体执行 65536 B 限制。当前正文上限不能代替完整行上限。
3. 实现 `OutputBatch` 和 Console/TextFile Appender，区分入 batch、write、durable；失败正文不得进入任何目标。
4. 继续 mailbox、Session/Worker/Runtime、控制命令与关闭流程。worker 应在解码视图有效期内完成正文复制；释放 Ring 后再进行潜在慢 I/O，按现有指南落地所有权边界。
5. 完成多 Channel 并发、I/O 故障、drain/shutdown、真实吞吐/尾延迟与原生 Linux 验收。

后续接口与实现顺序继续查阅 [剩余 V1 指南](./V1_REMAINING_IMPLEMENTATION_GUIDE_CHS.md) 和 [V1 收尾指南](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md)。无需重写已完成的正文 parser 或恢复 FormatPlan/FormatCache。
