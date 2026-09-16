# I2 下一步：修完接入层，完成 runtime Producer 写入

日期：2026-09-15。基于本轮实读 /home/qq344/QLog；生产未验收。本文是实施指南，不是已经修改生产的记录。
生效合同为 ADR-013 与主指南；身份直接使用 std::uint64_t，不引入别名、锁、显式绑定或新队列策略。
本批目标：业务调用 AsyncLogger::try_log，经过过滤和 TLS 接入，把 runtime 格式与两个整数编码进当前线程的 SPSC。
accepted 表示已提交 Ring，不表示已输出到 Console。真实 Backend/Appender 输出属于 I3。

## 1. 最新进度：先完成这一个修正批次

已写正确、保留：producer_identity.hpp 只含两函数声明；Channel 的 category_names 引用、直接 uint64_t 身份、去重复 RegistryView；make_context 的 const view 与 make_unique；install_tls_slot 的两个 last 更新；publish_context；ContextResult。

| 精确位置 / 当前内容 | 下一步写法与原因 |
|---|---|
| src/producer_context.cpp / ThreadRegistry::last_context | 当前仍为 `ProducerContext last_context{nullptr};`；改为 `ProducerContext* last_context{nullptr};`。TLS 借用节点，不按值创建 Context |
| take_id 的 while 条件 | 当前使用 `numeric_limits<size_t>::max()`；统一改为 `numeric_limits<std::uint64_t>::max()`。编号宽度由 uint64_t 决定，不能依赖 size_t 与其恰好相等 |
| prepare_tls_slot 首条件 | 改为 `registry.entries.size() == registry.entries.max_size()`；现有 size > size_t 最大值判断不成立 |
| acquire_thread_context / if (!slot) | 容量失败 return 后缺 `}`；先闭合该 if，再开始 ensure_thread_token 分支，二者不是嵌套 |
| token 耗尽分支 | 当前 rollback_tls_slot(registry) 少实参；改为 `rollback_tls_slot(registry, *slot)`，此时槽必须已准备成功 |
| src/async_logger.cpp / dependencies 初始化尾 | 六项内容已正确，但 `},` 后直接接函数体；删除最后一个逗号，形成 `dependencies{...} {}` |
| Impl 析构 | 当前仍为 `delete *node;`；改为 `delete node;`。保留 delete 前保存 next 的顺序 |
| async_logger.hpp | context_result.hpp 已直接 include；公共 LogResult 仍未定义。按 §2 恢复公共结果头并 include；两个桥接声明移到 private |
| 直接依赖 | channel.hpp 补 utility；async_logger.cpp 补 atomic，删除未使用 mutex include |

acquire_thread_context 的 try 前半段应完整写成下面这样，再接现有 make_context/publish/install/success 与 catch：

```cpp
std::optional<std::size_t> slot;
try {
    slot = prepare_tls_slot(registry);
    if (!slot) {
        return ContextResult::failure(ContextError::tls_capacity_exhausted);
    }
    if (!ensure_thread_token(registry)) {
        rollback_tls_slot(registry, *slot);
        return ContextResult::failure(ContextError::producer_token_exhausted);
    }
    // 接现有的创建、发布、安装和成功返回；完整函数对照主指南 §12.4。
```

该片段是插入位置说明，不是可以独立编译的完整 try 语句。catch bad_alloc 仍只在 slot 有值时回滚；发布后禁止分配与回滚。
本批结束条件：这些 cpp 能实际编译链接，不能仅看到编辑器没有红线。下面类型/模板完成后，按 §5 接构建验证。

## 2. 恢复 include/qlog/log_result.hpp：公共返回值完整参考

ContextResult 解决内部“拿到 Context 还是接入失败”；LogResult 解决业务“提交、过滤还是哪一阶段拒绝”。两者不是互相替换关系。
创建公共头，async_logger.hpp 同时 include 公共结果头与内部 context_result.hpp。不要再把 ContextError 放回公共结果头。

