# ADR-016：V1 format 对齐 BQLog 当前 UTF-8 worker 实现

- 日期：2026-09-17。
- 状态：设计已接受；2026-09-19正文formatter已实现并完成WSL2专项验证，见[formatter实现与验证报告](./V1_FORMATTER_IMPLEMENTATION_REPORT_20260919_CHS.md)。worker/输出/管理闭环尚未完成，不代表整个V1验收。
- 源码基准：本地 BQLog `60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`；这是本次实际检查的工作区版本，不宣称远端最新版本。
- QLog 基准：`/home/qq344/QLog`，HEAD `58b6948c33bb2f4b94db7c3e3d77b228eaf5afd7` 加当前未提交修改。
- 实施入口：[剩余 V1 实现指南](./V1_REMAINING_IMPLEMENTATION_GUIDE_CHS.md)；完整后端细节：[V1 收尾指南](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md)。

## 1. 决定和覆盖顺序

Producer 只做元数据/长度/配额检查及参数编码，把 format 原始字节完整复制入 Record；不扫描花括号、不做字段计数、不校验 spec、不渲染参数。BackendWorker 消费并解码 Record 后，按照 BQLog 当前 **UTF-8** 路径顺序扫描、解释并生成正文。

本 ADR 覆盖 ADR-010 的严格语法、字段必须等于参数数量、标准双花括号转义、严格 tag/spec 矩阵、默认最短浮点以及强制解析缓存。ADR-014 的数组便利入口与 reserve 后 copy-and-hash 保留；ADR-015 的共享/独立 worker、管理邮箱、mutex/CV 唤醒、文件恢复和每条正文只渲染一次保留。冲突时本 ADR 的 format 语义优先。

V1 先实现 worker 直接扫描，不建 `FormatPlan`、`FormatCache`、失败缓存，也不在公共 API 引入编译期花括号检查、NTTP literal、Callsite 注册或 `std::format`/`{fmt}`。旧 `format_plan.hpp` 草稿已移除，当前使用 `format_spec.hpp`，不同时保留两套活动结构。

这不是把已存在的 Producer parser 搬走：当前 QLog Producer 本来就没有 parser。9月17日先消除了指南冲突；9月19日已实现可由worker调用的正文formatter，worker接线仍待完成。本次没有Producer前后对照或整条日志管线基准，不声称已测得前台提速。

### QLog实现命名约定

本轮用户确认：QLog内部符号使用职责名称，不加参考项目或本项目缩写前缀。统一使用 `FormatSpec`、`render_message_utf8`、`parse_format_spec`、`render_argument`、`apply_padding`，均位于 `qlog::detail` 或实现文件的匿名命名空间。BQLog名称只用于真实参考源码、来源说明和兼容语义，不改变本ADR的格式行为。

## 2. 源码证据与职责

以下路径相对 `E:\VisualStudioProject\BqLog`；行号对应上述基准。

| 位置/函数 | 源码行为 | QLog 实施结论 |
|---|---|---|
| `include/bq_log/misc/bq_log_impl.h`，`log::do_log` | 过滤、计长、分配 Record、参数序列化、commit | Producer 无花括号解释 |
| `include/bq_log/misc/bq_log_wrapper_tools.h:336-342`，数组长度分支 | 由 N 与末尾 NUL 得到长度 | char[N] 不先退化为指针，不 strlen |
| `src/bq_log/api/bq_log_api.cpp:259-268` | UTF-8/16 format 在预留成功后 `bq_memcpy_with_hash` | “直接复制”仍包含 fused hash，不是删除 hash |
| `src/bq_log/log/layout.cpp:326-337`，`layout::do_layout` | prefix 后进入 `python_style_format_content` | 正文解释属于消费/输出侧 |
| 同文件 `:663-859`，`python_style_format_content_utf8` | 零参数 memcpy；有参数扫描 brace、顺序消耗参数 | 以下状态机是兼容基准 |
| 同文件 `:423-535`，`c20_format` | 宽松 spec 字符状态机，不是标准 C++20 parser | 不用旧严格文法替代 |
| 同文件 `:540-649`，padding / exponent；`:1111-1493`，类型转换 | padding、Bool/null/pointer/整数/浮点的真实输出 | 按类型分派移植、安全适配 |

