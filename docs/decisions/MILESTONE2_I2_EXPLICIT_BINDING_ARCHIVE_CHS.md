# 历史归档：显式绑定阶段的 I2 指南

2026-09-15 由 ADR-013 取代的旧参考。以下全文仅供追溯，不是当前实施步骤。
public ProducerHandle、bind_producer、注册 mutex、Appender 管理锁和旧代码进度不得直接沿用。
当前入口：[自动上下文动手指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)。

---

# QLog I2 Producer / Channel 动手指南

更新日期：2026-09-15。审阅基准：`35570da` 加当前未提交工作区，实际仓库 `/home/qq344/QLog`。

状态：设计文档交付；I1 已按 WSL2 开发范围收口，I2 生产骨架已开始，尚未完成与验收。
用户已确认显式绑定、BQLog 式动态过滤/条件诊断统计及生命周期基线。
本指南把接口和实现步骤具体化，不宣称声明骨架已具备生产功能。

规范顺序：用户已确认决定 → [ADR-012 多 Appender 补充](./ADR-012-v1-multi-appender.md) → [ADR-011](./ADR-011-v1-producer-channel.md) →
[I2 执行计划](./MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md) → 本指南。
Record wire、参数准入、时钟语义分别继续服从 ADR-007～010 与 I1 规范。

编排参考 [I1CD 动手指南](./MILESTONE2_I1CD_HANDS_ON_GUIDE_CHS.md)：
先解释为什么，再给出文件位置、声明顺序、接口骨架、逐步算法和交接条件。
维护者编写生产实现与生产 CMake 接线；Codex 编写测试、运行验证、审查汇编与性能。
本轮只交付文档；以后若用户明确授权 Codex 实现，则以该授权覆盖分工。

