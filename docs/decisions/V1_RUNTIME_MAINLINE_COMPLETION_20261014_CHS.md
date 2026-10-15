# V1运行主链与线程收口决定（2026-10-14）

本轮用户明确授权助手补全源码、修正已写问题、接库、继续完成后续主链并运行压测。该授权覆盖此前“新生产由用户填写”的分工限制；保留CR、已有草稿、既有协议及固定参考决定。权威QLog feat/spsc-ring-opt@c2faba4032c145de534d0a3d70cc05af58b523b4；BQLog参考78cbfbef4f87e7558335f0c04bf6dd6894b46451。参考整树仍有本地MISO/构建修改。正式性能比较使用git archive导出的固定提交，未修改参考工作树。

线程：把误放src的condition_variable公共声明移include；POSIX实现草稿中的Windows块归入Windows后端，两个实现改cpp并补命名空间/缺失成员；线程对象保留用户已写构造、internal_run、CR，补原名称探测/set_name/start/join/detach/析构。普通成员cpp限定名，原getter及CV模板头内。五个静态服务复用已有flat函数。原状态/CAS/内存序/default/assert、析构不自动join与原Windows事件回退均保留。

新增主链按原类和流程：runtime/log_imp、log_manager、log_worker、log_snapshot；runtime/appender下原appender_base、appender_console、appender_file_base、appender_file_text；扩展原log_global_vars中的Manager/Console/lifecycle，不造固定返回替身。namespace qlog::runtime，现有Buffer/Record/Layout/Config/utility保留所属模块，使用类型映射，已迁HashMap继续复用。公开qlog::log是Manager拥有运行对象的非拥有句柄，替代原产品C++入口，不恢复旧Logger/Channel/codec/PreparedRecord。

公开模板沿现有log_format_str_size/log_format_str_chars/log_custom_format与record::detail参数序列化：前置类别/等级过滤，原TLS线程ID和最大16字节名称、毫秒时间、RecordHeader/格式字节/参数/扩展头；同步使用原线程局部sync_buffer，异步使用LogBuffer LP/HP/Oversize；低空间唤醒、wait-and-retry的失败commit配对、正常commit保留。格式原生UTF8/UTF16拷贝后复用已有四路CRC哈希；当前是memcpy与get_hash_64两次处理，不声称与BQ融合拷贝哈希具有同等成本。普通UTF32格式/参数使用现有UTF16协议，custom四字节字符仍static_assert未决。原生堆栈支持迁POSIX/Windows，复用现有UTF软件核心；Windows链接DbgHelp。

Logger/Snapshot类别过滤继续复用原等名或点分后代规则；Appender为直接前缀或*，不混用。合并等级按Appender刷新，目标enable在输出时判断；async共享Manager Layout，sync/independent用自身Layout，逐目标完整布局。读取句柄保持到处理/I/O结束，HP最后项批次归还。配置reset复用同名同类型目标或替换，保留原部分成功规则；不动态改变已建线程模式、类别名或Buffer。原recover状态/时间戳非回退修正、文件缓存/滚动/保留清理/disk_full_drop与Snapshot文本双缓冲保留。

Manager保持原phase、锁与force_flush/uninit次序。force_flush处理及flush_write_cache只承诺内存缓存写向OS，不能改称逐条fdatasync/断电持久化；原flush_write_io由重置/文件切换等原路径调用。uninit先改变phase再取消/唤醒/join，不新增退出自动排空。调用方需要先明确force_flush。本轮不扩raw/compressed、Decoder、跨语言或自动崩溃信号处理，工厂仅Console/TextFile。

明确适配/修正：原文件清理qsort无法搬动std::tuple/std::string，使用std::sort保留时间排序键与等键顺序不保证；短写重叠搬移改memmove（BQ file_base.cpp:46）。失败分配先检查结果再形成格式指针，避免参考API空地址算术；空堆栈长度不执行空指针memcpy。既有标准容器按原追加/清空/移除语义适配，sync缓存保留至少8字节正常平台对齐。Windowsmin/max函数使用括号避免系统宏冲突；Windows堆栈头固定windows.h先于dbghelp.h，防止格式工具排序造成级联错误。未扩大Windows验证警告豁免，仍仅外部/wd4324 /wd4146。

BQ继承边界继续明示：GNU release/acquire CAS失败序、typed私有字节访问、MISO unused前块数读取、普通线程ID双写/首次API探测、CV时钟/宽度与事件广播、无锁组链/need_reallocate/oversize时间读写、恢复损坏尺寸等此前决定保留；当前压测和功能检查不证明这些分支没有UB/数据竞争。新TextFile源on_appender_file_recovery_end仍按参考start标记指针及end长度（参考text.cpp:67），Snapshot恢复/极端容量及重建分支也不新增另一合同。已验证的Snapshot扩容是正常容量且记录可转移案例，非所有碎片/故障状态。

GNU/Linux及原生MSVC x64 Debug/Release均严格构建并实际执行31/31 CTest：既有20项，新增线程取消/条件超时，三线程模式，UTF8/16/32/custom端到端，Logger/Appender不同类别规则、reset/目标enable、四生产者序号，TextFile缓存/滚动，堆栈/Console缓冲、128KiB Oversize、被阻塞消费者下discard/block/expand计数、Snapshot扩容迁移。不是完整BQ差分/崩溃恢复/存储错误注入/TSAN或长期SLO验收。

性能执行按用户选择：WSL/Linux Release主测，Windows仅功能验证。新runtime_benchmark与run_runtime_benchmark.py使用完整公开入口，不启用已删除Ring/旧生产基准；9场景、Q/BQ交替顺序、3次重复；Console采用相同计数回调（含2次原子RMW），Text写Linux文件系统，结束后核对计数/内容/逐生产者顺序；采样每256次、记录CPU/RSS、生产及排空时间。保留短轮与扩大负载两套原始证据，报告以扩大轮为主，不把短微探针或3次中位数当置信区间/服务器结果。最终结果见验证报告。

正式主测最终54次、156,000,000调用、144,000,000有效记录、60,000,000文本核对；源码漂移false。完整数据及结论见[性能/验证报告](../validation/V1_RUNTIME_PERFORMANCE_20261014_CHS.md)。