BQLog 完整消费调用关系为 worker 处理队列 → `log_imp` 消费记录与 Appender 分发 → text/console Appender 请求 `layout::do_layout` → UTF-8 scanner / `c20_format` / `insert_*`。具体线程模式会影响何处调用；本 ADR 对齐的是异步模式，不声称 BQLog 的所有模式都只在 worker 执行。

QLog 已有 `log_format.hpp::runtime_format` 只保存指针/长度/hash=0；`async_logger_impl.hpp::try_log` 调用 measure/reserve/clock/encode；`record_encoder.hpp::encode_v1` 复制格式串并计算 hash；`record_measure.hpp` 只检查 format 元数据与大小。上述模块不需要新增语法检查。

## 3. 两端固定调用链

```text
业务线程：
数组入口确定长度 / runtime_format(pointer, length)
  -> classify_call（level/category + 一次粗过滤）
  -> TLS ProducerContext（首次通过过滤允许冷分配）
  -> measure_record（参数类型/字节数/总配额，不解析 format）
  -> try_reserve 一次
  -> admission timestamp
  -> encode_v1（原样复制 format + fused hash；tagged arguments；最后 Header）
  -> commit / abort
  -> 已提交且低空间时 awake；full 时 awake 后返回 full

worker：
try_read -> decode_v1（固定32槽）-> Logger/Appender 过滤与选择
  -> render_message_utf8（本Record最多一次）
  -> 各健康目标 compose_line -> accept_line 到目标自有 batch
  -> release Frame + publish_reclaimed
  -> flush / write / recovery / sync / 控制命令
```

数组包装会检查末尾一个 byte 来确定长度；基础 `FormatView` 入口通过 Gate 前不扫描 format 内容。filtered/invalid 不创建 Context、不 measure/reserve/hash/clock；full 不取时、不复制、不重试 reserve。后端格式化结果不能倒改已经返回的 `LogResult::accepted`。

## 4. UTF-8 花括号状态机

### 4.1 初始零参数

`arg_count == 0` 时，把全部 format bytes 原样写入正文。`{}`、`{{`、`}}`、不配对 brace 均是原始文本。不能先做一次 escape 预处理。长度为0时不解引用空指针。

### 4.2 有参数

维护 `cursor`、`argument_index` 和输出游标；不递归、不回溯。

1. 批量复制普通 bytes，直到 `{` 或 `}`。可先用有界 scalar run，再优化成与 BQLog 同形的 SIMD scan-and-copy；所有块读写均受输入/输出剩余长度约束。
2. 遇到 `}}` 输出一个 `}`；单个 `}` 原样输出。
3. 遇到 `{` 且尚有参数，最多前瞻其后20个 bytes（偏移0..19）；先遇 `{` 则本次不成立；在窗口内先遇 `}` 才成立。未闭合/超出窗口仅输出当前 `{`，后续位置仍继续处理。
4. 成立时按 `c20_format` 状态机解释窗口，随后按当前 ArgumentTag 输出下一个参数。`{0}`、`{name}` 也顺序消费，不把内容当索引/名称；没有严格的参数类型与 spec 配对拒绝。
5. `c20_format` 的非零 offset 按源码推进；offset为0仍跳过本次已闭合窗口并消费参数，不能改成语法失败。
6. 参数耗尽后，`{` 按原字节输出，但整个循环继续扫描，因此后续 `}}` **仍然折叠**。不能把整个剩余尾串直接 memcpy。
7. 未被用到的参数忽略；不存在独立的“字段最多32个”限制。参数上限仍32，格式长度仍8192，额外占位内容可保留为正文。

