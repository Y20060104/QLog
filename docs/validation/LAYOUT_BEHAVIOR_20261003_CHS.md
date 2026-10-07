# Layout 公开入口行为测试（2026-10-03）

> 后续更新：生产参数序列化组合测试已补充，当前Debug/Release各10/10；见 [组合测试记录](./SERIALIZATION_LAYOUT_20261003_CHS.md)。本文7/7及SHA-256为上一轮快照。

## 范围与修复

QLog HEAD：ec3f72b27784b3b1a9047391117395b4848afb8d，本轮使用其上未提交工作树。BQLog参考：78cbfbef4f87e7558335f0c04bf6dd6894b46451，采用的layout.cpp无本地差异；参考整树非干净。

修正四处新增抄写错误：删除错误的 ArgumentTag 版 do_layout 重复声明；线程 snprintf 格式串移除多余右括号；类别后缀改为右括号加tab；入口正确赋值 categories_name_array_ptr_ 成员。保留 CR 与已确认参考边界。现有 LogEntryHandle 版 do_layout 参数名同步。

## 测试设计

新增 tests/layout_behavior_test.cpp，通过公开 do_layout/getters/tidy_memory 测试，不开放私有成员。夹具独立构造40字节记录头、参数槽位和扩展线程名；按头要求对齐，显式构造头对象。成功用例先调用现有 validate 验证夹具结构。断言使用显式检查并返回失败退出码，Release不依赖assert。

| CTest分组 | 覆盖 |
|---|---|
| prefix | 完整前缀、六个等级、空类别/正文、类别重绑定、时间与三位毫秒更新 |
| scanner | UTF-8无参数原样复制、有参数右括号转义、单括号、嵌套起始括号、顺序参数、参数耗尽、前瞻限制、UTF-16格式与非起点格式说明、内嵌NUL |
| numeric | 两种扫描器全部整数tag、64位极值与查表快路径、精确可表示的float/double、正号零值差异、十六进制前缀零填充、居中填充 |
| arguments | null、指针、bool、char/char16/char32、UTF-8/UTF-16字符串、槽位步长、中文/代理对/孤立代理、长度与尾部NUL |
| failure | 非法等级/类别返回parse_error，部分前缀保留，失败后再次布局 |
| reuse | 10000字节输出增长、tidy后复用、线程名缓存保留、新线程ID、时区重绑定 |

## 执行结果

编译器：g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0。构建目录：/tmp/qlog-layout-validation-20261003-05mg6wta。配置/构建/测试日志为该目录下 Debug-*.log 与 Release-*.log；临时目录不保证永久保留。

```sh
cmake -S /home/qq344/QLog -B <build-dir> -DCMAKE_BUILD_TYPE=<Debug或Release> -DCMAKE_CXX_FLAGS=-Werror -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build <build-dir> -j 2
ctest --test-dir <build-dir> --output-on-failure
```

| 检查 | Debug | Release |
|---|---|---|
| 严格构建与公开Layout调用方链接 | 通过 | 通过 |
| 新增Layout CTest分组 | 6/6 | 6/6 |
| 现有version smoke | 1/1 | 1/1 |
| 合计 | 7/7 | 7/7 |
| 库Layout与测试调用方QLOG_DEBUG | 均定义 | 均不定义 |

使用项目 -Wall/-Wextra/-Wpedantic/-Wconversion/-Wshadow，加 -Werror。上述计数是CTest分组，不是样本数，也不包含旧Ring删除前的47项测试。

## 验证边界

这些是依据协议和已核对参考源码编写的固定期望回归，不是执行BQLog库得到的差分。没有测试生产参数序列化到Layout的完整组合；夹具刻意独立于生产序列化器。

未验收：指数格式及异常宽度；NaN/Inf/浮点转整数越界；极端容量回绕；超长线程名；首次epoch0、时区解析/restore缓存、local/DST；损坏记录；SIMD；内部诊断副作用；Windows；性能；并发；Ring/Record完整行为与Worker/Appender。没有引入边界修复策略，不声明完整R0/V1验收。

## 文件SHA-256

- `src/layout/layout.cpp`: `2bf271e57cd91aace31b40bdf2b13ed358a0ff1ed8e5ede5c58226c0ec61b694`
- `include/qlog/layout/layout.hpp`: `3eb054ebdf39676a0f7e4cb366cd5b1a0f16df436042da490be5e38552d3e53d`
- `tests/layout_behavior_test.cpp`: `0395e6b0fa220358d9a5d5c8492e833bf1f9bf4682d56b8f310fcb5f1799f07d`
- `tests/CMakeLists.txt`: `f0e216054a42d1bc3faa1cdc75bab9c19959d0e023dc344cce6542508333c228`
