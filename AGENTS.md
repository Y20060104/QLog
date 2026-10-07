# QLog 工作约定

> **2026-10-07 过滤生产审查与Buffer配置指导（最新）：** 用户已填写runtime::LogLevelBitmap与两个config过滤helper。实际filter_config Debug/Release编译失败：maks/mask拼写及三处误用categories_name数组而非category_name字符串；bitmap语法通过，但缺clear定义，实际公开调用方链接失败。生产include/src/CMake/tests未改，CR保留；只在临时副本纠正并补clear，连接实际配置源码的Debug/Release及UBSan各61个定向检查通过，不能记作生产过滤通过。下一组完整指导为buffer::LogMemoryPolicy/LogBufferConfig、恢复checksum、config树到Buffer字段转换，并把已写equals_ignore_case复用到utility；不是新Logger/Manager/Worker/Appender/LogBuffer生产实现。Buffer候选Debug/Release/UBSan及-funsigned-char各48个检查通过，含每轮12个实际BQLog头文件checksum对照；utility候选迁移的过滤UBSan61个通过。配置/过滤cpp仍未接根target；先用户补三处，再审查/CMake收口，完整Buffer配置填写后进入MISO独立核对。参考78cbfbef相关文件无HEAD差异，整树仍脏；整数越界两方案继续暂不选定。见 [过滤审查](docs/validation/FILTER_CODE_REVIEW_20261007_CHS.md) 与 [Buffer配置指导](docs/decisions/BUFFER_CONFIG_IMPLEMENTATION_GUIDE_20261007_CHS.md)。

> **2026-10-07 类别过滤差异确认与后续指导（最新）：** 用户明确确认按 BQLog 保留 Logger/Snapshot 与 Appender 不同的类别过滤规则。Logger/Snapshot 共用 log_utils::get_categories_mask_by_config：相等或点号子路径，*default 特判索引0，*无通配语义；Appender::set_basic_configs 保留普通前缀或*，不特判*default。两者都只收集字符串 mask、不 trim、不折叠大小写，无字符串 mask 时全部允许。后台 Logger 类别先过滤，再分发 Appender/Snapshot，局部允许不能绕过全局拒绝。已重新核对参考78cbfbef相关源码及调用点，无 HEAD 差异，整树仍脏。生产配置前轮修正保持，等级位图/过滤模块尚未填写；本轮只更新决定与完整指导，不写生产实现。临时指导与实际 Property/PropertyValue/string_utils 的 Debug/Release 严格构建链接、定向场景及 UBSan 通过；Appender 只验证摘录匹配条件，非生产 Appender/BQLog双库差分。配置 cpp 尚未接根 qlog target，下一组仍由用户填写 runtime::LogLevelBitmap 和 config 两个 helper，再审查/CMake收口并进入Buffer。详见 [过滤指导](docs/decisions/CONFIG_FILTER_IMPLEMENTATION_GUIDE_20261007_CHS.md) 与 [指导验证](docs/validation/FILTER_RULES_GUIDE_20261007_CHS.md)。

> **2026-10-07 配置构树生产编译修正（最新，优先于下方审查快照）：** 用户明确授权助手把临时修正落实到实际源码。仅修改 src/config/property_value.cpp：两个构树 helper 放到识别/转换依赖之后，parse_null_value 删除多传的 bool 参数，create_from_string 移出匿名 namespace、保留在 qlog::config。原有代码/CR 除该参数删除外全部保留，清理一行搬移后的空行尾空白，其他 include/src/tests/CMake 文件 SHA-256 不变。实际 Property/PropertyValue 的 Debug/Release 严格语法检查通过；临时 CMake 调用方以公开头文件单独编译实际两个配置 cpp、链接实际 qlog 库，两种配置构建及构树 CTest 各1/1通过，PUBLIC QLOG_DEBUG/NDEBUG 传播核对通过；实际源码的 float-cast-overflow 定向检查通过。根 qlog target 尚未加入配置 cpp，本轮没有改 CMake，不计正式配置集成/BQLog双库差分/并发/Windows/性能验收。越界两方案继续仅记录、暂不选定；保留现有 double 草稿分支。详见 [生产编译修正记录](docs/validation/CONFIG_TREE_COMPILE_20261007_CHS.md)。