<!-- check:runtime-log-result -->
```cpp
#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace qlog {
enum class LogStatus : std::uint8_t {
    accepted, filtered, invalid_level, invalid_category, invalid_input,
    too_large, full, internal_error, resource_exhausted,
    registration_closed, identity_exhausted,
};
enum class FailureStage : std::uint8_t {
    validation, measure, reserve, encode, context,
};
enum class FailureReason : std::uint8_t {
    invalid_level, invalid_category,
    invalid_limits, invalid_format_metadata, invalid_string_metadata,
    format_too_large, argument_length_out_of_range, args_length_out_of_range,
    size_overflow, record_length_out_of_range, payload_too_large,
    full, reservation_pending,
    invalid_destination_metadata, destination_size_mismatch, unknown_flags,
    reserved_timestamp_status, invalid_time_value, fallback_timestamp_not_configured,
    context_allocation_failed, tls_capacity_exhausted,
    registration_closed, producer_token_exhausted,
};
struct LogFailure {
    FailureStage stage;
    FailureReason reason;
    std::size_t argument_index{0xFFU};
    std::size_t byte_count{0};
};
namespace detail { struct LogResultAccess; }
class LogResult final {
public:
    [[nodiscard]] LogStatus status() const noexcept { return status_; }
    [[nodiscard]] bool accepted() const noexcept {
        return status_ == LogStatus::accepted;
    }
    [[nodiscard]] const LogFailure* failure() const noexcept {
        return failure_ ? &*failure_ : nullptr;
    }
private:
    friend struct detail::LogResultAccess;
    LogResult(LogStatus status, std::optional<LogFailure> failure) noexcept
        : status_(status), failure_(failure) {}
    LogStatus status_;
    std::optional<LogFailure> failure_;
};
namespace detail {
struct LogResultAccess final {
    [[nodiscard]] static LogResult make_accepted() noexcept {
        return LogResult(LogStatus::accepted, std::nullopt);
    }
    [[nodiscard]] static LogResult make_filtered() noexcept {
        return LogResult(LogStatus::filtered, std::nullopt);
    }
    [[nodiscard]] static LogResult make_failed(LogStatus status,
                                              LogFailure failure) noexcept {
        assert(status != LogStatus::accepted && status != LogStatus::filtered);
        return LogResult(status, std::optional<LogFailure>{failure});
    }
};
}  // namespace detail
}  // namespace qlog
```

三个查询只读：status 返回分类；accepted 判断是否提交成功；failure 返回对象内失败信息的借用指针，不能超过结果对象寿命使用。
私有构造和三个工厂保证 accepted/filtered 不携带失败信息；make_failed 必须保存完整四字段，不自己输出日志或执行回滚。
不增加默认“成功”构造，不恢复 invalid_handle；这批没有 public 绑定句柄。

## 3. 新建 detail/producer_result_map.hpp：四个函数逐项实现

文件直接 include cassert、exception、qlog/log_result.hpp、context_result.hpp、record_measure.hpp、record_types.hpp、spsc_ring_buffer.hpp。
所有定义在 namespace qlog::detail，标记 `[[nodiscard]] inline` 和 noexcept。因为模板跨多个翻译单元使用，不能在头中放非 inline 普通函数定义。

四个签名：

```cpp
[[nodiscard]] inline LogResult map_context_error(ContextError error) noexcept;
[[nodiscard]] inline LogResult map_measure_failure(const MeasureFailure& failure) noexcept;
[[nodiscard]] inline LogResult map_reserve_error(ReserveStatus status,
                                                std::size_t requested) noexcept;
[[nodiscard]] inline LogResult map_encode_error(EncodeError error,
                                               std::size_t target_size) noexcept;
```

