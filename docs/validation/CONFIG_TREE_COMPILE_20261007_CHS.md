# 配置构树实际源码编译修正与验证

日期：2026-10-07。
用户授权：将上一轮的定义顺序、成员命名空间位置和 null 调用修正写入实际源码，确保 Debug/Release 编译通过并解释原因。

QLog：/home/qq344/QLog，feat/spsc-ring-opt，HEAD ec3f72b27784b3b1a9047391117395b4848afb8d，工作树有未提交草稿。
BQLog：/home/qq344/BqLog，HEAD 78cbfbef4f87e7558335f0c04bf6dd6894b46451。property_ex.cpp 无 HEAD 差异，不称参考整树干净。

## 修改范围

唯一生产文件：src/config/property_value.cpp。

- cpp helper 按依赖顺序定义；parse_to/trans 放到 try_convert_integral_double 后。
- null 调用由 parse_null_value(bvalue,value.c_str()) 修正为 parse_null_value(value.c_str())，对应参考 property_ex.cpp:139。
- create_from_string 移出匿名 namespace，仍位于 qlog::config，头文件 public static 声明保留。
- 除该参数删除外，非空代码行多重集合一致，原有函数体和 CR 保留；清理一行搬移后的空行尾空白。其他 include/src/tests/CMake 文件 SHA-256 均不变。
- 保留现有 double 草稿分支，用户的两种整数越界方案仍仅记录、暂不选定。

## 实际源码验证

证据目录：/tmp/qlog-config-tree-fix-dtwqknoc。

| 检查 | Debug | Release |
| --- | --- | --- |
| 实际 property.cpp/property_value.cpp 严格语法 | 通过 | 通过 |
| 实际 qlog 库及公开头文件配置调用方严格构建/链接 | 通过 | 通过 |
| 构树定向 CTest | 1/1 通过 | 1/1 通过 |
| 库/调用方 QLOG_DEBUG、NDEBUG 状态 | 通过 | 通过 |

严格检查使用 C++20，-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror。Debug 定义 QLOG_DEBUG，Release 定义 NDEBUG；CMake 调用方的 QLOG_DEBUG 由 QLog::qlog PUBLIC 传播，无单独覆盖。

调用方 behavior.cpp 只包含公开 property_value.hpp，没有直接包含配置 cpp 或临时生产副本；临时 CMake 目标把实际两个配置 cpp 作为独立翻译单元，链接实际 QLog::qlog，string_utils 从实际库解析。配置树测试覆盖标量/层级、混合/空数组、原空项与空白项、重复 key[]、未完成 EOF、转义、异常点号/[]、NUL 入口、字符串所有权和 double-first 边界/舍入。没有重复运行无关 Layout 测试。

另外，直接编译实际 property.cpp/property_value.cpp/string_utils.cpp 与公开调用方，以 -fsanitize=float-cast-overflow -fno-sanitize-recover=float-cast-overflow 运行同一组定向行为，通过。该项不是完整 sanitizer 验收。

## 复现与边界

```sh
python3 /tmp/qlog-config-tree-fix-dtwqknoc/validate_actual.py Debug
python3 /tmp/qlog-config-tree-fix-dtwqknoc/validate_actual.py Release
```

目录内保存 property_value.before.cpp、production-fix.patch、before/after-sha256.json、behavior.cpp、CMakeLists.txt、validate_actual.py，以及各阶段日志和 float-cast 检查产物。

根 qlog target 尚未列配置 cpp，本轮没有修改生产 CMake。此次验证证明实际源码的独立编译、公开接口链接和定向构树行为，不记作正式配置集成、BQLog 双库差分、并发、Windows、Buffer/运行态或性能验收。

实际 property_value.cpp 最终 SHA-256：
3e6f55fa2788d81aa6ddff9fbfe1f078cd0122603b6ebca35d93d4e13a0620b4
