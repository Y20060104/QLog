# I2 上下文指南核验（2026-09-15）

范围：本次重新读取 /home/qq344/QLog 当前未提交源码，更新主指南 §0/§12 和执行计划 §5。未修改生产实现。

## 实际执行

- 从主指南的 check 标记原样提取 producer_identity.hpp、channel.hpp、producer_context.hpp、producer_context.cpp，以及 §11 的 context_result.hpp 到临时目录 /tmp/qlog-context-guide-idcquas5。
- 临时 include 目录优先，其余 FilterState、clock、Record、SPSC 依赖使用当前生产 include；未改写这些生产依赖。
- GCC（g++）及 Clang 18（clang++-18），C++20，-Wall -Wextra -Wpedantic -Werror -fsyntax-only：完整 producer_context.cpp 均通过。
- 两种编译器分别单独 include 四个参考头，自包含语法检查共八项通过。
- 检查新入口锚点、文档内相对 Markdown 文件链接以及代码围栏配对；git diff --check 通过。另显式检查本轮三个文档的行尾空白，包含未跟踪文件。
- 文档更新前后对 include/src/tests/benchmarks 与根 CMakeLists.txt 共 235 个生产文件做 SHA-256 比较，内容不变。

## 验证边界

这是指南参考文件的组合语法检查，不是生产构建、链接或运行测试。没有宣称 TLS 失败注入、并发 CAS、退出顺序、内存回收、稳态分配、TSan 或性能验收通过。
当前生产文件仍需维护者按 §12 修正；生产 ContextResult::error 的 const 指针、身份类型、Channel 完整性、TLS helper 和主函数等问题没有由本次文档修改自动修复。
本次实读已确认 log_level.hpp 残留声明删除；旧 I2_DETAILED_GUIDE_VALIDATION_20260915_CHS.md 是先前快照的原始记录，不将其旧阻塞套用到当前源码。