> **2026-10-07 配置构树审查与过滤模块指导（修正前快照）：** 用户已填写 parse_to_property_value、trans_parson_to_property_value 与 create_from_string 的声明/定义。实际 Debug/Release 严格语法检查失败：辅助函数先使用后定义且无前置声明、parse_null_value 多传 bool 参数、类成员工厂误放匿名 namespace。生产源码未改；仅在临时副本调整顺序/工厂位置及 null 调用，Debug/Release 严格编译链接、构树定向行为和 float-cast-overflow 检查通过。当前数值分支写成保留 double，但用户“两个方案仅记录、暂不选定”的决定继续有效，不视为已选默认。配置 cpp 仍未接 qlog CMake。已核对参考 78cbfbef 的等级位图与 log_utils 两个过滤函数，相关参考文件无 HEAD 差异，整树仍有 MISO/构建修改。下一组为 runtime::LogLevelBitmap 与 config 两个过滤 helper；完整指导只存文档/临时目录，未写生产声明/定义。指导候选 Debug/Release 和定向 UBSan 通过，不计生产配置/过滤/BQLog双库差分/并发或系统验收。详见 [配置审查记录](docs/decisions/CONFIG_PROPERTY_ALIGNMENT_20261005_CHS.md) 与 [过滤实现指导](docs/decisions/CONFIG_FILTER_IMPLEMENTATION_GUIDE_20261007_CHS.md)。

> **2026-10-06 配置整数 helper 审查与构树指导（最新）：** 用户已填写 try_convert_integral_double，并在审查中把强转修正为 std::int64_t；以最新源码为准。实际生产 cpp 的 Debug/Release 严格语法及识别/转换探针通过，专门 float-cast-overflow 检查通过；fmod CR 保留。后续三个构树函数已按参考实际流程核对，本轮只准备完整 double/原字符串两套临时候选，各自 Debug/Release 与 float-cast-overflow 检查通过，没有写生产声明/定义。两个越界方案仍仅记录、暂不选定，不设置默认；候选指导不等于已接受合同。数组直接写 root、空 array 返回标记跳过覆盖、按 maps 构树/重复 key[] 最后一项、原始点号尾部和 [] 包含判断均保留参考。配置仍未接 qlog CMake，不计生产树/BQLog差分/完整 sanitizer/系统验收。详见 [CONFIG_PROPERTY_ALIGNMENT_20261005_CHS.md](docs/decisions/CONFIG_PROPERTY_ALIGNMENT_20261005_CHS.md)。

> **2026-10-06 字符串 utility 抽取（最新，优先于下方快照）：** 用户授权助手迁移 trim/split_nonempty，现位于 include/qlog/utility/string_utils.hpp 与 src/utility/string_utils.cpp，namespace qlog::utility；TimeZone/Property 已统一调用，旧局部定义已移除，CR 原样保留，新 cpp 已接 CMake。Debug/Release 严格构建及 CTest 各12/12（现有10项+临时调用方2项）通过；宏传播、迁移函数体/注释及其他生产草稿 SHA-256 已核对。配置 cpp 尚未加入 qlog 库，PropertyValue 未修改，不计完整配置树/BQLog差分/DST/Windows/性能验收。用户最新源码已补 errno 清零和 -0 检查；后续 PropertyValue 复用 utility，不再复制文本 helper，try_convert_integral_double 与树转换仍由用户填写。整数越界的 double/原字符串两方案继续仅记录、暂不选定。详见 [CONFIG_PROPERTY_ALIGNMENT_20261005_CHS.md](docs/decisions/CONFIG_PROPERTY_ALIGNMENT_20261005_CHS.md)。

> **2026-10-06 越界方案回复（优先于下方记录）：** 用户明确要求“仅记录两个方案，暂不选定”。超出 int64 范围保留 double、保留原字符串均只作为备选，不设置默认。下一组实施指导为 PropertyValue cpp 的 trim、split_nonempty、try_convert_integral_double 共同依赖；转换失败只返回 false/保留 out，不决定节点类型。之前 double 完整候选只是临时验证材料，树写入三函数待规则明确后闭合。生产仅同步了两个参数引用拼写，errno 清零和 -0 检查仍待用户修正，配置未接 CMake。详见 [CONFIG_PROPERTY_ALIGNMENT_20261005_CHS.md](docs/decisions/CONFIG_PROPERTY_ALIGNMENT_20261005_CHS.md)。

> **2026-10-06 配置树接续（最新）：** 用户已修正上一轮四处问题并填写六个 helper；本轮助手仅同步已实现函数的 ouut/out、sr/str 两处参数引用，保留 CR。当前 errno 未清零、is_decimal 缺 -0 检查仍待用户修正。生产两 cpp 严格语法通过，Property 单独行为检查通过，配置树整体未验收/未接 CMake。按用户要求记录“超出 int64_t 范围保留 double；原字符串为另一方案”；完整树指导候选按 double 分支验证，未声称用户明确选定，也未写生产定义。下一组为 parse_to_property_value、trans_parson_to_property_value、create_from_string。详见 [CONFIG_PROPERTY_ALIGNMENT_20261005_CHS.md](docs/decisions/CONFIG_PROPERTY_ALIGNMENT_20261005_CHS.md)。