以下是静态源码推导的正文用例，**本轮没有运行兼容测试**：

| format | 参数 | 正文 |
|---|---|---|
| `{{x}} {}` | 无 | `{{x}} {}` |
| `{} {}` | `7` | `7 {}` |
| `x` | `7, 8` | `x` |
| `{0}/{name}` | `7, 8` | `7/8` |
| `{{}}` | `7` | `{7}` |
| `{} }}` | `7` | `7 }` |
| `x{` | `7` | `x{` |
| `{{x}}` | `7` | `{7}` |

嵌入 NUL 作为普通 byte 按显式长度处理。BQLog 的 UTF-16 分支并非同一状态机；QLog V1 不扩展 UTF-16/32 支持，也不通过 UTF-16 分支推导 UTF-8 结果。

### 4.3 spec 与类型转换

把 `format_info` 的字段和 `c20_format` 的分支顺序逐项映射到 `FormatSpec`；每个替换字段新建默认 spec，不能让上一条 Record 的状态残留。

- 新建 spec 默认 upper=true、fill=' '、align='>'、sign='-'、prefix=' '、offset=0、width=0、precision=UINT32_MAX、type=' '；非冒号窗口按默认输出。不要把 reset 函数的 align='<' 误当新 spec 的默认值。
- 保留扫描 `index > 10` 的终止规则、两位 width/precision 数字累积、未知字符作为 fill 等宽松行为。不是“超过99直接报错”，也不是完整标准文法；第三位数字的忽略与重复 flag 按源码。
- 识别的 type 分支是 `b/B/e/E/f/F/x/X/o/d`；不能把旧合同的 `g/G/p/s/c` 当作独立严格指令。普通文本内容可能落入 fill 规则。
- 类型由 Record 的 tag 决定。Bool 输出 `TRUE/FALSE`，Char 输出一个原始 byte，NullUtf8 输出 `null`，Utf8String 按显式长度复制。Bool 写 `{:d}` 也不会改成旧合同的0/1。
- Pointer64 为0输出 `null`，非零默认 `0x` 加大写十六进制；显式 spec 对其内部整数输出的影响按 BQLog 代码处理。
- 整数保留 signed/unsigned 分支差异、`+` 对无符号0的行为、`#` 在二/十六进制的前缀、大小写、padding 和整数 `e` 分支。不能用旧“标准 sign/prefix/padding”规则覆盖；居中奇数 padding 多出的一个在左侧。
- F32 默认7位小数、F64默认15位，已定义的普通数值路径采用 BQLog 分离整数/小数并逐位输出的行为；不是 shortest，也不是默认6位或标准四舍五入。旧 `<null>`、小写 Bool、pointer `0x0`、浮点默认 shortest 的合同全部撤销。

## 5. 安全适配和兼容声明边界

对齐可定义的输出行为，不复制内存不安全或语言未定义行为。QLog 继续使用 decoder 的 `bits/bytes/byte_count`，不复制 BQLog 4B 参数布局或未对齐 typed-pointer 读取；有符号 bits 按各自宽度 `bit_cast`，INT64_MIN 用无符号 magnitude 或安全余数算法，不直接取负。

BQLog 浮点先转 uint64_t/int64_t，未先检查 NaN/Inf/范围。QLog 必须先分支：NaN输出 `nan`，正/负无穷输出 `inf/-inf`，这些是明确的安全扩展，不能作为与 BQLog 相等的样本。有限数在正数 `value < 2^64`、负数 `value >= -2^63` 才允许走相应转换；使用精确2的幂比较，不能用会舍入的 `double(UINT64_MAX)` 做上界。

