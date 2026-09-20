# Appender 更新后：下一步实现指南（2026-09-20）

> 后续更新：完整行已进入生产，本文件的追加代码保留为学习参考，不要重复粘贴。当前下一步见 [配置冷准备与邮箱指南](./V1_CONTROL_PREPARATION_NEXT_GUIDE_20260920_CHS.md)。

本指南对应当前 `/home/qq344/QLog`。本轮已直接补齐输出层，下面的下一阶段是 `compose_line`，不是再补 Appender 占位函数。V1 总顺序仍以 [总指南](./V1_REMAINING_CODE_IMPLEMENTATION_GUIDE_CHS.md) 为准。

## 1. 本轮完成的实际文件

| 文件 | 可直接阅读的实现 |
|---|---|
| `src/output_batch.cpp` | append/consume/discard 的偏移不变量 |
| `src/io_report.cpp` | 所有目标冷准备、固定阶段槽、首次 errno/path、max unwritten、一次性导出 |
| `src/appender.cpp` | write_all、flush、最终关闭、定时恢复、配置交换、历史首错与累计 |
| `src/console_appender.cpp` | 共用 gate、一次 syscall、明确长度、借用 stdout/stderr |
| `src/text_file_appender.cpp` | 初次打开、普通文件验证、fd 所有权、恢复名、16 次上限 |
| `src/appender_factory.cpp` | 已规范化配置的冷工厂与异常边界 |
| `tests/appender_test.cpp` | 基类脚本故障测试 |
| `tests/appender_posix_test.cpp` | 真实临时文件/管道、链接包装注入、C++ 分配计数 |

`write_some` 已统一为 `write`。`CalendarCache.local_second` 拼写已修正。`render_message_utf8` 恢复原有六参数正文接口；此前将行层参数塞进正文声明会破坏既有调用者。

本轮恢复计时采用 TextFile 的 retry_interval_us；Console 无此配置项，使用独立 100ms 常量。初次文件 open 增加 O_NONBLOCK，仅用于避免配置 FIFO 时阻塞在 fstat 之前；最终只接受普通文件。恢复名从 `.recovery.0` 起递增。

## 2. 先理解已实现函数的调用关系

```text
Session 借用 Record 前：
    selects → ready_for_record → 必要时 flush
借用期间：
    decoder → render_message_utf8 → compose_line → accept_line
释放 Record 后：
    service / 管理 flush / final_close

flush → flush_impl → write_all → 派生 write → 一次 ::write
durable 文件：write_all 完成 → 一次 fdatasync
永久错误：记录 → 实际丢弃 → close → reopen_pending
到期恢复：reopen_output → 至多 16 个名字 → 返回
退休：final_close → capture_first_error → merge_history_into
```

`note_io_failure` 是派生恢复方法报告实际 open/fstat/close 失败的唯一入口。open/fstat 使用 IoStage::open、blocking=true；临时 fd 清理 close 使用 IoStage::close、blocking=false。blocking 只决定是否替换当前阻塞输出的原因，不决定是否统计事件。

`sync_error_` 与 `last_error_` 分离。buffered 不解除同步故障；buffered 的字节输出可 completed，但 report 仍带未解除的 sync，最终管理结果必须依据 report.has_errors 判定。只有成功 durable 或切换到新的输出目标才解除活动同步状态；旧错误仍保留在历史。

Session **每次目标操作后立即**调用：

```cpp
appender.capture_first_error(session.retired_io);
```

这会将首错 string 移到 Session，避免按目标退休次序错误选择首错。所有 capture/merge 必须使用同一 Session 累计对象，直到 shutdown 结束才移出给调用者。`merge_history_into` 只能在 closed 后调用，负责 event_count/lost_bytes，并且幂等；不再为首错重新分配缓冲。

`IoReport` 禁止复制；允许 move。clear 仅适用于未 take 的报告；take_errors 后只能销毁或以新的 prepare 结果替换。后台周期报告与管理请求报告分开，不能把已恢复的旧错误写入后来请求。

## 3. 下一步只做完整行层：文件和顺序

按以下顺序修改；不需要修改现有正文函数签名，也不需要启动 worker。

