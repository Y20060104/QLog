# 两套类别规则与过滤指导定向验证

日期：2026-10-07。
目的：用户确认按 BQLog 分别保留 Logger/Snapshot 与 Appender 类别规则，验证下一组完整指导与实际配置树衔接。

QLog HEAD：ec3f72b27784b3b1a9047391117395b4848afb8d，工作树有未提交草稿。
BQLog HEAD：78cbfbef4f87e7558335f0c04bf6dd6894b46451；log_utils.h/.cpp、log_imp.cpp、log_snapshot.cpp、appender_base.cpp、log_level_bitmap.h/.cpp、string_impl.h及相关wrapper文件无采用文件HEAD差异，整树仍脏。

## 被验证对象

临时目录：/tmp/qlog-filter-rules-guide-j768okgl。

- LogLevelBitmap 与 filter_config 是临时指导候选，生产四个目标文件尚未创建。
- 配置值/文本/字符串实现直接编译实际 /home/qq344/QLog/src/config/property_value.cpp、property.cpp、src/utility/string_utils.cpp，包含实际公开头文件。
- Appender 仅把参考普通前缀或*的比较条件摘录到对照探针，未创建Appender对象，未调用BQLog原库。因此不能称生产Appender通过或BQLog双库差分。

## 验证结果

C++20，-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror。Debug使用QLOG_DEBUG/O0，Release使用NDEBUG/O2，UBSan使用QLOG_DEBUG/O1与undefined sanitizer。每个配置都对全部参与源文件使用一致宏。

| 配置 | 严格编译链接 | 定向行为 |
| --- | --- | --- |
| Debug | 通过 | 通过 |
| Release | 通过 | 通过 |
| UBSan | 通过 | 通过，无该场景报告 |

既有位图/过滤场景包括all全部32位、bit31、复制/指针、直接字符串不trim、配置trim、未知及非字符串项、非数组清零、空数组、替换旧位图、类别点号/大小写/空字符串/非字符串/零类别和实际树连通。

新增9组规则矩阵：net、*、*default、net+*default、空字符串、net*、Net、带空白net、空mask列表。另检查根log/snapshot/appenders_config配置构树、Logger与Snapshot各自helper输出，以及局部允许不能绕过全局拒绝的AND组合。源码已核对Snapshot配置在根config["snapshot"]，与log并列。

## 复现及范围

```sh
python3 /tmp/qlog-filter-rules-guide-j768okgl/validate.py Debug
python3 /tmp/qlog-filter-rules-guide-j768okgl/validate.py Release
python3 /tmp/qlog-filter-rules-guide-j768okgl/validate.py UBSan
```

目录保留候选头/cpp、filter-behavior.cpp、validate.py、production-sha256.json与各配置编译/运行日志。所有实际include/src/tests/CMake文件快照哈希核对不变，未修改生产函数、未接CMake、未新增生产测试。

不是生产过滤模块/正式CTest、BQLog原库差分、Appender/Snapshot生命周期、并发、Windows、Buffer或性能验收。先由用户填写位图及两个配置helper，后续复核实际源码与正式CMake/测试，继续Buffer集成。
