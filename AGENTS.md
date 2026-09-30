# QLog 工作约定

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
