# TimeZone模块接手提示词（2026-09-29）

以下内容可复制到新对话：

请接续QLog的BQLog对齐重建，从TimeZone模块开始，采用逐文件、逐函数的实现指导。先检查实际工作区；未经我要求，不直接代写整套TimeZone/Layout。

一、路径与读取顺序

- QLog权威目录：/home/qq344/QLog；Windows入口：\\wsl.localhost\Ubuntu\home\qq344\QLog。
- BQLog参考：E:\VisualStudioProject\BqLog；Linux为/home/qq344/BqLog，注意大小写。
- QLog此前HEAD为94fefc0f29b21a1dbdd258fdb5725aadf84d770e，分支feat/spsc-ring-opt；必须重新读取git状态，保护未提交草稿和CR。
- BQLog此前HEAD为60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9；Linux参考曾有MISO及构建修改，不能称整树干净。采用time_zone.h/.cpp前重新核对HEAD与文件差异。
- 先读AGENTS.md最新记录、R0_POST_CLEANUP_IMPLEMENTATION_GUIDE_CHS.md第0/5/8节、R0_BQLOG_LAYOUT_REBUILD_GUIDE_CHS.md第6节、ADR-015-bqlog-layout-record-alignment.md、V1_BQLOG_ALIGNMENT_ROADMAP_20260922_CHS.md。文档均在docs/decisions下。
- 构建证据：docs/validation/RECORD_CMAKE_20260929_CHS.md。

二、已完成与验收边界

- buffer模块的SpscRingBuffer是8字节block/u32游标/外部内存SISO；单条/批量/恢复/Debug已填写。不得恢复旧字节Ring或混入MISO。
- record已有RecordHeader、ArgumentTag、LogEntryHandle、参数序列化。通用工具保留qlog::utility归属。
- 自定义协议采用log_format_str_size/log_format_str_chars/log_custom_format，保留成员/ADL方式和CR注释。
- 普通UTF-32字符串参数转UTF-16，参数tag为string_utf16_type；单个char32_t是char32_type。custom四字节字符仍显式static_assert阻止，不能宣称全编码已对齐。
- CMake已编译version.cpp、src/buffer/spsc_ring_buffer.cpp、src/record/log_entry_handle.cpp。序列化是模板头，不建空cpp。
- QLOG_DEBUG通过target_compile_definitions(qlog PUBLIC $<$<CONFIG:Debug>:QLOG_DEBUG>)传播。仅Debug定义，其他配置不定义；不能QLOG_DEBUG=0，也不能在调用方单独改变宏。标准assert仍由NDEBUG控制。
- Debug/Release库及临时真实调用方链接通过，宏传播已检查；现有version smoke各1/1通过。序列化已通过Debug/Release严格模板实例化语法检查。
- 没有完成Ring/Record行为、并发、恢复、BQLog差分或性能验收。旧47项测试属于删除前历史。保留固定memcpy性能CR，完整日志后再测。
- TimeZone/Layout在本次交接时未实现；下一轮必须检查是否已有我新写的文件，不能覆盖。

三、本轮模块范围

- 目标头：include/qlog/layout/time_zone.hpp；目标cpp：src/layout/time_zone.cpp；namespace qlog::layout；类TimeZone。
- 参考：src/bq_log/utils/time_zone.h和time_zone.cpp。
- 普通成员头中声明、cpp带TimeZone::限定名定义；简单getter可类内inline。不得全部放全局detail，不恢复旧TimeZoneConfig，不额外造Manager/拥有型包装类。
- 按reset、parse_by_string、get_tm_by_epoch、inner_refresh_time_string_cache、refresh_time_string_cache、getter/get_time_str_by_epoch/restore_by_config等依赖顺序讲解，覆盖构造及所有声明。
- 使用std::string，时间输入毫秒，缓存char[129]；以参考实际源码核实解析别名、UTC偏移范围、符号、失败回退、local/DST和固定偏移语义。
- 明确restore_by_config是否刷新缓存、首次epoch=0是否跳过生成、-00:30是否丢失符号、负偏移与uint64时间加法的边界；发现参考缺陷需给出来源、影响与修复差异，不能静默复制UB或自创新合同。
- 缓存正文是“时区 年-月-日 时:分:秒.”，毫秒由Layout追加；get_time_str_by_epoch与Layout缓存接口不要混淆。缓存刷新策略按参考核对，不擅自优化成秒级缓存。
- Linux为权威实现环境；系统日历转换的返回值、错误和线程安全接口要明确，Windows映射单列边界。

四、指导形式
在对话框提供按依赖顺序的完整指导：每个实际文件、namespace/class/public/private、完整声明、cpp定义、参数返回值、内部步骤、边界、调用顺序以及对应BQLog函数。不再引用尚未给出实现的辅助函数。
步骤不要拆得过碎，以TimeZone为一个模块。先源码核对及说明，再指导我实现；当前优先静态审查/编译，运行测试可后置但不能算已通过。
TimeZone完成后才进入Layout；不要跳到Worker/Appender，也不要恢复旧Logger/codec/PreparedRecord或六参数格式器。