具体实现法：每个函数 switch 输入枚举，每个 case 立即调用 LogResultAccess::make_failed，填下面表中的 status、stage、reason、index、bytes。
不要根据两个枚举恰好同序而 static_cast。switch 后 `assert(false); std::terminate();` 处理不可能的内部枚举；与现有 I1 encoder 的做法一致，不制造一个合法失败原因。
ReserveStatus::ok 在错误映射中同样断言并 terminate，正确调用者仅在 reserve 失败后调用。

### 3.1 map_context_error

全部 stage=context、index=0xFF、bytes=0。

| ContextError | LogStatus | FailureReason |
|---|---|---|
| allocation_failed | resource_exhausted | context_allocation_failed |
| tls_capacity_exhausted | resource_exhausted | tls_capacity_exhausted |
| registration_closed | registration_closed | registration_closed |
| producer_token_exhausted | identity_exhausted | producer_token_exhausted |

例子：`return LogResultAccess::make_failed(LogStatus::resource_exhausted, LogFailure{FailureStage::context, FailureReason::context_allocation_failed, 0xFFU, 0});`。
本函数不 pop TLS、不 delete 节点，资源清理已经在 acquire_thread_context 内完成。

### 3.2 map_measure_failure

全部 stage=measure；index=failure.argument_index，bytes=failure.byte_count 原样保留。switch 的输入是 failure.error。

| MeasureError | LogStatus | FailureReason |
|---|---|---|
| invalid_limits | internal_error | invalid_limits |
| invalid_format_metadata | invalid_input | invalid_format_metadata |
| invalid_string_metadata | invalid_input | invalid_string_metadata |
| format_too_large | too_large | format_too_large |
| argument_length_out_of_range | too_large | argument_length_out_of_range |
| args_length_out_of_range | too_large | args_length_out_of_range |
| size_overflow | too_large | size_overflow |
| record_length_out_of_range | too_large | record_length_out_of_range |
| payload_too_large | too_large | payload_too_large |

九项全部覆盖。上下文的 quota 已经验证，invalid_limits 是内部配置不变量被破坏；普通输入元数据错误才是 invalid_input。

### 3.3 map_reserve_error

全部 stage=reserve、index=0xFF、bytes=requested。

| ReserveStatus | LogStatus | FailureReason |
|---|---|---|
| full | full | full |
| payload_too_large | internal_error | payload_too_large |
| reservation_pending | internal_error | reservation_pending |

measure 使用同一个 quota，之后 reserve 报 payload_too_large 表示接线不变量错误；不能再次当成普通输入超长。
reserve 失败没有本次有效 reservation，映射函数与调用者都不 abort。

### 3.4 map_encode_error

全部 status=internal_error、stage=encode、index=0xFF、bytes=target_size。
七种 EncodeError 分别映射同名 FailureReason：invalid_destination_metadata、destination_size_mismatch、invalid_level、unknown_flags、reserved_timestamp_status、invalid_time_value、fallback_timestamp_not_configured。
其中 invalid_level 的 stage 是 encode，与前置 validation 的同名原因区分。调用者先 abort 再调用映射；本函数不碰 Ring。

## 4. 新建 detail/async_logger_impl.hpp：runtime try_log 完整参考

先完成 §2/§3，再添加此头；async_logger.hpp 保留 try_log 声明，文件最末在类与 namespace 都结束后 include 本实现头。
实现头不再 include async_logger.hpp，避免循环；它依赖入口头已完整定义 AsyncLogger。不要把模板放进 cpp，否则其他消费方的参数实例无法链接。
两个私有桥接仍在 async_logger.cpp 中实现，模板不访问不完整的 Impl。

