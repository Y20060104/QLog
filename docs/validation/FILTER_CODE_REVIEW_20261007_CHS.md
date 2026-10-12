# 等级位图与过滤模块实际代码审查

> **2026-10-07 后续更正与当前状态：** 本文是早期审查快照；最新实际QLog HEAD为 `f24f3afcd8e9418ea48ad5a6b05680c622008145`。类别抄写问题已修正，utility引用/转换函数名称已同步，配置/过滤/Buffer配置已经接根库且已有实际调用方验证。下文“clear缺定义/链接失败”结论已在重新核对头文件后更正：已有类内clear，后续仅将既有函数体迁cpp，不算新增实现。详见 [实际收口与更正](BUFFER_CONFIG_CMAKE_20261007_CHS.md)。下方失败结果按原快照保留，不能用于判断当前生产源码；本次没有重跑测试。

日期：2026-10-07。QLog HEAD ec3f72b27784b3b1a9047391117395b4848afb8d，分支 feat/spsc-ring-opt，存在未提交草稿/CR。BQLog HEAD 78cbfbef4f87e7558335f0c04bf6dd6894b46451；本组采用 log_level_bitmap、log_utils、appender_base 文件无 HEAD 差异，整树仍有 MISO/构建等修改。

## 早期审查结果与问题记录（clear结论后续更正）

本轮用户已建立 include/qlog/runtime/log_level_bitmap.hpp、src/runtime/log_level_bitmap.cpp、include/qlog/config/filter_config.hpp、src/config/filter_config.cpp。不是前轮文档所写的“尚未填写”。

1. src/config/filter_config.cpp:32 的局部变量 maks 应为 mask；35/42/47/53 使用 mask，实际 Debug/Release 均报未声明。
2. 同 cpp:39-41 的三处 categories_name 应为 category_name：这是当前名称字符串的长度/前缀/边界检查。现在使用 vector 的长度、不存在的 compare 和 string 与字符比较，实际不能编译。只改 compare 仍不够，必须同步三处。
3. 更正早期clear判断：只检查cpp不足以确定缺少定义；后续重新核对最新头文件确认它已有类内clear，公开调用方在修正helper引用后链接通过。按普通成员分工仅将原bitmap_=0函数体迁cpp。早期缺定义/undefined-reference归因已撤回，不作为实际缺陷，不在保留头内定义时再补cpp造成重复定义。

本轮只审查、回答 CR、准备临时候选和更新文档；没有修改 include/src、CMake 或测试源码，没有替用户填写缺失的生产函数。生产草稿和 CR 保留。warnning 是诊断文字的拼写问题，不影响位图处理，可随修正写成 warning。

其余已读代码在清理这些抄写问题后保持指导对应的 BQLog 规则：all 为 0xFFFFFFFF，字符串级别本身不 trim，等级 helper trim 后替换输出；类别 helper 只收集字符串，无字符串时全允许，相等或紧接点号子路径，索引0另外匹配 *default，没有 * 通配语义。Appender 保留普通前缀或 *，不能直接复用 Logger/Snapshot 类别 helper。输出长度相同仍是前置条件，reserve 不建立元素。普通临时赋值不是线程同步。

## 两个 CR 的回答

equals_ignore_case 用于忽略大小写的完整名称匹配。先比较的是长度，长度不同不能是同一个级别；不是要求原始字符完全相同。循环的条件是 lhs != rhs && toupper(lhs) != toupper(rhs)：原始字符不同且转换成大写后仍不同才拒绝，因此 i/I 会通过。unsigned char 转换满足 ctype 对实参的要求。expected 的 string_view 借用字符串字面量，本函数不保存它；不执行 trim，也不提供 Unicode case folding。

fprintf 的字符串中，\" 输出一个双引号，%s 是 printf 格式占位符，取后一个 level_string.c_str() 的 C 字符串，\n 输出换行，最末未转义的 " 才结束 C++ 字符串字面量。例如 bad 会显示 qlog warnning: invalid level mask was found:"bad" 后换行。编译器先解析字符串字面量，fprintf 再解析 %s，是两层规则。

## 验证及边界

目录：/tmp/qlog-filter-review-20261007-ggvxthqk。临时目录可能随系统清理消失；持久记录以本文、下一组指导及生产源码为准。

- 实际位图 cpp Debug/Release 独立严格语法通过；实际 filter_config cpp 两种配置失败；实际 clear 公开调用方链接失败。
- 临时副本仅修正 mask 与三个 category_name 引用，补 clear 候选。连接实际 Property/PropertyValue/string_utils，Debug/Release 严格编译链接并运行各61个定向检查，通过；UBSan + float-cast-overflow 同组61个通过。
- 严格选项：C++20、-Wall/-Wextra/-Wpedantic/-Wconversion/-Wshadow/-Werror；Debug QLOG_DEBUG；Release NDEBUG/O2。首次实际失败探针使用 -Wall/-Wextra/-Wpedantic/-Werror，其日志保留。
- 覆盖全部位/位31、复制、清零、直接等级不trim、helper形状/替换/非字符串项、9组 Logger/Snapshot vs Appender条件矩阵、字面 * 名称、*default普通匹配、嵌入NUL和实际构树调用。
- Appender部分仍只摘录参考条件，不是调用生产Appender/BQLog完整库；全局门禁场景也是组合条件检查，当前没有 LoggerImpl 调用链。
- 根 qlog target 仍未编译 Property/PropertyValue/bitmap/filter。不是实际生产过滤行为通过，也不是正式 CMake/CTest 集成、双库过滤差分、并发、Ring恢复、Windows或性能验收。
- include/src 全部文件 SHA-256 与审查前清单一致；没有提交 Git。

上述生产修正、配置/过滤CMake接入及公开调用方验证已经完成；最新结果以上方收口记录为准。Buffer配置数据/校验值与配置树转换亦已完成，当前下一组是平台对齐分配。早期Buffer指导仍可用于理解实现，完整指导见 [BUFFER_CONFIG_IMPLEMENTATION_GUIDE_20261007_CHS.md](../decisions/BUFFER_CONFIG_IMPLEMENTATION_GUIDE_20261007_CHS.md)。整数越界两方案继续仅记录、暂不选定，不把当前 double 草稿认作新决定。