1. `include/qlog/detail/text_formatter.hpp`：在正文声明后新增下面的 compose_line 声明。
2. `src/text_formatter.cpp`：在文件最后追加 §4 的完整代码；与正文共享该翻译单元已有 CheckedTextWriter/make_success/make_failure，不重复定义 writer。
3. `src/producer_context.cpp::make_context`：把 ChannelCold.os_thread_id 的常量 0 替换为一次冷路径 gettid，见 §5。
4. 新建 `tests/compose_line_test.cpp`，按 §6 验收。此阶段不修改 AsyncLogger 的构造/析构或启动后台。
5. 上述通过后才进入 PreparedReset/ControlMailbox。不要把单槽邮箱、Session、worker 和析构拆成可以被误用的半接线。

头文件声明：

```cpp
[[nodiscard]] FormatResult compose_line(
    const ChannelCold& channel, const RecordHeader& header,
    const TimeZoneConfig& time_zone, CalendarCache& cache,
    const std::byte* message, std::size_t message_size,
    std::byte* output, std::size_t capacity) noexcept;
```

`FormatError::time_conversion_failed` 你已追加，保留，不要再添加同名值。`CalendarCache` 也已存在，不要重复定义。

## 4. compose_line 完整参考代码

以下代码没有大写占位 helper，也没有未定义的函数调用。依赖的已有函数位于同一个 `text_formatter.cpp`。独立副本见 [可追加代码](./examples/compose_line_next_step_20260920.inc)。本轮只对拼接后的翻译单元做编译检查；未接入生产库，也未完成完整行运行时验收。

