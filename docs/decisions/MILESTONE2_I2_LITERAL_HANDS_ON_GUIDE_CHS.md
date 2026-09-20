# I2 数组格式入口实现指南：BQLog 风格调用与运行时 hash

> 2026-09-17 format覆盖：[ADR-016](./ADR-016-v1-bqlog-worker-format.md)优先于本文旧的严格花括号、参数数目匹配、默认文本表示和解析缓存合同。Producer原样copy/hash；worker按BQLog当前UTF-8顺序扫描。当前起点与剩余实施见[剩余V1指南](./V1_REMAINING_IMPLEMENTATION_GUIDE_CHS.md)。本文未被覆盖的wire/参数/长度规则继续有效，历史验收记录不改写为当前实现状态。

> 2026-09-16 后续状态：数组重载已在源码中写入，尚未执行本批测试；下一步按 [V1 一轮收尾指南](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md) 连续推进剩余生产实现。本文保留数组入口参考。

日期：2026-09-16。依据 [ADR-014](./ADR-014-v1-bqlog-style-literal-format.md)、ADR-013 和本轮实际读取的 /home/qq344/QLog 源码。
本文给维护者实施；本次仅文档交付，没有修改生产代码，也不把参考片段当作实现验收。
本批目标：`logger.try_log(0U, LogLevel::info, "value={}", value)` 可以直接调用，保留数组长度，不调用 strlen，继续复用现有 runtime Producer。

## 1. 当前起点与本批修改范围

| 文件 / 搜索锚点 | 当前状态 | 本批动作 |
|---|---|---|
| include/qlog/async_logger.hpp / public try_log | 已有 FormatView 模板声明 | 在其后追加数组重载声明，补 cstddef |
| 同文件末尾 / include async_logger_impl.hpp | 已在 namespace qlog 结束之后 | 保留位置，不能再移到顶部 |
| include/qlog/async_logger_impl.hpp / AsyncLogger::try_log | runtime 主流程已存在 | 在当前函数结束后、namespace 结束前追加数组重载定义 |
| include/qlog/log_format.hpp / runtime_format(const char*, size_t) | 已能包装地址、长度，stored_hash=0 | 直接复用，无需改 FormatView 私有构造或增加 friend |
| detail/record_encoder.hpp / encode_v1 | 已有 runtime copy/hash 路径 | 保留，包装层不算 hash |
| src/async_logger.cpp / Impl、桥接 | 本批无需新状态 | 不新增重载的 cpp 定义 |
| CMakeLists.txt | 两个被修改文件均为已有头 | 数组重载无需新增生产源文件条目 |

上一篇 [POST_RUNTIME_REVIEW](./MILESTONE2_I2_POST_RUNTIME_REVIEW_CHS.md) 是当时的静态快照；其中包含顺序、invalid_category 语法等旧问题不应重复执行。本文只推进已接受的 ADR-014 切片，Debug 诊断的接口仍单独细化。

## 2. 先理解重载关系：为什么能保留 N

BQLog 的 `const STR&` 让字符串字面量按数组类型参与推导。QLog 当前无需接受 BQLog 的全部 STR 类型集合，因此用更窄的数组引用签名表达同一机制：

```cpp
template <std::size_t N>
void example(const char (&format)[N]);
```

`&` 和括号都不能省：`const char*` 接收指针，失去数组大小；函数形参写 `const char format[N]` 也会调整为指针。`const char (&format)[N]` 才引用整个数组。
`"value={}"` 有8个可见字符，加结束 NUL 后 N=9，传给 Record 的长度应是8。

本批不是严格“仅字符串字面量”的类型系统。`char buffer[16]`、`const char data[3]` 同样可以绑定，所以长度规则必须覆盖一般数组：