> **2026-10-06 配置最新审查：** 用户已修正 parse 空 while 并填写树 serialize；当前仍有冒号回退误写为点号、数组分支条件错误、行尾重复路径、遗漏整数序列化四处问题，尚未改生产函数。生产 Debug/Release 严格语法与独立链接通过，行为失败；四处临时修正版及六个值识别 helper 指导各自检查通过，不计作生产配置验收。下一组由用户填写 cpp 值识别函数；create_from_string 的 double 转 int64 越界结果仍待选择。配置未接 CMake。详见 [CONFIG_PROPERTY_ALIGNMENT_20261005_CHS.md](docs/decisions/CONFIG_PROPERTY_ALIGNMENT_20261005_CHS.md)。

> **2026-10-05 配置解析审查（优先于下方同日快照）：** 生产 parse 已填写，但分隔符 `-`/`.` 与续行 while 作用域有抄写错误，尚未改生产源码。配置两 cpp 的 Debug/Release 严格语法检查和 Property 独立链接通过，生产解析行为失败；临时修正版及树 serialize 指导候选各自检查通过，不计作生产配置验收。配置仍未接入 CMake。create_from_string 的参考 double 转 int64 越界缺陷已提出规则选择，待用户决定；独立 serialize 可先指导。详见 [CONFIG_PROPERTY_ALIGNMENT_20261005_CHS.md](docs/decisions/CONFIG_PROPERTY_ALIGNMENT_20261005_CHS.md)。

> **2026-10-05 配置接续：** 用户确认末尾未完成续行仅跳过当前键值项、保留其他有效项；正常 properties 规则对齐 BQLog。PropertyValue 已填写，Property helper/load 已填写，生产 parse 截至核对仍缺定义，由用户继续实现。候选 parse 仅在临时副本完成 Debug/Release 严格编译和定向检查，未接 CMake、不计入生产配置验收。详见 [CONFIG_PROPERTY_ALIGNMENT_20261005_CHS.md](docs/decisions/CONFIG_PROPERTY_ALIGNMENT_20261005_CHS.md)。

> **2026-10-03 测试补充：** 新增生产参数序列化→Layout组合测试3组，覆盖混合标量、字符串长度边界、UTF-32参数转UTF-16、成员/ADL自定义协议；Debug/Release严格构建与CTest各10/10（9个Layout分组+1个smoke）。本轮未改生产代码，仍非BQLog差分/SIMD/完整系统验收。见 [SERIALIZATION_LAYOUT_20261003_CHS.md](docs/validation/SERIALIZATION_LAYOUT_20261003_CHS.md)。

> **2026-10-03 Layout 软件路径已连接并新增行为回归（优先于旧快照）：** 用户已填写前缀/do_layout；本轮修正四处入口/前缀抄写错误并保留CR。新增公开入口Layout测试，Debug/Release严格构建及完整调用方链接通过；每种配置6个Layout分组和1个smoke共7/7通过。已核对PUBLIC Debug宏传播。不是BQLog双库差分，指数/异常浮点/超长线程名/local-DST/SIMD/性能等仍未验收。证据见 [LAYOUT_BEHAVIOR_20261003_CHS.md](docs/validation/LAYOUT_BEHAVIOR_20261003_CHS.md)。

> **2026-10-01 协作分工补充：** 已实现函数的参数名由助手核对参考并同步声明/定义/引用；尚未实现函数由用户完成，助手只提供指导。本次仅将 Layout::reverse 的 end_inclusive 同步为 end_cursor；保留 CR 和当前格式。

> **2026-10-01 最新决定与进度：** 参数命名按 BQLog 实际含义对齐，代码格式沿用 QLog 当前要求；扩容按参考 layout.cpp:1100，不增加 layout_failed_ 或通用防御性错误协议。Layout/UTF cpp 已接入 CMake，但扩容仍为空，完整布局尚未实现；Debug/Release 库与现有 smoke 可执行文件构建通过，保留空扩容函数警告；未运行测试，未验证完整 Layout 调用方链接。详见 [构建记录](docs/validation/LAYOUT_CMAKE_20261001_CHS.md)。详见 [命名与扩容补充](docs/decisions/LAYOUT_ALIGNMENT_20261001_CHS.md)。本条优先于下方历史进度。