```cpp
// Append this block at the END of src/text_formatter.cpp.
// This is a next-step reference, not yet part of the qlog library.
#include <ctime>
#include <string_view>
#include <utility>

namespace qlog::detail {
namespace {
bool line_fixed_decimal(CheckedTextWriter& writer, std::uint32_t value,
                        unsigned digits) noexcept {
    char buffer[10];
    if (digits == 0U || digits > sizeof(buffer)) return false;
    for (unsigned i = digits; i != 0U; --i) {
        buffer[i - 1U] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    }
    return value == 0U && writer.append_bytes(buffer, digits);
}

std::optional<FormatError> line_timestamp(
    CheckedTextWriter& writer, const RecordHeader& header,
    const TimeZoneConfig& zone, CalendarCache& cache) noexcept {
    if ((header.flags & kTimestampStatusMask) ==
        static_cast<std::uint8_t>(TimestampStatus::time_unavailable)) {
        constexpr std::string_view unavailable = "[time=unavailable]";
        return writer.append_bytes(unavailable.data(), unavailable.size())
            ? std::nullopt : std::optional{FormatError::text_output_limit_exceeded};
    }
    constexpr std::uint64_t billion = 1000000000ULL;
    const auto seconds = static_cast<std::int64_t>(header.time_value / billion);
    const auto local = seconds + static_cast<std::int64_t>(zone.offset_minutes) * 60;
    if (!cache.valid || cache.local_second != local ||
        cache.offset_minutes != zone.offset_minutes) {
        if (!std::in_range<std::time_t>(local)) return FormatError::time_conversion_failed;
        const auto time = static_cast<std::time_t>(local);
        std::tm calendar{};
        if (::gmtime_r(&time, &calendar) == nullptr ||
            calendar.tm_year < -1900 || calendar.tm_year > 8099)
            return FormatError::time_conversion_failed;
        std::array<char, 19> text{};
        CheckedTextWriter temporary{reinterpret_cast<std::byte*>(text.data()), text.size()};
        if (!line_fixed_decimal(temporary, static_cast<std::uint32_t>(calendar.tm_year + 1900), 4) ||
            !temporary.append_char('-') ||
            !line_fixed_decimal(temporary, static_cast<std::uint32_t>(calendar.tm_mon + 1), 2) ||
            !temporary.append_char('-') ||
            !line_fixed_decimal(temporary, static_cast<std::uint32_t>(calendar.tm_mday), 2) ||
            !temporary.append_char('T') ||
            !line_fixed_decimal(temporary, static_cast<std::uint32_t>(calendar.tm_hour), 2) ||
            !temporary.append_char(':') ||
            !line_fixed_decimal(temporary, static_cast<std::uint32_t>(calendar.tm_min), 2) ||
            !temporary.append_char(':') ||
            !line_fixed_decimal(temporary, static_cast<std::uint32_t>(calendar.tm_sec), 2))
            return FormatError::time_conversion_failed;
        cache.calendar = text;
        cache.local_second = local;
        cache.offset_minutes = zone.offset_minutes;
        cache.valid = true;
    }
    const int offset = zone.offset_minutes;
    const auto magnitude = static_cast<std::uint32_t>(offset < 0 ? -offset : offset);
    if (!writer.append_char('[') ||
        !writer.append_bytes(cache.calendar.data(), cache.calendar.size()) ||
        !writer.append_char('.') ||
        !line_fixed_decimal(writer, static_cast<std::uint32_t>(header.time_value % billion), 9) ||
        !writer.append_char(offset < 0 ? '-' : '+') ||
        !line_fixed_decimal(writer, magnitude / 60U, 2) ||
        !writer.append_char(':') ||
        !line_fixed_decimal(writer, magnitude % 60U, 2) ||
        !writer.append_char(']'))
        return FormatError::text_output_limit_exceeded;
    return std::nullopt;
}
} // namespace

FormatResult compose_line(
    const ChannelCold& channel, const RecordHeader& header,
    const TimeZoneConfig& time_zone, CalendarCache& cache,
    const std::byte* message, std::size_t message_size,
    std::byte* output, std::size_t capacity) noexcept {
    if (output == nullptr && capacity != 0U)
        return make_failure(FormatError::invalid_workspace, 0U);
    if ((message == nullptr && message_size != 0U) ||
        time_zone.offset_minutes < -840 || time_zone.offset_minutes > 840)
        return make_failure(FormatError::invalid_arguments, 0U);
    const auto status = header.flags & kTimestampStatusMask;
    if (header.category_id >= channel.dependencies.category_names.size() || header.level >= 6U ||
        (header.flags & static_cast<std::uint8_t>(~kKnownFlagMask)) != 0U || status == 3U ||
        (status == 2U && header.time_value != 0U) ||
        (status == 1U && !channel.dependencies.policy.fallback_timestamp_allowed()))
        return make_failure(FormatError::invalid_format_metadata, 0U);
    if (message_size > kMaxMessageBytes)
        return make_failure(FormatError::text_output_limit_exceeded, 0U);

    CheckedTextWriter writer{output, std::min(capacity, kMaxMessageBytes)};
    if (const auto error = line_timestamp(writer, header, time_zone, cache))
        return make_failure(*error, 0U);
    constexpr std::array<std::string_view, 6> levels{
        "TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "FATAL"};
    const auto append = [&writer](std::string_view text) noexcept {
        return writer.append_bytes(text.data(), text.size());
    };
    const auto& logger = channel.dependencies.logger_name;
    const auto& category = channel.dependencies.category_names[header.category_id];
    char tid[20];
    const auto converted = std::to_chars(tid, tid + sizeof(tid), channel.os_thread_id);
    if (converted.ec != std::errc{}) return make_failure(FormatError::number_conversion_failed, 0U);
    if (!append(" [") || !append(levels[header.level]) || !append("] [") ||
        !append(logger) || !append("/") || !append(category) || !append("] [tid=") ||
        !writer.append_bytes(tid, static_cast<std::size_t>(converted.ptr - tid)) ||
        !append("] ") || !writer.append_bytes(message, message_size) || !writer.append_char('\n'))
        return make_failure(FormatError::text_output_limit_exceeded, 0U);
    return make_success(writer.used);
}
} // namespace qlog::detail
```

实现说明：

