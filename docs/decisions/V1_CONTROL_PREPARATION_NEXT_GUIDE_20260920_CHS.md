> 当前更新：本文件指导的冷准备/邮箱已实现。内部类型以实际头文件为准，下一步见 [worker 交接指南](./V1_WORKER_HANDOFF_20260920_CHS.md)。以下保留为历史教学说明，不再是待粘贴代码。

# 完整行完成后的下一步：PreparedReset / ControlMailbox

本轮已修复 compose_line 头声明拼写和 make_context 重复定义，并增加完整行与真实 OS tid 测试。行层源码以 src/text_formatter.cpp 为准，不再追加上一份指南的参考实现，否则会重复定义。

下一阶段只实现冷准备数据与命令所有权交接，先用人工驱动测试。不要先启动 worker；AsyncLogger 的构造、shutdown、析构须在最后整体接线。总合同仍见 V1_REMAINING_CODE_IMPLEMENTATION_GUIDE_CHS.md §4。

## 1. 按文件依赖顺序实施

| 顺序 | 新文件 | 完成条件 |
|---|---|---|
| 1 | include/qlog/detail/appender_prepare.hpp | PreparedReset 值类型和 prepare_reset 声明完整 |
| 2 | src/appender_prepare.cpp | 规范化、匹配复用、新目标冷打开、所有容器预分配 |
| 3 | tests/appender_prepare_test.cpp | 复用矩阵、错误回滚、容量准备、资源清理 |
| 4 | include/qlog/detail/control_mailbox.hpp | 命令/阶段类型、完整 PreparedCommand、单槽 ownership |
| 5 | tests/control_mailbox_test.cpp | 人工发布、完成、领取；pending/completed 均拒绝再次提交 |

现有 make_appender、prepare_io_report、prepare_appender_configs、merge_appender_levels 直接复用，不再实现第二份。

## 2. PreparedReset 头文件

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>
#include "qlog/detail/appender_factory.hpp"