| 输入 | N | 记录长度 | 原因 |
|---|---:|---:|---|
| `""` | 1 | 0 | 去掉最后一个 NUL |
| `"x={}"` | 5 | 4 | 去掉最后一个 NUL |
| `char data[]{'x', '=', '{', '}'}` | 4 | 4 | 末尾不是 NUL，保留全部字节 |
| `"a\0b"` | 4 | 3 | 只去掉最后一个 NUL，中间 NUL 仍是内容 |
| `char buffer[8] = "x"` | 8 | 7 | 不扫描首个 NUL；此接口表示数组内容，不能推断使用者只想传 x |

如果 buffer 的有效文本只有一部分，应使用 `runtime_format(buffer, used_length)`。`std::string` 使用 `runtime_format(std::string_view{text})`。
`const char* p = "x={}"` 已失去 N，不能直接调用数组重载。显式给长度或 string_view；注意 `std::string_view(p)` 自己会求长度，不能把这种调用算成“完全无 strlen”。

## 3. 第一步：在 async_logger.hpp 增加声明

修改位置：文件顶部标准库 include 区添加 `<cstddef>`，因为声明直接使用 std::size_t。
然后在 AsyncLogger 的 public 区，紧接现有 FormatView 版本之后、private 桥接声明之前，添加以下完整声明。

```cpp
template <std::size_t N, typename... Args>
    requires(N > 0U) &&
            (sizeof...(Args) <= detail::kMaxArgCount) &&
            (detail::SupportedArgument<Args> && ...)
[[nodiscard]] LogResult try_log(std::uint32_t category_id,
                               LogLevel level,
                               const char (&format)[N],
                               Args&&... args) const noexcept;
```

各部分的职责：

- N 由格式数组推导，调用方不手填；N>0 使末尾 N-1 访问有前提，标准 C++ 数组本身也不支持零长度。
- Args 仍只表示日志参数，不把格式数组算进32参数上限。
- SupportedArgument 沿用 I1 参数准入，不能为了格式便利重载放开任意字符串指针或用户对象参数。
- Args&& 保留转发引用，后续通过 std::forward 传递；不要改成会复制参数的按值包。
- const/noexcept/返回 LogResult 与基础入口一致。

不要替换原来的 FormatView 声明，它承载动态格式和全部写入逻辑。此步完成时 public 中应该能看到两个 try_log 模板。

文件最后仍应是：

```cpp
}  // namespace qlog

#include "qlog/async_logger_impl.hpp"
```

上一次构建的 AsyncLogger 未声明错误来自实现头包含过早；新增重载时也必须保留这个顺序。

## 4. 第二步：在 async_logger_impl.hpp 增加完整定义

位置：现有 FormatView 版本函数体的右括号之后，文件末尾 `} // namespace qlog` 之前。
当前实现头已有 `<utility>`；补 `<cstddef>` 作为直接依赖。实现头继续由公共头在末尾包含，无需反向 include 公共头。

```cpp
template <std::size_t N, typename... Args>
    requires(N > 0U) &&
            (sizeof...(Args) <= detail::kMaxArgCount) &&
            (detail::SupportedArgument<Args> && ...)
[[nodiscard]] LogResult AsyncLogger::try_log(
    std::uint32_t category_id,
    LogLevel level,
    const char (&format)[N],
    Args&&... args) const noexcept {
    const std::size_t format_size =
        format[N - 1U] == '\0' ? N - 1U : N;

    return try_log(category_id, level,
                   runtime_format(format, format_size),
                   std::forward<Args>(args)...);
}
```

声明与定义的模板参数和 requires 按同样顺序书写，避免把不等价的模板声明误写成另一个重载。只在定义中使用 `AsyncLogger::` 限定。

这个函数只有三个步骤：

1. 读取数组最后一个字符，根据 ADR-014 规则算 format_size。没有 strlen/循环/格式语法解析。
2. `runtime_format(format, format_size)` 返回一个借用视图，stored_hash=0。数组在此处退化为指针是安全的，因为 N 已转换成显式长度。
3. 转发给基础版本，并原样返回 LogResult；不重新映射结果，不在这里创建 Context，也不添加第二次过滤。

### 4.1 为什么不会递归调用自己

