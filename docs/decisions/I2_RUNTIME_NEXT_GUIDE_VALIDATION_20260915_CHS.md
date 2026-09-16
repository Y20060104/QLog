# runtime 下一步指南核验（2026-09-15）

本轮仅更新主指南入口/进度表、执行计划和新增 runtime 下一步指南；生产文件 233 个 SHA-256 前后不变。

## 参考检查

- 从指南原样提取完整 log_result.hpp 和 async_logger_impl.hpp 至 /tmp/qlog-runtime-next-a3_8eosq。
- 临时 AsyncLogger 头使用当前生产副本，只补公共结果 include 与末尾模板实现 include；其余类型使用当前生产头。
- 临时 mapper 仅提供四个函数声明，供模板语法核验；没有用假成功函数体冒充错误映射实现。
- 实际实例化 runtime 格式和两个 int 参数调用，GCC、Clang 18 在 C++20、Wall/Wextra/Wpedantic/Wconversion/Wshadow/Werror、fsyntax-only 下均通过。
- mapper 表逐项根据当前 MeasureError、ReserveStatus、EncodeError 与 ContextError 核对，未宣称 mapper 实现或运行测试通过。
- 文档相对链接、代码围栏与行尾空白显式检查；git diff --check 通过。

## 边界

未修改生产头/cpp/CMake，没有执行生产构建、链接、模板运行、故障注入或 Producer→Ring→decoder 验收。
语法检查不能证明 mapper 函数已实现；其定义由维护者按完整映射表补齐。Guide 中 CMake 命令是后续操作步骤，不是本轮已执行结果。
当前接入层仍有新指南 §1 所列错误；不得把此次模板参考检查当作 I2 验收。
