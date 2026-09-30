# R0 冲突文件清理记录

日期：2026-09-23。用户明确授权删除剩余冲突文件。

当前清理移除了旧 Logger/Channel/Producer、codec、六参数格式器接口、旧 hash 结果包装及绑定旧协议的测试/基准。用户已改的 ArgumentTag 保留；40 字节 RecordHeader 保留并把 get_head_size_without_format_str 移回类内；LogLevel 修正枚举语法和范围检查；record_limits 删除旧额外限制。

生产新 Layout/LogEntryHandle/TimeZone 尚未实现。当前库仅构建版本信息和独立 Ring，测试通过只证明清理后剩余基础，不代表新 R0 已完成。

旧测试二进制语料 tests/corpus/record_core 和 docs/validation/tools 历史材料未当作当前构建入口；不把其中旧命令/结果解释为本轮验收。纯 hash 内核 src/format_hash_core.hpp、software/x86 文件作为待复核候选保留，但未链接到当前库。Ring 也仅是后续 Buffer 复核候选，不被冻结为最终队列架构。

旧文件在删除前逐文件备份，位置：`/tmp/qlog-conflict-cleanup-20260923-4ht1qpbe/before.tar.gz`；SHA-256 清单：`/tmp/qlog-conflict-cleanup-20260923-4ht1qpbe/deleted.json`。备份是当前改动恢复材料，不是实施入口。

## 本轮删除清单