namespace qlog::detail {
inline constexpr std::size_t kNoReuse = std::numeric_limits<std::size_t>::max();

struct PreparedReset final {
    std::vector<AppenderConfig> new_config_snapshot;
    std::vector<AppenderConfig> changes;
    std::vector<std::size_t> reuse_old_index;
    std::vector<std::uint8_t> old_reused;
    std::vector<std::unique_ptr<Appender>> next_appenders;
    std::vector<std::unique_ptr<Appender>> retired;
    std::vector<std::uint8_t> next_selection;
    IoReport next_shutdown_report;
    IoReport next_periodic_report;
    std::uint32_t prepared_merged_levels{};
};

std::unique_ptr<PreparedReset> prepare_reset(
    const AppenderConfig* input, std::size_t count,
    const AppenderConfig* acknowledged, std::size_t acknowledged_count,
    std::size_t category_count, ResetMode mode, ConsoleOutputGate& gate);
} // namespace qlog::detail
```

`next_periodic_report` 用于新活动目标的后台周期服务，必须与新路径的最大恢复长度一起冷准备。不能把旧配置预留的路径容量用于更长的新路径。该报告与 management command.report 分开，未 take 时可 clear 复用。

## 3. prepare_reset 的确定顺序

1. 检查 acknowledged == nullptr && acknowledged_count != 0。input 的检查与空数组默认 Console 行为交给 prepare_appender_configs。
2. 先完成全部输入规范化，再打开任何新文件。`new_config_snapshot = prepare_appender_configs(input,count,category_count)`。
3. 冷路径深复制 snapshot 到 changes。后续 worker 对复用对象执行 apply_compatible_config(changes[i])，不能将管理影子本身拿去 swap。
4. resize next_appenders、next_selection 为新目标数；reuse_old_index assign 新目标数个 kNoReuse；old_reused assign 旧目标数个 0；retired resize 旧目标数。
5. 同名匹配，再判断类型、batch_bytes、选中目标地址是否相同。TextFile 比较配置 base path，Console 比较 stream；不比较运行中的 recovery path。
6. reuse_compatible 且兼容：填 reuse_old_index[i]、old_reused[old]=1，next_appenders[i] 保持 null。冷线程不读、不移动活动 Appender。
7. 否则 `next_appenders[i] = make_appender(new_config_snapshot[i], gate)`。传入配置副本，保留 snapshot 和 changes。
8. 为新目标分别 prepare 两份 report，保存到 next_shutdown_report 和 next_periodic_report。
9. 调 merge_appender_levels 计算新合并位图；只保存数值，此阶段不发布到活动 FilterState。
10. 返回 PreparedReset。异常由 unique_ptr/vector 自动清理新对象；不删除已创建的外部文件，不触碰活动集合。

兼容条件的完整表达式可直接复用当前 Appender::apply_compatible_config 中的判断；建议后续抽为一个共享纯函数供两者使用，防止判断漂移。enabled、过滤、时区、flush/retry 周期允许更新，不影响复用身份。recreate_all 不做任何复用。

## 4. 单槽命令结构与所有权

control_mailbox.hpp 直接 include appender_prepare.hpp，再定义 PreparedCommand，从而保证 unique_ptr<PreparedReset> 的析构可见完整类型。

```cpp
enum class CommandState : std::uint8_t { empty, pending, completed };
enum class CommandKind : std::uint8_t { reset_appenders, flush_batches, drain };
enum class CommandPhase : std::uint8_t {
    begin, drain_records, flush_targets, retire_targets, apply_reset, finish
};
struct PreparedCommand final {
    std::uint64_t request_id{};
    CommandKind kind{};
    CommandPhase phase{CommandPhase::begin};
    FlushMode flush_mode{FlushMode::buffered};
    ResetMode reset_mode{ResetMode::reuse_compatible};
    std::size_t target_index{};
    std::unique_ptr<PreparedReset> reset;
    IoReport report;
    ManagementCompletion completion;
};
struct ControlMailbox final {
    alignas(64) std::atomic<CommandState> state{CommandState::empty};
    std::unique_ptr<PreparedCommand> command;
};
```

同时 include `<atomic>`、`<memory>` 以及所用类型头。这些类型位于 qlog::detail，不是 public API。

| 状态 | command 的访问所有者 | 允许操作 |
|---|---|---|
| empty | 单管理线程 | 检查、冷准备、安装 command |
| pending | worker/人工驱动消费者 | 推进阶段、记录报告、写 completion |
| completed | 管理线程 acquire 后 | 领取报告、更新影子、销毁退休对象 |

发布代码的关键顺序：

```cpp
mailbox.command = std::move(prepared);
mailbox.state.store(CommandState::pending, std::memory_order_release);
```

消费者必须先 acquire 读到 pending，再访问 command。完成时先填结果，再 release 发布 completed；完成发布后消费者不得继续访问 command。管理线程 acquire 到 completed 后领取和销毁，最后 release 发布 empty。

这里仅一个管理线程，不引入多提交者 CAS 协议。completed 但未领取仍是 busy，不能覆盖。busy 检查必须发生在 prepare_reset/open 之前。

## 5. 命令报告与目标下标

- flush/drain：report 根据已确认的活动 snapshot 冷准备，target index 就是活动下标。
- reset：按照总合同为旧+新目标预留独立槽；旧目标用 old_index，新目标用 old_count+new_index。拼接数量先检查加法/容器上限，nullptr+0 不参与指针运算。
- reset 冷打开失败直接返回 AppenderOpenError，不发布命令；该错误不借用 command.report 的未发布 worker 路径。
- 不能按同名将旧/新路径的错误并进同一个槽；路径变化时两个对象是不同目标。
- IoReport 是 move-only，结果领取调用一次 take_errors；之后不能 clear 重用。后台周期报告始终不 take。

## 6. 进入 BackendSession 之前的验收

1. enabled/过滤/时区/周期变化复用；path/type/batch/console stream 变化新建。
2. recreate_all 全新建；配置重新排序仍按名称匹配。
3. 新目标中途 open 失败，已准备 fd 自动关闭，活动对象/影子不变。
4. 所有下标目标 resize 正确，复用槽为空，新建槽非空。
5. 新路径更长时，shutdown/periodic/command report 均有足够容量。
6. empty→pending→completed→empty 完整；pending 与未领取 completed 都 busy。
7. 请求 ID 非零、不回绕；不匹配/已领取 ID 返回 unknown_request。
8. poll 先销毁 retired/command，再发布 empty；不让 worker 在报告被移动后访问它。

此阶段可以测试结构和人工推进，但 request_id 分配、public submit/poll 的真实接入属于 Session/生命周期后续步骤；不要把只有结构测试说成管理 API 已可用。

下一阶段顺序：PreparedReset/邮箱 → 单线程 BackendSession → 共享/独立 worker → 公共 API 与生命周期 → 完整 V1 验收。