<!-- check:runtime-try-log -->
```cpp
#pragma once
#include <cassert>
#include <exception>
#include <utility>
#include "qlog/detail/producer_context.hpp"
#include "qlog/detail/producer_result_map.hpp"
#include "qlog/detail/record_encoder.hpp"
#include "qlog/detail/record_measure.hpp"

namespace qlog {
template <typename... Args>
    requires(sizeof...(Args) <= detail::kMaxArgCount) &&
            (detail::SupportedArgument<Args> && ...)
LogResult AsyncLogger::try_log(std::uint32_t category_id, LogLevel level,
                               FormatView format, Args&&... args) const noexcept {
    switch (classify_call(category_id, level)) {
    case detail::CallGate::invalid_level:
        return detail::LogResultAccess::make_failed(LogStatus::invalid_level,
            LogFailure{FailureStage::validation, FailureReason::invalid_level, 0xFFU, 0});
    case detail::CallGate::invalid_category:
        return detail::LogResultAccess::make_failed(LogStatus::invalid_category,
            LogFailure{FailureStage::validation, FailureReason::invalid_category, 0xFFU, 0});
    case detail::CallGate::filtered:
        return detail::LogResultAccess::make_filtered();
    case detail::CallGate::proceed:
        break;
    default:
        assert(false);
        std::terminate();
    }

    const auto acquired = acquire_context_for_thread();
    auto* context = acquired.context();
    if (context == nullptr) {
        assert(acquired.error() != nullptr);
        return detail::map_context_error(*acquired.error());
    }
    auto& channel = context->channel_;
    auto& ring = channel.ring_;
    const auto& dependencies = channel.cold_.dependencies;
    const detail::FormatInput input{format.data(), format.size(), format.stored_hash()};
    const auto measured = detail::measure_record(
        input, channel.cold_.payload_quota, std::forward<Args>(args)...);
    if (!measured.succeeded()) {
        return detail::map_measure_failure(*measured.failure());
    }
    const auto& prepared = *measured.prepared();
    auto write = ring.try_reserve(prepared.payload_size());
    if (!write) {
        return detail::map_reserve_error(write.status(), prepared.payload_size());
    }

    const auto timestamp = detail::sample_admission_timestamp(dependencies.clock);
    const detail::RecordMetadata metadata{
        timestamp.time_value, category_id, static_cast<std::uint8_t>(level), timestamp.flags};
    const auto encoded = detail::encode_v1(write.data(), write.size(), prepared,
                                           metadata, dependencies.policy,
                                           dependencies.hash_dispatch);
    if (!encoded.succeeded()) {
        const auto target_size = write.size();
        ring.abort(write);
        return detail::map_encode_error(*encoded.failure(), target_size);
    }
    ring.commit(write);
    return detail::LogResultAccess::make_accepted();
}
}  // namespace qlog
```

函数各阶段的实际职责：

1. classify_call 只读取一次过滤；invalid/filtered 在接触 TLS 之前返回，不额外调用 filter。
2. acquire_context_for_thread 返回当前线程/Logger 的 Context，首次通过过滤可能分配。失败立即转换，不 measure、不 reserve。
3. FormatInput 只借用指针和长度；runtime stored_hash=0，让 encoder 计算，Producer 不重复计算。参数只 forward 给 measure 一次。
4. measured 局部对象保存参数规范化结果并活到 encode 结束，prepared 是它的借用引用。不要从临时 MeasureResult 取引用，也不二次扫描 cstr。
5. try_reserve 只调用一次；失败返回映射结果。成功后才调用 sample_admission_timestamp。
6. 时钟不可用也携带 time_unavailable flags 继续编码；不在取时失败处遗弃 reservation。metadata 四项按实际字段顺序构造。
7. encode 成功 commit 一次；失败先保存目标长度，再 abort 一次。当前句柄可能被 abort 修改，因此错误报告的数据先保存。
8. 返回 accepted 仅表示已提交。不要在函数里调用 Appender、字符串格式化输出或 Flush。

这是 runtime 功能切片，没有接入 Debug 诊断计数、literal 预计算与 Backend。诊断仍按主指南 §7 作为后续必做项，不因此声称 I2 完成。
本批未增加新配置，也无需决定 V2 频率策略。runtime 先走通后，再对 literal 工厂的具体 API 单独细化，不在这里虚构已实现的入口。

## 5. 构建接线：修改根 CMakeLists.txt 的现有 qlog target

