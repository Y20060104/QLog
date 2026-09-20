# 完整行实现复核报告

本轮读取用户完成的实际源码，修复并验证，不覆盖用户已有行格式化实现。

## 实际修复

- include/qlog/detail/text_formatter.hpp：std::yte 改为 std::byte，整理 compose_line 声明，写明输入/输出不重叠以及失败 size=0 的合同。
- src/producer_context.cpp：移除仍使用 tid=0 的旧 make_context 重复定义，保留新实现的冷路径 gettid。
- rollback_tls_slot 的 index 仅用于 assert；加 [[maybe_unused]]，消除 Release 告警，不改变运行逻辑。
- 新增 tests/compose_line_test.cpp，接入 CMake，包含 10 项完整行和实际 Context 测试。

用户写入的 compose_line 主体在本轮测试覆盖范围内无需更改。未将旧指南的同名参考代码再追加一遍。

## 测试覆盖

epoch0 精确格式、纳秒缓存、秒回退、负时区跨日、±14小时、独立目标缓存、2000闰日、UINT64_MAX纳秒、unavailable/fallback、六等级、非法 flags/category/level/指针/时区、容量刚好与差1、64KiB总行上限、NUL/换行保留，以及实际创建线程的 gettid 和 TLS Context 复用。

第一次专项测试中的时间戳截取长度以及最大纳秒日期预期由本轮新增测试写错，已独立核对后修正：包含方括号的标准时间戳为37字节，UINT64_MAX纳秒是2554-07-21T23:34:33.709551615 UTC；2262年对应INT64_MAX。未为迎合测试改变生产算法。

## 验证结果

| 配置 | 完整回归通过 | 跳过 | 失败 |
|---|---:|---:|---:|
| GCC Debug | 181 | 0 | 0 |
| GCC Release | 180 | 1 | 0 |
| Clang ASan/UBSan | 181 | 0 | 0 |

Release 跳过既有 CorruptedFrameDoesNotAdvanceReader，与该配置 Ring 验证开关一致。完整回归后仅追加 maybe_unused 属性；最终三配置重建无告警，相关 12 项（10 项行层+2项 Producer 诊断/公共探针）分别再次通过。日志保存在 ../validation/compose-review-20260920/。

没有单独新增行层动态分配探针，没有进行完整异步日志性能测试，也没有 native Linux 发布验收；不要用已有正文分配测试代替行层分配证据。

## 下一步

进入 [PreparedReset / ControlMailbox 指南](./V1_CONTROL_PREPARATION_NEXT_GUIDE_20260920_CHS.md)。先做配置冷准备、兼容复用映射和单槽所有权交接，再做单线程 BackendSession。尚未实现真实 worker、公共管理闭环和完整生命周期，不代表 V1 完成。

备份：build/compose-review-backup-20260920/ 保存本轮开始时相关文件。