> **2026-09-30 TimeZone 构建接入完成（优先于下方历史快照）：** 用户已填写 TimeZone，qlog 现额外编译 src/layout/time_zone.cpp；本轮仅修正剩余 time_st{} 赋值笔误并接入 CMake。Debug/Release 严格库编译及实际 TimeZone 调用方链接通过，PUBLIC 宏传播已核对，现有 version smoke 各1/1通过；调用方未运行，TimeZone 解析/DST/缓存/差分、Windows 和性能尚未验收。保留 CR 与已约定参考行为。下一模块为 Layout，先核对实际源码再逐函数指导，不自动代写。证据见 [TIMEZONE_CMAKE_20260930_CHS.md](docs/validation/TIMEZONE_CMAKE_20260930_CHS.md)。

> **2026-09-29构建接入完成（优先于下方历史快照）：** qlog现编译version.cpp、buffer/spsc_ring_buffer.cpp、record/log_entry_handle.cpp。QLOG_DEBUG通过target_compile_definitions(qlog PUBLIC $<$<CONFIG:Debug>:QLOG_DEBUG>)传播：仅Debug定义，Release/RelWithDebInfo/MinSizeRel及空配置不定义；标准assert仍由NDEBUG控制，不得在调用方单独定义/取消宏造成类布局不一致。Debug/Release实际构建与调用方链接均通过，两种配置现有version smoke各1/1通过；已核对库及调用方编译命令的宏状态。此次未执行Record/Ring行为、并发、恢复或BQLog差分测试。参数序列化保留log_*接口及CR，普通UTF-32参数转换为UTF-16，custom四字节字符仍为显式未决项。下一模块为TimeZone，目前未创建实现，进入前重新检查实际文件。详见[TIMEZONE_HANDOFF_20260929_CHS.md](docs/decisions/TIMEZONE_HANDOFF_20260929_CHS.md)。

> **2026-09-29更新：** 用户已填写Record参数序列化，本轮基于最新草稿修复剩余偏特化/requires/拼写错误，按依赖顺序移动定义，统一固定大小函数名，移除未完成声明和旧traits残留；普通UTF-32字符串参数仍转换为UTF-16，tag同步为string_utf16_type。保留用户log_*自定义协议命名及CR注释。实际生产头已通过C++20 Debug和NDEBUG/O2两种-Wall/-Wextra/-Wpedantic/-Wconversion/-Wshadow/-Werror模板实例化语法检查（含普通参数、string/view、UTF-32、成员/ADL接口、空包）；LogEntryHandle cpp通过独立严格语法检查。本轮未运行行为测试，未改CMake，未做完整Record链接或BQLog差分。custom四字节字符仍为显式编译期未决项。下一步先补Record CMake接入和QLOG_DEBUG PUBLIC传播，再进入TimeZone；下方旧进度仅为历史快照。

> **2026-09-27更新：** RecordHeader/limits/LogEntryHandle位于record；argument_tag.hpp、argument_serialization.hpp已由detail迁入record，checked_size.hpp迁入utility并使用qlog::utility；视图cpp位于src/record/log_entry_handle.cpp。修正const getter、validate空指针/类型/返回值、int16分支及扩展线程名长度校验。已做严格编译和独立链接，无运行测试。Ring已由用户加入CMake；Record cpp尚未加入，QLOG_DEBUG尚无PUBLIC配置。序列化头目前只有pragma once，未代写实现。


> **2026-09-26最新进度（优先于下方2026-09-23快照）：** 第三、四批已填写；本轮补全try_recover_from_exist_memory_map并修正batch结果、断言/拼写、线程身份判断和Release开关方法定义。恢复以外部存储重算有效块数，memcpy读取普通游标快照，用remaining有界验证u32回绕与chunk几何，验证后建立Head/原子并恢复游标；坏快照返回false，非法外部存储仍是assert前置条件。QLOG_DEBUG和NDEBUG/O2两种配置以-Werror编译，并通过独立共享检查库的--no-undefined链接；未接入项目CMake，未运行行为/并发/恢复差分或性能测试。性能CR保留。
>
> 下一轮先做Ring的CMake接入与宏PUBLIC传播，再以Record为一个模块：补全指南第2至4节，配合完整Layout指南第4、5节。TimeZone/Layout排在之后；不要把本次链接检查记为完整Ring验收。