有限超范围值采用 `std::to_chars` 的 fixed 模式，precision使用解析值或7/15默认值，检查返回结果后再做有界padding。这一支是 QLog 安全扩展，不承诺 BQLog 字节相等。转换暂存512B可覆盖double最大有限数和99位小数，仍需检查容量。普通兼容路径不统一换成 to_chars，以免把截断与历史符号行为改成标准格式。安全扩展先生成上述规定字节：nan/inf固定小写且不添加正号；有限超范围按fixed和所定precision输出，不应用spec的+、#、e/base或upper改写。随后只按width/fill/align做整体有界padding；这一分支不执行BQLog对符号/prefix的特殊移动规则。

小宽度、负小数、负零、科学计数和fill组合需要单独的兼容向量：例如 BQLog `-0.5` 的整数部分转为0会丢负号，这属于可定义的兼容行为，不能静默按旧合同修正。在上述安全扩展分支之后的兼容数值路径中，若仍会使索引越界、负长度转巨大无符号数或转换超范围，QLog返回 `number_conversion_failed`，size=0，放弃本次正文。输出容量不足统一返回text_output_limit_exceeded，不与数值运算失败混用。此边界不是完整 BQLog 浮点兼容承诺；只有验证过的有界输入域可标“输出一致”。

`CheckedTextWriter` 一律先做 `n <= capacity-used` 再 memcpy/memset/memmove。保留 format<=8192、arg_count<=32、正文/完整行各65536B、固定批缓存、无动态递归。完整行包含 prefix 和换行，正文65536B成功不代表该目标完整行可成功。输出超限不截断、不发半行。

错误只保留结构/工作区/转换/输出错误：invalid_format_metadata、invalid_arguments、invalid_workspace、format_too_large、number_conversion_failed、time_conversion_failed、text_output_limit_exceeded。孤立brace、参数不足/多余、未知spec不是严格语法错误。decoder仍验证全部参数，即使多余参数不会被渲染。

## 6. 为什么 V1 不再实现解析缓存

BQLog 当前 text layout 直接扫描，不能把 compressed Appender 的模板去重当作 text parse cache。旧 QLog plan 预先反转义全部文字、要求字段和参数数目完全匹配，与本 ADR 的零参数和耗尽分支直接冲突。

当前先去掉缓存分配、hash查表、全串比较、literal/plan复制和失败缓存，worker每条有界扫描一次，普通 run 顺手复制。主游标最多推进8192次，每次字段前瞻最多20次，spec解释只在至多32次实际参数替换时发生且最多10个位置；加上受65536B容量约束的输出，CPU工作量受输入与输出显式上限约束。每条正文最多渲染一次，多个目标仍复用正文。代价是热模板也重复扫描；这可能降低高复用场景吞吐，必须测量，不能说不缓存一定更快。

Record format_hash 与已有 I1 wire、CRC32C测试不变，Producer继续 fused copy-and-hash。Text worker不使用 hash、不为hash0补算。未来若经数据证明需要缓存，必须基于新的条件执行语义设计，比较hash+长度+全部bytes，保留每Record参数状态；缓存不保存Ring指针，不能复活旧严格 FormatPlan。

## 7. 验收和状态

后续实施要分别完成：原始字节入队检查、UTF-8状态机兼容向量、spec/tag矩阵、有界数值/异常值处理、固定scratch及多目标正文复用、Frame release/publish早于I/O、共享/独立worker生命周期，以及分层性能测试。

性能分别记录 Producer 的低占用与压力唤醒路径、worker scanner/转换、fanout、write、durable，以及 accepted/full/delivery_failed；共同报告p50/p99/p99.9、吞吐、CPU与内存。BQLog侧同机同编译选项、相同UTF-8内容/参数、discard策略、队列预算和worker模式；不把不同字节输出混为相同格式化工作量。浮点安全扩展单列。

本轮完成源码追踪、合同修订、剩余实施指南和文档一致性检查。没有修改生产实现，没有构建/运行测试，没有性能结果；不能据此标记 V1 或 formatter 已完成。