转发调用的第三个实参已经是 FormatView，不再是 char 数组。它匹配原来的基础重载，无法匹配数组引用形参。
若误写 `return try_log(category_id, level, format, ...)`，第三个参数仍是数组，就会再次选择自己，造成无限递归。必须保留 runtime_format 这一层。

### 4.2 为什么不能放到 cpp

不同调用方会实例化不同 N 和 Args。模板定义应在调用方可见的实现头中；只把它写进 src/async_logger.cpp 会导致其他翻译单元缺少相应实例。
不需要新 cpp、额外的 Producer 类、数组缓存或字符串副本。

## 5. 与过滤、复制和 hash 的准确顺序

```text
数组重载：读取末尾一个字符 → 构造 pointer+length 视图
    ↓
FormatView 基础重载：level/category 校验 → 一次粗过滤
    ↓ proceed
取得 TLS Context → measure → 一次 try_reserve
    ↓ reserve 成功
admission timestamp → encode 内格式 copy/hash → commit 或 abort
```

需要准确区分：按照本次接受的薄包装方案，末尾字符判断发生在基础入口过滤之前；因此不能宣传“filtered 路径完全不读格式数组”。它只读一个末尾字符，不扫描，不分配，不 hash。
BQLog 的类型分发/长度计算在过滤之后；QLog 的小包装通过复用一个基础入口避免重复 Gate。这里的差异已由 ADR-014 转发写法确定，不应为移动一个末尾读取而复制整个生产流程。
数组对象在调用时必须完整有效，即使日志最终被过滤，也不能传入无效存储或与写线程发生数据竞争的数组。

reserve 成功指 Ring 返回有效的写入句柄，不等于调用通用堆分配器。首次 TLS/Ring 冷分配和每次日志的 Ring 空间预留是不同步骤。
本批借用已有 encoder：stored_hash=0 时在写入 Record 的格式区域中 copy/hash，继续现有 raw→stored hash 规范化；不直接调用 BQLog 的 bq_memcpy_with_hash，也不引入新的哈希算法。
格式文本在 try_log 返回前已复制进 Ring；调用结束后，消费者只能读 Record 内的字节，不应借用原 format 指针。

## 6. 不同调用的写法与期望

```cpp
qlog::LoggerConfig config;
config.name = "array-format";
qlog::AsyncLogger logger(config);

const auto a = logger.try_log(
    0U, qlog::LogLevel::info, "x={} y={}", 12, 34);

const char raw[]{'x', '=', '{', '}'};
const auto b = logger.try_log(0U, qlog::LogLevel::info, raw, 12);

const auto c = logger.try_log(
    0U, qlog::LogLevel::info,
    qlog::runtime_format("x={} y={}", 9U), 12, 34);
```

a 和 c 的格式与参数内容应一致，二者分别调用时 timestamp 可以不同，不能要求整帧逐字节相等。b 的格式长度为4，即使没有 NUL 也不越界扫描。
本阶段 accepted 表示 Record 提交到 Ring；还没有 Backend，因此这段代码不会因空 Appender 配置而立即在终端打印。
char8_t[N] 的直接重载本批未冻结；现有 `runtime_format(const char8_t*, size_t)` 入口仍可用。不要顺手增加宽字符、隐式 const char* 或 std::string 的直接重载。

## 7. 完成后按什么顺序构建与交接

1. 检查声明/定义均使用数组引用，且未删 FormatView 版本。两处直接 include cstddef，实现头已有 utility。
2. 运行 `./scripts/build.sh`。它只构建库，且部分模板不会在库自身实例化，所以这一步通过仍不足以证明数组重载正确。
3. 用只 include qlog/async_logger.hpp 的真实消费方调用本节 a/b/c，并链接 QLog::qlog。至少实例化空参数、一个 int、两个 int 三种组合。
4. 交接给 Codex 验证数组分支和基础入口的行为，检查下一节矩阵。已有 tests/i2_runtime_public_probe.cpp 是独立探针，尚不能默认认为已自动纳入所有 CTest。
5. 本批通过后再细化 Debug 诊断。I2 最终验收仍需 Record 解码闭环、失败注入与分配/生命周期检查，随后才进入 I3 Backend/Console。