当前 add_library(qlog STATIC ...) 只有 I1 源文件，新 I2 cpp 尚未接入。保留已有项，在同一列表补：

```cmake
    src/filter_state.cpp
    src/admission_clock.cpp
    src/producer_context.cpp
    src/async_logger.cpp
```

producer_identity 两个函数定义在 producer_context.cpp，不要凭空添加 producer_identity.cpp。Channel 头内定义，无需空 channel.cpp。模板头不当作编译源。
不要创建第二个 qlog 库绕过现有配置。诊断宏 PUBLIC 的接线另按主指南完成，不把现有 Ring 宏随意改成 Producer 宏。
这批以 WSL Linux 构建为准，admission_clock 当前为 POSIX 实现；不能将结果宣称为原生 Windows 支持。

维护者完成上述代码后交接验证的步骤：

1. 在仓库目录配置新的 Debug 构建目录，例如 `cmake -S . -B build/i2-runtime-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON`。
2. `cmake --build build/i2-runtime-debug -j`。修完当前批编译错误，不屏蔽 warnings，也不注释错误分支伪装成功。
3. `ctest --test-dir build/i2-runtime-debug --output-on-failure`。既有测试通过只证明已有覆盖未回归；新增消费方与行为验证见 §6。
4. 下一轮验证用最小消费方链接 QLog::qlog，真实调用下面的模板，不以“模板声明能解析”结束。

```cpp
qlog::LoggerConfig config;
config.name = "i2-runtime";
qlog::AsyncLogger logger(config);
const auto result = logger.try_log(
    0U, qlog::LogLevel::info, qlog::runtime_format("x={} y={}", 9U), 12, 34);
```

模板支持受 I1 SupportedArgument 约束。示例格式长度9，不含末尾 NUL。此消费方只做一次提交，在析构前无其他线程使用 Logger。
没有 Backend 时反复运行循环会填满 Ring，这是预期背压，不是“日志已经打印”。

## 6. 本批结束条件与下一次交接材料

| 检查 | 通过条件 |
|---|---|
| 公共头与真实调用 | 单独 include async_logger.hpp 的消费方可编译并链接；runtime 两整数模板实际实例化 |
| 四个 mapper | 每个合法失败枚举的 status/stage/reason 正确，measure 保留 index/bytes；不漏七种 EncodeError |
| 前置拒绝 | 非法 level/category、filtered 不创建 Context/Ring；context 失败不走 measure/reserve/clock |
| 正常写入 | I2 测试消费者读 Ring 并复用 I1 decoder，核对格式字节、两个参数、category/level 和时间 flags |
| 背压/回滚 | full 不取时间、不 abort；encode 失败一次 abort，之后正常调用仍可提交 |
| TLS | 同线程重复命中；A→B→A→A 正确；无并发使用时两种退出顺序安全 |

读取 Logger 内部 Ring 的测试入口由后续测试实现选择，不新增面向业务的 public raw Context getter。生产骨架完成后再接测试访问，不为当前指南冻结 Backend 扫描 API。
交接时说明完成了 §1～§5 哪些文件，并附第一条完整构建错误或成功输出；下一轮据实际状态完成验证与诊断/literal 的下一切片。
不是要求维护者先写完完整测试框架；这张表定义后续 Codex 验证范围，避免把单次返回 accepted 当成整个 I2 验收。

## 7. 哪些需要商榷，哪些现在可以继续

本批类型恢复、四种错误映射、runtime 调用顺序、TLS 回滚和 I1 复用均已有合同，不需要再次确认。
Appender 运行期配置的无锁命令交接、文件 reset 失败处理、V2 SPSC/MPSC 切换时的跨队列顺序尚未确定，进入对应阶段前先讨论，不借本次指南冻结。
本轮只更新指南；参考核验结果见 [runtime 下一步指南核验](./I2_RUNTIME_NEXT_GUIDE_VALIDATION_20260915_CHS.md)。