- 权威源码、测试、构建和文档：/home/qq344/QLog；Windows入口：\\wsl.localhost\Ubuntu\home\qq344\QLog。
- E:\VisualStudioProject\BqLog 为参考/暂存目录，不替代QLog开发位置。 同级Linux参考实际路径为 /home/qq344/BqLog（区分大小写）；2026-09-23核对HEAD为60ef4d3，MISO与部分构建文件有本地修改，不能把整个工作树当作干净官方最新版。
- 工作前核对当前源码和git状态；用户要求指南时不据此擅自实现整套生产功能。

## 当前方向与状态（2026-09-23）

- 起点 feat/spsc-ring-opt@94fefc0；BQLog参考60ef4d3。全部当前主链先对齐BQLog，包含已实现/冻结部分；后续优化另议。
- 决定：docs/decisions/ADR-014-v1-bqlog-first-rebaseline.md 和 ADR-015-bqlog-layout-record-alignment.md。
- 路线：docs/decisions/V1_BQLOG_ALIGNMENT_ROADMAP_20260922_CHS.md。
- 当前动手入口：docs/decisions/R0_POST_CLEANUP_IMPLEMENTATION_GUIDE_CHS.md；算法配套 R0_BQLOG_LAYOUT_REBUILD_GUIDE_CHS.md。
- 用户明确授权清理后，旧Logger/Channel/Producer、codec、六参数格式器、相关旧测试/基准及过期文档已删除。详情 R0_CONFLICT_CLEANUP_20260923_CHS.md。不得恢复旧接口以迁就旧示例。
- 当前库只编译version.cpp：旧字节游标Ring及SpscRingBufferConfig已按用户要求退出，配置/相关测试/基准和过期Ring文档已清理。新SISO、LogEntryHandle/TimeZone/Layout尚未生产实现。仅smoke通过不等于新R0/V1通过。
- Layout拥有可复用缓冲、游标、格式说明，输出内部指针+长度；每个目标完整布局。记录头/tag/编码/视图/毫秒时间同时对齐，不保留DecodedArg或旧FormatResult包装。
- 配置树、LP/HP Buffer、Manager/Logger/Worker、Appender/文件、reset/flush/退出均依据参考实际路径；不恢复Session、固定SPSC最终拓扑、正文共享、单槽mailbox、公平预算、Frame释放后才I/O等旧约束。
- 默认block、三种线程模式、reset部分失败和flush/uninit实际边界按参考，不擅自增加强事务/持久化保证。
- 发现新的冲突先说明对应文件与替换方案；用户已授权本轮具体批量删除，无需重复确认。新疑似缺陷单列证据，不复制UB或静默改变行为。
- raw/compressed、跨语言等不自动扩项。纯hash内核保留为待复核基础，不代表已对齐；旧Ring实现已删除。

- 新Ring入口：docs/decisions/R0_BQLOG_BLOCK_RING_GUIDE_CHS.md；SISO block=8字节、uint32游标、外部内存，MISO另按缓存行block与多生产者协议实现。不能再创建旧max_payload_bytes配置或阈值publish_reclaimed策略。清理记录R0_RING_CONFIG_CLEANUP_20260923_CHS.md。

## 2026-09-23 用户正在编写Ring：最新进度

- 已实际迁移：include/qlog/buffer/log_buffer_defs.hpp、include/qlog/buffer/spsc_ring_buffer.hpp、src/buffer/spsc_ring_buffer.cpp；namespace qlog::buffer，类名SpscRingBuffer，对齐BQLog SISO block协议。
- defs、Head布局和类声明已建立；第一、二批初始化/单条读写已有实现，本轮补齐遗漏。第三批batch/traverse、第四批recovery/Debug尚未完成，不自动代写。
- 调试宏统一QLOG_DEBUG，标准assert仍由NDEBUG控制；初始化非法输入只用assert前置条件，无abort或返回0错误协议。CMake接入时宏须PUBLIC传播，确保类布局一致。
- 性能CR保留，完整日志后再测；当前静态审查为主，测试暂缓但最终验收不取消。
- utility/utility.hpp及inline round_pow_of_two原位保留。Ring容量逻辑留在Ring cpp。
- Ring仍未接入CMake；只有version smoke注册，不代表Ring链接、并发、恢复或差分通过。
- 后续顺序：Ring收口→Record/序列化→TimeZone/Layout→配置运行态及Buffer集成→Worker/Appender/生命周期。逐模块指导，入口R0_POST_CLEANUP_IMPLEMENTATION_GUIDE_CHS.md第0节。
- Record、Layout、Appender、Runtime各自模块；现有后续文档已修订目标目录，但Record等实际源码尚未迁移。MISO/TLS/LP-HP目前只有路线，进入前需独立逐函数指南。
