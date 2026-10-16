# Que（阙）分支验证（2026-10-16）

验证对象为 `feat/que`，起点 `c1c5e6cfc0466f788dad62521543a0883a01b6bb` 加本轮工作区修改。功能范围与配置合同见[分支决定](../decisions/NO_COMPRESS_BRANCH_20261016_CHS.md)。本轮未创建提交或推送。

分支由 `feat/no-compress` 更名为 `feat/que`；本次名称变更仅同步分支和文档，库、测试与 benchmark 源码保持验证时的内容。

## 构建与运行结果

| 环境 | Debug | Release |
| --- | --- | --- |
| WSL/Linux，GCC，项目警告与 `-Werror` | 库、两个 benchmark 目标构建通过；CTest 36/36 | 库、两个 benchmark 目标构建通过；CTest 36/36 |
| 原生 Windows x64，MSVC，`/WX` | 库、两个 benchmark 目标构建通过；CTest 36/36 | 库、两个 benchmark 目标构建通过；CTest 36/36 |

MSVC 只沿用项目既有 C4324/C4146 外部排除。逐翻译单元检查：GNU 两配置各 63 项、MSVC 各 64 项，Debug 定义 `QLOG_DEBUG` 且不定义 `NDEBUG`，Release 反之。两类宏继续由原有目标和构建配置控制。

常驻测试由完整版本 44 项减去 9 项压缩专用测试，再增加 `qlog.runtime.unsupported_compressed`，共 36 项。新增用例覆盖：

- 原 Appender 枚举顺序、数值和 `compressed_file` 配置名称保留。
- 有效 Console 与 Snapshot 在存在不支持的兄弟 Appender 时仍工作。
- 使用原本有效的压缩文件路径配置，不创建输出文件，不降级为 Console/Text。
- reset 去掉所有有效 Appender 后，仍按原准入规则过滤新记录，Snapshot 已有数据保留；重新配置 Console 后恢复输出。
- 仅配置不支持的 Appender 时，logger 继续沿用原初始化部分成功规则，保留枚举不启用日志等级。

该新增测试与保留的完整版本 Release 库做过反向检查：旧库实际创建压缩文件，在“不创建输出文件”断言失败；裁剪后的四配置全部通过。首轮新增测试曾错误期待 Snapshot 单独打开生产入口，已依据现有 `refresh_merged_log_level_bitmap` 修正测试预期，生产准入规则未改。

## 构建入口和诊断工具

根构建同时启用 `QLOG_BUILD_RUNTIME_BENCHMARK` 与 `QLOG_BUILD_BQLOG_BENCHMARK`，两平台、两配置均通过。当前官方对齐入口只保留两项普通 Text 吞吐与原写侧延迟负载。

组件诊断 Release 在两平台构建并实际运行：`all` 输出 15 项 copy/hash、SISO 顺序往返及离线 Layout 结果；Text 延迟探针实际接受 200/200 次调用。Linux 额外检查已移除的 `vernam`、`compressed` 模式返回失败，不产生伪测试结果。诊断运行只验证工具行为，不作为新性能报告。

可选 BQLog 组件调用方使用固定 `086f2fd831af86fcfa40e90015f89626d498d3d7` 与匹配的 Release 静态库，在 Linux 构建和实际调用通过；两项 Layout 的最终输出与 QLog 相同。参考工具运行不是完整官方 benchmark 复测。

以下为从项目根目录执行的等价复现命令；本轮实际构建与完整日志保存在独立工作目录：

```bash
cmake -S . -B build/que-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-Werror \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_TESTING=ON \
  -DQLOG_BUILD_RUNTIME_BENCHMARK=ON -DQLOG_BUILD_BQLOG_BENCHMARK=ON
cmake --build build/que-debug --parallel 3
ctest --test-dir build/que-debug --output-on-failure --timeout 90
```

Release 使用独立目录并将配置改为 `Release`。Windows 使用 MSVC x64 开发环境及相同目标，编译选项为 `/utf-8 /EHsc /WX /wd4324 /wd4146`。

## 文件与分支核对

- 以切分支前 423 个现有文件的 SHA-256 为基线：本轮删除 19 个专用文件，修改 16 个现有文件，其余 388 个保持原字节；新增决定和本验证文档另计。
- `src/buffer/log_buffer.cpp` 的未提交 CR 原字节保留；保留源码中的既有 CR 未丢失。用户原有 `AGENTS.md` 重写保留，本轮只同步当前分支事实。
- 原分支 `feat/spsc-ring-opt` 和当前 HEAD 均仍指向起点提交，暂存区未改动。
- Release 静态库符号中不存在 RSA/AES/Vernam、压缩 Appender 或二进制文件基类实现；保留源码没有包含已删除头文件。
- `git diff --check` 通过。README、路线与差异文档已明确完整版本历史数据与当前分支范围。