本次仅新增头内重载，无需为它修改根 CMake 的生产源文件列表。新增测试目标时才修改 tests/CMakeLists.txt，且继续链接现有 QLog::qlog。
不要修改脚本让 build.sh 的成功文案替代测试；build.sh 和 build_test.sh 的职责继续区分。

## 8. 本批验证矩阵：测试应证明什么

| 场景 | 必须观察的结果 |
|---|---|
| 只 include 公共头并真实实例化 | 数组和 FormatView 两版本均编译、链接成功，无递归 |
| 空字符串/普通字面量 | Record 格式长度分别0/N-1 |
| 非 NUL 结尾数组 | 长度N，拷贝全部字节，不越界读取 |
| 中间 NUL/大容量缓冲区 | 严格按数组长度合同保存字节，不误用 strlen 语义 |
| 传入同字节的数组与显式 view | decoder 读出的格式字节、hash、参数、level/category 一致；不比较两次采样的时间是否相同 |
| invalid level/category、filtered | 返回状态正确，Context/Ring/取时/hash 无副作用；允许包装层末尾字符读取 |
| measure 超限、Ring full | 对应 LogResult 不变；full 无本次有效 reservation，不 abort、不取时 |
| accepted 后改变原数组 | 消费者仍读到提交时字节，确认 Record 深拷贝 |
| 同线程重复写/两 Logger 切换 | 复用原 TLS 协议，没有新数组缓存或额外动态分配 |
| 33参数/不支持参数类型 | 数组版本保持 I1 参数准入，不能因新重载绕过限制 |
| const char*、直接 char8_t 数组 | 当前便利入口不接受；显式 runtime_format 继续有效 |

“不调用 strlen”与“没有 hash”不是同一件事：数组入口避免长度扫描，但成功 reserve 后仍要运行 hash。
返回 accepted 只能证明结果路径；要证明字节、hash 和深拷贝，必须从真实 Logger 的 Ring 读取并解码。测试访问器应遵守私有 Impl 和帧借用寿命，不把内部 Context 公开给业务。
单凭现有 I1 的 strlen/分配测试不能证明新重载；新测试应实际调用数组版本。测试程序 Release 断言需保持有效，避免 NDEBUG 去掉全部检查。

## 9. 常见错误的定位表

| 表现 | 优先检查 |
|---|---|
| AsyncLogger/LogLevel 未声明 | 实现头是否被自动整理 include 移到了公共头顶部 |
| 无匹配重载 | 公共区是否有新声明、参数是否已经退化为指针、声明约束是否一致 |
| 无限递归/栈溢出 | 转发的第三个实参是否忘记包装成 FormatView |
| 非 NUL 数组丢最后一个字符 | 是否无条件使用 N-1，而未检查末尾 |
| embedded NUL 后内容丢失 | 是否使用 strlen 或单参数 string_view 构造 |
| 重复过滤/统计翻倍 | 是否在数组重载也调用了 classify_call 或重复写入主流程 |
| hash 在 full 路径仍执行 | 是否在重载中提前 hash，或先编码再 reserve |
| undefined reference | 模板定义是否误放 cpp，或公共头末尾未 include 实现头 |

## 10. 本轮交付与实施边界

本次新建本文并更新主指南/计划导航；只做文档内容及链接检查，不声称数组重载已在生产实现或通过测试。
下一次交接应说明两个头是否已修改、真实数组消费方是否已实例化，以及任何第一条完整错误。不要因旧 runtime 探针通过就跳过本批数组特有边界。
ADR-014 已接受，本批无新的设计阻塞；Debug 诊断的计数结构/快照接口不由本指南代为冻结，Appender 动态配置与 V2 路由继续按各阶段商榷。