- `benchmarks/record_core_benchmark.cpp`
- `benchmarks/record_core_codegen_probe.cpp`
- `docs/decisions/ADR-007-self-contained-record-header.md`
- `docs/decisions/ADR-008-realtime-coarse-admission-timestamp.md`
- `docs/decisions/ADR-009-v1-packed-tagged-arguments.md`
- `docs/decisions/ADR-010-v1-backend-c20-format.md`
- `docs/decisions/ADR-011-v1-producer-channel.md`
- `docs/decisions/ADR-012-v1-multi-appender.md`
- `docs/decisions/ADR-013-v1-automatic-producer-context.md`
- `docs/decisions/I1D_ACCEPTANCE_20260913_CHS.md`
- `docs/decisions/I1D_ACCEPTANCE_20260913_SUMMARY.json`
- `docs/decisions/I1D_VALIDATION_20260913_CHS.md`
- `docs/decisions/I1_DECODER_VALIDATION_20260912_CHS.md`
- `docs/decisions/I1_HASH_ENCODER_VALIDATION_20260910_CHS.md`
- `docs/decisions/I2_CONTEXT_GUIDE_VALIDATION_20260915_CHS.md`
- `docs/decisions/I2_CONTINUATION_DOCUMENT_VALIDATION_20260914_CHS.md`
- `docs/decisions/I2_DETAILED_GUIDE_VALIDATION_20260915_CHS.md`
- `docs/decisions/I2_GUIDE_DOCUMENT_VALIDATION_20260913_CHS.md`
- `docs/decisions/I2_ID_LOOP_GUIDE_VALIDATION_20260915_CHS.md`
- `docs/decisions/I2_MULTI_APPENDER_DOCUMENT_VALIDATION_20260913_CHS.md`
- `docs/decisions/I2_RUNTIME_NEXT_GUIDE_VALIDATION_20260915_CHS.md`
- `docs/decisions/MILESTONE2_DESIGN_GUIDE_CHS.md`
- `docs/decisions/MILESTONE2_I1B_HANDS_ON_GUIDE_CHS.md`
- `docs/decisions/MILESTONE2_I1CD_HANDS_ON_GUIDE_CHS.md`
- `docs/decisions/MILESTONE2_I1_RECORD_CORE_DEVELOPMENT_GUIDE_CHS.md`
- `docs/decisions/MILESTONE2_I2_CONTINUATION_GUIDE_CHS.md`
- `docs/decisions/MILESTONE2_I2_EXPLICIT_BINDING_ARCHIVE_CHS.md`
- `docs/decisions/MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md`
- `docs/decisions/MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md`
- `docs/decisions/MILESTONE2_I2_PLAN_DRAFT_CHS.md`
- `docs/decisions/MILESTONE2_I2_RUNTIME_NEXT_GUIDE_CHS.md`
- `docs/decisions/MILESTONE2_RECORD_IMPLEMENTATION_GUIDE_CHS.md`
- `docs/decisions/NEXT_IMPLEMENTATION_GUIDE_CHS.md`
- `docs/decisions/R0_BQLOG_SOURCE_CONTRACT_MAP_CHS.md`
- `docs/decisions/R0_DEVELOPMENT_GUIDE_CHS.md`
- `docs/decisions/R0_GUIDE_REHEARSAL_20260922_CHS.md`
- `docs/decisions/V1_DECISION_LOG.md`
- `docs/decisions/V1_DEVELOPMENT_GUIDE_CHS.md`
- `docs/decisions/V1_TWO_MILESTONES_GUIDE_CHS.md`
- `include/qlog/appender_config.hpp`
- `include/qlog/async_logger.hpp`
- `include/qlog/detail/call_gate.hpp`
- `include/qlog/detail/channel.hpp`
- `include/qlog/detail/context_result.hpp`
- `include/qlog/detail/filter_state.hpp`
- `include/qlog/detail/format_hash_reference.hpp`
- `include/qlog/detail/producer_context.hpp`
- `include/qlog/detail/producer_identity.hpp`
- `include/qlog/detail/producer_policy.hpp`
- `include/qlog/detail/producer_result_map.hpp`
- `include/qlog/detail/record_decoder.hpp`
- `include/qlog/detail/record_encoder.hpp`
- `include/qlog/detail/text_formatter.hpp`
- `include/qlog/log_result.hpp`
- `include/qlog/producer_handle.hpp`
- `src/admission_clock.cpp`
- `src/async_logger.cpp`
- `src/filter_state.cpp`
- `src/format_hash.cpp`
- `src/producer_context.cpp`
- `src/record_decoder.cpp`
- `tests/argument_model_test.cpp`
- `tests/argument_tag_test.cpp`
- `tests/argument_traits_self_contained.cpp`
- `tests/arguments_self_contained.cpp`
- `tests/cmake/check_compile_case.cmake`
- `tests/compile_fail/arg_count.cpp`
- `tests/compile_fail/bare_const_char.cpp`
- `tests/compile_fail/bare_mutable_char.cpp`
- `tests/compile_fail/byte.cpp`
- `tests/compile_fail/char16.cpp`
- `tests/compile_fail/char32.cpp`
- `tests/compile_fail/char8.cpp`
- `tests/compile_fail/chrono.cpp`
- `tests/compile_fail/container.cpp`
- `tests/compile_fail/enum_bool.cpp`
- `tests/compile_fail/format_as.cpp`
- `tests/compile_fail/function_pointer.cpp`
- `tests/compile_fail/implicit.cpp`
- `tests/compile_fail/int128.cpp`
- `tests/compile_fail/long_double.cpp`
- `tests/compile_fail/member_pointer.cpp`
- `tests/compile_fail/object_pointer.cpp`
- `tests/compile_fail/u16_string.cpp`
- `tests/compile_fail/u32_string.cpp`
- `tests/compile_fail/uint128.cpp`
- `tests/compile_fail/volatile.cpp`
- `tests/compile_fail/wchar.cpp`
- `tests/compile_fail/wide_string.cpp`
- `tests/compile_pass/arg_count.cpp`
- `tests/compile_pass/bare_const_char.cpp`
- `tests/compile_pass/bare_mutable_char.cpp`
- `tests/compile_pass/byte.cpp`
- `tests/compile_pass/char16.cpp`
- `tests/compile_pass/char32.cpp`
- `tests/compile_pass/char8.cpp`
- `tests/compile_pass/chrono.cpp`
- `tests/compile_pass/container.cpp`
- `tests/compile_pass/enum_bool.cpp`
- `tests/compile_pass/format_as.cpp`
- `tests/compile_pass/function_pointer.cpp`
- `tests/compile_pass/implicit.cpp`
- `tests/compile_pass/int128.cpp`
- `tests/compile_pass/long_double.cpp`
- `tests/compile_pass/member_pointer.cpp`
- `tests/compile_pass/object_pointer.cpp`
- `tests/compile_pass/u16_string.cpp`
- `tests/compile_pass/u32_string.cpp`
- `tests/compile_pass/uint128.cpp`
- `tests/compile_pass/volatile.cpp`
- `tests/compile_pass/wchar.cpp`
- `tests/compile_pass/wide_string.cpp`
- `tests/format_hash_reference_self_contained.cpp`
- `tests/format_hash_self_contained.cpp`
- `tests/format_hash_test.cpp`
- `tests/format_hash_test_access.hpp`
- `tests/fuzz/cstr_length_cache.cpp`
- `tests/fuzz/format_hash_equivalence.cpp`
- `tests/fuzz/record_decode.cpp`
- `tests/fuzz/record_roundtrip.cpp`
- `tests/i1d.cmake`
- `tests/i1d_boundaries.cpp`
- `tests/i1d_error_names.cpp`
- `tests/record_allocation_test.cpp`
- `tests/record_core_labels.cmake`
- `tests/record_decoder_boundary_test.cpp`
- `tests/record_decoder_self_contained.cpp`
- `tests/record_decoder_test.cpp`
- `tests/record_encoder_self_contained.cpp`
- `tests/record_encoder_test.cpp`
- `tests/record_header_layout_test.cpp`
- `tests/record_measure_self_contained.cpp`
- `tests/record_property_test.cpp`
- `tests/record_types_self_contained.cpp`
- `tests/record_types_test.cpp`
- `tests/support/i1d_properties.hpp`

## 指南片段验证

从补全指南抽取 LogEntryHandle 的头/validate 与58字节fixture，在 `/tmp/qlog-guide-view-20260923-_05rd6ik` 用GCC C++20、告警、ASan/UBSan编译运行通过；覆盖合法样本、线程名截断、非法tag、扩展偏移、格式长度溢出、空指针和等级范围。此代码只在临时目录，不是生产实现；没有测试完整Layout。

## 剩余基础验收

新目录 `build/r0-cleanup-20260923`：GCC 13.3、Debug、现有GoogleTest源码缓存；CMake配置和构建通过，CTest **47/47** 通过（geometry 6、Ring 40、smoke 1）。所有现存include头合并做C++20语法/告警检查通过；活动源码/测试/基准的本地include无缺失；现行文档相对链接检查通过。

删除统计：{"benchmarks": 2, "docs": 37, "include": 16, "src": 6, "tests": 75}；总计136。没有运行新Layout或完整Logger性能验收。