快速定位：[A 类型](#i2-types) · [B 过滤](#i2-filter) · [C 时钟](#i2-clock) ·
[D Channel/绑定](#i2-channel) · [E Producer](#i2-producer) · [F 诊断](#i2-diagnostics) ·
[G 接线](#i2-build) · [H 验收](#i2-validation)。

## 0. 从哪里开始

**2026-09-14 续写入口：** [逐函数实施与性能理由](./MILESTONE2_I2_CONTINUATION_GUIDE_CHS.md)。
当前进度以本指南 §0.6 本次实读快照为准；续写文档中的旧快照不覆盖较新的源码。
本轮从当前代码继续时，先读 [N 逐项修正与落笔顺序](#i2-current-next)，再按其中检查点交接。
本指南已直接补齐定义和逐函数实施说明：A7/A8 结果与格式、B9 过滤、C8 时钟、
D11/D12 配置与绑定结果、D13/D14 所有权与冷函数、E11/E12 Producer、F6 诊断。
新增内容使用类型/签名、字段表和自然语言步骤，不提供生产实现代码；原有接口示例保留作对照。

### 0.1 当前已有的能力

已有源码位置如下；行号仅对应本次审阅快照，函数名是长期定位依据。

| 阅读顺序 | 现有文件/入口 | 必须理解什么 |
|---|---|---|
| 1 | `include/qlog/detail/spsc_ring_buffer.hpp:155` | `try_reserve/commit/abort` 与被动 WriteHandle |
| 2 | `src/spsc_ring_buffer.cpp:23` | Ring 构造抛异常、容量验证、成功 reserve 已写 FrameHeader |
| 3 | `include/qlog/detail/record_measure.hpp:20` | FormatInput 与零值 hash sentinel |
| 4 | `include/qlog/detail/record_measure.hpp:396` | `measure_record` 的 requires、quota、PreparedRecord 生命周期 |
| 5 | `include/qlog/detail/record_encoder.hpp:122` | `encode_v1` 精确目标长度、六个参数、Header-last |
| 6 | `include/qlog/detail/record_types.hpp:22` | validation policy 与运行过滤不是同一个集合 |
| 7 | `include/qlog/detail/format_hash.hpp` | `FormatHashDispatch::automatic()` 只在冷路径调用 |
| 8 | `docs/decisions/ADR-008-realtime-coarse-admission-timestamp.md` | 启动探测与每条 admission 取时的区别 |

现在不要重新实现 hash、measure 或 decoder。A1 等级与 A2 policy 桥接已有函数体，
先修 B 过滤验证与 C 时间转换，再按 A/D/E 补全值类型、所有权和 Producer；以 §0.6 为进度依据。

### 0.2 一个工作包的实施顺序

```text
A 公共等级、结果、format 适配
  → B 稳定过滤表与标量更新
  → C 冷路径 clock descriptor + admission timestamp
  → D Channel 所有权、注册发布、显式绑定
  → E 模板 Producer：filter → measure → reserve → clock → encode → commit
  → F Debug 诊断 / Release 零诊断字段与更新
  → G 生产接线与交接
  → H Codex 验证 / 错误反馈 / 性能基线
```

### 0.3 新增文件及内容顺序

下列路径相对于 QLog 根目录，文件不存在时新增；不放到 BQLog 参考仓库。

| 文件 | 从上到下放什么 | 实现性质 |
|---|---|---|
| `include/qlog/log_level.hpp` | LogLevel、合法值判断 | public 小值类型 |
| `include/qlog/log_result.hpp` | LogStatus、LogFailure/LogResult 访问合同 | public 无分配结果 |
| `include/qlog/log_format.hpp` | runtime view、受控 literal 格式入口 | 头内适配，不解析格式 |
| `include/qlog/detail/producer_policy.hpp` | public level → I1 policy | constexpr 纯值 |
| `include/qlog/detail/filter_state.hpp` | FilterState、原子读取短 helper | hot helper 在头中 |
| `src/filter_state.cpp` | 初始化、批量输入验证与更新 | 冷路径 |
| `include/qlog/detail/admission_clock.hpp` | ClockSource/Descriptor、Timestamp、小接口 | 不依赖 Ring |
| `src/admission_clock.cpp` | clock_getres、转换、生产 clock_gettime | Linux 适配 |
| `include/qlog/detail/producer_diagnostics.hpp` | 条件诊断块、访问约束 | 宏受 CMake 一致控制 |
| `include/qlog/detail/channel.hpp` | ChannelCold、Channel、内部只读 next 链 | 内部完整类型 |
| `src/channel.cpp` | 冷构造、析构、必要非模板定义 | 不含日志模板 |
| `include/qlog/producer_handle.hpp` | 非拥有轻量句柄、try_log 模板定义 | 调用点可见 |
| `include/qlog/async_logger.hpp` | Config、冷路径结果、AsyncLogger 声明 | 所有权封装 |
| `src/async_logger.cpp` | Impl、注册、过滤更新转发、关闭注册 | mutex 只在冷路径 |

小型结果类型可以保持在对应 public 头，不为了每个 enum 新建文件。
不要引入一个混装 Ring、codec、Sink 的 common.hpp，也不新建第二个生产 library。

### 0.4 依赖方向

```text
log_level / log_result / log_format
                   ↓
producer_handle → Channel → Ring
                   ↓        （Ring 不知道日志）
             I1 measure/encoder

AsyncLogger → owns Channel + FilterState + ClockDescriptor + HashDispatch
                                  ↓
                         immutable RecordValidationPolicy

I3 Backend → published Channel list → existing decode_v1 → ConsoleAppender
```

I1 头不能 include 新增的 Logger/Channel/clock 头。
producer_handle.hpp 可以 include detail/channel.hpp 以让模板看见短热路径；
async_logger.hpp 以 Impl 隐藏 mutex、注册容器和字符串所有权，不迫使所有调用点看到冷路径 STL。

### 0.5 不变量速查

- 每个活跃生产线程与一个 Logger 对应一个 Channel，V1 不动态选择 MPSC。
- Handle 可复制；所有副本只在绑定线程顺序使用，不创建第二 Writer。
- 首次绑定允许分配/锁/线程身份获取；已绑定的正常 Release 调用不做这些工作。
- category 表大小与名字稳定；level/category 过滤值允许独立原子更新。
- 每条调用最多 reserve 一次；满时返回，不能重试、等待、扩容或 fallback I/O。
- measure/encode 复用同一 PreparedRecord；cstr 不重复扫描。
- reserve 失败不取时；reserve 成功后才取 admission timestamp。
- encode 成功后 commit 一次；可恢复 encode 失败 abort 一次。
- Release 不维护这组诊断计数，但保留错误返回、Ring 同步状态与 decoder 检查。
- Producer 先停止并 join，再关闭注册、排空、join Backend，最后销毁 Channel。

### 0.6 本次源码进度：按函数体核对，而非按文件存在判断完成

2026-09-14 实读工作区；本表仅为静态审阅，没有执行 I2 生产编译或行为验收。
以下文件名相对 `include/qlog/`，显式标注 src 的除外。

| 文件/定义 | 当前确实已有 | 仍需完成或修正 | 本指南位置 |
|---|---|---|---|
| log_level.hpp / valid_level | 头保护、cstdint、六等级与判断函数 | 后续类型验收，不重复补头保护 | A1/A7 |
| detail/producer_policy.hpp | 固定六等级 policy 工厂 | 冷构造时接入 descriptor.has_fallback | A2/A7 |
| log_result.hpp | LogStatus、LogFailure、optional；failure() 已判空 | FailureStage 为空；reason 不全；status 未初始化；公开修改器错误，见 A7 | A3/A7 |
| log_format.hpp | 0 字节空文件 | FormatView、runtime/literal 入口及内部适配 | A4/A5/A8 |
| detail/filter_state.hpp | 查询、构造声明、私有发布声明和正确成员顺序 | count 尚非 const；配置访问桥接未完成 | B2/B9 |
| src/filter_state.cpp | 构造/发布函数体 | throw 语法、合法元数据下逐 byte 验证漏执行 | B3/B7/B9 |
| detail/admission_clock.hpp | 正确文件名、完整 Descriptor/Timestamp 声明 | 接线与验收，不重复改旧拼写 | C1/C8 |
| src/admission_clock.cpp | 转换、ID 映射、probe/read、descriptor 和 sample 均有函数体 | 十亿乘法仍用逗号；另新增错误的非 const 采样重载，见 N7 | C3/C7/C8 |
| detail/channel.hpp / src/channel.cpp | 空 namespace / 空 cpp | ChannelCold、Channel 构造、稳定借用 | D1/D13 |
| producer_handle.hpp | 已更名为 ProducerHandle，仅一个未初始化 Channel* | 补空构造、受控绑定、try_log 全链 | D8/E11 |
| async_logger.hpp / src/async_logger.cpp | 配置类型已起草、BindResult 重复定义且访问器错误；cpp 新增空验证函数 | 配置、结果、完整 Impl、构造析构、绑定/更新/关闭 | D11～D14 |
| detail/producer_diagnostics.hpp | 0 字节空文件 | 条件字段与终结计数 | F6 |
| 根 CMakeLists.txt | 既有 I1 接线 | 未接四个 I2 cpp、Producer 诊断宏及 Threads | G |

本次只更新这一份指南。上述问题仍在生产树中，不以本次文档修改标为已修复。
实现顺序是：基础修正 → 配置/结果/格式 → Channel/Impl/绑定 → Producer → 诊断/接线 → H 验收。
I1 已按 I1-D 报告在 WSL2 收口；I2 仍未验收，I3 Backend/I4 输出资源不在本轮实施范围。

<a id="i2-types"></a>

## A. 公共类型：先确定调用方能表达什么

### A1. LogLevel 对齐 BQLog 的命名与编号

文件：`include/qlog/log_level.hpp`。直接 include `<cstdint>`。

```cpp
namespace qlog {
enum class LogLevel : std::uint8_t {
    verbose = 0,
    debug = 1,
    info = 2,
    warning = 3,
    error = 4,
    fatal = 5,
};

[[nodiscard]] constexpr bool valid_level(LogLevel level) noexcept {
    return static_cast<std::uint8_t>(level) <= 5U;
}
}
```

`fatal` 只表示日志等级，不隐式 abort/flush。位图零值表示过滤全部，不增加写入 wire 的 off 等级。
不保留早期候选 trace/warn/critical 作为第二组枚举别名；当前 API 尚未发布，无兼容负担。
无效枚举如 `static_cast<LogLevel>(255)` 必须在位移前拒绝，不能先算 `1U << value`。

### A2. 构造 I1 validation policy

文件：`include/qlog/detail/producer_policy.hpp`。
直接 include `<array>`、`<cstdint>`、`record_types.hpp`、public level 头。

```cpp
namespace qlog::detail {
[[nodiscard]] constexpr RecordValidationPolicy make_producer_policy(
    bool has_fallback) noexcept {
    return RecordValidationPolicy{
        std::array<std::uint64_t, 4>{0x3fU, 0U, 0U, 0U}, has_fallback};
}
}
```

合法 wire 值为 0～5，等价第一槽低六位为 1。fallback 值来自已选 ClockDescriptor。
即使当前过滤全关闭，policy 仍接受这六个合法值。
否则关闭 verbose 会让 Backend 把已在队列中的合法 verbose Record 判成损坏。
policy 在 Logger 冷路径构造一次，Channel 借用或冷构造复制；不逐条重建四槽数组。

### A3. 结果：accepted 与 filtered 不能合成一个 bool

文件：`include/qlog/log_result.hpp`。结果不拥有字符串、不分配、不打印、不递归记录日志。

接口骨架：

```cpp
enum class LogStatus : std::uint8_t {
    accepted,
    filtered,
    invalid_handle,
    invalid_level,
    invalid_category,
    invalid_input,
    too_large,
    full,
    internal_error,
};

class LogResult final {
public:
    [[nodiscard]] LogStatus status() const noexcept;
    [[nodiscard]] bool accepted() const noexcept;
    [[nodiscard]] const LogFailure* failure() const noexcept;
    // 私有构造与内部工厂：维护状态和 failure 的匹配。
};
```

此块是接口说明，不是单独可编译文件；LogFailure 先于完整 LogResult 定义。
建议 failure 保存 stage、reason、argument_index、byte_count；全部按值、完整初始化。
argument_index 继续使用 0xFF 表示非参数错误；已有 I1 failure 的定位信息必须保留。
不要把两种 enum 直接 static_cast 对齐编号；用穷尽 switch 映射。
accepted()/status() 在 Release 仍存在，统计编译移除不能消除可观察调用结果。

构造和访问规则：

| 状态 | accepted() | failure() |
|---|---|---|
| accepted | true | nullptr |
| filtered | false | nullptr |
| full | false | 返回容量拒绝信息，或单独约定无扩展定位；本指南采用失败信息 |
| 其他失败 | false | 指向结果内部已初始化的 failure |

非拥有 failure 指针只在结果对象生命周期内有效；不要返回局部临时 error 地址。
不承诺 LogResult 固定二进制大小；检查 Release 是否优化掉调用者未读取的扩展字段。

### A4. FormatInput 不能作为任意用户 hash 注入入口

已有 detail::FormatInput 是 `{byte pointer, size, precomputed_stored_hash}`。
I1 信任非零 stored hash，因此 public API 不能要求用户填写该三元组。

在 `log_format.hpp` 提供两类受控来源：

- runtime：显式 pointer+size / string_view，构造 detail input 时 hash 固定 0；
- literal：编译期保存原字节和 `hash_literal_stored` 结果，仅内部能写非零 hash。

推荐入口形状：

```cpp
auto format = qlog::runtime_format(std::string_view{"id={}"});
auto result = producer.try_log(category_id, qlog::LogLevel::info, format, id);
```

literal 的辅助入口可采用 `qlog::literal_format<"id={}">()`，两者最终调用同一个 try_log 实现。
这是 format 来源适配，不是两种 Record；不增加 flag、Callsite 或 parser。
支持 char/char8_t 的要求与 I1 保持一致；宽字符继续拒绝。

### A5. 为什么不能声称 const char[N] 参数会自动编译期 hash

普通函数形参 `const char (&text)[N]` 不保证其内容是常量表达式。
即使被调用函数写 constexpr，运行时数组仍可能触发运行期 hash。
因此 literal 入口用结构化 NTTP 保存字节，并在 constexpr 存储对象中计算 hash。

实现路线（不是完整 API 代码）：

1. 定义 `BasicFixedFormat<CharT,N>`，公开 `CharT bytes[N]` 以满足 structural type。
2. constexpr 构造逐元素复制；约束 CharT 为 char 或 char8_t，末字节为 NUL。
3. `template<BasicFixedFormat Text>` 建立 inline constexpr 原始字符存储及 stored hash。
4. 到运行期适配时才将字符地址转换成 byte 指针，避免 C++20 constexpr reinterpret_cast 限制。
5. 返回只读 format view，引用上述静态存储，长度为 N-1。
6. 新 view 不能允许外部将任意 hash 与另一段 format bytes 拼在一起。

NTTP 可能增加编译时间/符号长度，这是编译期 hash 的代价，不宣称运行期收益免费。
不实现宏级惰性参数求值；用户表达式在进入 try_log 前已执行。
过滤避免的是内部 strlen/measure/hash/clock，不是 `expensive_function()` 的求值。

### A6. 本章交接条件

- public level 与 wire 映射唯一，policy 不依赖动态过滤。
- 所有运行时状态可区分，I1 精确失败定位不丢失。
- runtime format hash=0；literal 静态存储与 hash 字节身份一致。
- 无新增 formatter、span、用户自定义参数准入。

### A7. 结果相关定义与每个函数怎样完成

`LogStatus` 是调用的最终分类；`FailureStage` 表示在哪一步失败；`FailureReason` 表示具体原因。
同一个 internal_error 可以来自 measure/reserve/encode，不能只看最终 status 推断诊断计数。
建议 stage 覆盖 validation、measure、reserve、encode；命名可统一，语义按 E4/E5/E12。
reason 要覆盖 invalid_handle/level/category、全部九个 MeasureError、reserve full/pending/配额错误、
全部七个 EncodeError。枚举值不是 wire，不复制 I1 底层编号，也不将不同来源的同名错误混为一个阶段。

| LogFailure 字段 | 含义和初始化要求 |
|---|---|
| stage | 本次失败所在步骤；成功/filtered 不构造 failure |
| reason | 穷尽映射后的原因；当前只有 measure 类别不足以覆盖整个 Producer |
| argument_index | 保留 I1 参数序号；非参数错误用 0xFF，不能默认 0 冒充第一个参数 |
| byte_count | measure 原值原样保留；reserve 可记录申请 payload 字节数，encode 可记录目标长度；没有字节上下文时明确用 0，不伪造已写字节 |

当前 `LogResult` 必须修正：status_ 未初始化；非 const 的 void accepted() 会在非 const 对象调用时
遮住 bool accepted() const；filterd 拼写错误；failed 中将 optional 反向赋给 LogFailure 参数不能成立。
这些公开 setter 也允许产生 status/failure 不匹配状态，不能仅改一处赋值就算完成。
当前 failure() 已经安全判空，无需重复修复。

按以下函数职责重整，不增加运行时分配：

| 函数/操作（建议名） | 作用、实现步骤与失败规则 |
|---|---|
| valid_level(LogLevel) → bool | 取得 uint8_t 值，判断是否在 0～5；不读取过滤值，不进行位移；当前已有 |
| make_producer_policy(bool) → RecordValidationPolicy | 生成低六位有效的四槽 mask，将 fallback 能力按值保存；调用一次后冷存储，不逐条生成；当前已有 |
| LogResult 私有构造 | 完整初始化状态与 optional；不开放无意义默认构造；只允许受控工厂建立有效组合 |
| make_accepted() → LogResult | 建 accepted 且无 failure；只在 commit 完成后调用；与查询 accepted() 使用不同名字 |
| make_filtered() → LogResult | 建 filtered 且无 failure；只在过滤拒绝分支调用 |
| make_failed(status, failure) → LogResult | 接收完整按值失败；限制 status 为失败类别；不得接 accepted/filtered；调用点与工厂共同维护合法组合 |
| status() const noexcept → LogStatus | 只返回已初始化枚举，不改变结果 |
| accepted() const noexcept → bool | 只比较状态是否 accepted；删除同名非 const 修改器，保证 const/非 const 对象查询一致 |
| failure() const noexcept → const LogFailure* | optional 有值才取地址，否则 nullptr；借用有效期止于结果销毁或内容替换 |
| 结果复制/析构 | 只复制/释放内嵌值，不触碰 Channel，不产生日志、不补计数 |

工厂可放 detail 访问类内，通过 friend 调私有构造；小型非模板头内 helper 要满足 inline/ODR。
所有结果工厂应 noexcept，不因报告内存不足而分配错误字符串。
验收要检查每种 status 的 failure 有无、无参数 sentinel、const/非 const 查询、无默认假成功及错误映射全覆盖。

### A8. FormatView、literal 存储与各适配函数

`FormatView` 建议保存 `const std::byte* data`、`std::size_t size`、`std::uint64_t stored_hash`。
它只借用原字节，不拥有字符串；三个字段不能作为 public 可随意组合的聚合成员。
runtime 的 hash 必须为 0；非零 hash 只能由可信 literal 静态存储生成。
`BasicFixedFormat<CharT,N>` 是编译期字符载体，`bytes[N]` 为 structural NTTP 所需的公开数组，
只允许 char/char8_t；它与运行期 view 的权限边界不同。

| 函数/操作 | 输入输出、实现过程及约束 |
|---|---|
| FormatView 内部构造 | 保存地址、显式长度、可信 hash；不读字节、不复制、不解析；构造 hash 的权限受控 |
| runtime_format(pointer, size) → FormatView | char/char8_t 字节入口只建立 hash=0 的 view；不 strlen；非法元数据留给过滤后的 measure 判定 |
| runtime_format(string_view) → FormatView | 转交 data 和 size；如支持 u8string_view 同理；view 本身不延长输入寿命 |
| BasicFixedFormat 的 constexpr 构造 | 编译期逐元素复制，检查字符类型与末尾 NUL；长度使用 N-1，内嵌 NUL 仍算字节 |
| literal_format<Text>() → FormatView | 每个 Text 实例拥有 inline constexpr 静态字符存储与 hash_literal_stored 结果；运行期仅取其地址与 N-1；不能引用函数局部临时副本 |
| 内部 to_format_input(view) → detail::FormatInput | 过滤通过后按字段适配；不重新计算 hash、不读取正文、不添加 wire flag；非零 hash 不向调用者开放写入 |

如果提供 data()/size() 等只读 getter，只返回对应字段；不要暴露更改 hash 的 setter。
runtime 源缓冲区须活到同步 try_log 返回且期间无并发修改；encode 完成后 Record 已深拷贝。
空格式、内嵌 NUL、char8_t、runtime/literal 同字节一致性与 33 参数编译期拒绝均交给 H 验收。

<a id="i2-filter"></a>

## B. FilterState：完整声明、构造和两层过滤

本章取代旧版 B1～B6。多 Appender 合同见 [ADR-012](./ADR-012-v1-multi-appender.md)。
先完成 B1～B5，再接 Channel；不要把当前文件里的 `class Logger` 继续扩展成第二个 Logger。

### B1. 对象关系与所有权

一个 AsyncLogger 拥有一个稳定地址 FilterState、多个 Channel，以及一组可重置的 Appender。
Channel 只借用 `const FilterState*`。Producer 不保存 Appender 指针，不遍历输出目标。
FilterState 只保存 Producer 粗过滤和 Logger category；各 Appender 的精确过滤属于配置/后台层。
category 名称与数量在 Logger 创建时固定，0 是默认 category；数组从构造到 Backend join 不换地址。
Appender 增删不会改变 category ID、Channel 或 Record wire。

### B2. filter_state.hpp 的完整声明

沿用决策日志第 47 条的指针加显式长度风格；本构造和验证 helper 不使用 std::span。
这是接口一致性约定，不代表 std::span 本身必然更慢。

下面是这一文件的声明与头内查询参考；构造和私有发布函数按 B3 在 cpp 实现。
`FilterConfigAccess` 是 detail 内部配置桥接类，生产实现必须限制其调用路径；不是 public 用户 API。
不把 private 改 public 来让测试或业务代码绕过配置校验。

```cpp
#pragma once
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <cstddef>

namespace qlog::detail {
class FilterConfigAccess;
class FilterState final {
public:
    // initial_categories 非空，category_count > 0；每项只能为 0/1。
    // 第 0 项为默认 category；输入只借用至构造返回。
    // initial_merged_levels 由经过验证的 Appender 列表计算，允许为零。
    explicit FilterState(std::uint32_t initial_merged_levels,
                         const std::uint8_t* initial_categories,
                         std::size_t category_count);
    ~FilterState() = default;
    FilterState(const FilterState&) = delete;
    FilterState& operator=(const FilterState&) = delete;
    FilterState(FilterState&&) = delete;
    FilterState& operator=(FilterState&&) = delete;

    [[nodiscard]] std::uint32_t category_count() const noexcept {
        return category_count_;
    }
    [[nodiscard]] bool category_enabled_unchecked(std::uint32_t id) const noexcept {
        assert(id < category_count_);
        return category_enabled_[id].load(std::memory_order_relaxed) != 0;
    }
    [[nodiscard]] bool allows_unchecked(std::uint32_t id,
                                        std::uint8_t level) const noexcept {
        assert(id < category_count_ && level < 6U);
        const auto bitmap = merged_levels_.load(std::memory_order_relaxed);
        return (bitmap & (std::uint32_t{1} << level)) != 0U
            && category_enabled_unchecked(id);
    }
private:
    friend class FilterConfigAccess;
    void publish_merged_levels(std::uint32_t bitmap) noexcept;
    void publish_category(std::uint32_t id, bool enabled) noexcept;

    const std::uint32_t category_count_;
    std::unique_ptr<std::atomic<std::uint8_t>[]> category_enabled_;
    std::atomic<std::uint32_t> merged_levels_;
};
} // namespace qlog::detail
```

为什么这些字段这样声明：count 不随热更新变化，读它不需要 atomic；数组由本对象独占拥有，
不用会增长的 vector<atomic<T>>；对象禁止移动以保护 Channel 的借用指针。
merged_levels 初始化不能默认全开，它取决于实际配置。每个 category 不单独填充 64B。
头内查询是短路的两次 relaxed load，不含 mutex、shared_ptr、TLS、分配、RMW 或 Appender 循环。
Tier 1 的 atomic<uint32_t>/atomic<uint8_t> lock-free 条件仍须在实现验收时检查。

### B3. filter_state.cpp：按依赖顺序实现

文件先 include 自己的头，再直接 include `<limits>`、`<stdexcept>`。
匿名 namespace 写一个验证函数，建议签名：

```cpp
std::uint32_t validate_initial_filter(
    std::uint32_t merged, const std::uint8_t* categories,
    std::size_t category_count);
```

验证顺序：unknown level bits（`merged & ~0x3fU`）、category_count 为零、categories 为空指针、
category_count 超过 uint32_t 上限，最后逐项检查任一 byte 是否大于 1。
在这些元数据检查通过前不得解引用 categories 或计算派生指针。任一失败抛 invalid_argument，成功返回安全窄化后的长度。
源区间由调用者保证至少有 category_count 个可读 byte，且本次调用期间不被其他线程修改；
指针/长度检查不能验证野指针或实际分配长度。构造拒绝零 category；不把 nullptr/0 解释为隐式默认配置。

构造初始化列表必须按成员声明顺序：

```cpp
FilterState::FilterState(std::uint32_t merged,
                        const std::uint8_t* categories,
                        std::size_t category_count)
    : category_count_(validate_initial_filter(merged, categories, category_count)),
      category_enabled_(std::make_unique<std::atomic<std::uint8_t>[]>(category_count_)),
      merged_levels_(merged) {
    for (std::uint32_t i = 0; i < category_count_; ++i) {
        category_enabled_[i].store(categories[i], std::memory_order_relaxed);
    }
}
```

先验证再分配，bad_alloc 自然向上传播；构造失败不会得到半初始化 FilterState。
构造完成前不能发布 Channel，也不能启动借用本对象的 Backend。
源 category 数组可以在构造返回后销毁，因为所有 byte 已复制到拥有型存储。

两个 private 发布函数只接收冷配置层已经验证的值：

```cpp
void FilterState::publish_merged_levels(std::uint32_t bitmap) noexcept {
    assert((bitmap & ~0x3fU) == 0U);
    merged_levels_.store(bitmap, std::memory_order_relaxed);
}
void FilterState::publish_category(std::uint32_t id, bool enabled) noexcept {
    assert(id < category_count_);
    category_enabled_[id].store(static_cast<std::uint8_t>(enabled),
                                std::memory_order_relaxed);
}
```

这些 assert 不是 public 输入校验；Release 调用者仍必须验证后才进入 private helper。
不要在 noexcept 构造/更新函数里分配或获取可能抛异常的管理锁。

### B4. 位图来自哪里：不再提供独立 Logger level setter

冷配置层使用 AppenderConfig 保存 name、type、enabled，以及 filter 成员中的六位 levels、固定长度 category bytes，
以及该类型的输出配置。用拥有型 string/vector 保存输入；不借用配置解析器的临时内存。
名字唯一；名称到目标的查找和 category pattern 展开都在冷路径。

合并函数的规则是：从 uint32_t{0} 开始，对所有 AppenderConfig 的 filter.levels 做 OR。
**不因 enable=false 跳过，也不按 Appender category 裁剪**。纯 OR helper 对空序列返回 0；Logger 外部空配置须先按 N1 生成默认 Console 配置，不能把 helper 的空输入语义当成 Logger 默认静默。
这与本次核对的 BQLog refresh_merged_log_level_bitmap 一致。
因此旧版 `AsyncLogger::set_enabled_levels(bitmap)` 删除，不能独立覆盖派生位图。
public 配置操作改为按名字更新某个 Appender 的 enable/levels/category，或重置整份 Appender 列表。
Logger category setter 仍保留，但先校验 ID；它不修改 Appender category。

以 Appender level 更新为例，完整调用链：

1. public 冷入口验证名字、位图和对象生命周期；非法输入返回失败且不发布。
2. 获取配置管理锁；定位目标。所有配置写者串行，避免两个 writer 基于旧列表覆盖彼此。
3. 将该目标的 filter.levels 更新到受管理的有效配置，重新 OR 完整列表，内部桥接类调用 publish_merged_levels。
   I2 使用配置模型；I3 有真实 Appender 后按 D15 保持单一有效状态源，不能两份配置各改各的。
4. 释放锁，返回结果。Producer 从始至终不获取这把锁。

enable 更新只改变目标 enable，不需要改变合并位图。Appender category 更新只改变目标表。
Logger category 更新验证后通过 publish_category 发布；批量更新先验证全部 byte/长度再写。
“冷路径”指低频管理路径，它可以在业务 Producer 运行期间执行。

### B5. 内存序、Backend 与重配置的边界

Producer 的位图/category 是独立 relaxed 标量，允许新旧组合，不承诺整组事务或瞬时全 CPU 可见。
配置管理锁串行写者，但不能让不加锁的 Producer 自动获得整组快照。
原子过滤值不承担新 Appender、Channel 或字符串的发布责任。

Backend 的 Appender 列表和普通字段必须由同一管理锁保护。I3 在一次有界扫描服务开始前取锁，
再获取 Frame；完成本次所有 Frame 释放后解锁。不得拿着借用的 Frame 等待该锁。
可以一次服务处理多条，避免每条单独取锁；公平扫描预算仍按 I3 合同。
重置配置在取得该锁、无借用 Frame 的边界应用，旧对象退出后才销毁。
这是 QLog 的无数据竞争实现方案，不声称逐行照搬 BQLog 的 spin lock。
关停须先停止/join Producer 和配置调用者，再排空 Backend，最后销毁 FilterState。

Producer 基础校验 handle/level/category 后只做一次 allows_unchecked；无效 level 不得移位。
过滤拒绝不读 format 内容、不 strlen、不 measure、不 reserve、不读时钟。
Backend 安全 decode 后，重新检查 Logger category，并按当时各 Appender enable/level/category 分发。
已入队记录不保存 Appender ID 列表，也不带配置 generation；允许因后续修改而输出至不同目标。
codec policy 始终接受合法六等级，不能拿 merged_levels 当 Record 合法性约束。

### B6. 用这些例子验收，不只检查能否编译

| 配置/动作 | Producer | Backend |
|---|---|---|
| A=info，B=error，Logger category 开 | 合并 0x14；info/error 可入队 | 分别只去匹配目标 |
| A/B 都包含 info 且启用 | info 只入队一次 | 两份输出、一次 Frame release |
| A 禁用、B 只收 error | info 仍可能入队 | info 没有输出 |
| 用户 Appender 列表为空 | 先生成默认 Console，合并其 filter.levels | 通过过滤的记录按处理时 Console 配置输出 |
| Appender category 全关 | level 仍可通过粗过滤 | 精确过滤拒绝 |
| 入队后关闭 Logger category | 之前 accepted 不撤销 | 处理时零输出 |
| category 越界或 level=255 | invalid_category / invalid_level | 不访问数组/不移位 |

另外检查构造空表、nullptr 配非零长度、超 uint32_t 长度（检查时不得读数组）、非法 byte/unknown bits、源数组销毁后查询、更新非法值保持不变。
需要确定更新先后的并发测试使用 barrier/join 等同步；不要把 sleep 当同步证明。
I2 用配置模型与 test consumer 检查粗过滤；真正多 Appender 路由和重置与读取互斥属于 I3/I4。

### B7. 当前代码入口（2026-09-14 重新核对）

早期 class Logger、category 拼写和成员顺序问题已在当前 FilterState 头中修正，勿重复执行旧清单。
现有 cpp 仍有 `throw { ... }` 语法错误，逐 byte 验证位于错误分支内；这会漏掉合法元数据下的非法 category 值。
具体替换 helper、后续配置写入口及原因见 [补充指南 §3](./MILESTONE2_I2_CONTINUATION_GUIDE_CHS.md#3-b先修-filterstate-的构造再接配置写入口)。

### B8. 当前基础类型待办

log_level.hpp 的直接 cstdint include 与头保护已补齐；LogResult::failure() 已判空。
当前问题转为未初始化 status、同名 accepted 修改器、failed 反向赋值、空 FailureStage 与不完整原因集；
log_format.hpp 仍为空。精确现状见 §0.6，逐函数修改要求见 A7/A8。
本次只更新文档，不将这些生产问题标为已修复。

### B9. FilterState 与冷配置函数逐项实施索引

| 函数/定义 | 实现步骤和返回/失败行为 |
|---|---|
| validate_initial_filter(merged, categories, count) → uint32_t | 先检查未知位、非零长度、非空指针、可表示长度，再无条件逐 byte 检查 0/1；失败抛 invalid_argument；最后安全窄化返回。当前必须把逐项循环移出错误分支，并修 throw |
| FilterState 构造 | 按 count、数组、bitmap 的声明顺序初始化；验证先于分配，数组分配后逐项复制；bad_alloc 向上传播；尚未完成时不能发布 |
| category_count() const noexcept | 返回固定数量，普通读即可；建议成员改 const，不另建热更新入口 |
| category_enabled_unchecked(id) const noexcept | 前置条件 id 已合法；一次 relaxed load 判断非零；Debug assert 只诊断，不替代外部检查 |
| allows_unchecked(id, level) const noexcept | 前置校验完成后读一次位图，判断 level 位；通过才读 category 开关，允许短路；不读 Appender 列表、不重试获取快照 |
| publish_merged_levels(bitmap) noexcept | 仅桥接访问类调用；输入已验证且来自完整列表 OR；一次 relaxed store，不发布新对象 |
| publish_category(id, enabled) noexcept | 校验后的 id，bool 转 0/1 后一次 relaxed store；不改名称和数组地址 |
| FilterState 析构/删除的复制移动 | unique_ptr 自动销毁原子数组，前提是所有借用者退出；禁止复制移动以保护稳定地址 |

`FilterConfigAccess` 是权限桥接，不拥有第二份过滤表。其薄转发函数只接受既有 FilterState 引用和
已验证标量，再调用对应 private publish；验证、列表管理与锁由外层配置函数承担。
以下名称为建议 helper，尚不存在于源码，不能写成“已有 API”：

| 冷函数（建议） | 怎样实现 |
|---|---|
| validate_appender_configs(configs, count, category_count) | 先验证指针/长度关系，再检查名字唯一、类型合法、levels 低六位、每表长度一致及 byte=0/1；完全验证后才允许修改目标配置；不打开文件 |
| merge_appender_levels(configs, count) → uint32_t | 接收已验证配置；从 0 OR 所有 levels，包括禁用目标；不合并 category；空列表输出 0 |
| update_appender_levels(name, bitmap) | 外层拒绝未知位，锁内查找目标、修改 levels、重算完整 OR、发布；目标不存在不写入；若未来需要分配，先完成准备再变更 |
| update_appender_enabled(name, enabled) | 锁内定位后改普通 enable；不重算/缩小 Producer 位图；未知名字拒绝 |
| update_appender_category(name, id, enabled) | 校验 ID，锁内定位后改该目标表；不改 Logger category 与合并 levels |
| reset_logger_categories(pointer, count) | 冷入口先检查长度匹配和所有值，全部合法后逐项 publish；不部分接受非法输入；合法更新允许读者观察混合态 |
| reset_appender_configs(pointer, count) | I2 配置模型可先验证/复制候选值并计算 OR；真实目标列表替换在 I3 无 Frame 借用的管理锁边界落实；I4 文件失败合同未定，不新增假成功 public reset |

需要分配或获取管理 mutex 的配置入口不强行 noexcept；标量 setter 的特殊边界见 D14。
对 reset 的“不变”保证指非法输入在变更前拒绝，不意味着多个合法原子 store 是事务。

<a id="i2-clock"></a>

## C. AdmissionClock：启动探测与逐条采样分开

### C1. 文件、枚举与完整声明（2026-09-14 补充）

文件固定为 `include/qlog/detail/admission_clock.hpp` 与 `src/admission_clock.cpp`。
头文件按 source/语义枚举 → timestamp status → Descriptor → Timestamp → 函数顺序写。
cpp 先 include 自己的头，再直接 include `<time.h>`、`<cstdint>`、`<limits>`；不依赖 Ring/measure/encoder。
本节补全 [ADR-008](./ADR-008-realtime-coarse-admission-timestamp.md) 的既有字段语义，
以下是建议的 C++ 表达，不新增时钟策略，不冻结对象 sizeof 或二进制 ABI。

```cpp
#pragma once
#include <cstdint>

namespace qlog::detail {
enum class ClockSource : std::uint8_t {
    unavailable,
    realtime_coarse,
    realtime
};
enum class ClockDomain : std::uint8_t { unix_epoch };
enum class ClockUnit : std::uint8_t { nanosecond };
enum class ClockCalibrationKind : std::uint8_t { none };
enum class ClockSamplingPoint : std::uint8_t { admission_timestamp };
enum class TimestampStatus : std::uint8_t {
    primary_valid = 0,
    fallback_valid = 1,
    time_unavailable = 2
    // 3 是协议保留值，不作为正常 Producer 生成项。
};

struct ClockDescriptor final {
    // 冷描述版本，不是 Record ABI 版本。
    static constexpr std::uint32_t clock_descriptor_version = 1;
    static constexpr ClockDomain domain = ClockDomain::unix_epoch;
    static constexpr ClockUnit unit = ClockUnit::nanosecond;
    static constexpr ClockCalibrationKind calibration_kind =
        ClockCalibrationKind::none;
    static constexpr ClockSamplingPoint sampling_point =
        ClockSamplingPoint::admission_timestamp;

    ClockSource primary_source{ClockSource::unavailable};
    ClockSource fallback_source{ClockSource::unavailable};
    std::uint64_t primary_resolution_ns{};
    std::uint64_t fallback_resolution_ns{};

    [[nodiscard]] bool has_fallback() const noexcept {
        return fallback_source != ClockSource::unavailable;
    }
};

struct Timestamp final {
    std::uint64_t time_value{};
    std::uint8_t flags{
        static_cast<std::uint8_t>(TimestampStatus::time_unavailable)
    };
};

[[nodiscard]] ClockDescriptor probe_admission_clock() noexcept;
[[nodiscard]] Timestamp sample_admission_timestamp(
    const ClockDescriptor& descriptor) noexcept;
} // namespace qlog::detail
```

| 类型/字段 | 作用 | 为什么这样保存 |
|---|---|---|
| ClockSource | 选择 coarse、realtime 或不可用 | 内部枚举不直接等于 Linux clockid_t；cpp 显式映射 |
| ClockDomain / ClockUnit | Unix Epoch / 纳秒 | V1 固定语义，用 static constexpr，不为每个对象重复存储 |
| ClockCalibrationKind | none | 两个 source 同属 realtime，不增加 TSC 校准 |
| ClockSamplingPoint | admission_timestamp | reserve 成功后、写 Record payload 前采样 |
| TimestampStatus | 本条 primary/fallback/unavailable 结果 | 数值 0/1/2 对齐既有 flags；与 source 枚举分开 |
| primary/fallback_source | 启动时选中的 source | 构造后不修改，不因某次失败永久切换 |
| primary/fallback_resolution_ns | 实际探测分辨率 | 纳秒是单位，不是精度保证；缺失 source 对应值为 0 |

是否可用由 source 判断，不从 resolution 数值猜测。ClockDescriptor 默认是 unavailable-only。
Timestamp 默认 `{0, time_unavailable}`，避免未填充结果被当作成功；真正成功读取 Epoch 0 仍允许 flags=0。
realtime 晋升为 primary 后，成功状态仍是 primary_valid，不能按 source 名字决定 status。
状态枚举供 Producer 生成 flags；不替换 I1 已有的 status mask/known flags mask，不改变 Record wire。

Descriptor 在 Logger 初始化阶段由 probe 构造，之后只读，Channel 借用 const 引用/指针。
owner 必须存活至 Backend join、Channel 回收完成。无需 atomic、虚函数、shared_ptr 或逐条复制描述。
上述 struct 允许 probe 填字段，但运行期不得公开可修改它的入口；不可变性由 owner 生命周期和 const 访问落实。
原子发布 Channel 的职责仍属于注册链，不能把普通 Descriptor 字段当同步变量。
固定语义枚举不产生逐条运行时分支；生产路径只读取采样需要的 source。

### C1a. cpp 的四步实现顺序

1. 匿名 namespace 实现 C3 的 `epoch_ns_from_timespec`。所有检查成功才写 output；失败保持 output 不变。
2. 实现单 source 的 resolution probe helper，例如 `bool probe_resolution_ns(ClockSource source, std::uint64_t& output) noexcept`。
   switch 显式映射到 CLOCK_REALTIME_COARSE/CLOCK_REALTIME；unavailable 直接失败，不调用 libc。
   局部 timespec 零初始化；clock_getres 返回 0 后仍须验证 timespec 和转换溢出，最后才写 output。
3. 实现 `probe_admission_clock()`：分别探测两种 source，按 C2 表组装描述。不得重复把 realtime 作为 primary 和 fallback。
   realtime 晋升时，它的分辨率写 primary_resolution_ns；缺失 source 的分辨率保持 0。
4. 实现单 source 读取 helper 和 `sample_admission_timestamp()`，按 C4 处理 primary/fallback/unavailable。
   source 映射复用同一规则；不把 clock_getres 带进逐条采样路径。

I1 policy 使用 `make_producer_policy(descriptor.has_fallback())` 建立一致关系；
policy 与 Descriptor 一起在发布前构造。不能写死 fallback=true，也不把 TimestampStatus 放进 level mask。
测试注入沿用 C5；当前完整头文件只是声明参考，不能代替这些 cpp 实现与运行时验证。

### C2. 冷路径能力选择

先通过 `clock_getres` 分别探测 coarse/realtime；检查返回值与 timespec 合法性。
分辨率 probe 的转换必须同样检查溢出，不能把非法 probe 当作 source 可用。

| coarse 可用 | realtime 可用 | descriptor primary | descriptor fallback |
|---|---|---|---|
| 是 | 是 | coarse | realtime |
| 是 | 否 | coarse | none |
| 否 | 是 | realtime | none |
| 否 | 否 | unavailable | none |

最后一行保留日志能力，允许冷路径记录一次非递归诊断；热路径不反复探测已知不可用的 source。
policy.has_fallback 只由 fallback 是否存在决定，不能简单写 true。

### C3. timespec 转纳秒：用减法/除法界定再乘加

私有 helper 建议：

```cpp
[[nodiscard]] bool epoch_ns_from_timespec(const timespec& input,
                                         std::uint64_t& output) noexcept;
```

1. 拒绝负 tv_sec、负 tv_nsec 或 tv_nsec >= 1,000,000,000。
2. Tier 1 上合法非负值再转换到 uint64_t；不能先窄化后判断符号。
3. 令 billion=1,000,000,000，先证明 `seconds <= (UINT64_MAX - nanos) / billion`。
4. 只有证明成功后才计算 `seconds * billion + nanos` 并写 output。
5. 失败不使用 output 的旧值，调用方初始化局部结果。

0 秒 0 纳秒是合法 Epoch 值，不要求成功时间非零。
不要使用未检查的 chrono count，也不把纳秒精度和 coarse 实际分辨率混为一谈。

### C4. 热路径采样算法

```text
descriptor 无 primary → {0, time_unavailable}
调用 descriptor.primary 的 clock_gettime
  成功且转换合法 → {ns, primary_valid}
  否则，有 fallback：调用 fallback
    成功且转换合法 → {ns, fallback_valid}
  否则 → {0, time_unavailable}
```

primary=realtime 时也写 primary_valid(0)，而不是因为名字叫 realtime 就写 fallback_valid(1)。
每条重试已配置 primary；不因上条失败永久切换模式。
每次只在成功 reserve 与第一次 Record payload 写入之间调用。
两级均失败仍 encode/commit，flags=2 且 time_value=0。
Ring 自身 reserve 写外层 FrameHeader 不等于已经写入 Record payload，这不违反采样点。

### C5. 可测试注入而不污染生产热路径

不要新增运行时全局 clock setter 或每条 std::function。
推荐 cpp 内部模板 `sample_with(descriptor, clock_ops)`，生产绑定薄 LinuxClockOps；
测试通过单独的内部访问入口提供假 clock_getres/gettime。
注入类型只用于测试构建，或以静态模板内联；不得通过宏让不同 TU 看到不同的公共类型定义。
测试必须覆盖成功返回但 timespec 非法，而不只是 syscall 返回 -1。

### C6. 本章完成条件

- 四种 descriptor 组合、三种 timestamp status 都有明确生成路线。
- fallback 与 I1 policy 一致；reserved(3) 永不生成。
- 没有逐条 clock_getres、分配、共享 RMW、墙钟 clamp 或跨线程排序。
- helper 可独立验证，不依赖建立 Logger/Backend。

### C7. 当前时钟实现与继续位置（2026-09-14 重新核对）

头文件已统一为 admission_clock.hpp；clock_id_for 每个分支已有明确返回，
probe_resolution_ns、read_time_ns、probe_admission_clock、sample_admission_timestamp 均已有函数体。
仍需修正 epoch_ns_from_timespec 的十亿乘法：当前逗号表达式不能算出所需纳秒值。
本轮另发现非 const sample_admission_timestamp 重载会覆盖 resolution、未读取 fallback 就报告成功；应移除该重载，保留 const 版本，见 N7。
因此不再按“缺函数”重写整个模块，而按 C8 核对现有算法并完成接线与行为验收。

### C8. 现有时钟函数逐项检查与实现理由

| 函数/操作 | 作用与实施步骤 |
|---|---|
| ClockDescriptor 默认构造 | primary/fallback 均 unavailable，resolution 均 0；四类静态语义字段按 C1 固定；不探测系统 |
| has_fallback() const noexcept → bool | 仅判断 fallback_source 非 unavailable；不以 resolution 是否为零判断，不调用 libc |
| Timestamp 默认构造 | 明确 time_value=0、flags=time_unavailable；防止默认对象冒充 primary 成功 |
| epoch_ns_from_timespec(input, output) noexcept → bool | 先排负值及纳秒越界，再转换无符号，减法/除法证明乘加不溢出；全部通过后才写 output；失败不改 output。当前仅检查式正确，最终乘法必须修复 |
| clock_id_for(source, out) noexcept → bool | switch 显式映射两种 Linux ID；不可用/未知值返回 false 且不写 out；不是能力探测，当前已补返回值 |
| probe_resolution_ns(source, out) noexcept → bool | 映射失败即返回；clock_getres 成功后还需 timespec 转换成功；失败不写 out；只在冷构造使用 |
| read_time_ns(source, out) noexcept → bool | 映射后调用 clock_gettime 并检查转换；失败不接受旧 output；不顺手 probe 分辨率 |
| probe_admission_clock() noexcept → ClockDescriptor | 分别 probe coarse/realtime，按 C2 四种组合设置 source 和对应 resolution；缺失 source 保留 0；全不可用仍返回完整 descriptor，不使 Logger 创建失败 |
| sample_admission_timestamp(descriptor) noexcept → Timestamp | 无 primary 立即 unavailable；primary 读成功标 0，否则有 fallback 才读第二次并标 1；全失败标 2；realtime 为 primary 时仍标 0；不永久切换 descriptor |
| sample_with / LinuxClockOps（可选测试接缝） | 将上述静态算法与系统调用薄封装分离；生产仍直接绑定 Linux 实现，测试可计次数/注入非法值；不能加入逐条 std::function 或全局可改回调 |

重点验收 seconds=1、nanos=0 应得十亿，可直接捕捉当前逗号表达式问题；还需 Epoch 0、最大边界、
负值、纳秒越界、系统调用失败及“返回成功但 timespec 非法”。filtered/full 路径应零采样。
这些是待执行行为检查，本轮文档审阅没有运行这些测试。

<a id="i2-channel"></a>

## D. Channel / AsyncLogger：地址、线程和生命周期

### D1. Channel 冷数据保存什么

文件：detail/channel.hpp。
建议顺序：内部 forward declarations → ChannelCold → Channel 完整定义。

ChannelCold 至少关联：

- Logger identity 与稳定 category 名称表；
- 不复用的 producer token、用于显示的 OS thread_id/name；
- Frame ABI/Record ABI、Header bytes=32、hash identity；
- 本 Channel 的 payload quota；
- FilterState、ClockDescriptor、RecordValidationPolicy、FormatHashDispatch 的稳定引用。

不要把日志等级/当前 category 固定到 Channel；它们是逐条调用输入。
不要为了省字段把时钟 pointer 放进 Record；Record 字节身份保持不变。
引用的 Logger 冷对象必须比所有 Channel 活得更久。

### D2. Channel 直接内嵌不可移动的 Ring

建议成员组织：

```text
immutable cold metadata / borrowed stable references
immutable published_next pointer
SpscRingBuffer ring
Debug-only ProducerDiagnostics (separate cache line)
```

SpscRingBuffer 已不可复制、不可移动，因此 Channel 同样禁用复制/移动。
不要 `vector<Channel>`；使用稳定堆对象，由 `unique_ptr<Channel>` 持有。
普通 new/make_unique 对 over-aligned Channel 的对齐由 C++17+ 分配规则保证；别用未对齐字节数组承载对象。
内嵌 Ring 的现有 cache-line 隔离不修改；Channel 冷字段不要插入 Ring 内部四条热缓存线。

### D3. 容量不是 format 上限

沿用现有 Ring 默认：capacity=64KiB，max_payload=8KiB；Logger 配置允许创建前修改。
BQLog 桌面默认 buffer 也是 64KiB，但 QLog 不继承其 block/expand 策略。
本次维持固定容量和 drop_new。

验证调用现有 Ring 构造合同，同时增加 Logger payload 必须能容纳 32B RecordHeader：

```text
capacity 为 2 的幂，16 <= capacity <= 2^31
32 <= max_payload <= capacity / 2
```

因此 Logger 有效容量事实上至少 64B。
不增加另一套 frame geometry；Ring 自己的原始接口仍允许其已支持的更小 payload。
8KiB 是完整 Record quota，不是额外再容纳 8KiB format；format 8192B 加 Header 已会超过默认 quota。
用户若要接受最大 format，需要显式增加 max_payload 与必要容量。
默认值是资源设置，不是声称最优性能；总 Channel 数不设未经测量的固定 128 上限。
注册容器可在冷路径增长，分配失败要回报；短命线程多会累积资源到 shutdown，见 D8。

### D4. AsyncLogger 是 owner，ProducerHandle 是轻量引用

推荐 public 声明形状：

```cpp
class AsyncLogger final {
public:
    explicit AsyncLogger(const LoggerConfig& config);
    ~AsyncLogger();
    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;
    AsyncLogger(AsyncLogger&&) = delete;
    AsyncLogger& operator=(AsyncLogger&&) = delete;

    [[nodiscard]] BindResult bind_producer();
    // Appender 配置入口按 ADR-012 分阶段接入；禁止独立覆盖 merged levels。
    [[nodiscard]] bool set_category_enabled(std::uint32_t id, bool enabled) noexcept;
    void close_registration();  // 冷路径；调用者保证不再启动新的绑定活动。
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
```

LoggerConfig 以普通值/拥有型容器描述名字、category 名称和 Ring 配置；构造时深拷贝并验证。
LoggerConfig 同时携带完整的拥有型 AppenderConfig（其中 filter 成员描述过滤条件），用于初始合并位图；I2 不解析 BQLog 配置字符串，不创建 Sink/worker。
运行中真实输出列表 reset 在 I3/I4 接入，不能提供返回成功的空实现。
构造允许 invalid_argument/bad_alloc；bind_producer 使用可检查 BindResult 处理注册关闭/资源不足。
mutex 锁异常等基础设施错误不要在 noexcept 函数中意外 terminate；冷路径无需强行全部 noexcept。
BindResult 成功携带有效轻量 Handle，失败不携带半初始化 Channel。
分配失败应在发布之前捕获/回滚；捕获哪些异常要写明，不能吞掉未知逻辑错误。

### D5. 线程身份：OS thread_id 不能当永久唯一键

OS TID / std::thread::id 可能在线程退出后复用，而 Channel 保留到 Logger 退出。
直接拿它们匹配旧 Channel 会把新线程误绑定到旧生产上下文。

使用进程内不复用的 producer token，在首次显式绑定时取得并缓存到线程本地。
冷路径可使用一次全局原子分配 token；同一线程向多个 Logger 绑定时复用此 token。
0 保留为未分配；计数耗尽必须拒绝创建，不能 wrap 后重用旧身份。
实现可用冷路径 CAS 循环做 checked increment，或冷 mutex 保护递增。
这不违反“稳态不执行 TLS/map/共享 RMW”：每条 try_log 不查询 token 服务。
显示用 thread_id/name 与 token 分开，名字在绑定时复制，不能保留线程栈 view。

### D6. 绑定算法：一次完成初始化再发布

Impl 拥有 registry mutex、`vector<unique_ptr<Channel>>` 和 atomic published_head。
owner vector 只在锁内访问；Backend 不遍历这个可能增长的 vector。

```text
取得本线程 producer token（冷路径）
锁住 registry mutex
  检查 registration_open
  按 token 搜索本 Logger 已有 Channel
    已有 → 返回指向同一 Channel 的 Handle
  准备 metadata、线程名字和 unique_ptr<Channel>
  设置 new_channel.next = 当前 published_head
  将 unique_ptr 放进 owner vector（可能分配失败）
  全部成功后 published_head.store(new_channel, release)
  返回 Handle
解锁
```

owner vector 的增长可以移动 unique_ptr，不会移动 Channel 本体。
必须先确保 owner 插入成功，再发布原始地址；异常回滚期间不得暴露 new_channel。
next 只在发布前写一次，发布后永不修改；多注册者由 mutex 串行。
同线程重复绑定返回同一 Channel，复制 Handle 不创建新 Ring。

### D7. I3 的只读注册发现接口

内部 reader 获取 `published_head.load(acquire)`，沿 next 遍历这次取得的链。
release/acquire 发布完整初始化的 Channel；owner vector、filter resize 等不是这条链的读取对象。
新注册节点加在头部，不改变 reader 已取得的旧链，新节点下一轮发现。
Channel 永不在 Backend 运行中销毁，所以遍历不需要 hazard pointer/refcount。
不要用头插操作永久重置 I3 公平扫描游标；I3 将按预算扫描并合并新节点，不能因持续注册饿死旧节点。
I2 只提供安全发布/发现合同与测试接口，不实现完整公平调度。
atomic pointer 在 Tier 1 要验证 lock-free；不要引入逐条日志共享 head.load。

### D8. ProducerHandle 的复制、移动和失效

按用户“接口对齐 BQLog”取轻量可复制语义，取代先前 move-only 建议。
建议仅保存 Channel*，默认空构造可表达未绑定，复制不做分配/引用计数。
不冻结 sizeof 为永久 ABI；V2 若需要独立上下文可调整内部表示。

- 副本借用同一 Channel，仅原绑定线程可使用；不支持跨线程搬运后继续写。
- 副本析构没有 Ring commit/abort、注销、回收或等待动作。
- 默认移动可以保持轻量复制式语义；不能依赖 moved-from 一定为空，文档需与实现一致。
- 空 Handle 调用返回 invalid_handle；Logger 销毁后的非空悬空 Handle 属于前置条件违例，无法安全自检。
- Debug 可用独立校验发现线程误用；Release 不加每条 gettid/refcount 以伪装为安全共享对象。

BQLog log 可从不同线程调用是因为内部 TLS 自动路由；QLog 已选择显式绑定，不能因此复制其跨线程调用承诺。
短命线程退出后 Channel 仍存活、token 不复用，可能增长 RSS，这是用户确认的生命周期代价。
V2 再评估共享 MPSC/线程退出回收，不在 I2 暗中加入回收协议。

### D9. 生命周期和 shutdown 分界

```text
Logger 创建并初始化元数据
  → 业务线程运行，可显式注册
  → 应用停止生产并 join 所有 Producer 线程
  → 停止其他配置/注册管理操作，close_registration
  → I3 Backend 排空所有已发布队列、发布剩余 reclaim
  → join Backend / 完成 Sink 退出
  → 销毁 Channel
  → 销毁其借用的 Logger 冷数据
```

本次不支持 shutdown 与业务日志调用真正并发，不需要 Producer 每条检查 shared closing flag。
close_registration 的 mutex 与绑定互斥；已在锁内完成的注册必须属于最终排空集合。
I2 尚无 Backend：集成测试用直接 consumer 排空；不能实现一个丢弃数据的空 shutdown() 并称 I3 完成。
本章声明不暴露未实现 shutdown 函数；I3 接入时补真正排空与 destructor 协调。
I2 owner 销毁前必须由调用方/测试满足无 Writer、无 Reader；这是阶段性使用前置条件。

### D10. 本章交接条件

- 无 Channel 搬家，无 published 后 next 修改，无 owner vector 并发裸遍历。
- 重复绑定、双 Logger、线程 token 复用规避、关闭注册和异常回滚都有明确行为。
- 注册锁/TLS 只在冷路径；每条日志从 Handle 直达 Channel。
- 没有实现自动 MPSC、频率判断、每条 shared_ptr 增减或运行中回收。

### D11. LoggerConfig：创建输入的完整定义说明

位置：`include/qlog/async_logger.hpp`，位于 BindResult、AsyncLogger 之前。
当前 LoggerConfig 已有草稿，但混入 Ring 实例和单个 Appender 配置；修正点见 N2。以下是目标字段表示，
不冻结新增 public ABI。直接 include 使用到的 string/vector/cstdint 及 Ring 配置头。

| 建议字段/类型 | 表示什么 | 初始化、验证和所有权 |
|---|---|---|
| name / std::string | Logger 显示身份 | 配置自己拥有；Impl 再复制持有，Channel 只借稳定副本；显示名不是线程 token |
| category_names / std::vector<std::string> | 固定 ID 到名字映射 | 数量至少 1，且可表示为 uint32_t；第 0 项默认 category；创建后不增删/重排 |
| category_enabled / std::vector<std::uint8_t> | Logger 初始 category 开关 | 数量与名称一致，每项 0/1；复制进入 FilterState 独占原子数组，不借配置 vector 地址 |
| ring / detail::SpscRingBufferConfig | 每个 Channel 的容量与完整 Record quota | capacity_bytes 默认 65536，max_payload_bytes 默认 8192；D3 全部约束在创建时检查；不是 Logger 全部 Channel 的共享总容量 |
| appenders / 拥有型 AppenderConfig 数组 | 初始目标的配置值集合 | 先规范化空集合为默认 Console，再全量验证和合并 levels；I2 保存配置，不表示 Sink 已启动 |

为便于实现，可让默认配置带一个名为 default 的 category 且初始启用，Appender 集合默认空；
空 Appender 集合按 N1 自动补默认 Console 配置。调用者显式清空 category 表仍应被拒绝，
不由 FilterState 偷偷补一项。Logger 名称具体默认文本不影响合同；不凭本指南另加空名/重名 Logger 禁令。

AppenderConfig 是完整的创建/更新配置值类型，与抽象运行基类 Appender 分别定义。
过滤条件集中放在 filter 成员；运行对象的虚接口与所有权见 D15。字段建议如下：

| 字段 | 含义与约束 |
|---|---|
| name / string | 在本 Logger 的目标列表中唯一，用于更新和 reset 匹配；持有字符，不借解析器临时值 |
| type / 强类型枚举 | 目标种类，V1 范围 Console/TextFile；I2 只验证/保存模型，不能将 TextFile 标成已可运行 |
| enabled / bool | 后台是否尝试该目标；false 不影响其 levels 参加 Producer OR |
| filter / FilterConfig | 按值持有下表的 levels 与 category_enabled；不拥有 FilterState，不含原子、锁或输出资源 |
| 类型专用配置（后续） | Console 无文件路径参数；TextFile 配置描述文件位置、独立时区及 batch 设置，具体字段按 I4 规范补齐；与 type 必须匹配，不放文件句柄、打开的 Sink 或实际 batch 缓冲区 |

FilterConfig 是普通配置值，成员明确为：

| 成员 | 含义与校验 |
|---|---|
| levels / uint32_t | 独立六位 mask；允许 0，未知位拒绝，不是阈值；合并时读取 config.filter.levels |
| category_enabled / vector<uint8_t> | 与 Logger 固定 category 数量一致，每项 0/1；由目标精确过滤使用，不合入 Producer 位图 |

enabled 保留在 AppenderConfig 顶层，表示目标启停；filter 只描述记录筛选条件。
LoggerConfig::appenders 建议为 vector<AppenderConfig>；Config 本身不需要虚函数，也不继承 Appender。
配置构造/复制只复制拥有值；FilterConfig 不是运行中的 FilterState：后者负责 Producer 可并发读取的原子粗过滤。
目标类型专用配置可以使用受控值类型组合；具体表示不在本轮冻结，但不能让 type 与专用参数矛盾。
Console 也必须有完整配置，也不为 TextFile 的未决 I/O 失败策略补一个假成功实现。

配置构造/复制只是准备普通拥有值，不应开线程、注册 Channel、探测 clock 或写原子过滤表。
真正 `AsyncLogger(config)` 才验证并建立运行对象。传入配置可在 Logger 构造成功后修改/销毁，
不影响 Logger 的持有数据；运行中变更须走显式管理入口，不能改原 config 期待自动生效。
向内部批量函数传 `data()` 与 `size()`，不新增 std::span。

实现配置验证 helper 时按“元数据合法 → 每项合法 → 深拷贝候选 → 建立依赖”推进。
Ring 校验应复用既有约束/可访问的纯几何逻辑，不为验证创建一个临时 Ring 再销毁，也不照搬新的几何算法。
若需从现有构造检查提取共享 helper，属于后续生产改动，须保留底层 Ring 原合同与回归验收。
无效配置抛 invalid_argument；内存不足按 bad_alloc 传播；不要在构造中返回看似有效的空 Impl。

### D12. BindError / BindResult：绑定是否成功与日志是否入队是两种结果

位置同 D11，先完整定义 ProducerHandle，再让 BindResult 按值保存它；只有前置声明不够。
BindResult 回答“是否取得本线程本 Logger 的 Channel”；LogResult 回答“本次写入发生什么”。
重复绑定成功也没有新增日志，不能把它记为 LogStatus::accepted。

沿用续写指南的可选表示：两个 private optional，分别保存 ProducerHandle 与 BindError。
不开放默认构造，受控构造保证二者恰好一个有值；optional 内嵌保存，不需要堆分配或 shared_ptr。

| 定义/状态 | 精确含义 |
|---|---|
| BindError::registration_closed | 注册锁内检查到关闭；不发放新结果，包括关闭后再次 bind 的请求；此前发放 Handle 的使用受整体生命周期约束 |
| BindError::resource_exhausted | Channel/Ring/冷名字/owner 容器准备分配失败；未发布新节点，已有节点仍有效 |
| BindError::token_exhausted | 进程 token 分配不能再生成不复用的非零值；禁止回绕，也不退回 OS TID 作为键 |
| handle_ / optional<ProducerHandle> | 仅成功时有值，且指向有效已持有/已发布节点；不拥有节点 |
| error_ / optional<BindError> | 仅失败时有值；不是异常字符串，不混同 Producer 错误阶段 |

逐函数完成方式：

1. 私有 `BindResult(ProducerHandle) noexcept`：把有效 Handle 放入 handle_，error_ 为空。
   仅由 AsyncLogger 绑定成功路径调用；构造函数本身不能验证一个任意裸地址的生命周期。
2. 私有 `BindResult(BindError) noexcept`：handle_ 为空，error_ 保存值；无分配、不打印。
3. `handle() const noexcept → const ProducerHandle*`：有值时返回内部地址，否则 nullptr。
4. `error() const noexcept → const BindError*`：有错误时返回内部地址，否则 nullptr；不要解引用空 optional。
5. 复制/析构：仅操作内嵌值。取出的内部指针只在结果存活且未替换时有效；要保留使用资格就复制 Handle 值，
   但复制不延长 Logger 寿命。若提供 succeeded()，只按 handle 是否存在判断，不再保存第二份成功标记。

bind 不强行 noexcept：bad_alloc 映射为资源不足，mutex 等基础设施异常按冷路径异常传播，
不能 catch(...) 后全部伪装成资源不足。发布之后成功结果的包装必须无分配、无抛异常。
验收分别检查首次/重复绑定、关闭、token 耗尽、各分配点失败、结果复制与错误查询，不只测 handle 非空。

### D13. ChannelCold、Channel、Impl 的字段与构造析构

ChannelCold 位于 `detail/channel.hpp`，是不可变元数据集合，不拥有整个 Logger。

| 数据 | 谁持有、谁借用、如何初始化 |
|---|---|
| Logger identity/category 名称 | Impl 持有稳定字符串和固定表，ChannelCold 借用；不借 LoggerConfig 临时对象 |
| producer token | 按值保存进程级线程身份，与显示 TID 分开 |
| OS thread ID/name | bind 冷路径取得；名字应持有副本或借稳定 owner，不能借线程栈 |
| Frame/Record ABI、32B Header、hash identity | 引用/保存已有常量和冷身份；不再造 wire 版本，不逐条计算 |
| payload quota | 保存与内嵌 Ring 配置一致的值；供 measure 使用，不混用 capacity |
| filter/clock/policy/hash dispatch | const 指针或引用指向 Impl 稳定对象，所有依赖在 Channel 创建前完成 |
| Channel::published_next | 创建时初始化，发布前指向旧 head；发布后永不修改 |
| Channel::ring | 直接内嵌 SpscRingBuffer，由构造初始化列表用配置建立；禁止默认构造后赋值 |
| Channel::diagnostics | 仅诊断宏启用时存在，初始全部为零，按 F 隔离缓存线 |

`Channel(cold, ring_config)` 的具体参数可按实现统一：先保存冷元数据和 next 初值，
再直接构造 Ring；分配失败自然抛出并销毁已构造成员。它不获取注册锁、不写 head、不发 Handle。
`~Channel()` 由 owner 在无 Writer/Reader 时调用，自动释放 Ring；不隐式 commit/abort 或异步注销。
Channel 复制、赋值、移动全删除，因为 Ring 不可移动且节点地址已被借用。

`AsyncLogger::Impl` 完整定义放 `src/async_logger.cpp`，其字段分两组：

- 稳定依赖先声明：拥有型 Logger/category 配置、FilterState、ClockDescriptor、policy、dispatch。
  Filter 依赖初始 Appender OR；policy 依赖已 probe 的 clock.has_fallback；dispatch automatic 冷选一次。
- 管理状态与 owners 后声明：注册 mutex、registration_open=true、vector<unique_ptr<Channel>>、
  atomic published_head=nullptr，以及冷 Appender 配置管理状态。被 Channel 借用的数据都应先于 owners 声明，
  使逆序销毁先清 Channel。注册锁与配置锁各管各的状态，当前避免嵌套持锁。

Impl 构造完成前不启动 reader。成员声明顺序决定构造/析构，不能仅调整初始化列表来改变顺序。
没有 Backend 时也必须等测试 reader 退出才能析构；成员顺序不替代 join。

### D14. AsyncLogger 与内部冷函数：按职责逐一实现

| 函数（helper 名为建议） | 作用与实现步骤、错误及同步边界 |
|---|---|
| validate_logger_config(config) | 按 D11 全量验证 category/初始开关/Appender/Ring；不创建临时 Ring、不发布；invalid_argument 表示输入错误 |
| Impl(config) | 验证并复制拥有值，算 merged，建 Filter，probe clock，建 policy/dispatch，再初始化空注册表；任一失败自动清理已构造成员 |
| AsyncLogger(config) | 在 cpp 中构造 unique_ptr<Impl>；不能把空 Impl 当成功；异常传播给创建者 |
| ~AsyncLogger() | 定义放完整 Impl 之后，保证 unique_ptr 删除器看见完整类型；I2 在调用方已停止所有使用者的前提下销毁成员；不冒充 I3 shutdown |
| Logger 复制/移动及赋值 | 均删除；保护元数据和 owner 身份稳定，不通过搬运 Impl 增加未定义的 Handle 语义 |
| acquire_producer_token() | 冷路径读取 thread_local 缓存；未分配则以进程级锁或 checked CAS 分配非零 token 并缓存；耗尽失败、不回绕；try_log 不调用它 |
| find_channel_by_token(token) | 注册锁内遍历本 Logger owners；命中返回已有节点，不按显示 TID 匹配；不需新增热路径 map |
| bind_producer() | 取得 token，注册锁内检查 open、查重复；未命中则准备节点/名字，设 next，插入 owners，最后 release 发布 head；返回受控 BindResult；全部顺序见 D6 |
| set_category_enabled(id, enabled) noexcept → bool | 只访问已存在稳定 FilterState；先检查 id，越界 false 且不写，合法经桥接 relaxed store 返回 true；无分配、无 mutex、无配置 vector 写入，才满足当前 noexcept 声明 |
| close_registration() | 注册锁内把 open 设 false；重复关闭可保持 false；不等待 Handle、不排空、不销毁节点；锁异常可传播；只影响此后的 bind |
| published_head_for_reader()（内部） | acquire load 一次返回本轮头节点；reader 沿只读 next 行走，不读 owners；不作为 public 可任意销毁节点的接口 |

category 标量 setter 不需要复制一份运行态值回普通 vector；初始配置副本不是第二个实时状态源。
如果后续管理功能要求这个 setter 也获取 mutex，须先协调 noexcept 声明与异常合同，不能在现有签名内直接加可能抛异常的锁。
Logger 销毁与 setter/bind 并发仍属调用方违约，atomic 过滤不保护 Impl 生命周期。

绑定回滚重点：本地 unique_ptr 先持有，owners 插入可能失败，此时 head 不得改变；插入成功后才发布。
reader acquire 看见节点意味着其初始化已完成，不意味着 acquire 可以防止回收；保留到 reader 退出是另一项必要条件。
每个函数完成后对应 H 的生命周期/故障测试；不因 cpp 有定义就宣称安全发布完成。

<a id="i2-appender-runtime"></a>

### D15. Appender 抽象基类：配置、运行职责与所有权（2026-09-15 用户确认）

本节明确采用带虚析构和受保护纯虚扩展点的 Appender 抽象基类；这里的“虚基类”指运行时多态的基类，
不是 C++ 菱形继承中的 virtual inheritance。ConsoleAppender、TextFileAppender 公开继承 Appender，
业务配置则仍使用独立的 AppenderConfig 值类型，不能让 Config 继承运行对象。
此决定不再作为候选；下面的局部函数名和返回类型名称是实施建议，不冻结 ABI。

#### D15.1 BQLog 的参考位置与 QLog 的落点

参考当前 BQLog 提交 `60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`：

- `src/bq_log/log/appender/appender_base.h`：虚析构，非虚 log/init/reset 入口，以及 protected 纯虚 init_impl/reset_impl/log_impl。
- `src/bq_log/log/appender/appender_base.cpp` 的 log：先判 category/levels/enable，再调用 log_impl；set_basic_configs 处理公共配置。
- `src/bq_log/log/log_imp.h` 的 appenders_list_：Logger 用 unique_ptr<appender_base> 持有目标。
- `src/bq_log/log/appender/appender_file_text.h`：具体 Text 类通过 override 提供输出行为。

QLog 采用“公共入口统一规则、派生类实现输出”的结构，不照搬 BQLog 的通用 property 配置解析、
recovery hooks 或 bool 错误模型。运行接口属于内部 Backend 层；本轮不新增任意用户插件 Appender API。
虚调用发生在 Backend 对目标的分发，不进入 ProducerHandle 的逐条写入链。

建议配置类型放 `include/qlog/appender_config.hpp` 并由 async_logger.hpp 引入；
内部抽象类放 `include/qlog/detail/appender.hpp`，定义放 `src/appender.cpp`；
Console/TextFile 派生类分别在 I3/I4 增加对应内部头与 cpp。本轮只写文档，不创建空实现文件。
配置头不 include 运行基类，Producer/Channel 不持有 Appender，也不调用其虚函数。

#### D15.2 基类与派生类分别持有什么

| 对象 | 持有的数据与运行职责 |
|---|---|
| AppenderConfig | 名称、种类、enabled、FilterConfig 与专用配置值；无运行资源、无线程、无 Appender 指针 |
| Appender 基类 | 稳定目标身份、当前生效的公共配置、受控更新入口和可选诊断；统一过滤与分发入口，不能直接实例化 |
| ConsoleAppender | 生成完整文本行并复制进自有输出缓冲；Frame 释放后写控制台；不关闭进程标准流，不保存 Record 借用，不能仅计数就返回输出成功 |
| TextFileAppender | 持有已应用的 Text 配置、独立时区处理状态、独立 batch，以及文件/Sink 资源的唯一所有权；生成完整行并复制进自己的 batch |
| Logger::Impl | 拥有目标集合 vector<unique_ptr<Appender>>、配置管理锁和目标累计诊断；控制增删、替换与最终销毁 |
| Backend | 在管理锁保护的有界服务期间借用 Appender；控制 Frame 获取、共享 decode、全部目标结束后的单次 release 和后续 I/O |

Appender 不拥有 Logger；若需父级元数据，只借用 const 引用，且 Logger 的相关数据必须存活更久。
基类不放“所有目标共用的 Text batch”，也不要求 Console 拥有文件路径字段；Sink 是 Text 运行对象使用的具体输出机制。
Appender 及派生运行对象禁复制/移动，通过 unique_ptr 移动所有权槽位而不搬对象，避免资源重复释放和借用失效。

I2 暂时由 Impl 保存配置模型；I3 接入运行对象后，应以各 Appender 当前生效的公共配置作为过滤状态来源，
列表合并也在管理锁内读取这份配置。调用者提交的 Config 与待验证候选只是输入，不能再维护一份独立可写的
“Logger 当前配置副本”却只更新其中一边。派生类自己的已应用专用配置也由同一次受控更新协调。
Producer 的 FilterState 是刻意保留的原子派生投影，仍允许独立标量混合态，不是完整配置的第二权威副本。

#### D15.3 每个函数的职责、实现步骤与扩展点

下表为自然语言接口契约，不提供函数实现代码。涉及文件初始化/reset/flush/close 的精确结果类型和恢复策略
仍须 I4 前讨论；不以一个笼统 bool 预先承诺文件事务。交付失败与过滤拒绝必须可区分。

| 函数/操作（建议形状） | 虚性与逐步实施要求 |
|---|---|
| protected Appender(validated_common_config) | 非虚构造；复制已验证的身份/公共配置，初始化诊断；可能分配，不强行 noexcept；不在基类构造中调用纯虚函数 |
| virtual ~Appender() noexcept | 必须有定义，允许 unique_ptr<Appender> 经基类安全析构派生对象；只负责资源清理，不承担可报告失败的最终 flush，不调用纯虚 close hook |
| name()/type()/enabled()/filter() const | 非虚只读访问；返回有效配置，调用方持有管理锁或已停止 Backend；返回借用不得越过锁/对象寿命；不重复查询虚函数获取普通字段 |
| matches(metadata) const → bool | 非虚公共规则；先保证 category/level 合法，再按 enabled/filter 判定；读取同一管理锁保护的配置，不在每个派生类重写一遍过滤 |
| append(record_view, context) → 投递结果 | 非虚入口；先公共过滤，拒绝返回 filtered；符合条件才调用 append_impl；区分 filtered、accepted、failed，供 no_destination/selected_deliveries 统计使用 |
| protected append_impl(record_view, context) | 纯虚扩展点，对应 BQLog log_impl；Console/Text 都完成格式化并复制完整行入各自缓冲；不保留 Ring/scratch 指针、不 release Frame、不做慢 I/O |
| prepare_for_record(max_line_bytes) → 准备结果 | 非虚包装并调用派生 prepare_impl；Backend 在借用 Frame 前为每个目标保证一条最大完整行的空间，必要时先 flush；Console 同样必须准备自有缓冲；准备失败后的具体文件处理策略留 I4 |
| protected prepare_impl(max_line_bytes) | 纯虚资源准备扩展点；Console/Text 都检查缓冲容量/可用空间；不将静默丢记录作为腾空间方法 |
| flush_pending() / protected flush_impl() | 非虚包装加纯虚输出扩展点；仅在所有相关 Frame 已释放后处理自有 batch；Console/Text 都报告真实输出状态，不能将 batch accepted 当 durable |
| apply_config(validated_candidate) / protected reset_impl(...) | 非虚公共入口加纯虚类型专用扩展点；管理锁内、无 Frame 借用时执行；先完整验证名称匹配、类型兼容及配置，协调公共/专用状态，再重算并发布合并 levels；不照搬先清空旧配置的流程来声称失败可回滚 |
| close() / protected close_impl() | 显式非虚退出入口加纯虚资源扩展点；无借用/不再分发后处理剩余 batch 和资源退出，诊断归并后才销毁；Console 完成缓冲输出但不关闭标准流；Text 文件失败合同待 I4；不以析构吞错代替它 |
| make_appender(config, dependencies) | 冷路径工厂，不是虚成员；按配置类型选择派生类，先由局部 unique_ptr 持有并完成必要初始化；只有可用实例才加入列表；基类构造不调用虚 init，必要的初始化由工厂在完整对象建成后发起 |

这里的 context 只借 Backend scratch/格式计划等本次服务数据，不传递 Ring 的回收所有权。
处理同一 Record 前先查 Logger category，再进入各目标 append；一个目标失败仍继续其他目标。
Console/Text 完整行必须在 append_impl 返回前完成复制；Backend 最后统一 release 一次，再执行 flush_pending。
纯虚扩展点确保 Appender 抽象；普通配置 getter/setter 无需全部虚化。不要仅为“对齐 BQLog”再加
QLog 当前没有需求的恢复/压缩扩展点，也不必现在增加只有一个 Text 派生类需要的文件中间基类。

#### D15.4 创建、热更新、移除和关停顺序

创建：校验并拥有 Config → 工厂完成派生对象构造/初始化 → Logger 接收 unique_ptr → 管理边界安装列表 →
从有效配置 OR 所有 filter.levels（包括 disabled）并发布 Producer 位图。I2 只做前端配置模型，不声称已创建这些运行对象。

更新：同名兼容类型可受控 apply_config；类型改变则创建替代对象，旧目标先完成缓存/退出处理再销毁；
目标缺失表示移除，新名字表示新增。变更期间不能存在借用的 Frame 或正在使用旧目标的 Backend 引用。
输入非法在修改前拒绝；文件资源失败后的保留/重试/部分生效结果按 I4 待议合同处理，不预设全资源事务。

关停：先停止/join Producer 和管理活动，关闭注册，再由 Backend 排空并释放全部 Frame、处理目标剩余 batch/退出，
Backend join 后销毁目标及 Channel，最后销毁二者借用的 Logger 元数据。批处理与退出失败不能靠虚析构报告成功。
I2 没有这些完整后台行为，不新增空 shutdown/空 TextFileAppender 来标记完成。

验收分期：I2 验证配置嵌套/复制/非法输入/位图 OR；I3 验证 Appender 为抽象类、虚析构经基类指针销毁派生对象、
两份 Console 分发且 processed=1/selected_deliveries=2、过滤与失败分类、增删及无悬空借用；I4 再验收 Text 独立配置/batch、
所有目标使用完后单次 Frame release、释放后 I/O，以及明确冻结后的文件失败策略。生产虚派发的实际成本留待 I3/I4 测量。

<a id="i2-producer"></a>

## E. Producer：唯一的直写闭环

### E1. 模板位置与签名

文件：`include/qlog/producer_handle.hpp`，模板定义与声明同头可见。
直接 include 所用的 cstddef/cstdint/utility、public 类型、channel、I1 measure/encoder。
requires 与现有 measure 保持一致：参数不超过 kMaxArgCount，且每个满足 SupportedArgument。

```cpp
template <typename... Args>
    requires(sizeof...(Args) <= detail::kMaxArgCount) &&
            (detail::SupportedArgument<Args> && ...)
[[nodiscard]] LogResult try_log(std::uint32_t category_id, LogLevel level,
                                 FormatView format, Args&&... args) const noexcept;
```

FormatView 指 A4 的受控只读适配结果；其具体名字可以在 public 头统一，不能再暴露可写 precomputed hash。
按当前 SupportedArgument 定义转发，不增加用户 formatter/隐式转换。
超过 32 参数或不支持类型编译失败，不产生 runtime calls 计数。

### E2. 固定执行顺序

```text
空 Handle 检查
Debug calls++
level 合法性检查
category 范围检查
一次过滤判定
构造 detail FormatInput（不扫描 format）
measure_record(format, Channel quota, forward(args)...) 一次
Debug attempted++
ring.try_reserve(prepared.payload_size()) 一次
sample_admission_timestamp
构造四字段 RecordMetadata
encode_v1(handle.data(), handle.size(), prepared, metadata, policy, dispatch)
encode 成功 → ring.commit → Debug accepted++ → accepted
encode 失败 → ring.abort → Debug failed_internal++ → internal_error
```

空 Handle 无 Channel 诊断位置，由外部测试统计 invalid_handle；不要为它引入共享全局计数。
一个有效 Handle 调用的每条 return 都应落入一类互斥结果。
不先取当前时间再检查 full；不先 reserve 最大空间再调整 commit 长度。

### E3. measure 结果的正确存活期

真实 API 示意：

```cpp
auto measured = detail::measure_record(input, quota, std::forward<Args>(args)...);
if (!measured.succeeded()) {
    // 显式映射 *measured.failure()，没有 reserve，没有读时钟。
}
const auto& prepared = *measured.prepared();
```

PreparedRecord 指针借用 measured 内部对象。
不要把它保存到 Channel 留待下一次调用，也不要从临时 MeasureResult 取得指针后让临时析构。
保持 measured 活到 encode/commit 完成。
标量已由 measure 快照，字符串借用地址与长度复用到 encode 完成。

### E4. MeasureError 完整映射

| I1 错误 | public 状态 | 理由 |
|---|---|---|
| invalid_limits | internal_error | quota 来自已验证且不可变的 Channel |
| invalid_format_metadata | invalid_input | 非零长度空 format 地址 |
| invalid_string_metadata | invalid_input | 非法字符串 metadata/数组合同 |
| format_too_large | too_large | 超过 format 8192B 硬上限 |
| argument_length_out_of_range | too_large | 单字符串不能编码为 u32 |
| args_length_out_of_range | too_large | 参数区 u32 表示失败 |
| size_overflow | too_large | 累加尺寸不可表示 |
| record_length_out_of_range | too_large | Record 长度表示失败 |
| payload_too_large | too_large | 超过 Channel quota |

internal_error 的 stage=measure 可区分系统配置不变量错误；计入 pre-admission failure 的子类，
因为它发生在 attempted++ 之前。不要为了 status 相同而把所有 internal_error 都计入 attempted。
非法非空 cstr 地址/缺少可达 NUL 是调用方违约，不承诺可恢复，不为测试制造随机坏指针解引用。

### E5. reserve 返回分类

真实 WriteHandle 使用 `operator bool` / `status()`，不是 `succeeded()`。
`data()` 非 const 成员，因此用 `auto write = ...`，不要声明 const auto write 后取 data。

| ReserveStatus | 行为 | 取时 | abort |
|---|---|---|---|
| ok | 进入取时与 encode | 是 | 仅后续 encode 失败 |
| full | 返回 full | 否 | 否 |
| payload_too_large | 返回 internal_error/reserve | 否 | 否 |
| reservation_pending | 返回 internal_error/reserve | 否 | 否 |

prepared 已按同 quota 验证，reserve 的 payload_too_large 表示接线不变量错误。
reservation_pending 可能来自内部重入/错误；当前调用没有成功 Handle，不能 abort 另一次操作的 reservation。
失败不能清零 Ring pending 状态，也不伪装成 full。

### E6. 取时、metadata 与 encode 的真实调用

```cpp
const auto timestamp = sample_admission_timestamp(clock_descriptor);
const detail::RecordMetadata metadata{
    timestamp.time_value, category_id, static_cast<std::uint8_t>(level), timestamp.flags};
const auto encoded = detail::encode_v1(
    write.data(), write.size(), prepared, metadata, policy, hash_dispatch);
```

metadata 字段顺序对应当前 record_types.hpp：time_value、category_id、level、flags。
生产实现可用成员赋值避免顺序误读，但仍完整初始化。
第二参数必须是 write.size()，它应恰好等于 prepared.payload_size()；不要传 Ring capacity。
不重新测字符串、不临时分配完整 Record staging buffer、不预计算 runtime format hash。
reserve 后成功路径只写入目标 payload；encoder 自己 Header-last。

### E7. commit/abort 的单次终结

encode 失败仍在 I1 preflight 区域，目标保持不变；I2 负责 abort 当前成功 reservation。
encode 成功返回精确字节数，可 Debug assert 与 write.size() 一致；不二次复制 Header。
commit 才 release-store 发布 Frame，Header-last 不是线程发布机制。
正常操作顺序可显式完成，不必为两个语句引入会复制 ownership 的通用 RAII Handle。
如果引入本地 guard，仅能服务此调用，不能改变底层 16B 被动 Handle ABI。

底层内部 terminate 不会返回给 I2，不能写 catch(...) 假装全部 fault 都可 abort 恢复。
用户非法地址和进程终止不在可恢复统计守恒的保证内。

### E8. “失败不污染 Ring”到底保证什么

必须区分三个层次：

1. filter/measure/reserve 失败：没有发布新 Frame；reserve 失败不写当前新 Frame。
2. 成功 reserve 后 abort：发布游标与可消费序列不前进，空间可重新申请。
3. 不承诺整个 Ring storage byte-for-byte 不变：成功 reserve 已写外层 FrameHeader。

测试应检查发布可见性、游标和下次 reserve 可用性；不能把 I1 encoder 的目标不变合同扩大到整个 Ring。
使用 write.size() 而不是 frame_bytes 交给 decoder；外层 padding/tail waste 不是 Record 字节。

### E9. 完整例子：两个整数

format="a={} b={}"，9 字节；两个 int32 参数各 5 字节（tag+4B）。
payload = 32+9+10 = 51B，正常 Frame = 8 + align_up_8(51) = 64B。
每条调用依次：过滤 → measure 51 → reserve 51 → timestamp → encode 51 → commit。
默认 quota=8192，能够准入；是否 full 取决于 Ring 当前剩余 Frame 空间。
多出来的 5B 外层对齐字节不由 encoder 写、不由 decoder 解释。
literal/runtime format 的 Record 布局和后端行为相同，区别仅在 format hash 来源。

### E10. 第一条集成成功并不等于 I2 完成

还需覆盖双 Logger、重复绑定、并发新 Channel 发布、过滤更新、满队列、异常时钟、
所有错误映射、Release 诊断消除和引用生命周期。
实际 Backend 未接入时由 test consumer 解码和释放，不实现假 ConsoleAppender 生产占位。

### E11. ProducerHandle 每个操作及 try_log 的实施分段

当前文件已更名为 ProducerHandle，但仍只有未初始化指针；不再重复更名，按 N4 补全构造和权限。
头部直接依赖 public 结果/格式/等级及参数约束；模板函数体访问 Channel 成员前必须看见其完整定义。
Channel 头不要反向 include Logger/Handle；仅存 Channel* 的类声明可以使用前置声明。

| 操作/函数 | 怎样完成，为什么 |
|---|---|
| ProducerHandle() noexcept | 将 channel_ 初始化 nullptr；默认对象调用可返回 invalid_handle，不能保留当前未初始化指针 |
| private ProducerHandle(Channel*) noexcept | 只保存已发布节点地址，由 AsyncLogger 受控调用；不分配，保证发布后包装不失败 |
| 拷贝构造/赋值 | 复制指针，不建 Ring、不加引用计数；赋值不会注销旧 Channel |
| 析构 | 无注册/回收/提交/等待副作用；Channel 寿命由 Logger 决定 |
| 移动（若声明） | 轻量复制式语义即可；不要承诺源必为空，不增加 owner 转移协议 |
| try_log(category_id, level, FormatView, Args&&...) const noexcept | 下述唯一状态机；const 只约束 Handle 本身，不能据此跨线程写同一 SPSC |

try_log 声明/定义使用一致的 requires：参数数目不超过 kMaxArgCount，且每个 Args 满足 SupportedArgument。
在头中实现模板，普通 cpp 无法替未知参数组合生成实例。可按下面七段写出真实函数，但不要先返回假 accepted 占位：

1. **基础校验**：先判空指针；有效 Channel 后记诊断 calls，再验证 level 与 category。
   非法立即构造对应 validation 失败，不能在判定前移位/访问数组。
2. **过滤**：调用一次 allows_unchecked，拒绝就 filtered；此时不读取 format 字节，不 measure/strlen/clock。
3. **测量**：适配 FormatInput，调用一次 measure_record，传该 Channel quota 和 args。
   失败按 E4 映射；成功保留 measured 局部对象，并借它的 PreparedRecord，不能从临时结果取悬空引用。
4. **预留**：此处才记 attempted；调用一次 try_reserve(prepared.payload_size())。
   分类 full 与其他失败；没有得到成功 reservation 就不能 abort。WriteHandle 局部对象非 const，以调用 data()。
5. **取时与元数据**：成功 reserve 后 sample；用 time_value/category/level/flags 填 RecordMetadata。
   无可用时间不退回失败，继续生成 unavailable Record，不更新 clock descriptor。
6. **编码和失败终结**：以 write.data()、write.size()、prepared、metadata、policy、dispatch 六参数 encode_v1。
   失败先 abort(write) 一次，再返回 internal_error/encode；不能 abort 无关 pending。
7. **成功终结**：encode 成功才 commit(write) 一次，然后计 accepted、返回 make_accepted。
   不重新测量、不 staging、不复制第二遍完整 Record，函数返回前源参数仍须有效。

当前 I1 `EncodeResult::failure()` 指向 EncodeError 枚举，不是带 stage 的 EncodeFailure 结构；
只有确认失败后才取其值。PreparedRecord::payload_size() 是申请/编码 payload 长度，
max_payload_size() 是额度；不要混用 Frame padding/capacity 作为编码目标长度。

### E12. 三个错误映射 helper 与复用的 I1/Ring 函数

映射 helper 建议位于 producer_handle.hpp 的 detail 区（头内 inline），不创建第二套 codec。

- `map_measure_failure(const MeasureFailure&) → LogResult`：穷尽 E4 九项；invalid_limits 为 internal_error，
  两项元数据错误为 invalid_input，其余长度/溢出/配额错误为 too_large；stage=measure，精确复制 index/byte_count。
  当前 FailureReason 的 agument 拼写需统一为 argument；映射不要依赖 enum 编号。
- `map_reserve_failure(ReserveStatus, requested_bytes) → LogResult`：full 为 full；payload_too_large 和
  reservation_pending 为 internal_error；stage=reserve，index=0xFF。ok 不应进入失败 helper，主路径已分开处理。
- `map_encode_failure(EncodeError, target_bytes) → LogResult`：全部七项映射 internal_error/encode，并保留具体原因：
  invalid_destination_metadata、destination_size_mismatch、invalid_level、unknown_flags、reserved_timestamp_status、
  invalid_time_value、fallback_timestamp_not_configured。这些是接线不变量错误；abort 由持有 write 的主函数负责，
  映射 helper 不碰 Ring，避免两处终结。

本轮不重写下列已实现函数，但接线时必须知道它们完成什么：

| 已有函数 | 在 I2 中如何使用 |
|---|---|
| measure_record | 验证格式/参数元数据、归一化并计算完整 payload；不写 Ring；结果活到 encode 完成，避免重复 cstr 扫描 |
| try_reserve(size) | 申请精确 payload，成功已写外层 FrameHeader 但未发布；失败分类见 E5 |
| WriteHandle::operator bool/status/data/size | 判断成功/取得失败类别/可写 payload 地址/精确长度；析构没有自动 abort |
| encode_v1 | 复用 PreparedRecord 直接编码，先 preflight 后写，Header-last；失败不改目标 payload，不等于整个 Ring 从未写入 |
| commit(write) | 发布当前成功 reservation，建立消费者可见性；成功后才返回 accepted |
| abort(write) | 取消本次成功 reservation，不发布不推进提交游标；仅 encode 可恢复失败分支使用 |
| try_read / decode_v1 / release(read) | I2 测试消费者取得 payload、按固定 workspace 解码、用完后释放；不表示已有生产 Backend |
| publish_reclaimed | 测试消费者需要立即返还可复用空间时发布回收进度；不把延迟回收误判为 Ring 泄漏 |

验收对每个 return 检查“当前调用是否持有 reservation、是否已终结、计入哪一类”，再测 deep copy/FIFO/full。
普通 Release 没有诊断计数，也仍须完整返回这些错误。

<a id="i2-diagnostics"></a>

## F. 诊断统计：Debug 有，常规 Release 没有

### F1. 条件编译的范围

新编译配置建议名称 `QLOG_ENABLE_PRODUCER_DIAGNOSTICS`，CMake 取 AUTO/ON/OFF。
AUTO：Debug=1，Release/RelWithDebInfo/MinSizeRel=0；ON 用于独立诊断构建。
与 QLOG_ENABLE_RING_VALIDATION 分开，绝不共用开关关闭 decoder 检查。

Producer 模板与 Channel 类型出现在使用者 TU 中，因此宏必须通过 target PUBLIC 一致传播。
不能只在 qlog library PRIVATE 定义，再让应用看到另一种 Channel 布局或内联函数体。
宏缺失时建议产生清晰编译错误并要求通过 QLog::qlog 或安装导出 target 使用；
不能静默按本 TU 的 NDEBUG 猜值导致 Debug consumer + Release library 的 ODR 问题。

### F2. Debug 字段及所有权

建议每 Channel 独立 ProducerDiagnostics，按 cache line 对齐；
结果计数可按 BQLog 使用 relaxed atomic uint64_t 更新，避免 Debug 观察扩展误读非原子字段。
对外不提供运行中统计快照；测试/诊断读取在 Producer 停止后。
每个 atomic 值安全不等于整组同时刻快照，因此 stop/join 仍是精确守恒前提。
Release 用预处理移除整个成员和更新块，不只提供会被间接调用的空函数。

建议诊断类别：calls、filtered、rejected_pre_admission 及原因、attempted、accepted、dropped_full、failed_after_attempt。
invalid_handle 没有 Channel，不进入某个 Channel 的 calls；外部验收驱动另计。
编译期拒绝没有运行调用，不计 unsupported 伪运行次数。
跨线程共享计数更新成本只存在明确启用的诊断构建，不用于无诊断性能结论。

### F3. 守恒公式与错误阶段

对有效 Handle，所有调用终结并且计数未溢出后：

```text
calls = filtered + rejected_pre_admission + attempted
attempted = accepted + dropped_full + failed_after_attempt
```

rejected_pre_admission 包括 invalid_level/category/input、too_large 和 measure invalid_limits 内部错误。
failed_after_attempt 包括非 full reserve 失败与 encode_abort；每次只进入一个子类。
I3 完整排空后 accepted=processed；processed 包括已隔离并终结的 decode/format/sink 错误。
对于 uint64_t 极端溢出，明确有限测试区间或按模计数解释，不能声称永远无界整数精确。

### F4. Release 的验收如何观察

不能让 Release 公开统计 getter 永远返回 0 并把 0=0 当通过。
test producer 依据 LogResult 在自己的测试数组记结果，consumer 依据 Record 中测试 ID 记唯一处理结果。
停止后聚合并校验没有重复、遗漏、额外记录和乱序。
测试 ID 是普通测试参数，不增加生产 Header 字段。
正常 Release 和 Release+诊断构建分别运行，不拿后者的速度冒充前者。

### F5. 关停不依赖计数

I3 必须在已无 Producer/新注册后检查所有已发布队列是否排空，并完成回收游标发布。
不要循环等待 accepted==processed，因为常规 Release 没有这些字段。
计数是验收/诊断，不是协议同步变量。

### F6. ProducerDiagnostics 字段与更新函数实施说明

文件 `detail/producer_diagnostics.hpp` 当前为空。建议全部字段为初始化到 0 的 atomic<uint64_t>，
仅在 QLOG_ENABLE_PRODUCER_DIAGNOSTICS=1 时定义/内嵌；Channel 内存隔离按 F2。

| 字段 | 精确加一位置 |
|---|---|
| calls | 空 Handle 检查通过后，每次有效 Handle 调用一次 |
| filtered | 唯一过滤拒绝分支 |
| rejected_pre_admission | 非法 level/category、measure 失败，包括 measure internal_error |
| attempted | measure 成功后、调用 reserve 前 |
| accepted | commit 返回后 |
| dropped_full | reserve 确认为 full |
| failed_after_attempt | 非 full reserve 失败或 encode 失败 abort 后；每次最多一次 |

若拆 `note_call`、`note_filtered`、`note_rejected`、`note_attempt`、`note_accepted`、`note_full`、
`note_failed_after_attempt`，每个 helper 只做对应 relaxed fetch_add；调用块与字段由同一宏移除。
不要另在错误映射 helper 里加计数，否则主路径和映射会重复计数。
原因子类计数是互斥细分；若添加 accepted_bytes，明确累计完整 Record payload，仅在 commit 后加，
不把 Frame padding、尝试字节和成功字节混合。

内部测试 `read_after_stop`（如需）在 Producer join 后逐项读取到普通快照；不得作为 public live snapshot。
默认 Release 用外部结果/Record ID 验收，不能提供返回假零值的诊断 getter。
诊断对象的构造/析构不注册、不发布、不承担关闭同步；业务生命周期不能依赖这些可移除字段。

<a id="i2-build"></a>

## G. 生产构建接线与一次交接

### G1. 先添加真实 .cpp

在已有根 `add_library(qlog STATIC ...)` 之后增加：

```cmake
target_sources(qlog PRIVATE
    src/filter_state.cpp
    src/admission_clock.cpp
    src/channel.cpp
    src/async_logger.cpp
)
find_package(Threads REQUIRED)
target_link_libraries(qlog PUBLIC Threads::Threads)
```

不要创建 producer_handle.cpp 来隐藏 parameter-pack 定义。
不要将 src/*.cpp 直接 include 到头中，也不把生产 .cpp 只接入某个测试 executable。
保持 C++20、CXX_EXTENSIONS OFF、严格 warnings；-msse4.2 仍只属于硬件 hash TU。

### G2. 配置宏接线骨架

```cmake
set(QLOG_ENABLE_PRODUCER_DIAGNOSTICS "AUTO" CACHE STRING
    "Producer diagnostics: AUTO, ON or OFF")
set_property(CACHE QLOG_ENABLE_PRODUCER_DIAGNOSTICS PROPERTY STRINGS AUTO ON OFF)
if(QLOG_ENABLE_PRODUCER_DIAGNOSTICS STREQUAL "AUTO")
    set(QLOG_PRODUCER_DIAGNOSTICS_VALUE "$<IF:$<CONFIG:Debug>,1,0>")
elseif(QLOG_ENABLE_PRODUCER_DIAGNOSTICS STREQUAL "ON")
    set(QLOG_PRODUCER_DIAGNOSTICS_VALUE 1)
elseif(QLOG_ENABLE_PRODUCER_DIAGNOSTICS STREQUAL "OFF")
    set(QLOG_PRODUCER_DIAGNOSTICS_VALUE 0)
else()
    message(FATAL_ERROR "QLOG_ENABLE_PRODUCER_DIAGNOSTICS must be AUTO, ON or OFF")
endif()
target_compile_definitions(qlog PUBLIC
    QLOG_ENABLE_PRODUCER_DIAGNOSTICS=${QLOG_PRODUCER_DIAGNOSTICS_VALUE})
```

不要沿用 Ring 宏的 PRIVATE 可见性；新增宏会影响 public 模板所见内部类型。
配置不同的库/头不能混链接，CI 需至少测试 consumer 配置传播。
安装导出以后也必须保留同一编译定义；当前不顺手新增打包工程。

### G3. 交接清单

| 组 | 维护者提供 | Codex 接手检查 |
|---|---|---|
| A/B | public 值类型、受控 format、过滤存储/更新 | self-contained、类型准入、原子语义、更新失败不变 |
| C | source probe、timespec 转换、采样 | 四 descriptor、三 status、溢出、零值、调用次数 |
| D | 稳定 Channel、绑定与发布、owner 销毁顺序 | 双 Logger、token reuse、异常回滚、并发注册与 reader |
| E | 真实模板调用链、错误映射 | 单次 reserve、时钟位置、deep copy、full/abort/FIFO |
| F/G | 诊断块与宏、生产 CMake | Debug/Release ODR 一致、Release 字段/更新消除 |

维护者无需手写测试或整理 benchmark；交接说明已完成哪些生产文件和未解决诊断即可。
不要把还未确认的问题藏在 TODO 后提交一个可运行但语义不同的版本。

<a id="i2-validation"></a>

## H. Codex 验收：验证设计合同，不仅验证能打印

### H1. 验证顺序

1. 自包含头与生产链接；确认没有依赖测试 TU 才能解析的生产符号。
2. A/B/C 的独立值类型、过滤、时钟测试。
3. Channel 生命周期、异常回滚、注册发布与身份测试。
4. Producer → Ring → 已有 decoder 的集成。
5. GCC/Clang Debug/Release、ASan/UBSan、软件 hash 回退和诊断 ON/OFF。
6. 正确性通过后再做 Release 代码生成与性能基线。

无需为本次纯文档交付运行 I1 一小时 fuzz；未来生产改变有影响时按回归范围执行。
I1 已完成验收，不重新把旧 decoder TODO 当成 I2 前置缺口。

### H2. 测试文件规划

由 Codex 后续创建，不要求维护者先实现：

```text
tests/producer_header_self_contained.cpp
tests/producer_types_test.cpp
tests/producer_filter_test.cpp
tests/admission_clock_test.cpp
tests/channel_lifecycle_test.cpp
tests/channel_registration_test.cpp
tests/producer_integration_test.cpp
tests/producer_allocation_test.cpp
tests/producer_diagnostics_test.cpp
benchmarks/producer_benchmark.cpp
```

GoogleTest 已由 tests/CMakeLists.txt 的 FetchContent 管理，复用已有来源与发现方式。
新 label 建议 producer/channel/filter/clock；实际注册后检查 ctest -N，0 tests 不是通过。
不把 I1 的 record_core 标签全部改为 producer，保持故障定位边界。

### H3. 按行为列完整验收矩阵

| 领域 | 必须覆盖 |
|---|---|
| level/policy | 0～5 全合法；6/255 非法；bitmap 0/0x3f/unknown bits；过滤变化不改 policy |
| category | 默认 0；首尾合法；越界；空/非空 reset 输入；名称源销毁后仍有效 |
| filter | 拒绝时不 strlen/measure/reserve/clock；独立更新允许混合态；同步后的新值可见 |
| format | literal/runtime 一致；空；内嵌 NUL；char8_t；非法非零长度空地址 |
| 参数 | 0/1/32；33 compile-fail；全部 I1 tag；cstr 恰好一次扫描 |
| quota | exact、超 1B、format limit 与 payload quota 分开；非法 Logger config |
| reserve | ok/full/pending/配额不变量；成功后 encode 失败只 abort 自己的 Handle |
| clock | 四 descriptor；primary/fallback/unavailable；非法 timespec；u64 边界；失败继续记录 |
| 绑定 | 同线程同 Logger 复用；同线程双 Logger 隔离；并发注册；新线程不复用旧 token |
| 发布 | reader 同时扫描；发布前初始化完整；分配失败不发布；旧链 next 不变 |
| 生命周期 | Handle 副本同线程；source 修改/销毁后 Record 深拷贝；join 后关闭注册与排空 |
| 诊断 | Debug 守恒；Release 外部逐 ID 守恒；宏传播；不依赖假零统计 |

不要用故意跨线程写同一 SPSC 的未定义行为作为 Release 返回值测试。
非法地址不应通过“测一下是否崩溃”建模成正常可恢复输入。

### H4. 故障注入边界

- 时钟：测试 Ops 控制 probe/gettime，覆盖返回 0 但值非法。
- 分配：绑定初始化/owner vector 扩容失败，验证未发布且已有 Channel 不受影响。
- encode abort：用内部测试接缝或受控失败依赖触发，不能修改生产 policy 伪造真实公共输入。
- full：小合法 Ring 写满，用正常 API 验证，不越权修改游标。
- Release 消除：与 Debug/诊断构建对比 symbols/反汇编，不仅源码 grep。

测试 hook 不作为 public runtime 回调；生产热路径不为测试增加虚调用/锁。
单测替身证据与真实 Ring/codec 集成证据分开记录。

### H5. 构建命令模板

以下在生产源与测试 target 完成后由 Codex 执行；当前未创建相应 target/标签。
仓库没有 CMakePresets.json，不提供虚构 preset。

```bash
cd /home/qq344/QLog
cmake -S . -B build/test/i2-gcc-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DCMAKE_CXX_COMPILER=g++ -DCMAKE_CXX_FLAGS=-Werror
cmake --build build/test/i2-gcc-debug
ctest --test-dir build/test/i2-gcc-debug --output-on-failure --no-tests=error

cmake -S . -B build/test/i2-gcc-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_CXX_COMPILER=g++ -DCMAKE_CXX_FLAGS=-Werror
cmake --build build/test/i2-gcc-release
ctest --test-dir build/test/i2-gcc-release --output-on-failure --no-tests=error
```

Clang Debug/Release 使用独立 build dir 与 clang++-18。
ASan/UBSan 独立构建加 compile/link sanitizer flags、no-recover、frame pointer，检查真实运行日志。
软件回退加现有 QLOG_ENABLE_X86_CRC32C=OFF；不要让新文件获得全局 ISA flags。
诊断 ON 的 Release 使用独立 dir 并标注“diagnostic”，不与默认 Release 混用。
本次文档没有创建 run_i2_matrix.py；未来需要时由 Codex 根据实际 target 编写。

### H6. 并发验证与 WSL 边界

WSL2 可以跑功能并发压力和 ASan/UBSan，但不把它们叫作数据竞争检测。
TSan 在可用原生 Linux 环境作为并发发布/过滤的发布前门禁；没有 runner 就明确未执行。
多 Channel 公平调度、Backend 排空、Sink failure 属于 I3/I4，I2 不以 test consumer 替代它们的生产验收。
I2 要证明 Channel 安全发布、独立 SPSC 正确使用和 Producer 不与配置形成非原子数据竞争。

### H7. Release 汇编要检查什么

选 literal 两整数、runtime 混合参数、filtered、full 四类 probe。

- literal 无运行期 reference hash，runtime 一遍 copy/hash。
- 不逐条调用 TLS 注册、gettid、registry mutex、map 查找、CPUID、clock_getres。
- 默认 Release 无诊断字段更新、shared refcount、队列选择虚派发。
- 过滤 atomic load 不应变成锁或共享 RMW；不能把它与 Ring release/acquire load/store 混为一谈。
- 必须保留 Ring 发布/回收同步、encoder preflight、正确错误返回。
- 固定宽度 payload 写入正常内联，不据调用点外观提前添加 always_inline。

有可变长度 memcpy 或外部 sample_admission_timestamp 调用不自动判失败；先测组合成本。
诊断开关不得让不同 TU 实例化出冲突的函数体；记录 compile_commands 与链接对象来源。

### H8. 性能基线和对照

优先测 QLog 同一 Producer 核心的成本，不先比较不同功能的 README 数字。

| 维度 | 场景 |
|---|---|
| format/参数 | 空、literal 两整数、runtime 两整数、混合字符串、32 参数 |
| 调用形态 | 单 Logger 连续、双 Logger 交替；首用绑定另测 |
| 过滤 | 全拒绝、全接受、运行中低频更新；稳定过滤值单列 |
| 容量 | 充足、接近满、持续 full；精确 quota/拒绝路径 |
| Producer | 1/2/4/8 线程，各自 SPSC，保持相同总消息工作量 |
| 诊断 | 默认 Release 与 Release+诊断分别报告 |

计时边界包含所声称的 Producer 工作；setup/分配在计时外。
外部 accepted/dropped 观测会增加成本，必须记录其位置；不能无声地关闭计数后仍报告不存在的精确数据。
吞吐批次 median/MAD 与单次 latency 分开；batch 均值的 P99 不等于单条日志 P99。
无绝对 ns/op 冻结指标；后续优化采用同机交替 before/after，至少超过 3% 且超过合并 MAD 才主张稳定收益。
统计关闭、原子更新和新 API 的吞吐尚未实测，本指南不提供虚构排名。

### H9. V2 MPSC 的正确承接

V1 的显式绑定不会妨碍多个线程的独立上下文指向共享 MPSC。
但 MPSC 的 reservation/publish/failure 回滚/FIFO 规则必须重新设计，不能复用 SPSC 非原子写游标。
V2 分别测预绑定访问成本、共享竞争、内存总量、低频线程注册与 Backend 扫描；不把这些混成“TLS 更快”。
只有 V2 基准支持时才引入共享/分片/自动升降级；V1 热路径不提前为它付费。

## I. 第一个有界切片与交付边界

按 2026-09-14 实际代码，第一步完成补充指南 §3/4：修正已有 FilterState 构造、完成时钟转换/探测/采样，
保留已经补齐的 log_level 自包含与 A1/A2，接入这两个生产 source；再完成 A7/A8 与 D11～D14。
当前源码进度以本指南 §0.6 为准，后续 Producer/诊断按 E11/E12/F6 推进。
这些模块不需要虚构 Backend，也不必先改变 Ring。
可以一次交接这个基础切片，Codex 验证后继续 D/E/F 完成同一 I2 工作包。

完成基础切片后，你应能解释：

1. 为什么过滤 bitmap 全零时 decoder policy 仍是 0x3f？
2. 为什么两个 relaxed 标量安全，却不能保证整组快照？
3. 为什么 primary=realtime 时 flags 是 0？
4. 为什么诊断宏必须 PUBLIC，而现有 Ring validation 可以 PRIVATE？

I2 最终关闭需要生产链路、构建矩阵、Release 消除、外部数量守恒、基线与文档同步的真实证据。
本指南交付只代表实现路线明确；不代表上述生产源、测试或性能已经完成。

## J. 本轮文档验证记录

文档交付核验结果由同目录 `I2_GUIDE_DOCUMENT_VALIDATION_20260913_CHS.md` 记录。
核验包括相对链接、代码入口、章节覆盖、独立骨架编译和变更范围。
接口片段中声明未实现的部分不作生产链接成功声明；编译骨架仅验证声明/调用契约。

## K. 多 Appender 后续实施与本次文档验证

路由、独立时区、Frame/batch 生命周期和分期见 [ADR-012](./ADR-012-v1-multi-appender.md)。
本次验证见 [多 Appender 文档验证](./I2_MULTI_APPENDER_DOCUMENT_VALIDATION_20260913_CHS.md)。
旧验证报告只证明旧版指南当时的检查，不覆盖本次修订。


## L. 2026-09-14 本次定义与逐函数讲解更新的核验边界

本次直接更新主指南：核对执行计划、ADR-011/012、I1-D 验收报告、I2 当前头文件/源文件与根 CMake。
修正旧进度：等级头已自包含、failure() 已判空、时钟文件已更名且 probe/read/sample 已写；
该次历史快照的待办现以 N1～N9 为准；配置和绑定结果已经开始填写，尚未完成。
新增 A7/A8、B9、C8、D11～D14、E11/E12、F6 以字段表、函数职责和自然语言实现步骤说明，不新增实现代码块。

本次文档检查覆盖：章节/显式锚点唯一、相对文档链接可解析、必需类型/函数讲解存在、Markdown 围栏配对，
以及生产源码/测试/benchmark/CMake 文件修改前后指纹一致。旧验证报告不代表本次或当前 I2 生产验收。
没有执行 I2 编译/运行测试、并发验证、TSan、代码生成或性能测试；这些仍按 H 交接执行。

## M. 2026-09-15 Appender 配置与多态职责修订

已按用户确认将完整配置统一为 AppenderConfig，过滤字段归入 FilterConfig 成员；D15 明确 Appender 抽象基类、
BQLog 来源、逐函数职责、派生类资源和 Logger 唯一所有权，并同步 ADR-012 与 I2 执行计划。
本次仅文档修改；检查相对链接、章节/围栏、旧类型名清理和生产文件指纹，不代表 I2/I3/I4 生产编译或行为验收。


### 2026-09-15 最新补充：Console 默认目标与实施分期

用户明确不需要 NullAppender；生产目标采用 ConsoleAppender / TextFileAppender。
本轮将“没有就走 console”解释为创建或整表 reset 的输入 Appender 列表为空时，规范化为一个默认 Console 配置；
显式配置非空时仅使用该列表。disabled、过滤拒绝、文件故障都不自动添加 Console，也不绕过 Logger category。
默认 Console 配置与具体修正顺序见 [主指南 N](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md#i2-current-next)。
纯 OR helper 的空序列结果仍为 0；Logger 应先规范化再合并，因此默认 Logger 不是零位图。
需要静默时可通过显式目标 filter.levels=0 或关闭 Logger category 表达，无需 Null 输出类型。

I2 完成 Console/TextFile 配置模型、默认化、Producer/Channel；不启动控制台输出。
I3 完成 Appender 抽象基类、真实 Console、Backend 和排空；Console 所需的基础完整行格式化及自有缓冲必须前移到 I3，
不能继续按原 Null 路径仅计数，也不能保留“全部文本格式化到 I4 才做”的旧依赖。
I4 接入 TextFile、文件生命周期/失败合同、多目标独立配置与格式缓存的后续优化；共用基础格式化不再重复实现。
Console 接收完整行入缓冲与实际终端写入成功分开记录；慢 I/O 均在释放 Frame 后，测试需捕获输出字节而非使用真实终端测速。
I2 test consumer 可以无输出地核对 Record，但它只是测试设施，不能注册成一个生产 NullAppender。


<a id="i2-current-next"></a>

## N. 从你现在的代码继续：先修类型和对象边界，再完成函数（2026-09-15）

本节以本轮实际读取的未提交源码为准，覆盖 §0.6 中较早的进度。以下只讲声明、字段、算法和修改位置，
不提供函数实现代码。当前缺口不仅是少写函数体，还包括配置/运行对象、类型/函数、optional/其中值的区别；
先解决这些问题，后续 D/E 的实现步骤才有可用的输入和对象。

### N1. Console 默认化：先确定有效配置，再创建运行对象

输入 Appender 列表为空时，创建或整表 reset 的冷配置阶段补一个 Console 目标；显式非空则不额外补。
此处按“缺配置时默认输出”理解用户要求，不是“任何日志没输出就回退”。禁用目标、过滤拒绝和文件失败不触发默认化。
列表规范化后，默认目标与显式目标走相同的验证、位图合并、工厂、虚分发链，Producer 不含 console 特殊分支。

默认值实施建议：name 为 console，type=AppenderType::Console，enabled=true，filter.levels=0x3f，
filter.category_enabled 按 Logger 固定 category 数量填 1；Logger 自己的 category 开关仍独立生效。
类别开关必须按实际长度创建，不能因为默认通常只有一个 category 就永久写死长度 1。
Console 输出目的地建议先用 stdout，V1 不默认做颜色/按等级双流分发；具体输出适配的错误处理在 I3 落笔前明确，
本轮不将其作为生产实现已完成。标准流是进程资源，Appender 退出不能关闭 stdout/stderr。

| 输入/动作 | 规范化与预期 |
|---|---|
| 默认 LoggerConfig，Appender 列表为空 | 补默认 Console；若默认 category 启用，六等级均可通过粗过滤 |
| 一个 TextFile 配置 | 只安装该文件目标，不额外复制到控制台 |
| 显式 Console + TextFile | 两个目标独立过滤，一条 Record 只入队/解码一次 |
| 非空目标全部 disabled | 不补 Console；levels 仍参与 OR，Backend 可能 no_destination |
| 非空目标 levels 都为 0 | 合并 0，Producer filtered；没有回退输出 |
| 非空但配置非法 | 拒绝输入，不能用默认 Console 掩盖错误 |
| 整表 reset 输入空集合 | 同样规范化为默认 Console，替换发生在无 Frame 借用的管理边界；文件退出错误另按 I4 合同 |

纯 merge_appender_levels 对空输入返回 0 是算法单位元；它不知道 Logger 默认策略。
normalize_logger_config 才负责补默认目标；验证/合并必须使用规范化后的同一份拥有配置。

### N2. 先修 async_logger.hpp 中的配置定义

以下源码锚点对应本轮读取，修改后行号会变化，以类型/成员名定位为准。

| 当前位置 | 当前问题 | 应怎样修改以及原因 |
|---|---|---|
| async_logger.hpp:3 | include cstring 却使用 std::string | 直接 include string；cstring 是 C 字符串函数，不是 std::string 定义 |
| :11 AppenderType | 已有 Console/TextFile | 保留这两个值，不再添加 Null；验证拒绝未知强转值，枚举编号不是 Record wire |
| :16 FilterConfig | class 成员默认 private，无构造/访问接口 | 配置采用 public 数据成员的 struct；它是调用者可填写的值，不需要继承或虚析构 |
| :18 category_enabled | 带括号的成员声明容易误读，但此写法本身可编译 | 改为普通“类型 + 成员名”以便阅读，目标类型仍为 vector<uint8_t>；真正缺口是字段访问权限及初始化/规范化，不把冗余括号误报为编译错误 |
| :21 AppenderConfig | 字段全部 private 且没有填写方式，标量未默认初始化 | 同样采用 struct；name/string，type 默认 Console，enabled 默认 true，filter/FilterConfig；专用配置按分期扩展 |
| :28 LoggerConfig | 值模型已经开始，不能再按空结构重写 | 保留 name/category_names/category_enabled；默认 names 有 default、开关有 1，两数组长度一致 |
| :32 ring | 存了 SpscRingBuffer 实例 | 换为 detail::SpscRingBufferConfig 值；实际 Ring 每个 Channel 一个，只有 bind 时才分配；Config 不持有不可复制的队列 |
| :33 config | 只保存一个 AppenderConfig | 换为 vector<AppenderConfig> appenders；这是多目标输入列表，空输入按 N1 默认化 |

FilterConfig 的 levels 建议默认 0x3f，category_enabled 可先为空；规范化阶段仅把空 category 过滤输入展开为
对应 Logger 数量的全 1 表；显式非空但长度不匹配必须拒绝，不能截断/补齐掩盖错误。
这是 public 配置的便利表达；FilterState 构造仍严格要求非空、正确长度的实际数组，不能把两个层次混为一谈。
如果希望“全部 category 关闭”，传正确长度的全 0 表；空表表示默认展开，与全关区分。

先完成这些定义，再分文件：AppenderType、FilterConfig、AppenderConfig 放 appender_config.hpp；
async_logger.hpp include 它并保留 LoggerConfig、BindError/BindResult、AsyncLogger。
这样挪动时检查只保留一份完整定义；配置头不 include AsyncLogger/Appender 运行头，避免循环依赖。
直接依赖：配置头需要 cstdint/string/vector；Logger 头另需 memory/optional、Ring 配置和完整 ProducerHandle。

阶段检查点：调用者可以填配置、复制配置、放入两个目标，复制过程不分配 Ring、不构造 Appender。
结构体带 string/vector 的复制可能分配配置内存，不能把“无 Ring 分配”写成“配置复制零分配”。

### N3. BindResult：先删除重复定义，再处理 optional 中的值

async_logger.hpp:36 的空 struct BindResult 已经是完整定义，不能与 :44 的 class BindResult 并存。
删除前者，只保留后者；struct 与 class 在这里不是两个命名空间，也不是声明与定义关系。
BindError 放在 BindResult 之前，ProducerHandle 也要在 optional 成员定义前完整可见。

| 函数/声明 | 应有形状与逐步实现 |
|---|---|
| 私有 BindResult(ProducerHandle) noexcept | 当前两个初始化目标方向正确，保留：handle_ 有值、error_ 空；只接有效已发布 Handle |
| 私有 BindResult(BindError) noexcept | 保留 handle_ 空、error_ 有值；不公开默认构造，防止两个 optional 都空 |
| handle() const noexcept → const ProducerHandle* | 先检查 handle_ 是否有值；无值返回 nullptr；有值先取得 optional 内部 ProducerHandle 的引用，再取该值地址 |
| error() const noexcept → const BindError* | 返回类型应是指针，不是当前 const BindError 值；先检查 error_ 是否有值，再取得内部 BindError 地址，否则 nullptr |
| friend class AsyncLogger | 加在结果类型内，允许 Logger 成员调用私有构造；friend 不创建对象、不引入 shared ownership |

当前 handle() 的 &handle 指向的不是 handle_ 保存的值；函数名、optional 成员名、其中的对象是三件不同的东西。
&error_ 是 optional 包装器的地址，存在的成员对象地址不表达“optional 有没有值”，也不能转成 BindError*。
顺序要记清：判断 optional → 取得其值 → 取得值地址，而非判断包装器地址。
两个访问器返回的地址只能借用结果对象；要长期用，就复制其中的 ProducerHandle，继续遵守 Logger 寿命。

阶段检查点：无重复类型定义，失败时 handle() 为空、error() 有值；成功相反；私有构造只能由授权入口使用。
不要为了让编译通过就把所有构造改 public 或返回局部临时对象地址。

### N4. ProducerHandle：在写 try_log 之前补最小可用的非拥有值

producer_handle.hpp:8 已正确使用 ProducerHandle 名称，保留；:10 的 channel_ 仍未初始化。
在类中先公开默认构造/复制/赋值/析构，指针类内初值设 nullptr；这些操作只处理地址，不访问完整 Channel。
增加 private 的显式 Channel* 构造并 friend AsyncLogger，只允许绑定成功的 Logger 发放。
默认对象本身不等于绑定成功，try_log 入口首先判空后返回 invalid_handle。

此时不需要实现 Appender，甚至不需要先写 try_log 函数体；先让 BindResult 能按值保存/复制 Handle。
后续引入模板时才按 E11 加入完整 Channel、结果/格式/参数约束依赖；模板参数/const/noexcept/约束声明定义一致。
“复制 Handle 不拥有 Channel”意味着析构不 delete、不注销、不等待；它与可跨线程写入完全无关。

### N5. validate_logger_config 和 Impl 不是同一种东西

当前 async_logger.hpp:76 的 bool validate_logger_config(AppenderConfig&) noexcept 只接一个 Appender，
无法验证 Logger category/Ring/整份列表；src/async_logger.cpp 中它的空函数体也没有返回值。
把它移出 public AsyncLogger：作为 cpp 匿名 namespace 的冷 helper，建议声明形状为
`void validate_logger_config(const LoggerConfig& normalized)`；成功正常返回，无效配置抛 invalid_argument，
只读输入、不修改、不标 noexcept。需要修改/分配默认值的是另一个 normalize 函数，不能混在验证里偷偷做。

async_logger.hpp:77 的 void Impl() 声明的是成员函数，不能代表 :80 前置声明的嵌套类，也不是构造函数。
移除这个普通函数声明；保留 private 的 class Impl 与 unique_ptr<Impl>。
在 cpp 的 namespace qlog 中完成 class AsyncLogger::Impl 定义；里面的 Impl(...) 才是构造函数，构造函数没有返回类型。
Impl 的成员在对象内真实存在；写一个函数名不会自动创建 FilterState、clock 或 owners。

请按以下文件内部顺序落笔，避免把实现错误归因于 include 顺序：

1. 先 include async_logger.hpp 和实际依赖，头文件自己的定义须独立成立。
2. 在 qlog 下的匿名 namespace 放 normalize/validate/merge 等自由 helper。
3. 退出匿名 namespace，定义完整 AsyncLogger::Impl，声明字段和构造；它不是匿名 namespace 内的另一个 Impl。
4. 完整类定义后再定义 AsyncLogger 构造、析构及转发成员。
5. 析构必须在 Impl 完整之后定义，unique_ptr 默认删除器才能安全销毁它；不要改成头内默认析构遮住缺定义。

### N6. 配置冷路径：每个函数的输入、输出与操作顺序

建议在 cpp 中依次完成以下 helper；命名是本轮明确的落笔建议，不是生产树已有函数。

| 函数形状 | 输入输出与实施步骤 |
|---|---|
| LoggerConfig normalize_logger_config(const LoggerConfig& input) | 先检查 Logger category 数量非零且可表示、初始开关长度匹配/值合法；深拷贝输入到局部候选；Appender 空则创建默认 Console；将每个目标空 category 表展开为全 1；返回拥有值。不能修补显式非法非空长度，也不修改调用者对象；分配失败传播 |
| void validate_appender_config(const AppenderConfig& config, size_t category_count) | 输入已规范化；检查 Console/TextFile 已知类型、levels 无未知位、category 数量准确且每项 0/1；需要类型专用配置时检查其匹配；I2 没有真实文件能力，不能把保存 TextFile 模型当打开成功 |
| void validate_logger_config(const LoggerConfig& normalized) | 验证 Logger category/开关、Ring 几何及 quota>=32；循环验证各目标、检查目标名字在本 Logger 内唯一；不分配 Ring、不发布、不修改输入；invalid_argument 表示输入不成立 |
| uint32_t merge_appender_levels(const AppenderConfig* configs, size_t count) noexcept | 调用方保证有效区间且已验证；从 0 OR 每项 filter.levels，包括 disabled；不合并 target category；nullptr 仅可配 count=0，不解引用空序列 |
| Impl(已规范化且验证的拥有配置) | 接收完整候选并稳定持有；建 FilterState、probe clock、建 policy、automatic dispatch，再建空 owners/head/open；不启动 Backend、不创建输出资源 |

normalize 成功后必须执行完整 validate，再构造 Impl；不能先合并/发布一半配置后才发现最后一个目标非法。
若选择由 AsyncLogger 构造先调用 helper 再 make_unique<Impl>，Impl 构造的前置条件应只在私有实现可达，
不要额外开放一个业务调用者能绕过校验的 public Impl 工厂。

Impl 的建议字段顺序：拥有配置 → FilterState → ClockDescriptor → RecordValidationPolicy → FormatHashDispatch →
注册锁/open/head/Channel owners。所有被 Channel 借用的对象都在 owners 前声明，逆序析构先销毁节点。
FilterState 初值来自规范化配置的 merge 和 Logger category 数组；policy 的 bool 来自已经完成的 clock.has_fallback。
配置在 Impl 内稳定后不再挪动；I3 再按 D15 转接目标有效配置的唯一状态来源。

阶段检查点：默认输入变成一个 Console 配置，两个显式目标保持两个；校验失败没有 Impl/节点发布；
创建 Logger 只建立冷数据，此时无打印、无 Backend、无 Channel 分配。不要调用空 bind 返回成功来演示。

### N7. 当前两个基础模块要修的具体位置

**filter_state.cpp / validate_initial_filter**：修复 throw 的语法；元数据失败直接抛出。
逐 byte 循环放到元数据检查通过之后，不能嵌在已经失败的 if 内；否则正常指针/长度下 byte=2 仍被漏过。
返回安全窄化后的 count，构造按既有成员顺序分配和复制。这里不做 Appender 默认化，职责在 N6。

**admission_clock.cpp / epoch_ns_from_timespec**：现有上界检查保留，最后计算必须使用真正十亿整数常量的乘加；
逗号不会成为数字千位分隔符。用一秒零纳秒应得十亿、Epoch 零合法、边界溢出失败三个例子理解算法。

**admission_clock.cpp:91 / const 采样版本**：保留此版本，读取本地 ns；primary 成功标 0，fallback 实际读取成功才标 1，全失败标 2。
**:109 / 非 const 采样版本**：移除新增重载，它把采样结果写进 primary_resolution_ns，破坏冷描述；
其 fallback 分支仅判断“配置存在”却没调用 read_time_ns，将分辨率误当时间戳返回。
has_fallback 只表示可以尝试，不表示本次读取成功。应只有头中声明的 const descriptor 接口，descriptor 运行中只读。
检查所有声明、定义、调用只走这一条采样实现；两种重载同时存在时可见范围和参数 const 性还会改变重载选择，不能靠调用者碰巧选对。

**log_result.hpp**：仍按 A7 修正空 FailureStage、原因集、未初始化 status、同名 accepted 修改器与 failed 反向赋值；
本轮这些没有完成。log_format.hpp、channel.cpp、诊断头仍为空，不能因为有配置类型就跳过它们直接开始 Console。

### N8. Console 运行链的依赖顺序，避免再次靠猜接口

此项属于 I3；现在先读清楚关系，不要求在 I2 写完整后台。
ConsoleAppender 继承 Appender，实现的是实际文本输出，不是改名后的无输出接收器。
基础格式化在 I3 完成：共享一次 decode，按既有完整行/格式合同生成文字，再复制到目标自有缓冲；
I4 TextFile 复用基础格式化，并扩展文件资源、独立配置与缓存优化。不能用原格式串加未展开参数冒充 Console 完整输出。

| Console 函数/资源 | 实施职责与离开时的保证 |
|---|---|
| 构造 | 冷路径保存已验证公共配置及必要输出配置，分配自有缓冲；不复制 Ring、不开后台线程，不声称拥有标准流 |
| prepare_impl(max_line_bytes) | 在 Backend 读 Frame 前检查能容纳一条最大完整行；必要时先输出旧缓冲；若空间准备失败必须可报告，不能开始借用后再扩容/等待 I/O |
| append_impl(view, context) | 使用本次共享 decode 和格式计划，按完整行合同格式化并复制进自有缓冲；接受仅表示复制成功；返回后不保存 view、参数区或 scratch 地址 |
| flush_impl() | 所有本次 Frame 已 release 后输出自有缓冲；按真实输出结果处理短写/中断/永久失败，不能直接写 strlen 丢失内嵌长度语义；返回模型和失败保留策略在 I3 实现前明确 |
| reset_impl(validated_config) | 管理锁内且无 Frame 借用时协调输出配置与基类有效过滤；I2 仅配置模型，不能先写一个总返回成功的函数 |
| close_impl()/析构 | 显式退出阶段处理尚未输出字节与可报告错误；析构释放自身内存；不 fclose/close 进程标准流，不销毁 Logger |

多目标调用顺序仍是：准备空间 → 读 Frame/decode → Logger category → 各目标公共过滤/append_impl →
全部借用结束 → 单次 release → Console/Text 的慢 I/O。一个目标失败继续其他目标，不把 console 当文件失败兜底。
共用 Backend 上 console 阻塞也会影响其他目标，验收应使用可捕获输出的测试适配，性能测试分离终端速度。

### N9. 现在真正可执行的交接顺序

| 次序 | 这次只完成哪些生产内容 | 交接时应能说明什么 |
|---|---|---|
| 1 | N2 配置值、N3 BindResult、N4 最小 Handle、删除错误 public helper/void Impl 声明 | 每个类型可填写或受控创建；RingConfig 与 Ring、optional 与其中值、Impl 类型与构造的区别；头自包含 |
| 2 | N7 Filter/clock 修正与 LogResult、runtime FormatView | 非法输入怎样返回/抛出，时间何时是 fallback；真实基础函数有定义且可链接 |
| 3 | N6 normalize/validate/merge、完整 Impl、Logger 构造析构 | 空配置默认 Console；字段初始化先后明确；未注册时没有 Ring，没有输出线程 |
| 4 | D13 Channel、D14 token/bind/关闭注册 | 一个线程两 Logger 两 Ring，同线程重复 bind 同址；所有权插入成功后才发布；资源不足不暴露半节点 |
| 5 | E11 runtime 两整数闭环，再 literal 与全部失败映射 | 精确测量/一次 reserve/取时/encode/commit；失败只 abort 自己的 reservation |
| 6 | F/G 诊断与生产接线，交由 Codex 执行 H | 默认 Release 没有诊断更新，I2 测试消费者验证 Record；仍不声称 Console/Backend 已运行 |

第 1 批完成就可以交接，不必一次写完 D15 的全部虚接口。不要把指南中的建议函数表逐项复制成空 public 成员。
维护者实现生产，Codex 接手有意义的编译/功能验收；当前本轮仅静态阅读并更新指南，未修复或编译这些生产文件。