- 纳秒无符号除以十亿后再转 int64，加有符号时区；`UINT64_MAX` 纳秒除后的秒数也可表示。
- 缓存键为 local_second 与 offset_minutes 的相等比较；时钟回退仍然刷新。只缓存 19 字节日期秒，不缓存纳秒。
- 先验证 time_t 范围，再 gmtime_r；已经手动加时区，不调用 localtime_r。
- epoch 0 是合法 1970 年时间；只有 flags 标记 unavailable 才输出 `[time=unavailable]`。
- 正文、名字、类别按明确字节长度复制，保留 NUL 和换行。输出最后增加一个 LF，不额外写终止零。
- output 和 message 必须是不重叠的 scratch；调用失败时 size=0，丢弃整个输出 scratch，不交付部分行。
- 固定小整数除数交由编译器优化，不自行替换为未经验证的倒数乘法。

## 5. OS tid 冷采样的准确修改

当前 `src/producer_context.cpp::make_context` 的聚合初始化第三项仍为 0，`ChannelCold` 已有 os_thread_id 字段。Linux 当前目标可使用 `<unistd.h>` 的 `::gettid()`，该调用不报告失败；不要把 producer_token 当线程 ID。

```cpp
#include <unistd.h>

std::unique_ptr<ProducerContext> make_context(
    const ContextRegistryView& view, std::uint64_t token) {
    const auto tid = static_cast<std::uint64_t>(::gettid());
    ChannelCold cold{
        view.logger_id, token, tid, std::string{},
        view.ring_config.max_payload_bytes, view.dependencies};
    return std::make_unique<ProducerContext>(std::move(cold), view.ring_config);
}
```

这是创建 Context 时一次系统调用，不能移进每次 try_log。当前外层只捕获 bad_alloc；这里不引入新的抛异常路径。非 Linux 移植另做平台适配，不把 pthread_t 转成用户可见 Linux tid。

## 6. 行层测试必须验证什么

| 输入 | 断言 |
|---|---|
| time=0、UTC、INFO、tid=123 | `[1970-01-01T00:00:00.000000000+00:00] [INFO] [logger/category] [tid=123] body\n` |
| 同秒 1ns/999999999ns | 日期缓存相同，纳秒各自正确 |
| offset=-60、epoch0 | 1969-12-31T23:00:00，不能无符号下溢 |
| offset=840/-840；随后改时区 | 边界正确，缓存失效/更新 |
| 时间从 2s 回退到 1s | 输出 1s，不能复用“更大秒”的缓存 |
| flags=2 且 time=0 | unavailable 占位，不转日期 |
| flags=1 | policy 允许 fallback 时走正常时间格式 |
| flags=3、未知位、unavailable+非零time | 失败，size=0 |
| capacity=刚好/差1/0；超 64KiB | 无越界、失败不交付部分行 |
| logger/category/message 含 NUL | 按长度输出，不截断 |
| 两目标不同 timezone | 独立 cache，正文只需生成一次 |

CMake 使用与 `qlog_text_formatter_test` 相同的 GTest 接线，并给测试 label `formatter`。完整行运行验收通过前，不宣称单条完整日志已端到端输出。

## 7. 行层之后的依赖顺序

| 阶段 | 交付边界 | 关键约束 |
|---|---|---|
| PreparedReset / mailbox | 只冷准备、发布、人工驱动 apply/poll | make_appender 用已规范化配置；捕获 AppenderOpenError；busy 时不打开文件 |
| BackendSession | 人工调用 run_one_visit 可消费真实 Ring | 借用 Record 时不 I/O；调用 capture_first_error；工作区冷分配 |
| worker/runtime | 共享/独立、CV 唤醒、attach/detach | 启动时只屏蔽 SIGPIPE；关闭确认前不释放 Session |
| 公共 API 生命周期 | 构造、管理 API、shutdown、析构一次接线 | 默认共享 worker；独立 join；异常接管不输出 Console |
| V1 验收 | 完整功能、压力、端到端性能 | 原生 Linux 证据独立；本轮 C++ 分配探针不替代吞吐/P99 |

## 8. 复现输出层验证

```bash
cd /home/qq344/QLog
cmake --build build/formatter-gcc-debug -j 4
ctest --test-dir build/formatter-gcc-debug -L appender --output-on-failure
ctest --test-dir build/formatter-gcc-debug --output-on-failure
```

验证结果、已知边界及日志见 [本轮实现报告](./V1_APPENDER_IMPLEMENTATION_REPORT_20260920_CHS.md)。
