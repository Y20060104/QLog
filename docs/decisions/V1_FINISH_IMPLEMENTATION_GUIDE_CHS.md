# QLog V1 一轮收尾实现指南：诊断、Backend、格式化与多目标输出

> **2026-09-19 编码入口已替换：** 本文保留历史设计/进度，不再作为剩余 V1 的逐步编码指令。请按 [剩余代码级实施指南](./V1_REMAINING_CODE_IMPLEMENTATION_GUIDE_CHS.md) 的依赖顺序和唯一接口实施；五项决定及用户最新 flush 更正见 [ADR-017](./ADR-017-v1-output-and-completion.md)。旧接口片段不得与新指南混用。flush 对齐 BQLog：单次持续短写、write EINTR重试；取消暂存预算/跨轮续刷方案。历史验收结果的原始范围不变。

日期：2026-09-19（R9：正文formatter已实现；后端设计继续采用R8）。权威源码 /home/qq344/QLog。当前总入口为[剩余V1指南](./V1_REMAINING_IMPLEMENTATION_GUIDE_CHS.md)；格式合同以[ADR-016](./ADR-016-v1-bqlog-worker-format.md)优先，其他按ADR-015/014/013/012及ADR-010未被覆盖部分。
此前A阶段已完成配置迁移。9月19日已补全正文formatter并执行构建、回归、差分与sanitizer验证，详见[formatter实现与验证报告](./V1_FORMATTER_IMPLEMENTATION_REPORT_20260919_CHS.md)。B0三处历史编译问题在当前工作树已修复；诊断专项随后已完成；compose_line、Backend、管理请求和文件输出仍按本指南继续完成。

## 0. 当前起点、实施顺序和文件地图

**A配置迁移、B诊断专项和C正文formatter保留；下一步补compose_line并按完整V1范围推进D/E/F/G。**

9月17日B0记录的CMake target顺序、诊断分支逗号和ProducerDiagnostics include问题，9月19日当前工作树均已修复。本次完整构建/回归通过，具体范围见实现报告；诊断守恒另见[专项验收](./V1_DIAGNOSTICS_ACCEPTANCE_20260919_CHS.md)，后端生命周期尚未验收。旧迁移步骤保留作解释，勿再次删除/替换一遍。

| 范围 | 当前结果 | 接下来怎么做 |
|---|---|---|
| 配置依赖 | management_result.hpp 已修正；ConfigValidationError 已定义；LoggerConfig 已接 backend | 保留现有声明，后续管理实现消费这些类型 |
| 配置实现 | logger_config.cpp 已有五个局部 helper、三个共享函数；默认Console、校验与路径准备已实现 | 两个 prepare 共用列表 helper，不再复制一套 reset 规则 |
| 旧函数和调用点 | async_logger.cpp 的六个旧配置函数已移除；构造与位图初始化调用 qlog 共享入口 | Impl、FilterConfigAccess、Producer路径已保留 |
| 构建/回归 | src/logger_config.cpp 已纳入 qlog；配置测试已注册 qlog.logger_config | 下一阶段保留此回归 |
| 当前剩余 | 正文Formatter和诊断专项已完成；compose_line、Appender/Backend/管理闭环仍待完成 | C行组装→D→E→F→G→H及完整验收 |

**历史配置迁移验证（R7源码时点，不代表当前工作树）：** WSL2 GCC 13.3 Debug/Release 的 qlog 静态库构建通过；配置回归在 Debug/Release 运行通过；CTest 的 qlog.logger_config 通过；已有 i2_runtime_public_probe 在 Debug 通过。新增配置测试覆盖两个入口的默认值与错误一致性、reason/index、输入不变、路径规范化、未选中字段、disabled等级合并和真实构造/try_log接线。未运行完整测试矩阵、TSan或性能测试。

R7的Release构建曾报告一条警告：src/producer_context.cpp 的 rollback_tls_slot 参数 index 在 NDEBUG 下未使用；本次未修改该函数，不把带警告的构建描述为全仓 -Werror 通过。测试翻译单元使用 -Werror 编译通过。

复现历史配置 CTest（在仓库根执行；要求构建目录已配置 BUILD_TESTING=ON）：

~~~sh
cmake --build build/config-migration-debug --target qlog_logger_config_test -j 4
ctest --test-dir build/config-migration-debug -R '^qlog.logger_config$' --output-on-failure
~~~

**当前完成边界：** 配置值准备可用，Logger构造已使用新实现；request_reset_appenders 等管理成员仍只有声明，未来 appender_prepare.cpp 尚未实现。没有打开日志文件/启动Backend，也没有完成 flush/drain/shutdown。正文render_message_utf8已实现并验证；诊断专项已通过，下一步补compose_line，再接批量输出，不以空函数success代替后续模块。

阅读位置：B→§2.3；C→§3/4；D→§5；E→§6；F→§7/8；G→§9；H→§10；完整验收→§11。§1保留配置算法与合同说明，§1.4.2/1.4.4是已完成迁移的对照表。

按以下顺序实施，不要以空函数返回 success 临时冒充闭环：

表内采用目录加文件名的写法：`include/qlog/detail/` 后列出的头文件都在该目录，`src/` 后列出的实现都在 src；不把头文件与实现合写为 `hpp/cpp`。

| 顺序 | 文件（均相对仓库根） | 本节给出的主要类型/职责 |
|---|---|---|
| A | include/qlog/：appender_config.hpp、backend_config.hpp、management_result.hpp、logger_config.hpp；src/：logger_config.cpp | 已完成：配置字段、结果类型、统一配置准备；供E/G复用 |
| B | include/qlog/detail/：producer_diagnostics.hpp；改 include/qlog/：async_logger.hpp、async_logger_impl.hpp；src/：async_logger.cpp；CMakeLists.txt | 条件诊断，Release 无新增热更新 |
| C | include/qlog/detail/：format_spec.hpp、text_formatter.hpp；src/：text_formatter.cpp | BQLog UTF-8 scanner、spec、类型转换、有界输出；不建plan/cache |
| D | include/qlog/detail/：io_result.hpp、output_batch.hpp、appender.hpp、console_appender.hpp、text_file_appender.hpp；src/：output_batch.cpp、console_appender.cpp、text_file_appender.cpp、appender.cpp | 基类、固定 batch、真实输出 |
| E | include/qlog/detail/：control_mailbox.hpp；src/：control_mailbox.cpp、appender_prepare.cpp | 调用A的配置准备；创建/复用资源、所有权与完成反馈 |
| F | include/qlog/detail/：backend_session.hpp、backend_worker.hpp、backend_runtime.hpp、worker_wakeup.hpp；src/：四个同名cpp；改 include/qlog/detail/：producer_context.hpp、channel.hpp、spsc_ring_buffer.hpp；src/：spsc_ring_buffer.cpp | Session/Worker分离、公共注册、CV唤醒、低空间查询与detach |
| G | 改 include/qlog/：async_logger.hpp；src/：async_logger.cpp；新增 include/qlog/detail/：filter_config_access.hpp | 使用A的配置接口；构造接线、管理桥接和 shutdown |
| H | CMakeLists.txt；新增 examples/v1_logging_demo.cpp | 源文件接线、线程依赖、完整使用例 |

以下文件路径均相对权威仓库根 `/home/qq344/QLog`，各实施段落就地标明声明/定义位置；例如 `include/qlog/appender_config.hpp` 的绝对路径为 `/home/qq344/QLog/include/qlog/appender_config.hpp`。
本指南新增头文件统一放 `include/qlog/` 或 `include/qlog/detail/`，不放 src。后文 `detail/xxx.hpp` 是 `include/qlog/detail/xxx.hpp` 的简称；公共头简称对应 `include/qlog/`，cpp简称对应 `src/`。

不要把内部细节全塞进 public AsyncLogger；成员函数需要完整 Impl 的部分留在 cpp，模板入口只调用窄桥接。
所有区间接口使用 pointer + 显式长度。不新增 LoggerId 等整数别名，不修改 I1 wire、被动 Ring Handle 和 SPSC 游标算法。

## 1. 配置与公共管理结果先补齐

### 1.1 AppenderConfig

**修改文件：`include/qlog/appender_config.hpp`；位置：`namespace qlog` 内，已有 `AppenderType`、`FilterConfig` 之后，`struct AppenderConfig` 定义之前。**

采用用户确认的“平铺配置 + 枚举选择 + 运行期继承”。保留现有 AppenderType（Console/TextFile）和 FilterConfig。下列 `ConsoleStream`、`TimeZoneConfig`、`ConsoleConfig`、`TextFileConfig`、`TextOutputConfig` 均定义在这个头文件的上述位置；文件中已有同名类型时直接补齐/修正，不能重复定义：

```cpp
enum class ConsoleStream : std::uint8_t { stdout_stream, stderr_stream };
struct TimeZoneConfig { std::int16_t offset_minutes{0}; }; // 0=UTC
struct ConsoleConfig { ConsoleStream stream{ConsoleStream::stdout_stream}; };
struct TextFileConfig {
    std::string path;
    std::uint32_t retry_interval_us{100000U}; // 默认100ms，故障恢复周期
}; // 初始文件追加，故障可自动切新编号恢复文件
struct TextOutputConfig {
    TimeZoneConfig time_zone;
    std::size_t batch_bytes{256U * 1024U};
    std::uint32_t flush_interval_us{100000U};
};
```

**同一文件 `include/qlog/appender_config.hpp`：用下列定义补齐现有 `struct AppenderConfig`，放在上述专用类型之后。**

```cpp
struct AppenderConfig {
    std::string name;
    AppenderType type{AppenderType::Console};
    bool enabled{true};

    FilterConfig filter;
    TextOutputConfig text;

    ConsoleConfig console;
    TextFileConfig file;
};
```

在 `include/qlog/appender_config.hpp` 顶部的 include 区直接加入 `<cstddef>`、`<cstdint>`、`<string>`、`<vector>`，不依赖其他头的间接 include。
Config 不持有 fd、线程或 batch，不继承 Appender；ConsoleConfig/TextFileConfig 也不继承配置基类。复制 Config 只复制配置值。
type 是配置类型选择的唯一依据：Console 只校验并使用 console，忽略 file；TextFile 只校验并使用 file，忽略 console。公共字段始终校验，非法 type 映射 invalid_type。
非选中字段允许保留任意配置值，不校验、不打开资源、不影响工厂选择或 reset 兼容性；例如 Console 的 file.path 非空也不创建文件。
BQLog 使用 property_value 树读取 type 并创建 appender_base 派生运行对象；QLog 保留普通 C++ 配置字段，参考相同的按类型创建、公共控制与派生输出流程，不引入动态属性树。
本版替换前次 variant 方案，不引入 AppenderCommonConfig/AppenderTargetConfig；Backend 仍经 Appender 虚接口调用输出。

**使用示例位置：`examples/v1_logging_demo.cpp` 的 `main()` 函数体内**，包含 `qlog/async_logger.hpp` 后使用；下文为省略 `qlog::` 的局部片段，可在该函数体内写 `using namespace qlog;`。这些赋值语句不放入配置头文件的命名空间作用域：

```cpp
AppenderConfig console;
console.name = "console";
console.type = AppenderType::Console;
console.console.stream = ConsoleStream::stdout_stream;

AppenderConfig file;
file.name = "file";
file.type = AppenderType::TextFile;
file.text.time_zone.offset_minutes = 480;
file.file.path = "/tmp/qlog.log";
file.file.retry_interval_us = 100000U;
```

实施时保留 async_logger.cpp、配置 helper、示例和配置测试中的 name/type/enabled/filter 直接访问，补齐 text/console/file。
ConsoleConfig::stream 与构造校验已接入；未来 reset 调用同一 prepare_appender_configs。
本节配置字段与构造调用点已补齐；后台资源创建和运行时 reset 留到后续阶段。

