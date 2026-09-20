# ADR-014：V1 BQLog 风格字面量格式入口

> 2026-09-17 format覆盖：[ADR-016](./ADR-016-v1-bqlog-worker-format.md)优先于本文旧的严格花括号、参数数目匹配、默认文本表示和解析缓存合同。Producer原样copy/hash；worker按BQLog当前UTF-8顺序扫描。当前起点与剩余实施见[剩余V1指南](./V1_REMAINING_IMPLEMENTATION_GUIDE_CHS.md)。本文未被覆盖的wire/参数/长度规则继续有效，历史验收记录不改写为当前实现状态。

> 实施配套：[数组格式入口逐文件实现指南](./MILESTONE2_I2_LITERAL_HANDS_ON_GUIDE_CHS.md)，包含声明、定义、转发步骤、长度语义与验证矩阵。

- 状态：已接受
- 日期：2026-09-16
- 范围：I2 Producer 的格式入口、长度获取和 hash 时机
- 参考：本地 BQLog 60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9
- 关联：ADR-009、ADR-010、ADR-013

## 1. 决定

QLog V1 增加 BQLog 风格的字面量便利入口：

~~~cpp
logger.try_log(category_id, qlog::LogLevel::info, "value={}", value);
~~~

入口只接收 UTF-8 的 const char (&format)[N] 数组引用，保留编译器推导出的 N，不让格式串先退化为 const char*。末尾是 NUL 时有效长度为 N-1，否则为 N，因此不调用 strlen。

该重载只把数组地址和显式长度包装成现有 FormatView，然后转发到：

~~~cpp
try_log(std::uint32_t category_id, LogLevel level,
        FormatView format, Args&&... args) const noexcept;
~~~

CallGate、TLS Context、measure、reserve、admission timestamp、encode、commit/abort 只在这个基础入口中实现。数组重载不能复制这些步骤，也不能绕过过滤。

## 2. BQLog 事实与可借鉴边界

BQLog 的 info 等入口在 include/bq_log/bq_log_entry.h:238-261 以 const STR& 接收格式串。log.info("value={}", value) 推导出 STR 为 char[N]；bq_log_wrapper_tools.h:225-236、:474-499 保留数组类型，:336-342 使用 N 和末尾 NUL 计算长度，避免 strlen。

BQLog 的 hash 不是编译期计算。过滤通过并成功取得记录空间后，src/bq_log/api/bq_log_api.cpp:259-268 调用 bq_memcpy_with_hash()，在复制格式字节时运行时计算 hash；其实现位于 src/bq_common/utils/util.cpp:156-319,325-332。

因此 QLog 借鉴数组引用保留长度和调用体验，不照搬 BQLog 的宽泛 STR/custom/UTF16/UTF32 分发、Appender 端 hash 回算、压缩缓存局部 XOR key 或满队列 CPU-relax 重试。

## 3. QLog V1 接口合同

在 include/qlog/async_logger.hpp 的 AsyncLogger 中增加：

~~~cpp
template <std::size_t N, typename... Args>
    requires(N > 0U) &&
            (sizeof...(Args) <= detail::kMaxArgCount) &&
            (detail::SupportedArgument<Args> && ...)
[[nodiscard]] LogResult try_log(std::uint32_t category_id, LogLevel level,
                                const char (&format)[N], Args&&... args) const noexcept;
~~~

公共模板实现头中的数组重载只执行末尾 NUL 判断、FormatView 构造和转发：

~~~cpp
const std::size_t format_size =
    format[N - 1U] == '\0' ? N - 1U : N;
return try_log(category_id, level,
               runtime_format(format, format_size),
               std::forward<Args>(args)...);
~~~

runtime_format 只接收地址和长度并设置 stored_hash=0；producer 仍在 reserve 成功后通过既有 FormatHashDispatch 和 encoder 的 copy/hash 路径计算 hash。

V1 先只冻结 char[N]。char8_t[N] 的同形重载必须等现有格式解析语义单独核对后再决定；不因为 BQLog 支持 UTF-16/UTF-32 就扩大 QLog 参数类型集合。动态字符串继续使用 runtime_format(pointer, length) 或 runtime_format(string_view)。

## 4. 长度、hash 和性能

| 入口 | 长度 | hash 时机 |
|---|---|---|
| try_log(..., "x={}", args...) | N-1 或 N，不调用 strlen | reserve 后运行时 copy/hash |
| try_log(..., runtime_format(data, size), args...) | 调用方显式提供 | reserve 后运行时 copy/hash |
| V1不新增严格literal入口 | 不解析花括号/字段数量 | constexpr仅保留I1算法参考能力 |

本 ADR 不承诺编译期 format hash，不新增 NTTP、宏或普通调用方可填写的非零 hash。这样保持一个 hash 来源和一个 Record wire 合同，避免 literal 与 runtime 路径产生不一致的格式身份。

避免 strlen 只减少长度扫描；运行时 hash 和格式复制仍然存在。不能只凭数组重载声称 QLog 比 BQLog 更快，必须统一 payload、队列容量、丢弃策略和消费者负载后测量。

## 5. 实现顺序和不变量

1. 先声明数组重载，保留 FormatView 基础重载；约束和 noexcept 一致。
2. 数组重载只做 NUL 判断、视图构造和转发，不调用 classify_call、measure_record 或 try_reserve。
3. 基础入口继续先执行一次 CallGate；invalid 和 filtered 在 Context、Ring、measure、clock、hash 之前返回。
4. 通过 Gate 后严格执行 measure → 单次 reserve → admission timestamp → encode → commit/abort。
5. stored_hash 保持 0；encoder 在已经保留的帧内完成复制和 hash。数组包装层不读时钟、不分配、不提前 hash。
6. N==0 在约束中拒绝；末尾不是 NUL 的 char[N] 使用全部 N 字节，不能无条件减一。
7. 格式数组只借用调用方存储，必须在 try_log 返回前完成 copy；Ring 不保存调用方指针。

## 6. 验收要求

- 编译期检查直接字面量选择数组重载，runtime_format 仍选择基础重载。
- 带末尾 NUL 的字面量长度为 N-1；非 NUL 结尾数组长度为 N；两者都不调用 strlen。
- 数组重载和显式 runtime_format 传入同一字节时，I1 decoder 读到的格式、参数、category、level 和 hash 一致。
- invalid level/category、filtered 不创建 TLS、不 reserve、不取时、不计算 hash。
- accepted 在 commit 后返回；full 不取时；encode 失败先 abort；数组包装层不能改变这些结果。
- 稳态同线程/Logger 不分配；首次通过过滤的 Context 分配仍按 ADR-013 允许发生。

## 7. V1之后的独立性能提案

V1不扩展此项。未来若性能基线证明运行时hash是瓶颈，再另行提交纯hash常量化提案；不得附带严格花括号校验改变ADR-016语义，必须给出 constexpr 算法、存储期、hash=0 sentinel、与 FormatHashDispatch 的逐位等价测试及编译成本对比。在该提案接受前，不能把普通 const char (&)[N] 重载称为 compile-time hash。
