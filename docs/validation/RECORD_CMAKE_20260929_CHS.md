# Record CMake接入验证（2026-09-29）

权威仓库：/home/qq344/QLog；HEAD基线94fefc0，工作树未提交。修改：增加src/record/log_entry_handle.cpp；QLOG_DEBUG按Debug配置PUBLIC传播。

## 实际验证

使用GNU C++ 13.3.0和C++20。临时工程位于/tmp/qlog-record-cmake-20260929，add_subdirectory引用权威QLog，调用方只链接QLog::qlog，不自行定义QLOG_DEBUG。调用方通过预处理#error检查Debug收到宏、Release没有宏，并调用非内联LogEntryHandle::validate()，确保Record对象从静态库参与实际链接。调用方还编译包含Ring和序列化头，static_assert检查普通UTF-32参数tag为UTF-16。

- Debug：库及record_consumer链接通过；现有qlog.smoke 1/1通过。
- Release：库及record_consumer链接通过；现有qlog.smoke 1/1通过。
- 两份compile_commands.json中库、smoke和consumer的宏状态均经脚本逐项核对。
- record_consumer仅构建链接，未运行；CTest运行的只是现有version smoke。
- 未验证Ring/Record行为、并发、恢复、BQLog差分、Windows编译或多配置生成器。多配置支持来自配置生成表达式设计，不记为已运行验证。
- 之前旧Ring的47项结果与本轮无关。

## 复现

临时工程可能被清理，consumer工程源码备份在本目录record_cmake_probe_20260929/。该目录仅是手工构建验证夹具，不加入正式测试计数。工程内权威源码路径固定为/home/qq344/QLog。在QLog根目录执行：

    cmake -S docs/validation/record_cmake_probe_20260929 -B /tmp/qlog-record-check-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build /tmp/qlog-record-check-debug -j 4
    ctest --test-dir /tmp/qlog-record-check-debug --output-on-failure
    cmake -S docs/validation/record_cmake_probe_20260929 -B /tmp/qlog-record-check-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build /tmp/qlog-record-check-release -j 4
    ctest --test-dir /tmp/qlog-record-check-release --output-on-failure