配置模块固定为：`include/qlog/logger_config.hpp` 放配置函数声明，`src/logger_config.cpp` 放唯一的非模板函数实现。构造与 reset 的调用方包含同一个声明头，链接同一份实现。具体命名空间、依赖、复用方式和现有文件拆分顺序见 [§1.4](#config-module)。

`prepare_appender_configs` 步骤：检查 null+nonzero；复制输入；空列表填一个 name="console"、type=AppenderType::Console 的默认目标，其余字段使用默认值；filter 的空 category 表扩展为全1；检查 name 非空且唯一、type 合法、等级位合法、category 长度及0/1值；校验公共 text，最后仅按 type 校验选中的 console 或 file 字段。
batch_bytes 至少65536、至多16MiB；flush_interval_us 范围1..1000000；offset_minutes 范围-840..840；ConsoleStream 合法；TextFile path 非空且不含 NUL，retry_interval_us范围1000..60000000；恢复文件命名/路径容量见§5。
仅对 TextFile 目标，将 file.path 在准备线程上转为绝对且词法规范化的路径，不要求文件预先存在；父目录必须存在。仅支持普通文件，open 后 fstat 确认。不同目标/Logger 指向同一文件不会提供合并写原子性，业务应避免共享路径。
Logger 的 ring/category 校验沿用现有边界，并补§1.2的 backend 校验；当前源码没有 Logger name 非空限制，本次不新增该限制，Appender name 仍必须非空且唯一。运行时 reset 不改变 Logger name/ring/category_names/backend 等稳定配置，也不重置独立的 Logger category 开关。全部目标 filter.levels 按位 OR，enabled=false 的目标仍参与，切勿合并 Appender category。

### 1.2 BackendConfig

**补齐已有文件：`include/qlog/backend_config.hpp`（命名空间与注释均已修正）；位置：`namespace qlog` 内，先定义 `ThreadMode`，再定义 `BackendConfig`。** 文件直接包含 `<cstddef>`、`<cstdint>`。

**接入文件：`include/qlog/async_logger.hpp`**，在 include 区加入 `qlog/backend_config.hpp`，在已有 `struct LoggerConfig` 的字段末尾增加 `BackendConfig backend;`。下列定义只放在 backend_config.hpp：

```cpp
enum class ThreadMode : std::uint8_t { async, independent };
struct BackendConfig {
    ThreadMode thread_mode{ThreadMode::async};
    std::uint32_t records_per_channel{64U};
    std::size_t bytes_per_channel{256U * 1024U};
};
```

thread_mode校验枚举；两项配额必须非零，允许一条Frame超过字节配额后让出。worker统一默认66ms wait_for，作为内部kWorkerProcessInterval；V1不做运行中mode切换。batch默认仍256KiB以容纳QLog最大64KiB完整行及批量空间，不盲目复制BQLog不同缓存布局的容量。以上是参考默认而非实测最优，不默认绑核或增加V2频率采样。

### 1.3 返回类型：不能用一个 bool 混淆提交与生效

**补齐已有文件：`include/qlog/management_result.hpp`；位置：`namespace qlog` 内。** 先定义下列枚举，再定义本节的结果结构；文件直接包含 `<cstddef>`、`<cstdint>`、`<optional>`、`<string>`、`<vector>`。枚举及意义如下：

```cpp
enum class FlushMode : std::uint8_t { buffered, durable };
enum class ResetMode : std::uint8_t { reuse_compatible, recreate_all };
enum class SubmitStatus : std::uint8_t {
    submitted, busy, invalid_config, resource_exhausted,
    open_failed, identity_exhausted, stopped
};
enum class PollStatus : std::uint8_t { pending, completed, unknown_request };
enum class CompletionStatus : std::uint8_t {
    applied, applied_with_io_error, completed, completed_with_io_error,
    backend_failed
};
enum class IoStage : std::uint8_t { open, write, sync, close };
```

**仍在 `include/qlog/management_result.hpp` 的 `namespace qlog` 内**，结果结构放在上面的枚举之后，按依赖顺序先定义 `IoFailure`，再定义其余结构。配置错误 reason 枚举也放此文件，并在 `SubmitResult` 之前定义；§5.5所用、最终由 `ShutdownResult` 按值保存的 `RetiredIoSummary` 也在此文件定义，排在 `ShutdownResult` 之前。字段清单：

| 类型 | 必须具有的字段 | 含义 |
|---|---|---|
| IoFailure | appender_name:string、stage:IoStage、system_error:int、unwritten_bytes:uint64_t、durability_uncertain:bool、output_path:string | 有名字的故障，不依赖删除后的列表下标；未写字节不等于完整记录数 |
| SubmitResult | status、request_id:uint64_t（失败为0）、optional<IoFailure> error | 提交阶段；invalid_config 另存 config_index:size_t（非目标错误为最大值）与 reason 枚举 |
| ManagementCompletion | request_id、status、vector<IoFailure> errors | 执行阶段；applied_with_io_error 仍表示新列表已经生效 |
| PollResult | status、optional<ManagementCompletion> completion | pending 没有完成对象，completed 搬走结果；旧 ticket 返回 unknown_request |
| ShutdownResult | vector<IoFailure> errors、backend_failed:bool、drain_incomplete:bool | 排空/输出不同问题分别报告；重复 shutdown 返回保存结果的 const 引用 |

**A1已完成：management_result.hpp 修正记录，以下不再是待办：**

| 原草稿 | 已改为 |
|---|---|
| 无头文件保护 | 文件第一行加 #pragma once |
| SumbitStatus / resource_axhausted / identity_exhaust | SubmitStatus / resource_exhausted / identity_exhausted |
| Iostage / out_path | IoStage / output_path（声明及使用处同步） |
| ManagementCompetion / requet_id | ManagementCompletion / request_id |
| ConfigError 在结构之后，category_count_match | 枚举移到 SubmitResult 之前，成员改为 category_count_mismatch |
| SubmitResult::error 为 optional<ConfigError> | optional<IoFailure> error；另加 ConfigError reason{} 和 size_t config_index{std::numeric_limits<std::size_t>::max()} |
| ManagementCompletion/ShutdownResult 的 vector<ConfigError> | vector<IoFailure> errors |
| PollResult::completion 为 optional<CompletionStatus> | optional<ManagementCompletion> completion |

文件直接包含 limits 以使用最大 index。reason 仅在 status==invalid_config 时有效，不能以默认枚举值判断是否出错；request_id 默认0，布尔标记默认false，optional/vector 默认空。类型排列为：枚举（含ConfigError）→IoFailure→SubmitResult→ManagementCompletion→PollResult→RetiredIoSummary（字段见§5.5）→ShutdownResult。既有声明按§1.3最终名字同步，不另留旧拼写别名。ShutdownResult 的退休错误摘要按§5.5补入，避免后续只剩活跃目标的错误。

为避免退休摘要仍只有文字没有类型，RetiredIoSummary 在上述位置定义为：

~~~cpp
struct RetiredIoSummary {
    std::uint64_t event_count{0};
    std::uint64_t lost_bytes{0};
    std::optional<IoFailure> first_error;
};
~~~

在 ShutdownResult 中增加 RetiredIoSummary retired_io;。两项计数饱和累加；first_error 只在首次退休故障时移动保存，此后的故障累计计数但不覆盖首个故障。这里定义保存形状，累计/转移的执行逻辑放在§5.5/6.3的 Backend 退休路径，A阶段不提前实现后台逻辑。

配置错误 reason 定义：invalid_pointer、empty_name、duplicate_name、invalid_type、invalid_level_bits、category_count_mismatch、invalid_category_value、invalid_output_config、invalid_backend_config、invalid_path。构造仍可抛 invalid_argument/system_error；管理提交捕获可恢复的 bad_alloc/length_error/system_error，映射这些结果，不 catch-all 吞程序错误。
请求提交与准备可分配，因此 request_reset_appenders 不标 noexcept；无法为 error 分配字符串时也可能抛 bad_alloc，核心契约是未发布请求不改变生效状态。try_log 的 noexcept 不受影响。
IoFailure 的名字和结果 vector 容量在冷路径预填/预留，Backend 写错误码时不得临时构造 string 或扩容。下面所有 noexcept 后台函数均依赖这点。

**声明位置：`include/qlog/async_logger.hpp` 的 `class AsyncLogger` → `public:` 区域**，并在文件顶部包含 `qlog/management_result.hpp`。**五个成员函数的实现位置：`src/async_logger.cpp`，完整 `AsyncLogger::Impl` 定义之后**；reset的候选准备委托给§6.2的 `src/appender_prepare.cpp`。下列代码只是在类内增加声明：

```cpp
[[nodiscard]] SubmitResult request_reset_appenders(
    const AppenderConfig* configs, std::size_t count,
    ResetMode mode = ResetMode::reuse_compatible);
[[nodiscard]] SubmitResult request_flush_batches(FlushMode mode);
[[nodiscard]] SubmitResult request_drain(FlushMode mode);
[[nodiscard]] PollResult poll_management(std::uint64_t request_id);
[[nodiscard]] const ShutdownResult& shutdown(
    FlushMode mode = FlushMode::buffered);
```

request_flush_batches 仅覆盖 Backend 执行此命令时已经进入各目标 batch 的字节，不是所有 Producer 的截止屏障。
request_drain 要求所有 Producer 已停止，直到结果领取前不得恢复写入；它先把 Ring 排空再 flush。完成后可恢复 Producer，注册状态不变。
shutdown 要求停止/join 全部 Producer、无其他并发管理调用；永久停止，不允许之后 try_log。调用方违反前置条件是 API 误用，不给热路径加共享计数补救。

<a id="config-module"></a>

### 1.4 配置模块：qlog头声明、cpp实现，冷路径共用

#### 文件边界和声明

| 文件 | 内容与调用边界 |
|---|---|
| `include/qlog/async_logger.hpp` | 保留公共 LoggerConfig/AsyncLogger 声明；不在此展开配置处理算法 |
| `include/qlog/logger_config.hpp` | 以下三个配置函数声明，位于 qlog；沿用当前已创建的头 |
| `src/logger_config.cpp` | 三个函数的唯一实现；仅本文件使用的校验/原地规范化 helper 放匿名命名空间 |
| `src/async_logger.cpp` | 唯一的 AsyncLogger::Impl 与非模板成员定义；构造调用 qlog::prepare_logger_config，初始化 FilterState 时调用 qlog::merge_appender_levels |
| `src/appender_prepare.cpp` | 定义 prepare_reset；调用 prepare_appender_configs 和 merge_appender_levels，随后准备资源与命令 |

`include/qlog/logger_config.hpp` 的完整声明骨架（先按§1.3修正management_result.hpp，原因见§1.4.3）：

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "qlog/appender_config.hpp"
#include "qlog/management_result.hpp"

namespace qlog {
struct LoggerConfig;

class ConfigValidationError final : public std::invalid_argument {
public:
    ConfigValidationError(ConfigError error_reason, std::size_t index,
                          const char* message)
        : std::invalid_argument(message),
          reason(error_reason), config_index(index) {}

    ConfigError reason;
    std::size_t config_index;
};

[[nodiscard]] LoggerConfig prepare_logger_config(const LoggerConfig& input);
[[nodiscard]] std::vector<AppenderConfig> prepare_appender_configs(
    const AppenderConfig* input, std::size_t count, std::size_t category_count);
[[nodiscard]] std::uint32_t merge_appender_levels(
    const AppenderConfig* configs, std::size_t count) noexcept;

}  // namespace qlog
```

这里只声明返回 LoggerConfig 的函数，可以前置声明该类型；实现及实际构造/接收该返回值的调用方必须包含 `qlog/async_logger.hpp` 获得完整类型。不要为此再复制定义一个 LoggerConfig。
`src/logger_config.cpp` 先包含 `qlog/logger_config.hpp`，再包含 `qlog/async_logger.hpp` 及自身使用的标准库头。三个跨文件函数定义位于 `namespace qlog`，不能放匿名命名空间或标为 static，否则其他cpp无法调用。
匿名命名空间容纳本cpp使用的category/ring/backend/单目标/列表helper；逐函数迁移与删除见§1.4.2，完整函数体和调用顺序见§1.4.3。
`src/async_logger.cpp` 和 `src/appender_prepare.cpp` 包含 `qlog/logger_config.hpp`；不包含cpp文件，不在调用方手写第二套前置声明，也不把配置helper加入 `async_logger_impl.hpp` 的逐条日志模板。


#### 1.4.1 先理解两种命名空间：qlog 对外连接，匿名空间只在本 cpp 内使用

本次按维护者要求，**配置头保留在 `include/qlog/logger_config.hpp`，三个配置函数都使用 `namespace qlog`**；不创建 `detail/logger_config.hpp`，不添加 `qlog::detail` 的转发版本。公共类型 `LoggerConfig` 继续只在 `async_logger.hpp` 定义，当前前置声明方案不要求移动该类型。

头文件所在目录和 C++ 命名空间是两件事；本节明确同时选择 `qlog/logger_config.hpp` 与 `qlog::prepare_...`，与当前已写的声明一致。`namespace qlog` 可以在一个文件中分段打开；现有头已合并为一段 qlog，语义不变。

“跨文件”指 `async_logger.cpp` 和未来的 `appender_prepare.cpp` 都能通过头文件声明，链接到 `logger_config.cpp` 中的同一份函数定义：

```text
qlog/logger_config.hpp          logger_config.cpp
qlog::prepare_logger_config  -> qlog::prepare_logger_config 的唯一函数体
qlog::prepare_appender_configs -> qlog::prepare_appender_configs 的唯一函数体
qlog::merge_appender_levels  -> qlog::merge_appender_levels 的唯一函数体
                                      |
                                      +-> 本文件匿名空间里的局部 helper
```

匿名命名空间不是“不能被函数调用”：同一 cpp 内、后面定义的 qlog 函数可以调用它的 helper；其他 cpp 不能用这里的 helper 作为共享接口。
因此下面的括号位置很关键：先关闭匿名空间，再定义三个共享入口，最后关闭 qlog。不在头文件放匿名空间，不把三个共享定义标为 `static`。仅给头文件写 qlog 声明、却把 cpp 定义放匿名空间，不能为那个 qlog 声明提供正确的外部定义。

#### 1.4.2 已完成的六个旧函数迁移对照

以下锚点来自 2026-09-17 实读 `src/async_logger.cpp`，行号会随编辑变化，按函数名定位。**“迁移”表示剪切到唯一新归属并接好调用，随后删除旧位置；不保留两套同名算法。**

| 旧函数/位置 | 可以复用的具体内容 | 新归属和最终操作 |
|---|---|---|
| `validate_logger_categories`，原第21行 | category 数量 1..UINT32_MAX、Logger category_enabled 长度一致、每项0/1；目前没有 category 名称非空/唯一约束，不顺手增加 | 整体迁入 logger_config.cpp 的匿名空间。构造准备只调用一次；reset只校验传入的 category_count，不重验稳定 Logger 元数据 |
| `normalize_logger_config`，原第39行 | 空 Appender 列表补默认 Console 的块；空 Appender category 表 assign 全1的循环 | 两块迁入共享的 `normalize_validate_appenders_in_place`；`LoggerConfig result = input` 移入新的公开 prepare_logger_config；旧函数中的 category 校验调用不带过去。拆完后删除整个旧函数，不在新文件保留这个名字 |
| `validate_appender_config`，原第66行 | type switch、levels 非法位检查、category 长度与0/1检查 | 迁入新文件匿名空间，保留为单目标 helper；扩展公共 text 和选中 console/file 的规则，并传入 config_index 以生成结构化错误。不要同时在列表helper再写一遍这些检查 |
| `validate_logger_config`，原第91行 | capacity/quota 校验块；Appender 的 i/j 重名检查 | ring 块拆成匿名 `validate_logger_ring`；重名循环迁入列表helper；删除重复的 category 检查调用；单目标校验改由列表helper调用。拆完删除旧的总校验函数 |
| `merge_appender_levels`，原第119行 | 从0开始，逐项 `merged |= configs[i].filter.levels` 的整个函数体 | 原样移到 logger_config.cpp 的 qlog 空间、匿名空间外；不加 enabled 判断，不移进 Backend 热循环。async_logger.cpp 删除旧定义 |
| `prepare_logger_config`，原第129行 | 返回独立拥有的已准备 LoggerConfig 这一职责 | 原函数体的 normalize -> validate 两步不再保留，改用下方新调用顺序。新定义位于 qlog 空间；async_logger.cpp 删除旧匿名定义 |

新增函数只有职责明确的几项：

- `validate_logger_ring(const LoggerConfig&)`：接收旧 ring 校验代码，不改边界。
- `validate_backend_config(const BackendConfig&)`：新增 §1.2 枚举及非零配额校验。
- `normalize_validate_appenders_in_place(std::vector<AppenderConfig>&, std::size_t)`：负责列表默认值、category 展开、名字校验和调用单目标规则。
- `prepare_appender_configs(const AppenderConfig*, std::size_t, std::size_t)`：新增 reset 的值准备入口；复制一次后调用同一个列表helper。
- 如为路径规范化拆小 helper，它仍只放 logger_config.cpp 匿名空间；不要在 appender_prepare.cpp 再写一份路径规范化。

旧规则需要补齐的部分不能只“搬家”：旧源码没有 Appender 空名字校验、text/console/file 校验与路径规范化、backend 校验；这些按 §1.1/1.2 添加。旧的 O(n²) 重名比较可以直接保留，属于冷路径，当前无需引入另一套哈希索引。

#### 1.4.3 配置 cpp 完整实现：按最终版本整理，不再拼旧代码块

**目标文件：src/logger_config.cpp。** 下方包含五个局部 helper 和三个共享函数的完整函数体；§1.4.2 只解释旧代码来源，不是另一套实施步骤。§1.3结果类型、ConfigValidationError、LoggerConfig::backend 均已接入。原单数 helper 名已统一为 normalize_validate_appenders_in_place，定义与两个调用点一致。

“共享列表 helper”**只指 normalize_validate_appenders_in_place 这一个函数**：

| 函数 | 参数 | 检查方式 | 调用者 |
|---|---|---|---|
| validate_logger_categories | const LoggerConfig&，配置对象的只读引用 | category_names.size() 及 category_enabled 内容 | prepare_logger_config |
| normalize_validate_appenders_in_place | vector<AppenderConfig>&，已有 vector 的可写引用 | configs.empty() 检查没有元素 | 两个 prepare |
| prepare_appender_configs | const AppenderConfig* input + count，外部数组区间 | input == nullptr && count != 0U | 未来 prepare_reset |
| merge_appender_levels | 已验证数组指针 + count | count为0不解引用；非零区间须有效 | 构造与 reset |

引用不是指针，前两行不能写 config == nullptr 或 configs == nullptr，也不要改成 &configs == nullptr。vector 对象存在但没有元素，用 empty()。Logger 的 category_names 为空是错误；Appender 列表为空则补默认 Console，规则不同。

原 prepare_appender_configs 的 return ConfigError::invalid_pointer 已删除：成功返回 vector，失败 throw ConfigValidationError(...)，未来管理入口再转换成 SubmitResult。未使用的局部 vector config 已删除，只保留 result。

~~~cpp
#include "qlog/logger_config.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "qlog/async_logger.hpp"
#include "qlog/backend_config.hpp"

namespace qlog {
namespace {

constexpr std::size_t kNoConfigIndex = std::numeric_limits<std::size_t>::max();

// 1. Logger 的空 category 表不补全。
void validate_logger_categories(const LoggerConfig& config) {
    const auto count = config.category_names.size();
    if (count == 0U || count > std::numeric_limits<std::uint32_t>::max() ||
        config.category_enabled.size() != count) {
        throw ConfigValidationError(ConfigError::category_count_mismatch, kNoConfigIndex,
                                    "invalid logger category count");
    }
    for (const auto value : config.category_enabled) {
        if (value > 1U) {
            throw ConfigValidationError(ConfigError::invalid_category_value, kNoConfigIndex,
                                        "logger category value must be 0 or 1");
        }
    }
}

// 2. 沿用旧 ring 边界；reset 不接收 ring。
void validate_logger_ring(const LoggerConfig& config) {
    const auto capacity = config.ring.capacity_bytes;
    const auto quota = config.ring.max_payload_bytes;
    if (capacity < 16U || capacity > (std::size_t{1} << 31U) ||
        (capacity & (capacity - 1U)) != 0U) {
        throw std::invalid_argument("invalid ring capacity");
    }
    if (quota < 32U || quota > capacity / 2U) {
        throw std::invalid_argument("invalid record payload quota");
    }
}

// 3. Backend 模式与非零配额。
void validate_backend_config(const BackendConfig& config) {
    switch (config.thread_mode) {
        case ThreadMode::async:
        case ThreadMode::independent:
            break;
        default:
            throw ConfigValidationError(ConfigError::invalid_backend_config, kNoConfigIndex,
                                        "unknown backend thread mode");
    }
    if (config.records_per_channel == 0U || config.bytes_per_channel == 0U) {
        throw ConfigValidationError(ConfigError::invalid_backend_config, kNoConfigIndex,
                                    "backend quota must be nonzero");
    }
}

// 4. 单目标只读检查，category 表已展开。
// 名字/重名与路径实际改写属于列表 helper。
void validate_appender_config(const AppenderConfig& config, std::size_t category_count,
                              std::size_t config_index) {
    switch (config.type) {
        case AppenderType::Console:
        case AppenderType::TextFile:
            break;
        default:
            throw ConfigValidationError(ConfigError::invalid_type, config_index,
                                        "unknown appender type");
    }
    if ((config.filter.levels & ~std::uint32_t{0x3F}) != 0U) {
        throw ConfigValidationError(ConfigError::invalid_level_bits, config_index,
                                    "unknown appender level bits");
    }
    if (config.filter.category_enabled.size() != category_count) {
        throw ConfigValidationError(ConfigError::category_count_mismatch, config_index,
                                    "appender category count mismatch");
    }
    for (const auto value : config.filter.category_enabled) {
        if (value > 1U) {
            throw ConfigValidationError(ConfigError::invalid_category_value, config_index,
                                        "appender category value must be 0 or 1");
        }
    }
    const auto& text = config.text;
    if (text.batch_bytes < 65536U || text.batch_bytes > 16U * 1024U * 1024U ||
        text.flush_interval_us == 0U || text.flush_interval_us > 1000000U ||
        text.time_zone.offset_minutes < -840 || text.time_zone.offset_minutes > 840) {
        throw ConfigValidationError(ConfigError::invalid_output_config, config_index,
                                    "invalid text output config");
    }
    if (config.type == AppenderType::Console) {
        switch (config.console.stream) {
            case ConsoleStream::stdout_stream:
            case ConsoleStream::stderr_stream:
                break;
            default:
                throw ConfigValidationError(ConfigError::invalid_output_config, config_index,
                                            "unknown console stream");
        }
    } else {  // type已验证，此处一定是TextFile。
        if (config.file.path.empty() || config.file.path.find('\0') != std::string::npos) {
            throw ConfigValidationError(ConfigError::invalid_path, config_index,
                                        "file path must be nonempty and contain no NUL");
        }
        if (config.file.retry_interval_us < 1000U || config.file.retry_interval_us > 60000000U) {
            throw ConfigValidationError(ConfigError::invalid_output_config, config_index,
                                        "invalid file retry interval");
        }
    }
}

// 5. 共享列表 helper：只修改 prepare 复制后的候选。
void normalize_validate_appenders_in_place(std::vector<AppenderConfig>& configs,
                                           std::size_t category_count) {
    if (configs.empty()) {
        AppenderConfig console;
        console.name = "console";
        console.type = AppenderType::Console;
        configs.push_back(std::move(console));
    }
    for (std::size_t i = 0; i < configs.size(); ++i) {
        auto& config = configs[i];
        if (config.filter.category_enabled.empty()) {
            config.filter.category_enabled.assign(category_count, std::uint8_t{1});
        }
        if (config.name.empty()) {
            throw ConfigValidationError(ConfigError::empty_name, i, "empty appender name");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (config.name == configs[j].name) {
                throw ConfigValidationError(ConfigError::duplicate_name, i,
                                            "duplicate appender name");
            }
        }
        validate_appender_config(config, category_count, i);
        if (config.type == AppenderType::TextFile) {
            const auto path = std::filesystem::absolute(std::filesystem::path(config.file.path))
                                  .lexically_normal();
            if (!std::filesystem::is_directory(path.parent_path())) {
                throw ConfigValidationError(ConfigError::invalid_path, i,
                                            "file parent must be an existing directory");
            }
            config.file.path = path.string();
        }
    }
}

}  // namespace

LoggerConfig prepare_logger_config(const LoggerConfig& input) {
    validate_logger_categories(input);
    validate_logger_ring(input);
    validate_backend_config(input.backend);
    LoggerConfig result = input;
    normalize_validate_appenders_in_place(result.appenders, result.category_names.size());
    return result;
}

std::vector<AppenderConfig> prepare_appender_configs(const AppenderConfig* input, std::size_t count,
                                                     std::size_t category_count) {
    if (input == nullptr && count != 0U) {
        throw ConfigValidationError(ConfigError::invalid_pointer, kNoConfigIndex,
                                    "null appender input with nonzero count");
    }
    if (category_count == 0U || category_count > std::numeric_limits<std::uint32_t>::max()) {
        throw ConfigValidationError(ConfigError::category_count_mismatch, kNoConfigIndex,
                                    "invalid logger category count");
    }
    std::vector<AppenderConfig> result;
    if (count != 0U) {
        result.assign(input, input + count);
    }
    normalize_validate_appenders_in_place(result, category_count);
    return result;
}

std::uint32_t merge_appender_levels(const AppenderConfig* configs, std::size_t count) noexcept {
    std::uint32_t merged = 0U;
    for (std::size_t i = 0; i < count; ++i) {
        merged |= configs[i].filter.levels;
    }
    return merged;
}

}  // namespace qlog
~~~

**调用顺序与失败边界：**

1. 两个 prepare 先检查本入口的结构，再复制一次原始输入；列表 helper 的 category_count 已合法，不重复验证。
2. 空 Appender 列表/空 Appender category 表补默认值；显式非空但非法的表直接拒绝。
3. 原始路径检查在单目标 helper；绝对化、词法规范化、父目录检查在列表 helper。filesystem 的系统错误继续抛 filesystem_error（派生自 system_error）；可查询但不是目录则报 invalid_path。不调用 canonical，不要求目标文件已存在。
4. 父目录检查不是资源成功保证；之后 open/fstat 仍可能失败，由§6工厂负责。此处不打开文件、不启动线程、不发布配置。
5. 任一目标失败销毁外层 result；调用者输入与活动列表不变。不得 catch-all 返回空 vector，把错误伪装成默认 Console。
6. merge 不加 enabled 条件，不合并 category，不分配；只接收准备完成的配置。nullptr+0 返回0，nullptr+非零违反调用前置条件。

ConfigValidationError 的完整定义见§1.4头文件代码，不能只声明类名。非目标错误 index 为 size_t 最大值，具体目标为 i。构造允许异常向外传播，保持 invalid_argument 契约；未来 prepare_reset 捕获 ConfigValidationError，映射 invalid_config、request_id=0、reason/index，不解析 what()，不把 ConfigError 放进 optional<IoFailure>。bad_alloc/length_error/system_error 按§1.3处理。

本段实现已落实到 src/logger_config.cpp，定向构建与验证见§0。

#### 1.4.4 构造接线已完成，未来 reset 复用方式

**src/async_logger.cpp（以下迁移已完成，仅供核对）：**

- include区新增 `#include "qlog/logger_config.hpp"`。
- 从文件开头匿名空间中移除表中六个旧函数；迁移前该匿名空间只包含这六个函数，因此可连同空的 `namespace { ... }` 外壳一起删除。以后若加了其他无关helper，保留那些helper和外壳。
- `AsyncLogger::AsyncLogger(const LoggerConfig& config)` 改为 `impl_(std::make_unique<Impl>(qlog::prepare_logger_config(config)))`。
- `AsyncLogger::Impl::Impl` 的filter初始化首参改为 `qlog::merge_appender_levels(config.appenders.data(), config.appenders.size())`。写全限定名可明确调用共享入口，防止旧匿名实现残留而被误用。
- 保留Impl、析构、注册与Producer桥接、category setter。FilterConfigAccess此步不删除；到§9.1迁入其共享头后再删旧类定义。它属于过滤状态访问，不是配置值准备。
- `<limits>`/`<stdexcept>` 在迁移后若本文件不再使用才删除；`<utility>`、`<memory>` 等仍被Impl与构造使用，不按旧helper一起整段删include。

**未来 src/appender_prepare.cpp：**

```cpp
// prepare_reset 的函数体片段；参数名对照该函数实际声明。
// category_count 来自 Logger 已验证的稳定元数据。
auto prepared = qlog::prepare_appender_configs(configs, count, category_count);
const auto merged =
    qlog::merge_appender_levels(prepared.data(), prepared.size());
// 后续使用 prepared 做兼容性匹配、资源准备与影子/命令组装；
// merged 存进待发布命令。Backend只发布此值。
```

reset 不再自己补Console、展开category、检查重名或规范化路径；资源工厂仍负责open/fstat等资源检查，它们不属于被删除的重复配置规则。后续需要保存影子时按所有权要求复制/移动prepared，不重新使用调用者原始数组做第二轮准备。

**迁移完成后的名字数量：** logger_config.cpp有三个qlog共享定义；同文件匿名空间有各一份category/ring/backend/单目标/列表helper；async_logger.cpp没有这六个旧配置函数定义；全仓不再有旧 `normalize_logger_config`、`validate_logger_config` 函数定义。头文件的函数声明不是重复实现，无需删除。


#### 一次配置准备怎么复用

1. **构造入口 `prepare_logger_config`**：先检查 Logger category 元数据、ring 与 backend 参数；随后 `LoggerConfig result = input` 深拷贝一次，在 `result.appenders` 上调用共享原地helper，成功返回拥有值。不要再对这个已复制vector调用会复制输入的 `prepare_appender_configs`。同一份结果移动进 Impl，FilterState 使用其规范化后的目标列表。
2. **reset入口 `prepare_appender_configs`**：先验证 null+nonzero 和 category_count（1..UINT32_MAX），再将输入复制到局部vector；count=0直接从空vector开始，不能对nullptr做 `input + count` 或构造空指针迭代器区间。调用同一个原地helper，成功返回拥有值。调用方保证非空区间有效且准备期间不并发修改输入。
3. **共享原地helper**：空列表补默认Console；逐目标把空category表扩展为全1，再按§1.1校验名字唯一性、type、filter、公共text及选中专用字段，并规范化TextFile路径。显式非法非空表不修补，未选中字段不校验。整份列表成功后，才允许进入Appender资源准备。该helper不打开文件、不创建worker、不修改生效状态；父目录检查在准备线程完成，open后的fstat由资源工厂负责。
4. **`merge_appender_levels`**：只接受已经规范化、验证的配置；从0对所有filter.levels做OR，包含disabled目标，不合并Appender category。nullptr仅允许配count=0，空序列返回0，不补Console、不分配、不访问原子。构造在FilterState初始化时合并一次，reset在准备命令时合并一次，Backend应用时仅发布准备好的位图。
5. **失败交付**：只返回完整准备结果；失败销毁局部候选，调用者输入和生效配置不变。配置错误保留§1.3的reason与config_index，构造按异常契约向外报告，reset按SubmitResult映射，不能通过解析异常文案猜测错误类型。分配/路径错误沿用§1.3，不在helper里catch-all后返回空vector，否则错误会被当成默认Console。

“深拷贝一次”指每个值准备入口不重复复制原始输入；§6要求的管理影子、待交换配置和错误存储仍须分别拥有其数据，不能为了省复制共享可变vector或借用调用者指针。后续阶段只读取已准备配置，必要的资源检查不等于重新规范化一遍。

调用链固定如下：

```text
AsyncLogger 构造
  -> qlog::prepare_logger_config
     -> 校验稳定元数据 -> 复制一次 -> normalize_validate_appenders_in_place
  -> Impl 移入配置 -> merge_appender_levels -> FilterState 初始值
  -> 冷准备 Appender/Session/影子 -> 发布Session或启动独立worker（§8.1，共享worker可先运行）

request_reset_appenders
  -> 检查 stopped / mailbox busy
  -> prepare_reset（src/appender_prepare.cpp）
     -> prepare_appender_configs -> normalize_validate_appenders_in_place
     -> merge_appender_levels -> 名字匹配/兼容复用/创建候选与结果存储
  -> 发布 pending -> Backend apply_reset 移动/swap资源并发布合并位图
  -> completed -> poll 接收影子并回收退休对象

try_log
  -> classify_call -> FilterState::allows_unchecked
  -> 原有 Context / measure / reserve / clock / encode / commit 路径
```

#### BQLog依据与性能选择

本次直接核对本地BQLog提交 `60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`，以下路径相对BQLog参考仓库：

| 源码锚点 | 已确认的行为 | QLog采用的机制 |
|---|---|---|
| `src/bq_log/log/appender/appender_base.cpp:55-76`，init/reset | 两者都调用set_basic_configs，reset先判断类型 | 初始化与reset共享公共配置规则；QLog先完整验证候选再发布 |
| `src/bq_log/log/log_imp.cpp:218-275`，reset_config | 按名字匹配旧目标，reset成功则复用；该段使用scoped_spin_lock | 保留按名字及兼容性复用；交接继续使用ADR-015单槽邮箱与Backend独占 |
| 同文件 `:478-487`，refresh_merged_log_level_bitmap | 预先OR全部Appender等级，循环不检查enable | 在配置准备阶段合并，Producer不用逐目标遍历 |
| `include/bq_log/misc/bq_log_impl.h:186-188`，is_enable_for | 逐条入口只检查合并等级与Logger类别 | 保留QLog FilterState独立relaxed读取，不引入配置解释器或管理对象访问 |

本次选择“qlog头声明 + 单cpp实现”。把普通非模板冷函数放头文件不会自动让Producer更快；源码目录本身也不决定机器码性能。头文件只承载跨翻译单元声明，配置工作始终发生在构造/管理线程。
把所有helper继续留在async_logger.cpp匿名命名空间，只适用于调用方全部在该文件的布局；计划中的prepare_reset独立放在appender_prepare.cpp，需要共享接口。把整套算法改为头文件inline也可实现语义，但没有日志热路径收益依据，还会让调用方解析实现并扩大重编译依赖；本版不采用。
可解释的性能收益是：逐条粗过滤成本不随Appender数量增长；构造/reset各自避免一轮多余输入复制；兼容reset避免重建batch、重开文件和丢弃恢复状态。shared worker、低空间/full唤醒的既定成本不变。以上是成本分析，不是已经测得吞吐提升或超过BQLog的结论。

#### 配置迁移完成后的维护边界

本节 A1～A6 已落地。后续 prepare_reset 只调用配置模块共享入口；资源工厂负责 open/fstat，不在它那里再做默认Console/category展开或字段规范化。回退时须同时回退配置实现、头文件依赖、构造调用点与CMake接线，不能单独删掉 logger_config.cpp。

## 2. Debug 诊断：先完成返回路径记账

### 2.1 类型和放置

**文件位置：**诊断计数辅助类型定义在 `include/qlog/detail/producer_diagnostics.hpp` 的 `qlog::detail` 内；Impl的计数字段在 `src/async_logger.cpp` 的 `AsyncLogger::Impl` 内；`record_call_result` 在 `include/qlog/async_logger.hpp` 的 `AsyncLogger::private` 区声明、`src/async_logger.cpp` 定义；模板返回点的调用写在 `include/qlog/async_logger_impl.hpp`；CMake选项和宏写在根 `CMakeLists.txt`。

新增 detail/producer_diagnostics.hpp；CMake 新增 QLOG_ENABLE_DIAGNOSTICS=AUTO/ON/OFF，AUTO 仅 Debug 启用。
导出数值宏 `QLOG_ENABLE_DIAGNOSTICS=0/1`，必须是 target_compile_definitions(qlog PUBLIC ...)，使公共模板与库保持一致。
开启时 Impl 持有七个 relaxed atomic<uint64_t>：calls、filtered、rejected_pre_admission、attempted、accepted、dropped_full、failed_after_attempt。可附 accepted_bytes；不要为了 filtered 计数分配 Context。
以两个缓存行组隔离诊断与 merged filter/head 等热点，Debug 原子 RMW 的代价不混入 Release 性能结论。
新增私有桥接 `void record_call_result(const LogResult&, std::size_t accepted_bytes) const noexcept;`，诊断声明/定义和每个诊断调用点均用宏包住；普通 Release 连桥接调用也不保留。

### 2.2 try_log 的每个返回怎么记

仅基础 FormatView 重载记账，数组重载不计。每个出口先生成 result，再调桥接，再 return；不要引入一个会二次复制流程的包装入口。
桥接一次 calls++，再按互斥结果归类：

| 结果 | 计数 |
|---|---|
| filtered | filtered++ |
| validation/context/measure 失败 | rejected_pre_admission++ |
| reserve full | attempted++，dropped_full++ |
| reserve 非full失败、encode失败 | attempted++，failed_after_attempt++ |
| accepted | attempted++，accepted++，accepted_bytes+=prepared.payload_size() |

用 FailureStage 判定，不要把 too_large 全部当成同一阶段；reserve 的异常 too_large 仍已 attempted。
Debug 聚合在停止 Producer 后读取，Backend统计在本Session detach确认或独立worker join后读取，不提供 public live statistics API。计数溢出使守恒检查失效，诊断报告必须标明；不拿计数决定是否关停。
BackendSession非原子诊断字段：processed/decode_failed/no_destination/dispatched/selected_deliveries/delivery_accepted/delivery_failed。按本Session计数，detach或独立worker join后读取。无parse-cache六项；共享worker汇总只能在Runtime停止后读取。关闭宏移除诊断，I/O故障状态和结果保留。

### 2.3 下一步 B：按这五步完成诊断，不改配置模块

**当前起点：** producer_diagnostics.hpp、record_call_result及宏已部分写入，先修§0的B0三处问题。下列B1～B5作为完成检查表逐项对照现有源码，不重复添加同名类型或函数。

**B1 — 根 CMakeLists.txt：统一库和调用者的宏。**

在现有 QLOG_ENABLE_RING_VALIDATION 配置附近新增以下配置；target_compile_definitions 必须放在 qlog target 创建之后。缓存值为 AUTO/ON/OFF；传给C++的一定为数值0或1。

~~~cmake
set(QLOG_ENABLE_DIAGNOSTICS "AUTO" CACHE STRING "Producer diagnostics: AUTO, ON or OFF")
set_property(CACHE QLOG_ENABLE_DIAGNOSTICS PROPERTY STRINGS AUTO ON OFF)
if(QLOG_ENABLE_DIAGNOSTICS STREQUAL "AUTO")
    set(QLOG_DIAGNOSTICS_VALUE "$<IF:$<CONFIG:Debug>,1,0>")
elseif(QLOG_ENABLE_DIAGNOSTICS STREQUAL "ON")
    set(QLOG_DIAGNOSTICS_VALUE 1)
elseif(QLOG_ENABLE_DIAGNOSTICS STREQUAL "OFF")
    set(QLOG_DIAGNOSTICS_VALUE 0)
else()
    message(FATAL_ERROR "QLOG_ENABLE_DIAGNOSTICS must be AUTO, ON or OFF")
endif()
target_compile_definitions(qlog PUBLIC
    QLOG_ENABLE_DIAGNOSTICS=${QLOG_DIAGNOSTICS_VALUE})
~~~

PUBLIC 是必要的：AsyncLogger 的模板在用户翻译单元编译，宏不一致会产生库/头的定义差异。不要只在 async_logger.cpp 写 define。手工 g++ 编译 probe 时也必须传与库一致的宏；默认通过链接 QLog::qlog 传播。

**B2 — 新建 include/qlog/detail/producer_diagnostics.hpp，并在 Impl 持有它。**

以下是完整类型定义。一个对齐块用于 calls/filtered/rejected，另一个用于 admission 结果；64字节为本版本布局选择，不宣称所有CPU缓存行均为64。accepted_bytes 本步不新增，先验证七项守恒。

~~~cpp
#pragma once
#include <atomic>
#include <cstdint>

#if QLOG_ENABLE_DIAGNOSTICS
namespace qlog::detail {
struct alignas(64) ProducerCallCounters {
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> filtered{0};
    std::atomic<std::uint64_t> rejected_pre_admission{0};
};
struct alignas(64) ProducerAdmissionCounters {
    std::atomic<std::uint64_t> attempted{0};
    std::atomic<std::uint64_t> accepted{0};
    std::atomic<std::uint64_t> dropped_full{0};
    std::atomic<std::uint64_t> failed_after_attempt{0};
};
struct ProducerDiagnostics {
    ProducerCallCounters call;
    ProducerAdmissionCounters admission;
};
}  // namespace qlog::detail
#endif
~~~

src/async_logger.cpp 包含此头，在 Impl 的现有字段之后增加：

~~~cpp
#if QLOG_ENABLE_DIAGNOSTICS
    detail::ProducerDiagnostics diagnostics;
#endif
~~~

不要在 ProducerContext 放这七项：filtered/validation 可能在获取 Context 之前返回。它们只用于诊断，不能据此判断 Logger 是否能关停。

**B3 — 声明并实现唯一记账桥接。**

include/qlog/async_logger.hpp 的 private 区，在 classify_call 附近加入：

~~~cpp
#if QLOG_ENABLE_DIAGNOSTICS
    void record_call_result(const LogResult& result,
                            std::size_t accepted_bytes) const noexcept;
#endif
~~~

src/async_logger.cpp 在 Impl 完整定义之后加入以下函数，并直接包含 cassert 和 exception（不依赖其他头间接提供 assert/terminate）：

~~~cpp
#if QLOG_ENABLE_DIAGNOSTICS
void AsyncLogger::record_call_result(
    const LogResult& result, std::size_t accepted_bytes) const noexcept {
    (void)accepted_bytes; // 本步只做七项计数；保留既定桥接签名。
    auto& counters = impl_->diagnostics;
    const auto increment = [](std::atomic<std::uint64_t>& value) noexcept {
        value.fetch_add(1U, std::memory_order_relaxed);
    };
    increment(counters.call.calls);
    if (result.status() == LogStatus::filtered) {
        increment(counters.call.filtered);
        return;
    }
    if (result.accepted()) {
        increment(counters.admission.attempted);
        increment(counters.admission.accepted);
        return;
    }
    const auto* failure = result.failure();
    assert(failure != nullptr);
    if (failure == nullptr) {
        std::terminate(); // LogResult 内部不变量破坏，不伪造计数类别。
    }
    switch (failure->stage) {
        case FailureStage::validation:
        case FailureStage::context:
        case FailureStage::measure:
            increment(counters.call.rejected_pre_admission);
            return;
        case FailureStage::reserve:
        case FailureStage::encode:
            increment(counters.admission.attempted);
            if (failure->stage == FailureStage::reserve &&
                result.status() == LogStatus::full) {
                increment(counters.admission.dropped_full);
            } else {
                increment(counters.admission.failed_after_attempt);
            }
            return;
    }
    std::terminate();
}
#endif
~~~

分类依据是 FailureStage，不能只按 too_large 的状态分类；reserve阶段失败已经 attempted。计数器只做 relaxed：停止并 join Producer 后再读取，不把多个 load 当运行期一致快照。计数溢出边界的报告规则沿用§2.2。

**B4 — 只改 async_logger_impl.hpp 的基础 FormatView 重载。**

每个返回先保存 result，宏内记账，再返回。下面以队列满/预留失败分支为例；其他失败按表替换相应生成表达式：

~~~cpp
if (!write) {
    const auto result =
        detail::map_reserve_error(write.status(), prepared.payload_size());
#if QLOG_ENABLE_DIAGNOSTICS
    record_call_result(result, 0U);
#endif
    return result;
}
~~~

| 当前返回点 | result 生成方式 | accepted_bytes |
|---|---|---|
| classify_call 的 invalid_level/invalid_category | 保留原 make_failed 参数 | 0 |
| classify_call 的 filtered | make_filtered() | 0 |
| context==nullptr | map_context_error(*acquired.error()) | 0 |
| !measured.succeeded() | map_measure_failure(*measured.failure()) | 0 |
| !write | map_reserve_error(...) | 0 |
| !encoded.succeeded() | 先保留 target_size，先 ring.abort(write)，再 map_encode_error(...) | 0 |
| ring.commit(write) 之后 | make_accepted() | prepared.payload_size() |

以上共8个返回位置，每次调用恰好经过其中一个。数组重载仍直接转发，不能再记一次 calls；不能改测量/预留/时间采样顺序，也不能在 filtered 路径创建 Context。宏关闭后没有桥接调用和原子更新。

**B5 — 完成标准与验证范围。**

先检查声明、定义、字段、调用点都被同一宏保护，再验证 Debug AUTO启用、Release AUTO禁用，以及显式ON/OFF可覆盖。沿用配置回归与runtime probe检查既有行为；用仅测试内部访问在全部Producer停止后读取计数，核对：

~~~text
calls = filtered + rejected_pre_admission + attempted
attempted = accepted + dropped_full + failed_after_attempt
~~~

输入场景至少含 invalid_level、invalid_category、filtered、context失败、measure失败、full和accepted；reserve非full/encode失败需用内部映射构造或受控故障注入，不能声称正常公开API已覆盖这些内部不变量路径。不要新增 public live statistics API 只为取计数；测试访问器或独立分类单测的具体接入在实施B时与已有测试设施一起完成。

Release 检查生成代码确实无诊断调用/RMW；Debug的原子计数成本单列。B专项现已收口，正文scanner也已完成；按[完整V1收尾执行计划](./V1_FULL_COMPLETION_EXECUTION_PLAN_20260919_CHS.md)继续compose_line、输出和后台管理。

## 3. Formatter：BQLog UTF-8顺序扫描与有界输出

### 3.1 格式类型和入口

本节由[ADR-016](./ADR-016-v1-bqlog-worker-format.md)覆盖旧strict parser。Producer当前已经不解析花括号；这里实现的是worker功能。

当前 `include/qlog/detail/format_spec.hpp` 已提供 `FormatSpec`、`FormatError`、`FormatFailure`、`FormatResult`，正文render_message_utf8及匿名辅助函数已实现。以下正文算法可用于理解/维护现有代码；compose_line仍需继续实现。旧format_plan.hpp已移除，不恢复strict默认值。

```cpp
struct FormatSpec {
    bool used{false};
    bool upper{true};
    char fill{' '};
    char align{'>'};
    char sign{'-'};
    char prefix{' '};
    std::uint32_t offset{0};
    std::uint32_t width{0};
    std::uint32_t precision{0xFFFFFFFFU};
    char type{' '};
};
```

每次替换字段产生新的默认spec。BQLog reset的align='<'与新构造的align='>'不同，不能将reset默认值搬成构造默认值。完整转换与padding规则以ADR-016和其中BQLog源码锚点为准。

FormatError只保留invalid_format_metadata、invalid_arguments、invalid_workspace、format_too_large、number_conversion_failed、time_conversion_failed、text_output_limit_exceeded。FormatFailure保存error、byte_offset:size_t、argument_index:uint8_t（非参数为0xFF）；FormatResult保存size:size_t及optional<FormatFailure>，失败size=0。没有unmatched_brace、argument_missing/unused、type_mismatch等严格语法错误。

**声明放 `include/qlog/detail/text_formatter.hpp`，定义放 `src/text_formatter.cpp`，命名空间均为qlog::detail。** text_formatter.hpp包含format_spec.hpp和record_types.hpp。

```cpp
[[nodiscard]] FormatResult render_message_utf8(
    const std::byte* format, std::size_t format_size,
    const DecodedArg* args, std::size_t arg_count,
    std::byte* output, std::size_t capacity) noexcept;
```

输入来自decode_v1成功结果，不读取未验证的Ring字段。format、参数字符串与output不重叠；输出scratch失败后允许有临时字节，但不能提交。独立调用时检查null+nonzero、arg_count<=32、format_size<=8192、输出metadata及tag有效性。arg_count=0不得对空args指针做算术；format_size=0不解引用format。有效tag与bytes/byte_count契约按I1保留。

### 3.2 单向scanner和spec

实现辅助函数均放text_formatter.cpp匿名命名空间：CheckedTextWriter、copy_literal_run、parse_format_spec、render_argument、apply_padding；不新建独立format_parser.cpp。

1. 初始arg_count==0：检查容量后原样复制整串并成功返回，不处理任何brace。
2. 有参数：普通byte run批量复制。遇单个'}'原样，遇'}}'输出一个'}'。
3. 遇'{'且仍有参数：从后一个位置起最多看20B；若先遇'{'或窗口内无'}'，只输出当前'{'，随后继续扫描。否则按窗口调用parse_format_spec并消费一个参数。
4. parse_format_spec映射BQLog c20_format的分支顺序：非冒号走默认；index从1开始，超过10终止；两位width/precision收集，不引入32B/4096/64旧上限或严格错误。
5. 解析offset非零按offset+1推进；offset=0仍按已闭合窗口长度推进并输出参数。'{0}'/'{name}'也是顺序参数，不解释索引/名字。
6. 参数耗尽后继续扫描；'{'原样，但'}}'仍折叠。多余参数忽略，不检查总field_count与arg_count相等。

所有输入查看按剩余长度减法限定，窗口上限是常数；每轮至少前进一字节，不递归，不反复全串扫描。首版scalar run+memcpy即可；后续SIMD必须保证不越过剩余输入和输出容量，不能用无保护32B load读短尾。

### 3.3 类型转换、安全适配和padding

按decoder的ArgumentTag分派，无旧严格spec/tag匹配阶段。Bool始终TRUE/FALSE、Char原byte、NullUtf8为null、Utf8String全量复制；空Pointer64为null，非空默认0x加大写hex。16个枚举值中Invalid拒绝，15个有效tag全部实现。

整数按宽度还原signed bits，禁止未对齐typed load和直接负INT64_MIN。保留BQLog signed/unsigned符号差异、二/八/十/十六进制、整数e、prefix和padding；center奇数填充多一个在左侧。不要套用旧标准zero flag组合限制。

F32默认7位/F64默认15位，普通可定义范围保持逐位输出，不改为shortest或标准round。NaN/Inf/越出整数转换范围、负长度和padding移动索引等按[ADR-016§5](./ADR-016-v1-bqlog-worker-format.md#5-安全适配和兼容声明边界)执行安全分支；无法安全完成返回number_conversion_failed。兼容域与安全扩展分别建立测试向量，不能称全输入字节等价。

CheckedTextWriter保存output/capacity/used，每次写先验证 `n <= capacity-used`，成功后才推进used；移动先检查源/目的区间，不能复制BQLog动态扩容算术。输出上限失败size=0，当前scratch整体作废。

### 3.4 接入与完成标准

先独立完成formatter，再接worker。覆盖零参数raw、缺参后转义、多余参数、嵌套左brace、20B/10索引窗口、全部tag关键spec、负整数极值、浮点安全域、NUL和输出边界。输出成功后才可交给各目标compose_line；宽松格式处理不算delivery_failed，容量/安全转换失败才失败。

## 4. worker工作区与完整行组装

### 4.1 V1不建解析缓存

删除原计划中的format_cache.hpp/format_cache.cpp、lookup_or_parse、miss_plan/miss_literal以及cache诊断六项；这些尚未进入生产，本轮没有删除任何生产实现。BackendWorkspace只需32个DecodedArg、有限数值转换临时区和message/line各64KiB。

既有format_hash仍随Record写入，I1算法/ABI不变；text worker不读取hash来查表，也不为hash0回算。未来确需缓存时，必须保留初始零参数与参数游标条件，不能复活提前全串反转义的strict plan；候选比较仍须hash+长度+全部bytes。

直接扫描省去cache查找、比较、拷贝与entry内存，但重复模板会重复扫描；是否更快依赖负载。先完成可验证的BQLog语义与整条正文一次渲染，再凭benchmark决定SIMD或缓存实验。

### 4.2 一次正文，多目标前缀

**声明文件：`include/qlog/detail/text_formatter.hpp`**，在 `qlog::detail` 内定义 `CalendarCache` 并声明§5.6的 `compose_line`；**实现文件：`src/text_formatter.cpp`**。message/line工作区字段位于 `include/qlog/detail/backend_worker.hpp` 的BackendWorkspace内；每目标的CalendarCache成员位于 `include/qlog/detail/appender.hpp` 的Appender内。

每个实际BackendWorker分配 message_scratch[65536] 与 line_scratch[65536]，构造时在堆上作为worker资源，不在每轮栈上创建大数组。
正文 render_message_utf8 每Record最多一次；每个被选择且健康的目标生成元数据前缀、复制正文、追加一个换行到line_scratch，再整体复制到其batch。
这比逐目标重复float conversion更适合fanout；代价是额外64KiB/worker和正文复制。ADR-015明确此变化；完整行上限仍65536。

固定前缀合同：`[YYYY-MM-DDTHH:MM:SS.nnnnnnnnn+HH:MM] [LEVEL] [logger/category] [tid=OS_TID] `。
UTC输出+00:00；level使用TRACE/DEBUG/INFO/WARNING/ERROR/FATAL；timestamp unavailable输出 `[time=unavailable] `。若os_thread_id还为0，在上下文冷创建时获取Linux TID一次；不要用producer_token冒充OS TID。
Logger/category名字按原bytes追加，不做字符串格式解析，嵌入NUL/换行不被隐式截断；本协议不保证业务文本只有一个物理换行。
时区只用固定offset_minutes。time_value先取秒/纳秒，以int64_t加分钟偏移；gmtime_r用于转换偏移后的秒，不修改进程TZ。
每Appender保存“上一秒+时区”的日历前缀缓存及有效标记，只在秒变化时转换；日志时间可能倒退，必须比较相等，不能只检测变大。小数纳秒每Record填9位。
转换失败或完整前缀+正文+换行超限只影响该目标delivery；不得输出截断行。先减剩余容量再加长度，不能依赖size_t溢出后的比较。

## 5. Appender、batch 与 BQLog 风格文件恢复

### 5.1 运行对象与所有权

**基类声明文件：`include/qlog/detail/appender.hpp`；非模板实现文件：`src/appender.cpp`，命名空间 `qlog::detail`。** 下方Appender类、OutputState与基类字段放该头；派生类分别在 `include/qlog/detail/console_appender.hpp`、`include/qlog/detail/text_file_appender.hpp` 声明，并在 `src/console_appender.cpp`、`src/text_file_appender.cpp` 实现。

**I/O结果类型文件：`include/qlog/detail/io_result.hpp`。** `IoWriteResult`、`IoCallResult`、`IoErrorSlot`、`IoReport` 在该头的 `qlog::detail` 内定义，包含 `qlog/management_result.hpp` 使用公共IoFailure；不在Appender头中再定义一份。

Appender放detail空间，Config仍是独立拥有型值。Logger拥有BackendSession，Session持有vector<unique_ptr<Appender>>；发布后只有负责该Session的worker可操作运行对象，detach完成后才能回收。
运行期继承明确保留：ConsoleAppender final : public Appender，TextFileAppender final : public Appender。后续若出现多个文件输出类，可抽取 FileAppenderBase : public Appender，再由 TextFileAppender 继承它；V1 单一文件类型不强制增加中间层。
Appender为不可复制/移动的抽象类，虚析构noexcept；公共非虚方法执行过滤、容量及状态控制，受保护纯虚方法提供输出机制。Producer不做Appender虚调用。

```cpp
class Appender {
public:
    virtual ~Appender() noexcept;
    bool selects(std::uint32_t category_id, std::uint8_t level) const noexcept;
    bool ready_for_record() const noexcept;
    bool accept_line(const std::byte* line, std::size_t size) noexcept;
    void prepare_capacity(IoReport&) noexcept;
    void flush(FlushMode, IoReport&, bool final_attempt = false) noexcept;
    void service_recovery(std::chrono::steady_clock::time_point, IoReport&) noexcept;
    void close(IoReport&) noexcept;
    void apply_compatible_config(AppenderConfig& prepared) noexcept;
protected:
    virtual IoWriteResult write_some(const std::byte*, std::size_t) noexcept = 0;
    virtual IoCallResult sync_output() noexcept = 0;
    virtual IoCallResult close_output() noexcept = 0;
    virtual IoCallResult reopen_output() noexcept = 0;
};
```

内部enum OutputState{active,disk_full,reopen_pending,console_retry,closed}；不要再实现“只能手动reset恢复”的faulted状态。另存sync_failed，它不自动禁止普通行接收。
字段：config_（规范化后的 AppenderConfig，公共字段直接通过 config_.name/enabled/filter/text 读取，作为生效配置的单一来源）、batch_、state_、last_error_、next_flush_deadline、next_recovery_deadline、calendar_cache、sync_failed、预分配错误槽。TextFile另有fd、base_path、current_path、recovery_index、路径工作缓冲；Console只保存标准输出fd选择，不拥有它。
IoWriteResult{written:size_t,error:int}；IoCallResult{error:int}。IoErrorSlot{present:bool,failure:IoFailure,event_count:uint64_t}；IoReport保存预分配slots，每目标open/write/sync/close各一槽，按index×4+stage偏移定位。
名字与output_path字符串在冷准备时预填/预留容量，Backend仅更新已有容量内的路径和标量；用当前目标名字报告，不让外部持有删除后的下标。
selects只做配置过滤。disk_full/reopen_pending仍可能被选中，但接收失败记delivery_failed，不能改记no_destination。
ready_for_record要求active且batch可容纳65536；accept_line只复制完整行，不flush、不open。prepare_capacity只在无Frame时对active目标做容量冲刷；失败进入恢复状态，其他目标照常。
兼容reset用swap替换配置（整个 AppenderConfig 的拥有值，包括未选中的专用字段；当前成员采用默认分配器，实施时确认交换 noexcept）；不关闭fd、不重置恢复状态、不清未写缓存。时区变更清日历缓存，输出周期变更重新安排未来deadline。

### 5.2 OutputBatch 与短写

**类型/接口声明文件：`include/qlog/detail/output_batch.hpp`；非模板实现文件：`src/output_batch.cpp`，命名空间 `qlog::detail`。** 下列缓冲字段属于OutputBatch；驱动write_some并按错误转换Appender状态的flush控制流程实现在 `src/appender.cpp`。

OutputBatch字段storage:unique_ptr<byte[]>、capacity:size_t、used:size_t、written:size_t；未写区间为[written,used)。构造时固定分配，append/free_bytes/pending_bytes/clear均noexcept。
append先检查size<=capacity-used再copy。发生部分写后进入恢复状态，不接收新行；无需每次memmove余量。完整写出后清零used/written。
flush过程：write(storage+written,used-written)；正返回推进written，短写继续；EINTR重试同一后缀。非空请求返回0按EIO处理。任何时候都不能从0重放已写前缀。
下面的恢复规则按错误分类，不把所有errno混成同一结果。

### 5.3 ENOSPC：保存旧缓存，按周期自动重试

**实现位置：`src/appender.cpp` 的flush/service_recovery及状态转换流程；实际文件write位于 `src/text_file_appender.cpp` 的TextFileAppender::write_some。**

这部分直接学习BQLog的disk_full_drop_：

1. write返回ENOSPC，保留written/used，state=disk_full，安排next_recovery_deadline；当前投递先前已经进入batch，delivery_accepted不回撤。
2. disk_full时拒绝新行，继续消费Ring和服务其他目标；固定batch不增长。
3. 周期到达后service_recovery在无Frame时对同一fd、同一未写后缀尝试flush。默认恢复周期100ms，每轮到期至多发起一次恢复过程；过程内仍正确处理短写/EINTR。
4. 仍ENOSPC则重新安排下一次；只有未写字节完全清空且write成功才清disk_full、state=active，恢复接收新行。
5. 设备恢复后无需业务手动reset。retry_interval_us用于限频，不能改成对每条被拒绝Record重试。

BQLog此路径确实保留缓存，但这不是崩溃/掉电保证；QLog V1缓存仍在普通内存。EDQUOT在QLog归容量耗尽同组，这一点是显式扩展；BQLog本地判断直接列出的是ENOSPC。
重试过程中如果变成其他写错误，转入下一节的新文件恢复，剩余字节按下一节记丢弃；不把它继续称为无损恢复。

### 5.4 其他文件写错误：记录损失并自动创建新编号文件

**实现位置：`src/appender.cpp` 负责错误分类与恢复状态；`src/text_file_appender.cpp` 的TextFileAppender::reopen_output负责恢复路径、编号、open/fstat与fd替换。**

BQLog flush_write_cache 的非ENOSPC错误路径会调用open_new_indexed_file_by_name，该函数先close旧文件并clean_cache_write。因此这一类故障不能宣传为“保留全部未写数据”。
QLog对齐其恢复方向并明确损失边界：

1. 对EIO/EBADF等非容量错误，记录当前path/errno/未写suffix字节数，将这些剩余字节计入lost_bytes后clear batch；已经成功write的前缀不重放。
2. close旧fd一次，错误另报；state=reopen_pending。可以立刻尝试一次新文件open，失败则下一恢复周期重试，避免每条日志open风暴。
3. 恢复文件命名为`<base_path>.recovery.<index>`，初始日志仍写配置path；index直接用uint64_t。这是QLog简化命名，不声称逐字复制BQLog日期/保留策略。
4. 冷构造时为base_path+后缀+20位数字预分配路径容量；用to_chars写编号。open使用O_CREAT|O_EXCL|O_APPEND|O_WRONLY|O_CLOEXEC，避免覆盖其他文件；EEXIST递增编号，每次恢复最多试16个，余下下周期继续。
5. 非EEXIST open错误保留同一候选编号供后续周期重试；编号耗尽报告不可恢复，但保持对其他目标服务。open成功、fstat确认普通文件后转active，后续新日志写新文件；旧文件可能留部分行尾。
6. 当前实际路径写入错误报告（预分配容量）与内部状态；不将“新文件可写”冒充原文件数据恢复。

EAGAIN/EWOULDBLOCK采用同fd延后重试，不换新文件；固定batch保留后缀并停止接收，使用console_retry通用暂时重试状态。TextFile仅承诺普通阻塞文件，该分支为异常fd属性/测试注入提供有界行为。
fdatasync失败仅记sync_failed及durability_uncertain，不清缓存、不按普通write错误切文件；后续显式durable请求再尝试sync。普通周期flush不隐式执行fdatasync，避免磁盘同步成本进入常规输出。
只有成功的sync才能清当前sync_failed，历史错误仍可在报告中保留；即使write全成功，sync失败也不能返回durable成功。

### 5.5 flush_due、retire、shutdown如何区别

**实现位置：`src/backend_session.cpp` 负责周期调度、退休与关停编排，调用 `src/appender.cpp` 的flush/service_recovery/close。** RetiredIoSummary的类型归属见§1.3，Session内的累计字段在 `include/qlog/detail/backend_session.hpp` 声明。

正常周期：active且到期则flush；disk_full/console_retry/reopen_pending到恢复期则service_recovery。处理一轮后重排deadline，不能在同一轮因deadline仍过期再次无界重试。
retire和shutdown：不等待磁盘空间恢复；对有旧fd和未写缓存的目标做一次最终flush过程。失败报告/丢弃suffix并close，不为即将删除的目标创建新恢复文件。reopen_pending直接报告未恢复并关闭。
恢复成功不消除历史错误；退休将故障计数、lost_bytes和首个错误move到Session的RetiredIoSummary，避免历史随目标删除而消失。不要重复把同一ENOSPC缓存计入lost_bytes，只有确实丢弃时记。
关闭时仍可能出现部分文件尾；accepted、batch接收、write完成、durable是四个不同边界。

### 5.6 派生类和辅助函数落点

**工厂声明：`include/qlog/detail/appender.hpp` 的 `qlog::detail` 内，放在Appender类之后；工厂定义：`src/appender_prepare.cpp`。** 工厂实现包含两个派生类头，供构造及reset冷准备调用。`compose_line` 和CalendarCache的声明位于 `include/qlog/detail/text_formatter.hpp`，实现位于 `src/text_formatter.cpp`；派生类声明/实现的完整路径见§5.1。

ConsoleAppender保存STDOUT_FILENO或STDERR_FILENO，close_output只逻辑关闭，不close标准fd；sync_output不承诺终端持久化。EPIPE等转console_retry，每恢复周期重试旧后缀，保持有界缓存。
worker线程自身屏蔽SIGPIPE，使write失败成为EPIPE；不改变进程全局handler。
TextFile构造在管理线程分配batch/路径/报告容量，用fd RAII覆盖异常；初次open失败仍使构造/reset准备失败。运行后才适用自动恢复状态机。
TextFile::reopen_output只实现§5.4有限次数候选open；Console::reopen_output不创建文件。write_some调用write，sync_output调用fdatasync（EINTR重试），close_output先置fd=-1再close一次，Linux下不盲目重试close。
内部工厂`std::unique_ptr<Appender> make_appender(AppenderConfig)`仍在构造/管理线程使用。工厂在输入校验后按 config.type 分支创建 ConsoleAppender/TextFileAppender；公共配置由基类处理，派生类仅解析选中的 console/file 配置，返回 unique_ptr<Appender>；移动配置到运行对象，资源由派生运行类拥有。派生类放各自detail头和cpp；正常健康路径零分配，故障路径也复用预备容量。
compose_line签名仍为`FormatResult compose_line(const ChannelCold&, const RecordHeader&, const TimeZoneConfig&, CalendarCache&, const std::byte*, std::size_t, std::byte*, std::size_t) noexcept`；每Appender保存CalendarCache。

## 6. 配置准备与单槽无锁命令交接

### 6.1 为什么采用单槽

**类型/接口声明文件：`include/qlog/detail/control_mailbox.hpp`，命名空间 `qlog::detail`。** 本节CommandState、CommandKind、PreparedReset、PreparedCommand、ControlMailbox都在该头；按依赖先定义PreparedReset、再PreparedCommand、最后ControlMailbox。下方示意将ControlMailbox提前展示，实际排布仍须先声明其所用类型；需要完整Appender类型的非模板析构等实现放 `src/control_mailbox.cpp`。prepare_reset单独在 `src/appender_prepare.cpp` 实现，详见§6.2。

配置是低频控制操作，队列深度不能提高Producer吞吐。单个未完成请求使完成结果、配置版本和资源回收都可有界表示，不引入通用MPSC、promise/future或共享引用计数。
“单管理线程”是外部调用合同：提交与poll只由一个线程顺序调用；要换线程先join/建立同步。任何时刻只有一个提交者，不用CAS忙位替代此约束。

```cpp
enum class CommandState : std::uint8_t { empty, pending, completed };
enum class CommandKind : std::uint8_t { reset_appenders, flush_batches, drain };
struct ControlMailbox {
    alignas(64) std::atomic<CommandState> state{CommandState::empty};
    std::unique_ptr<PreparedCommand> command;
};
```

PreparedCommand字段：kind、request_id:uint64_t、flush_mode、reset_mode、prepared_reset:optional<PreparedReset>、report:预分配IoReport、completion_status。
PreparedReset字段：new_config_snapshot:vector<AppenderConfig>（给管理影子）、changes:vector<AppenderConfig>（给复用目标swap）、reuse_old_index:vector<size_t>（最大值表示新建）、next_appenders:vector<unique_ptr<Appender>>（事先resize）、next_selection:vector<uint8_t>（按新目标数resize）、next_shutdown_report（按新目标预填名字/错误槽）、retired容器、prepared_merged_levels:uint32_t。
retired不需单独分配N个节点：应用时把旧active vector swap进命令容器，已复用槽变nullptr。所有容器容量和错误名字在发布前准备齐。
static_assert atomic<CommandState>::is_always_lock_free；不使用atomic<shared_ptr>。邮箱仍只在所属Session配额边界被读；Producer只可能调用WorkerWakeup，不读取管理邮箱。

### 6.2 管理线程prepare_reset

管理侧保留上次“已领取且applied”后的配置影子，不读取活动Appender对象。初始影子等于规范化后的构造配置。
提交前检查未shutdown及worker未failed/exited；已停止的worker返回SubmitStatus::stopped。再检查state==empty（acquire）；忙时直接返回busy，不做open/分配。单线程协议保证本线程准备期间不会被另一个提交者抢槽。
在 `src/appender_prepare.cpp` 的 prepare_reset 中调用 `qlog::prepare_appender_configs`；只使用返回的拥有值，按§1.4合并一次位图，不重新复制原始输入或再调用prepare_logger_config。每个新目标按稳定 name 匹配影子；先比较 type，再比较 text.batch_bytes，以及 TextFile 的规范化 file.path 或 Console 的 console.stream；未选中的专用字段不参与兼容性比较；兼容且mode为reuse_compatible则记录旧index，否则先创建完整新Appender。
过滤、enabled、时区、flush_interval变化仍兼容；新增batch大小不同必须新建。recreate_all一律新建，供主动重建使用；自动文件恢复不依赖reset。
预留两个快照、目标槽、退休存储、错误槽（旧目标数×4，加新目标相关诊断）、输出报告；失败时销毁局部候选，原状态不变。
request_id从1起，最大值作为耗尽边界，发布前检查；失败不回绕。PreparedCommand全构造后赋到mailbox.command，再state.store(pending,release)，调用负责worker的wakeup.awake()。返回submitted与id，管理线程从此不能碰command内容。

内部接口 `PreparedReset prepare_reset(const AppenderConfig*, std::size_t, std::size_t category_count, const std::vector<AppenderConfig>& acknowledged, ResetMode);`（可抛）在 `include/qlog/detail/control_mailbox.hpp` 声明，在 `src/appender_prepare.cpp` 定义。`void apply_reset(PreparedReset&, IoReport&) noexcept;` 和 `void complete_command(CompletionStatus) noexcept;` 是BackendSession私有成员，在 `include/qlog/detail/backend_session.hpp` 声明、`src/backend_session.cpp` 定义；后者只写结果和release状态。`src/control_mailbox.cpp` 承担邮箱/命令对象的非模板辅助实现，不重复定义prepare_reset。邮箱不实现通用序列化，交接的是拥有型C++对象，不把unique_ptr写进日志Ring。

### 6.3 Backend apply_reset

1. 仅在没有ReadHandle且相关回收已publish时执行；state.load(acquire)==pending后读取command。
2. 校验内部映射没有重复复用索引且所有空槽都有candidate，这属于准备阶段应保证的不变量；不能一边move一边做可失败校验。
3. 对将退休的旧目标做最终普通flush（不创建恢复文件、不等待设备恢复）和close，把error填预留槽；对复用目标不做这两步。
4. 按reuse index把旧unique_ptr移入已resize的next槽；对该实例swap prepared config；新槽已持有准备好的candidate。
5. active_appenders.swap(next_appenders)，同时swap selected工作区与未来shutdown的错误存储；命令的旧vector现在只含退休对象/null槽。所有转移均noexcept，无新内存分配。
6. 由FilterConfigAccess增加私有桥接发布prepared_merged_levels，沿用relaxed；不把Logger category或全部配置伪装成原子事务。新旧混合窗口按ADR-012允许。
7. errors为空→applied，否则applied_with_io_error；state.store(completed,release)。完成发布后Backend不能再读写command，哪怕管理线程还没poll。

旧目标flush失败不阻止切换，剩余suffix在报告后随退休对象销毁；不会自动将旧格式字节写到新文件。被复用的实例保留disk_full/reopen_pending等状态及自动重试安排；无需手动reset才能恢复。
运行期I/O错误采用每目标每stage（open/write/sync/close）固定槽，保留最近errno和首个故障标记；同类重复错误累加有饱和上限的事件数；unwritten_bytes保存当前后缀大小，只有明确丢弃时才累加lost_bytes摘要，不存无界历史。退休时合并到Logger BackendSession的RetiredIoSummary（事件数、丢弃字节、首个故障），首个故障名字用move转移而不是后台分配。最终ShutdownResult增加这个summary，避免目标删除后故障消失；它是功能错误报告，Release保留，不是Producer在线统计。
候选open失败发生在发布前，所以它不需要Backend回滚；已创建空文件不是可回滚的外部事务。

### 6.4 poll_management 与回收

请求id不等于当前有效id返回unknown_request；state acquire为completed则取得独占权；pending且worker正常时返回pending。若pending且worker已failed，必须先acquire观察exited，确认worker放下全部Session/Frame/command借用；退出确认前仍为pending，确认后由唯一管理线程将该请求终结为CompletionStatus::backend_failed，使用预备结果存储并回收命令拥有的资源。不得仅看到failed就抢回command，也不得worker退出后永远pending。提交前检查不排除发布后worker才失败的竞态，故poll必须保留此分支。
用move取出错误结果；reset若applied或applied_with_io_error，用swap更新管理配置影子；销毁已close退休对象和命令，然后state.store(empty,release)。最后返回completed。
不要先写empty再销毁命令，否则Backend可观察下一条请求而旧资源仍在使用；不要在Backend发布completed后继续读取result大小。
未领取完成结果时下一次提交返回busy，避免覆盖。不提供取消pending：取消会新增后台资源所有权竞态。
poll过程中若需要压缩错误vector，复用已预留存储原地压缩，move vector到返回对象，不因格式化错误消息分配。

## 7. BQLog 风格后台：共享/独立 Worker、定时等待与低空间唤醒

### 7.1 分开BackendSession和BackendWorker

**文件归属（类型均在 `qlog::detail`）：**

| 类型 | 声明/类型定义文件 | 非模板实现文件 |
|---|---|---|
| BackendSession | `include/qlog/detail/backend_session.hpp` | `src/backend_session.cpp` |
| BackendWorker、BackendWorkspace | `include/qlog/detail/backend_worker.hpp` | `src/backend_worker.cpp` |
| BackendRuntime、RuntimeNode、Lifecycle | `include/qlog/detail/backend_runtime.hpp` | `src/backend_runtime.cpp` |

RuntimeNode与Lifecycle放在BackendRuntime之前；跨头借用类型使用前置声明，拥有值需要完整类型时包含对应头。下面的字段分别写入表中所属类型，不能全部堆在AsyncLogger头。

本轮对齐BQLog的两种异步模式：`ThreadMode::async`默认共享进程public worker，`ThreadMode::independent`使用专属worker。AsyncLogger不新增同步模式，也不把V2的共享MPSC提前到V1。
必须拆开对象，不能把“线程”与“一个Logger的后台状态”继续写成同一个类：

| 对象 | 必须拥有的字段 | 生命周期 |
|---|---|---|
| BackendSession | Logger稳定metadata/dependencies引用、Context发布头引用、FilterState/mailbox引用、active_appenders、selected、BackendConfig、轮询cursor、stop_mode、stop_requested、draining、retired summary、shutdown报告、诊断 | Logger的Impl独占，worker只借用；detach确认后销毁 |
| BackendWorker | thread、WorkerWakeup、32个DecodedArg、有界数值转换区、message/line各64KiB、startup/exited/failed状态、遍历游标 | async由Runtime拥有；independent由Logger拥有 |
| RuntimeNode | immutable published_next、worker专用active_next、BackendSession* session、原子Lifecycle{published,attached,detached,failed} | 一次注册分配，Runtime统一回收；Session可先销毁 |
| BackendRuntime | atomic<RuntimeNode*> registration_head、worker专用active链、last_discovered_head、public worker、全局退出状态 | 默认实例首次使用时初始化，进程结束或显式全局停机后回收 |

32槽解码区、数值暂存及message/line scratch归worker，当前Session只在consume_quantum期间借用。无缓存键或plan；Session不能保存跨worker调用的工作区指针，也不能保存跨release的Record视图。
每Session公平消费配额按Logger/Channel轮转：记录数64、payload字节256KiB是每次visit上限；还有待处理Channel时保存下一个指针，下轮接续，不能每次从最新head开始。
ProducerContext保留仅消费者写的consumer_faulted冷字段；Appender的自动恢复状态与Ring结构损坏的consumer_faulted完全不同。

### 7.2 Runtime注册、发现和解绑：不用并发owner vector

默认Runtime采用函数内static进行一次冷初始化。标准库初始化保护、分配/线程创建不属于严格lock-free承诺；QLog不自建注册mutex/spinlock。正常Producer不调用Runtime::instance、不增减引用计数。
注册步骤：先完整构造Session、RuntimeNode及全部可能失败的资源；CAS发布到Runtime只增链，next发布后不变；发布成功后节点归Runtime，不能因等待启动失败从管理线程直接delete可见节点。
worker acquire读取registration_head，从新head走到last_discovered_head，只发现本轮新增节点；将它们接入worker私有active链，随后更新last_discovered_head。历史detached节点只留作生命周期锚点，不在每轮日志扫描中遍历。
附着成功后Lifecycle.store(attached,release)，构造线程acquire等待；等待同时检查worker failed/exited。若已发布但尚未附着且worker失败，先acquire确认exited后，由构造控制方清本node的session并标Lifecycle::failed，再回收未运行Session；可见node仍归Runtime，不能删除。失败后拒绝新Session，不自动复活公共worker；发布前检查不能替代发布后的失败复查。构造未返回前没有业务Producer，但共享worker本身可以已运行。
detach必须由负责该Session的worker执行：排空/flush/close完成→从active链摘除→清除任何指向Session的调度游标和node.session→最后release写detached。之后worker永不访问Session及它的结果，Logger acquire看到detached才能回收Context/Session。
RuntimeNode不立即delete，避免发现链/旧head发生ABA或悬挂；最后所有Logger停止、公共worker join后统一删除。该方案按Logger创建次数积累小节点，不积累已销毁Logger的Ring/batch；长期反复创建的成本需记录。
默认Runtime的退出次序要求所有Logger已shutdown，且无并发构造/注册；全局退出才stop/wake/join公共线程，单个Logger析构不能停掉公共worker。
独立模式无需Runtime注册：私有worker借用一个Session，shutdown写完成后线程退出，Logger join后回收。

### 7.3 WorkerWakeup：直接采用BQLog的mutex/CV等待与唤醒

BQLog使用mutex/CV，并在低空间时awake；用户本轮明确要求这部分也对齐，取代此前“不引入任何锁”的绝对约束及futex候选。
**类型/成员声明文件：`include/qlog/detail/worker_wakeup.hpp`；成员实现文件：`src/worker_wakeup.cpp`，命名空间 `qlog::detail`。** 下列带 `WorkerWakeup::` 的签名用于标识成员，实现写在cpp；头文件的类内声明去掉该限定前缀。WorkerWakeup字段为mutex_、condition_variable cv_、单独缓存行上的atomic<bool> waiting_{false}。所有这些状态归一个worker，不是每Channel一套。
waiting=true表示worker已在持mutex状态下准备进入等待；false表示不需要重复唤醒。一个worker等待，多个Producer可awake。

```cpp
void WorkerWakeup::awake() noexcept; // 不等待日志被消费；命中waiting时短暂取得mutex
void WorkerWakeup::wait_for(std::chrono::milliseconds timeout);
```

awake按BQLog的awake_flag_机制：`if (!waiting_.exchange(false, std::memory_order_relaxed)) return;`；命中true后lock_guard取得mutex_，cv_.notify_one()，然后释放mutex。只有一个等待者，BQLog的notify_all在此可等价缩成notify_one。
wait_for：unique_lock取得mutex_→waiting_.store(true,relaxed)→cv_.wait_for(lock,timeout)→waiting_.store(false,relaxed)→解锁返回主循环。CV等待会原子地释放mutex并进入等待；醒来先重新取得mutex。
不能将awake改成“exchange后不拿mutex就notify”：worker设置waiting=true与实际wait之间存在窗口，使用同一mutex使通知者等到wait释放mutex后才notify，避免这个窗口丢通知。
若awake发生在worker还未置waiting=true时，它可以被合并/忽略，最坏由有限66ms周期发现新工作；这和BQLog的定时兜底方向一致，不宣称所有控制命令零等待或事件持久化。任何唤醒/超时/虚假唤醒后都重新检查工作，不能把CV本身当作条件。
relaxed waiting只用于优化唤醒次数，不发布Record或命令：Record靠Ring release/acquire，管理靠邮箱状态，注册靠CAS/acquire。不要通过提高waiting内存序掩盖缺失的数据发布。
worker绝不持这把mutex进行扫描、formatter、write/open/sync、reset或drain；它只覆盖进入等待/发通知的短段。Producer低空间/full路径可能为awake短暂阻塞在mutex，故不能再宣称所有try_log路径都严格无锁/非阻塞。
awake若标准库mutex操作抛system_error，必须在noexcept边界保存wake_failed原子并返回；已commit日志仍是accepted，不能回滚或二次提交，有限超时兜底。worker的wait异常由线程外层标为failed。异常处理不走递归日志。

### 7.4 Producer唤醒接线：只在低空间/full路径

**修改文件：** `include/qlog/detail/channel.hpp` 中的ChannelDependencies增加引用；`include/qlog/detail/spsc_ring_buffer.hpp` 的SpscRingBuffer类内声明producer_low_space、`src/spsc_ring_buffer.cpp` 定义；`include/qlog/async_logger_impl.hpp` 接入full/commit后的通知；`src/async_logger.cpp` 接线依赖。

BQLog本地SISO以近似占用达到容量一半设置low_space_flag，API在low_space或分配失败时awake。QLog沿用这个触发方向，不加入每条无条件通知。
ChannelDependencies追加`WorkerWakeup& wakeup`，在Session选择public/independent worker之后、Context创建前绑定稳定引用。所有Context共享所属worker的WorkerWakeup。
现有16字节WriteHandle和wire布局保持不变。SpscRingBuffer新增仅Producer可调用的`bool producer_low_space() const noexcept;`：

1. 在commit后、无pending reservation时读取writer本地current_write_cursor/cached_read_cursor及capacity。
2. 近似used=current_write_cursor-cached_read_cursor（原Ring无符号游标差合同）；`used >= capacity/2`返回true。cached read可能偏旧，允许保守误唤醒，不声称精确水位；不要为了每条判断强制acquire读取共享read_cursor。
3. reserve返回full：awake一次后返回原full，不重试reserve、不等待空位、不取时；awake可能短暂拿worker mutex。其他reserve内部错误不借通知掩盖。
4. commit成功：如producer_low_space则awake，再返回accepted；通知发生在commit发布后。正常低占用路径只多本地差值比较和分支。
5. validation/filter/context/measure拒绝及encode abort不通知。管理发布、Runtime注册、drain/stop都显式awake对应worker。

高占用/full路径确实新增exchange RMW，只有原waiting=true的调用者进一步拿mutex/通知CV；修订旧“Release所有路径无共享RMW/通知”的绝对承诺。不额外每条轮询worker状态，不把CV通知变成每条固定成本。
本次formatter实现未改Ring/Channel/Producer接线；后续worker实施仍须修改Ring头/cpp、ChannelDependencies构造、AsyncLogger模板等初始化点，不能只添加worker而遗漏生产侧。

### 7.5 worker与Session函数及循环

**声明/实现位置：** 下列BackendSession方法在 `include/qlog/detail/backend_session.hpp` 类内声明、`src/backend_session.cpp` 定义；BackendWorker方法在 `include/qlog/detail/backend_worker.hpp` 类内声明、`src/backend_worker.cpp` 定义；BackendRuntime方法在 `include/qlog/detail/backend_runtime.hpp` 类内声明、`src/backend_runtime.cpp` 定义。

BackendSession函数：`bool consume_quantum(BackendWorkspace&) noexcept`、`process_record(...)`、`service_command()`、`prepare_outputs()`、`flush_due_outputs(now)`、`bool finish_stop() noexcept`。
BackendWorker函数：start/run、discover_sessions、service_round、wait_for_work、join；Runtime函数publish_session、default_instance、shutdown_all（只能全部Logger结束后调用，关闭后不自动重启）。
BackendWorkspace包含worker私有解码工作区与message/line scratch，Session只在consume_quantum期间借用；私有类型可以直接定义于backend_worker.hpp，不增加公共模板复杂度。
worker startup先屏蔽本线程SIGPIPE，release公布ready/failed；构造/注册等待acquire确认，失败要返回错误而非假成功。
正常一轮：发现Session → 每Session一次配额（内部按Channel续接）→发布回收→处理该Session控制命令/stop→周期flush及recovery→下一Session。
已知还有积压则继续公平轮次，不sleep；当所有Session一次完整扫描均空，进入§7.3握手，定时等待默认66ms。批输出/恢复默认检查间隔100ms，worker tick可能使实际完成晚于100ms；没有硬实时延迟承诺。
66ms是BQLog参考默认，不是推荐吞吐基准结论；低流量日志可等一个tick，TextFile batch还可能等输出检查。低空间/控制事件会提前唤醒。删除旧50µs忙轮询默认，不再把它作为本轮基线。
只用steady_clock判断周期/重试deadline；BQLog部分刷新逻辑使用日志epoch，QLog保留单调时钟避免业务时间倒退影响恢复。该差异明确记录。
共享worker减少线程/解码工作区/scratch内存，但同线程慢I/O会拖慢其他Logger；独立模式隔离不同Logger的worker，仍共享OS/设备资源。

### 7.6 消费与Frame生命周期（两种worker模式共用）

Session按有限发布链快照消费各Channel，每次配额后保存后继；整个快照走完才加入新head。持续新注册不能使尾部永远不被访问。
try_read返回empty→publish_reclaimed后继续；corrupted/read_pending→标consumer_faulted、drain_incomplete，不能猜测frame边界跳过，不能关停时无限重试。
ok→作用域清理器持ring引用与ReadHandle，保证恰好release一次；decode_v1用worker32槽，检查category_id范围；Record decode错误只跳过可信外层Frame。
process_record先读取Logger category，再一次选择Appender；selected=0归no_destination；其他归dispatched；正文容量/安全转换失败给全部selected记delivery_failed；每目标前缀/接收失败不影响其他目标。
恢复状态目标仍属于selected但本次delivery_failed；不能因拒绝新行停止消费整个Channel，否则会妨碍其他目标和自动恢复。
正文每Record一次，fanout复制到各batch。退出Frame作用域后视图作废；在任何write/open/reopen/sync/close/等待/命令应用前publish相关Channel的回收。容量足够时按配额批量publish，不强制每Record一次。
开始持Frame之前为健康目标确保最大行空间；不足容量需在无Frame时flush。recovery和新文件open同样只能在Frame释放之后。

### 7.7 失败与可回收边界

正常I/O/格式错误用结果处理，不让异常逃出线程。最外层意外失败公布worker_failed和exited；公共worker失败影响其全部Session，后续管理提交返回stopped，未完成管理请求在退出确认后终结为backend_failed，不再声称排空。
控制方只有在观察worker.exited(acquire)后才可清理未detach的Session；exited发布前worker必须放下所有Session/Frame借用，发布后不能再访问Session。公共线程最终join只由Runtime执行，不能多个Logger竞争join同一thread。
Producer不为worker_failed新增逐条检查，后台故障后可能继续入队至full；shutdown用drain_incomplete揭示未处理记录。唤醒失败不回滚accepted日志。

## 8. drain、shutdown与两种worker模式接线

### 8.1 构造顺序

构造先在src/async_logger.cpp调用 `qlog::prepare_logger_config`，将完成值移动进Impl；配置准备内部的复制与校验复用见§1.4。Impl先准备稳定metadata/filter/clock/policy/hash，再根据ThreadMode取得公共worker或创建独立worker，得到WorkerWakeup引用，然后构造ChannelDependencies、Session、Appender、管理邮箱与影子。
shared worker自身不依赖单个Logger，因此可先运行；Session只有全构造后才能发布。independent线程也必须等其Session就绪后启动，不让线程访问半构造Impl。
初始文件目标open失败仍构造失败；空列表规范化Console。注册发布后的失败由Runtime生命周期协议收尾，不能在异常路径直接delete可见node。
管理影子不读取运行期自动恢复路径：资源兼容判断使用固定base_path等配置身份；current_path/recovery_index属于运行状态，不能拿它与配置path不等就错误重建。

### 8.2 管理与临时drain

第1项单管理线程/单槽邮箱不变，发布pending后必须awake对应worker；不同Logger可由不同管理线程调用，各邮箱仍是SPSC，公共WorkerWakeup是多通知者/单等待者。
reset由BackendSession在无Frame边界应用，保留兼容实例的disk_full/reopen状态、未写缓存与重试期限；变更retry周期只调整后续期限，recreate_all仍作为主动强制替换选项，但不再是唯一恢复方式。
request_flush_batches冲刷已有batch；故障目标可执行一次立即恢复尝试，不无限等磁盘。durable只有write完成后sync成功才算该目标成功。
request_drain要求Producer停止且完成前不恢复写入；Session进入draining，service_command看到同一pending drain且draining已置位时不重新启动；逐轮按配额排空所有Channel后flush，清draining并发布结果。共享worker继续服务其他Session，不在一个drain函数里占住worker等本Logger全部结束。
drain完成不detach Session、不关闭注册；随后允许恢复Producer。Ring已排空而文件仍ENOSPC时返回completed_with_io_error，不把文件恢复成功当成drain永远不返回的条件。

### 8.3 shutdown共用前半段

1. 调用方先停止/join本Logger的全部Producer，结束并发管理活动。close_registration不能阻止已缓存Producer，因此不是Producer join替代物。
2. 处理并领取已有pending/completed管理请求；控制线程可短睡眠等结果，worker在运行；若worker_failed，先等待exited后按失败清理。
3. 复用构造/reset已准备的最终IoReport和RetiredIoSummary，关停清理不能依赖新分配成功。
4. close_registration；先写stop_mode再release置Session.stop_requested，notify其worker。
5. worker按轮次排空本Session，所有Channel最终publish；外层Frame坏则drain_incomplete，不等待故障位置恢复。
6. 本Session每目标最终尝试一次flush/sync/close，停止未来recovery deadline；ENOSPC仍未写出的字节报告并丢弃，reopen_pending不为关闭创建新文件。处理完所有目标，保存结果。

### 8.4 async共享模式：detach，不join公共线程

worker从active链移除RuntimeNode，清Session/Channel调度引用，node.session=nullptr，最后Lifecycle.store(detached,release)。
Logger shutdown acquire等detached后读取自己的最终结果，再回收Context链与Session；RuntimeNode继续归Runtime。shutdown不能调用public_worker.join或修改它的stop标志。
其他Logger照常工作。诊断精确读取在本Session detached后即可，不需要等待整个公共worker停止。

### 8.5 independent模式：退出并join自己的线程

相同排空/输出完成后独立worker退出；Logger join后读取结果、删除Context/Session/worker。只有该模式保留“一Logger一thread join”的操作。
重复shutdown返回第一次保存结果；析构在无并发使用前提下普通shutdown兜底且不能抛异常。显式shutdown用于读取文件/未排空错误。
公共Runtime的shutdown_all是全局终点：前置条件所有Logger已shutdown且无人再构造Logger；stop/wake/join公共worker，删除全部RuntimeNode。默认static析构作同样兜底，不能依赖未定义的跨模块静态析构顺序来停止仍在运行的业务线程。
阻塞I/O无硬超时，不可detach仍借用Session的线程。自动故障恢复不意味关停必须等设备恢复。

## 9. 文件级接线与函数完成清单

### 9.1 对已有文件的具体修改

- async_logger.hpp：include backend_config/management_result；LoggerConfig加backend；加五个管理方法；保留两个try_log、set_category_enabled、close_registration，桥接仍private。
- async_logger_impl.hpp：加Debug出口记账；基础入队顺序保留，full路径和commit后低空间按§7.4调用awake；不每条无条件通知，不加管理邮箱检查或flush。
- src/async_logger.cpp：包含 `qlog/logger_config.hpp`，移出通用配置helper但保留唯一Impl和AsyncLogger非模板成员；构造与位图初始化使用qlog::函数；Impl加管理影子、邮箱、Session与RuntimeNode/独立worker持有状态；按mode注册/启动；析构按共享detach或独立join回收。
- src/logger_config.cpp：§1.4配置迁移已完成；只定义配置准备/校验/合并函数，配套声明在 `include/qlog/logger_config.hpp`，三份跨文件函数声明只维护这一处。
- detail/filter_state.hpp：不新增公共setter；FilterConfigAccess增加publish merged路径给Backend；访问器类的共享定义放 `include/qlog/detail/filter_config_access.hpp`，friend分别授权AsyncLogger/BackendSession使用所需私有桥接，供async_logger.cpp与backend_session.cpp包含，避免两份类定义漂移；不塞入logger_config.hpp。
- detail/producer_context.hpp：加consumer_faulted单消费者字段；producer_context.cpp在冷创建填os_thread_id；ChannelDependencies增加稳定WorkerWakeup引用并修齐所有聚合初始化；SpscRingBuffer增加Producer侧近似低空间查询，不改16B Handle和wire。
- producer_handle.hpp：当前未采用的历史public接口查引用后隔离/移除，不能在demo或新指南又引入绑定步骤。

### 9.2 新函数实现完成标准

| 函数组 | 实现闭合条件 |
|---|---|
| prepare_logger_config / prepare_appender_configs / merge_appender_levels | qlog/logger_config.hpp声明、logger_config.cpp唯一实现；共用原地规范化/校验；各入口不重复复制输入；默认Console与合并规则一致 |
| copy_literal_run / parse_format_spec | 实现ADR-016的UTF-8窗口/顺序规则，不产strict plan或语法失败缓存 |
| render_argument | 覆盖15种有效tag，Invalid拒绝；兼容输出与安全扩展分别验证 |
| render_message_utf8/compose_line | 实际生成字节，失败不进batch，零分配、显式容量 |
| CheckedTextWriter | 每次copy/fill/move均先检查容量；无跨release的Ring视图 |
| Appender/OutputBatch | 虚基类实际被Console/TextFile实现；接收与系统I/O分离 |
| prepare_reset/apply_reset/poll | 提交与完成分开；兼容复用；忙不覆盖；退休对象有回收者 |
| service_round/consume_quantum | Session间及Channel间配额轮转；实际读Ring、release、publish；区分Record坏与Frame坏 |
| service_command | pending drain只启动一次；reset不跨持Frame区间 |
| start/run/shutdown | 公共worker真实运行；共享Logger排空后确认Session脱离，独立Logger排空后join；禁止std::thread::detach或假成功 |

### 9.3 CMake

把表0列出的新cpp加入qlog target；`src/logger_config.cpp` 必须先完成§1.4拆分再添加且仅添加一次，`src/async_logger.cpp` 保留唯一Logger实现。`include/qlog/logger_config.hpp` 由现有include搜索根找到，不增加src头搜索目录；头文件可按工程展示需要列出，不作为独立翻译单元编译。新增 `find_package(Threads REQUIRED)` 和 `target_link_libraries(qlog PUBLIC Threads::Threads)`。
QLOG_ENABLE_DIAGNOSTICS验证AUTO/ON/OFF，用与现有Ring验证相同generator expression方式，但必须PUBLIC传播。统计开关不得改变RecordHeader布局。
新增QLOG_BUILD_EXAMPLES选项，开启时add_executable(qlog_v1_demo examples/v1_logging_demo.cpp)，链接QLog::qlog。只支持Linux输出实现，其他平台明确配置失败，不用空write伪装成功。
9月19日text_formatter.cpp已纳入qlog并通过构建/回归；历史配置迁移与本次验证范围分别见§0及实现报告。后续新增源文件仍须真实编译和链接，不能只检查源列表。

## 10. 完整使用顺序与本轮实现终点

**示例文件：`examples/v1_logging_demo.cpp`，在main及其局部辅助函数内实现以下流程。构建选项与目标定义写入根 `CMakeLists.txt`，见§9.3。**

demo必须覆盖以下真实操作，不能只打印accepted计数：

1. 构造LoggerConfig，设置name/category与两个Appender：console与一个绝对路径TextFile。文件目标使用独立时区偏移；默认UTC，示例可设+480；一个Logger用默认async，另一个用independent，展示共享Logger退出不关闭其他Logger。
2. 创建AsyncLogger，启动业务线程直接 `try_log(0U,LogLevel::info,"value={}",value)`；可统计返回值供将来外部验收，不从后台修改Producer计数。
3. 唯一管理线程准备新目标列表，request_reset_appenders；若submitted，保存uint64_t ticket，定期poll直到completed；检查applied_with_io_error也更新业务所认知的配置。
4. 单纯需要已有batch落write可request_flush_batches；不要把它宣传为所有线程日志屏障。
5. 停止/join业务线程；需要暂时排空后恢复才request_drain并等待；最终退出直接shutdown(buffered或durable)，检查errors/backend_failed/drain_incomplete。
6. shutdown返回后不再调用try_log；随后销毁Logger。不要先析构Logger再join业务线程。

生产实现全部覆盖后，项目状态更新为“V1 实现已补齐，待验收”。无需为了继续实现再拆出下一轮I3/I4设计问答；本文已给出接口、状态、所有权和失败边界。

## 11. 后续完整验收清单（本轮未执行）

后续由Codex按完整实现集中验证，以下不可省略或提前打勾：

- 模板真实消费方与Debug/Release构建；数组长度、约束、无递归；诊断宏跨TU一致。
- 配置模块：构造/reset两个调用方真实链接同一份实现，无重复AsyncLogger定义或未定义引用；构造与reset的空列表默认、非法字段拒绝规则一致；nullptr+0和nullptr+nonzero分开覆盖；显式非法非空category表不被修补。两个值准备入口各自不重复复制输入；Producer调用链不进入配置helper；各列表只在冷准备时合并位图。
- BQ UTF-8零参/有参/耗参状态机、20B与10索引窗口、全部tag关键spec、负整数极值、浮点兼容域与安全扩展、NUL、64KiB边界；不运行旧严格parser/cache预期。
- Ring真实消费解码、多个Channel公平性、运行中新注册、低流量周期输出、release及publish早于慢I/O。
- mailbox busy/未领取/耗尽、反复reset和退休回收；filter-only reset不重开；prepare失败旧配置保留；应用失败返回applied_with_io_error。
- short write/EINTR/ENOSPC/EDQUOT/其他write/open/sync/close注入；磁盘满保后缀、周期恢复后自动接收；非容量错误准确记丢弃并切新编号文件；重试不在单轮无限循环；Console输出捕获/SIGPIPE隔离。
- Producer停止→drain/shutdown→共享Session detach或独立thread join→回收；公共worker继续服务其他Logger、反复attach/detach无悬挂；CV置waiting到wait窗口、timeout/虚假唤醒、关闭与awake寿命；Frame损坏不死循环。
- 正常排空且计数未溢出：calls=filtered+rejected_pre_admission+attempted；attempted=accepted+dropped_full+failed_after_attempt；accepted=processed；processed=decode_failed+no_destination+dispatched；selected_deliveries=delivery_accepted+delivery_failed。
- Release分别检查低占用路径无新增共享RMW/锁、低空间/full路径exchange与mutex/CV成本，不能合并成全路径无锁结论；Backend健康scanner/render/batch无分配。首次TLS、配置和故障恢复单列。

性能比较必须同机同编译器/CPU绑定/Producer数/文本内容/队列预算/丢弃策略/输出设备/持久化边界：BQLog Text对应discard，并分别对齐async/shared与independent；分别记录共享worker资源成本和多Logger干扰。spdlog async对应discard_new和一个worker，同时说明其消息预格式化成本属于Producer。
分别测Producer延迟、无输出测试消费者（不是NullAppender）、单目标格式化、fanout、普通write、durable；报告accepted/full、字节、吞吐、p50/p99/p99.9、CPU、RSS。只按成功调用计吞吐会掩盖drop，必须给出完整分母。
对等待策略以BQLog风格66ms周期/半容量唤醒为基线，分别测稀疏/突发/持续负载、共享/独立和唤醒竞争；缩短周期仅作后续独立实验。batch比较128/256/1024KiB，并检查最大行预留造成的实际批大小。结论以实测为准，不能把“无锁/更少步骤”直接变成“已经超过BQLog/spdlog”。

## 12. 查阅顺序与来源

本指南是当前收尾入口；配置模块的目录、声明、实现与复用统一按[§1.4](#config-module)，早期指南中留在async_logger.cpp匿名命名空间的配置helper示例已由此覆盖；[ADR-015](./ADR-015-v1-backend-control-and-output.md)记录源码对比与新决定，[ADR-010](./ADR-010-v1-backend-c20-format.md)保留hash基线，[ADR-016](./ADR-016-v1-bqlog-worker-format.md)覆盖格式语义与移除V1强制解析缓存，[ADR-014](./ADR-014-v1-bqlog-style-literal-format.md)保留数组入口，[ADR-013](./ADR-013-v1-automatic-producer-context.md)保留TLS/注册合同。
AppenderConfig 以§1.1的平铺字段与枚举选择为准，前次配置组合方案已替换；旧片段仅有 name/type/enabled/filter 时按§1.1补齐 text/console/file。旧指南里NullAppender、显式绑定、Appender管理锁、50µs轮询、故障只手动恢复、尚待讨论的三项，不再作为当前实施步骤。没有实现和验收证据的项不改写为完成。
