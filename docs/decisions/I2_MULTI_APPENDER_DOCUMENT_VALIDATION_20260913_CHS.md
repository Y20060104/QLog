# I2 多 Appender 文档修订验证（2026-09-13）

本报告只覆盖本次设计/指南修订，不是 I2 生产验收。

## 变更

新增 ADR-012，更新 ADR-011、I2 执行计划、I2 指南 B 章与 D4 接口、设计索引和 AGENTS。
FilterState 参考含完整声明、构造步骤、独立原子发布、配置层权限与多 Appender 验收矩阵。
真实生产树已有未完成骨架，未将空文件或头文件语法问题标为已修复。

## 已执行检查

- 从指南 B2 提取完整 FilterState 头文件，GCC/Clang C++20 均以 `-Wall -Wextra -Werror -pedantic -fsyntax-only` 通过。
- 骨架探针检查声明自包含及禁止拷贝/移动；没有链接构造函数，没有运行过滤行为测试。
- 对 include/src/tests/benchmarks 和根 CMake 中共 230 个文件（包括未跟踪文件）计算修改前后 SHA-256，完全一致。
- 检查本次 ADR、计划和指南的相对文档链接存在，I2 filter 锚点存在。
- `git diff --check` 通过。仓库已有其他文档修改，本次未提交或覆盖生产工作。

证据目录：`/home/qq344/QLog/build/validation/i2-multi-appender-guide-20260913/`。
`production_fingerprints.json` 保存逐文件修改前后摘要；`filter_state.hpp`/`header_probe.cpp` 是文档提取物。
`document_checks.json` 保存链接、编译器和摘要复核结果。

## 未执行与下一步

未编译当前未完成的生产 I2，未执行 I2 运行时、并发、TSan 或性能验收；文档中的 B6 是待实现验收要求。
维护者下一步按 B7 修正骨架，完成 B2/B3，再接 B4 配置合并及更新链路。
I3 完成真实多目标分发；I4 在文件 reset 前先商榷打开/flush/close 失败策略。
旧 I2_GUIDE_DOCUMENT_VALIDATION_20260913_CHS.md 只说明旧版当时的检查，不自动覆盖本次新内容。

## 指针与长度合同勘误

用户指出初版 FilterState 示例误用了 std::span。已按既有约定将构造、验证 helper 和调用统一改为
const uint8_t* 加 size_t，直接 include cstddef，并明确空指针/零长度/过大长度在读数组前拒绝。
重新提取头文件后，GCC 13 与 Clang 18 严格语法检查通过。旧生产指纹已与当前工作树不同，不能沿用旧一致性结论；本勘误脚本只写文档和 build 下的文档验证物，不覆盖当前生产修改。
本次仍仅为文档声明验证，未声称已测试生产构造函数的运行时行为。
