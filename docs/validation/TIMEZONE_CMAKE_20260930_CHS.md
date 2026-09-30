# TimeZone CMake 接入验证（2026-09-30）

权威仓库：/home/qq344/QLog；feat/spsc-ring-opt@94fefc0，工作树未提交。

## 本轮修改

- CMake 的 qlog 源列表增加 src/layout/time_zone.cpp。
- 修正 inner_refresh_time_string_cache 内剩余的 time_st{} 为 time_st = {}，解除编译阻断。
- 保留用户 CR 和既定行为：local 调用 localtime_r 支持系统 DST；固定偏移调用 gmtime_r；完整毫秒缓存比较；首次 epoch=0、负零小时、restore 不失效缓存等参考边界未改变。

## 实际验证

GNU C++ 13.3.0、C++20、Linux/WSL。Debug/Release 均使用独立构建目录及 -Werror，库保留 -Wall/-Wextra/-Wpedantic/-Wconversion/-Wshadow。

- 两种配置的 qlog 静态库及 time_zone_consumer 均编译、链接通过。
- 调用方仅链接 QLog::qlog，实际引用两个构造、reset、解析、restore、日历转换、文本转换、本地名称、缓存刷新及 getter；不通过 include cpp 绕过库链接。
- 调用方用预处理检查宏传播；两份 compile_commands.json 的全部 6 个翻译单元也已核对：仅 Debug 定义 QLOG_DEBUG，Release 不定义。
- 现有 qlog.smoke 各 1/1 通过，仅验证 version。
- time_zone_consumer 只构建链接，未运行；不能把其返回表达式视为已通过行为断言。
- 未执行 TimeZone 解析/DST/缓存/极值行为测试、BQLog 差分、Windows 编译或性能测试；未补做 Ring/Record 行为验收。

## 复现

夹具保存在 time_zone_cmake_probe_20260930/，不注册为正式行为测试。项目路径固定为权威 QLog 路径。

```sh
cmake -S docs/validation/time_zone_cmake_probe_20260930 -B /tmp/qlog-timezone-check-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CXX_FLAGS=-Werror
cmake --build /tmp/qlog-timezone-check-debug -j 4
ctest --test-dir /tmp/qlog-timezone-check-debug --output-on-failure
cmake -S docs/validation/time_zone_cmake_probe_20260930 -B /tmp/qlog-timezone-check-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CXX_FLAGS=-Werror
cmake --build /tmp/qlog-timezone-check-release -j 4
ctest --test-dir /tmp/qlog-timezone-check-release --output-on-failure
```

本轮实际临时工程：/tmp/qlog-timezone-cmake-20260930-mdgye5jt，可能被系统清理。

## 下一步

进入 Layout 源码核对与逐函数指导，入口为 R0_BQLOG_LAYOUT_REBUILD_GUIDE_CHS.md 第7至10节；重新检查实时参考实现及其依赖，不恢复旧六参数格式器，不提前实现 Worker/Appender。TimeZone 行为和 DST 差分仍列为后续验收项。
