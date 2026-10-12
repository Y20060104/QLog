# 平台路径基础实际审查与CMake接入

日期：2026-10-09。QLog feat/spsc-ring-opt@f24f3afcd8e9418ea48ad5a6b05680c622008145；BQLog main@78cbfbef4f87e7558335f0c04bf6dd6894b46451。采用的平台、TimeZone、UTF及BQ字符串相关源码相对HEAD无差异，参考整树仍有MISO/构建等本地修改。

## 审查起点与已收口问题

用户已实现两个is_absolute及Windows编码适配器，但位置为平铺src/platform/file_path.cpp、file_path_internal.cpp，内部头放在公开include，适配器声明属于qlog::platform、定义属于qlog::platform::windows。

实际GCC Debug/Release语法通过，POSIX单独调用各14检查通过；实际头的Windows适配器调用在GCC链接报undefined reference，原生MSVC Debug/Release报LNK2019/LNK1120。改为windows限定名调用又因头没有该namespace声明而编译失败。若同时链接两个平铺cpp，则出现is_absolute重复定义。这些结果不能被没有编译路径源的旧根库构建掩盖。

按此前用户授权的系统拆分整理已实现文件：

- src/platform/file_path.cpp移为src/platform/posix/file_path.cpp，内容字节一致。
- src/platform/file_path_internal.cpp移为src/platform/windows/file_path.cpp，只更改内部头include，函数体及replace CR原样保留。
- include/qlog/platform/file_path_internal.hpp移为src/platform/windows/file_path_internal.hpp，声明namespace改为qlog::platform::windows。
- 根CMake在WIN32/else分别加入实际file_io.cpp和file_path.cpp，当前每个平台16cpp；同目标只选择一种is_absolute，PUBLIC QLOG_DEBUG定义未改。

整库原生Windows验证另发现既有TimeZone直接使用POSIX localtime_r/gmtime_r，无法编译。已按照BQLog time_zone.cpp:21-26补齐cpp内平台调用宏：Windows localtime_s/gmtime_s返回0表示成功，POSIX _r返回非空表示成功。get_local_timezone_name的strftime段仅用于POSIX，Windows名称按参考回退localtime。解析、偏移算术、日历/缓存流程及原失败处理未重写；未加入新的错误协议或辅助对象。

MSVC严格实例化还提示序列化helper的prefix局部名遮蔽Layout回归的全局prefix常量。保留BQLog对齐的生产参数名，只将原测试预期常量及四处引用改为expected_prefix；测试内容和prefix分组名不变，没有新增常驻用例。

## 最终实际验证

全部最终结果来自实际根qlog target与单独编译的调用方，调用方不再重复编译或替代I/O、路径实现：

| 对象 | Debug / Release结果 | 边界 |
| --- | --- | --- |
| Linux GCC13.3实际库/调用方 | -Werror严格构建链接，CTest各13/13 | 常驻11加临时实际路径/I/O各1；最终16库源已强制重编，测试常量改名后再编译回归 |
| 原生Windows MSVC19.51实际库/调用方 | CTest各13/13通过 | /W4 /WX /permissive- /utf-8 /EHsc，临时命令仅排除C4324；根警告策略未修改 |
| 实际POSIX路径判定 | 各14检查通过 | 空、斜杠、~、盘符及相对路径 |
| 实际Windows路径/编码适配器 | 各21检查通过 | 14判定加7编码/分隔符场景，含中文、补充平面、嵌入零、点号不整理 |
| 实际两系统I/O | 各31检查通过 | 已经从根库链接，大小/读写/短读/EOF/seek/truncate/flush/同进程锁/无效句柄与零长度 |
| 源码选择与宏 | 两系统两配置已核对 | 16cpp及对应系统目录；库和调用方仅Debug定义QLOG_DEBUG，Release定义NDEBUG |

C4324来自SISO Head显式cache-line alignas导致的预期补齐，尺寸/偏移静态断言通过。未排除时原生/WX构建失败，不称“无例外全警告严格通过”。/wd4324只存在于临时验证命令，未加入根CMake或生产头。调试脚本最初覆盖CMake默认CXX参数遗漏EHsc导致的C4530也已在临时命令补齐，不作为源码缺陷。

Linux首次跨NTFS构建曾出现clock skew提示；最终库16源已强制重编并运行验证，没有依赖旧对象。未重新进行UTF标量/sanitizer、DST专项、BQ双库差分、并发/恢复、信号EINTR/强制部分写/跨进程锁或性能验收。

审计目录：D:/qq344/Documents/qlog-path-review-20261009-8d714e；WSL /mnt/d/qq344/Documents/qlog-path-review-20261009-8d714e。保留149文件前后SHA-256、源码快照、早期失败与候选、最终构建/CTest/调用方日志、两系统宏和源列表证据。143个无关原文件哈希不变；只修改上述CMake、TimeZone平台调用、测试常量，以及三处移动文件。未提交，原生产CR保留。

## CR：std::replace

std::replace(first,last,old_value,new_value)在半开区间[first,last)内逐个比较，相等的元素原地改为new_value，返回void，不改变容器长度，也不为这个替换分配新的字符串。当前result的元素为char16_t，所以使用u'/'和u'\\'两种同类型字面量；u字符串字面量与u单字符字面量用途不同。

当前调用只处理已转换UTF-16字符串的码元，a/../b变为a反斜杠..反斜杠b，不解释点号、不补绝对路径、不检查磁盘。begin/end按size遍历，所以嵌入零不会提前停止；隐含结尾零位于end之外。复杂度O(result.size())。

## 下一组

完整指导见[Windows词法路径](../decisions/WINDOWS_LEXICAL_PATH_IMPLEMENTATION_GUIDE_20261009_CHS.md)。本组按实际依赖先提供Windows完整get_lexically_path，复用现有utility::trim/split_nonempty。POSIX用户目录分支的诊断依赖及基准目录的spin_lock/common_global_vars继续先核对真实基础；不写不存在helper的调用或临时Manager，不扩大Worker/Appender。

用户“严格对齐BQLog”继续有效：Windows首反斜杠/UNC前缀处理、重复斜杠等按参考实际规则；POSIX ~user拼接和账户失败分支不选择此前建议的修正/回退，不把参考UB说成安全合同。整数越界double/原字符串仍仅记录、暂不选定。
