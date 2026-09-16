# I2 设计与动手指南文档交付核验

日期：2026-09-13。源码审阅基准：35570da。
实际仓库：/home/qq344/QLog。

## 结论

本轮完成正式合同、执行计划、详细动手指南及相关入口同步。
I2 生产源码、测试与生产 CMake 未修改；I2 仍处于实现待开始状态。
下面通过的是文档/骨架核验，不是 I2 集成、sanitizer 或性能验收。

## 交付物

- [ADR-011 正式合同](./ADR-011-v1-producer-channel.md)。
- [I2 执行计划](./MILESTONE2_I2_IMPLEMENTATION_PLAN_CHS.md)。
- [I2 动手指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)：1026 行。
- 原 I2 草案标记为历史；总体计划、设计/Record 指南、I1 交接入口、决策日志和根 AGENTS.md 已同步。

## 骨架语法核验

直接从指南抽取 A1 LogLevel、A2 policy 和 E6 真实 encode 调用片段，
在独立 probe 中实例化现有 measure/Ring/encode API 的零参数与两个 int32 参数路径。
另外验证 A5 的 C++20 structural NTTP 字符/char8_t 静态存储与已知 literal hash，
以及独立布局 probe 在诊断 0/1 下的条件字段。

| 编译器 | 诊断关闭 | 诊断开启 |
|---|---|---|
| GCC | exit 0 | exit 0 |
| Clang | exit 0 | exit 0 |

使用 -std=c++20、-Wall、-Wextra、-Wpedantic、-Wconversion、-Wshadow、-Werror、-fsyntax-only。
这是四次声明/类型/调用语法检查；ClockDescriptor/采样函数在 probe 中仅为声明支持，未做生产链接。
不是整份指南的所有声明自动编译，也不是新 Channel/Logger 类已实现或 ABI 已经验证。

工具版本：

```text
g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
Ubuntu clang version 18.1.3 (1ubuntu1)
cmake version 3.28.3
```

## CMake 配置核验

直接提取 G1/G2 CMake 块，在 build 内隔离 configure-only 工程验证。
占位 .cpp 仅让 CMake 校验 target_sources 路径，不编译、不链接生产实现。

| 配置 | 设置 | PUBLIC interface 定义 |
|---|---|---|
| Debug | AUTO | QLOG_ENABLE_PRODUCER_DIAGNOSTICS=1 |
| Release | AUTO | QLOG_ENABLE_PRODUCER_DIAGNOSTICS=0 |
| RelWithDebInfo | AUTO | QLOG_ENABLE_PRODUCER_DIAGNOSTICS=0 |
| Release | ON | QLOG_ENABLE_PRODUCER_DIAGNOSTICS=1 |
| Debug | OFF | QLOG_ENABLE_PRODUCER_DIAGNOSTICS=0 |

五组 configure 和生成的 interface definition 核对均通过。
这验证了指南写法的 PUBLIC 宏传播，尚未证明未来实际生产 Channel 的跨 TU ABI 正确。

## 文档与变更范围

- 核对新合同、计划、指南与已修改入口的本地链接目标。
- 核对动手指南快速导航显式 anchor、代码围栏成对。
- git diff --check 通过。
- include/src/tests/benchmarks 与根 CMakeLists.txt 相对 HEAD 无差异。
- 文档中的未存在生产文件均作为新增任务列出，不作为现有文件链接交付。
- 已知旧进度保留于标明历史的 I1/草案中；当前入口指向正式 I2 计划。

指南 SHA-256：`6344e18cacc1e31a62bdf3a25125f2aa6dfdfe1e32ab2a23cadce62a69ee8056`。

## 可复现证据

证据目录：build/validation/i2-guide-20260913/。
其中包含 results.json、guide_contract_probe.cpp、四份编译日志、五组 CMake 输出与配置日志。
核验脚本已复制到同目录，可在 WSL 中运行：

```bash
cd /home/qq344/QLog
python3 build/validation/i2-guide-20260913/verify_i2_guide.py
git diff --check
```

build 内证据不随 Git 自动归档；本报告保存结论与精确验证边界。
不把文档交付写成 I2 正确性/并发/性能完成；下一步是按指南 A/B/C 实现基础切片。
