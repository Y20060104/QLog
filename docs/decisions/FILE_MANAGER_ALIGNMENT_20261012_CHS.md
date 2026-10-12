# FileHandle / FileManager 主线对齐决定

> **2026-10-12 最新实际收口：** 用户已填剩余47定义；助手移回cpp并修正遗漏seq、!=比较、is_valid及last拼写/局部名/参数file，TLS移cpp，原12行注释保留；174生产/测试/CMake仅头/cpp改变。实际Linux/MSVC Debug/Release严格库和双翻译单元调用方链接、现有CTest各12/12；未运行新文件调用方，不计文件行为/并发。下一模块已核对MemoryMap，完整候选只在仓库外四配置编译链接通过、未运行，生产由用户写。见[MemoryMap决定与证据](MEMORY_MAP_ALIGNMENT_20261012_CHS.md)。下方47待写等为此前快照。

日期：2026-10-12。实现指导留在对话，本文只记录决定与验证边界。

## 实际基础收口

QLog feat/spsc-ring-opt@f24f3afcd8e9418ea48ad5a6b05680c622008145；BQLog main@78cbfbef4f87e7558335f0c04bf6dd6894b46451。两树既有未提交修改保留，采用file_manager.h/.cpp、string_impl.h及相关基础参考相对HEAD无差异，不称BQ整树干净。

本轮按最新草稿同步POSIX析构局部变量为mutex_destroy_result，Windows有参构造的store__seqcst纠正为原store_seq_cst；默认构造已经由用户修正，保留。CR不删。根CMake新增6公共cpp及POSIX mutex或Windows mutex/system_api，Linux27/Windows28cpp；native file_io/file_path/thread_ops原注册不重复，PUBLIC Debug宏和仅硬件hash.cpp的SSE4.2选项不变。174个生产/测试/CMake文件仅上述两个mutex和CMake改变，其余171个SHA-256不变。FileManager实际cpp的6个已写函数及公共头未改。

实际Linux/GCC和原生MSVC x64 Debug/Release严格库构建、基础公开调用方链接通过；现有CTest四配置各12/12。MSVC仅在外部验证项目排除既有C4324/C4146。调用方QLOG_DEBUG只在Debug出现。此结果不是FileManager核心、文件行为/并发/恢复/退出/MISO或性能验收。

## 本轮模块与接口

沿用include/qlog/io/file_manager.hpp与src/io/file_manager.cpp、namespace qlog，原file_handle/file_manager及私有file_descriptor，不增加另一套Manager或拥有包装。普通成员在cpp以限定名定义；已有简单getter保留inline。复用已写的构造/instance/get_base_dir和三纯路径函数；本轮指导覆盖剩余47定义，由用户填写，助手未代写生产新函数。原参数/default/字段顺序保持，包括ref_cout拼写；原子使用add_fetch_seq_cst及默认load/赋值，不添加内存序默认实参或额外断言。

描述符以idx/seq校验，原生句柄标量单独malloc保存以使vector扩容时handle_ptr稳定；复制增加引用，移动转移引用，最后引用由desc.dec_ref→clear→platform.close/free释放。manager锁可重入，查询内部再次获取；读写本身不以该锁包围seek+I/O。错误槽为原thread_local int32，get_and_clear显式读并清零，不保证每个失败都更新或成功清除。

## 保留的参考边界

- file_handle复制赋值原代码先覆盖字段再inc_ref，未释放目标旧引用且无自赋值检查，可能泄漏引用；移动赋值保留先close和无自移动检查。本轮显式说明来源与影响，按用户严格对齐要求保留，不静默改合同。
- copy_file原10MiB缓冲和循环保留：短读不能推进时可能无限循环；seek/truncate/write结果未完整核验，true不是逐字节成功/持久化保证。不添加重试上限或回滚。
- open_file无路径去重，定位末尾包括只读；malloc/seq回绕无额外处理，初始seek失败未核验。fast bool与is_valid两级检查按原规则，不为全部I/O增加有效性检查。
- seek(current,0)直接true；get_file_size(handle)先flush且忽略其结果；read_file最多10次，错误分支不累计该次tmp_count；read_text会改变共享游标。
- abs_file_path/get_file_absolute_path返回存储的词法路径，输入相对路径不保证返回绝对路径。绝对转相对按BQ string::replace删除所有非重叠匹配文本，不改成目录边界前缀剥离或filesystem::relative。std适配将原replace的非空断言放在调用点，并保留替换算法。
- get_all_files也加入目录名，目录末尾带/；get_file_size(path)复用托管open/size，不能换成裸平台重载。析构只flush，不自行遍历close，也不启用common的delete。
- std::string建立实际size后写data；std::vector<uint8_t>::resize适配fill_uninitialized，多出的值初始化属于已选标准容器差异，流程和缓冲大小不变。

## 指导验证和接续

仓库外完整候选复用当前6函数、补全47定义，以单cpp替换实际FileManager cpp后，Linux/GCC和原生MSVC Debug/Release严格构建/新调用方链接通过，源数仍27/28，Debug宏传播核对。候选没有运行、没有新增常驻测试，不计FileManager行为验收。首次外部字符串适配曾误改desc.is_empty，已仅在候选纠正为精确字符串empty适配后四配置重链通过，不涉及生产函数。

证据与备份：D:/qq344/Documents/qlog-file-manager-guidance-20261012-103302。

用户填写本模块后下一轮实际审查、必要修正和定向验证，再进入MemoryMap→NormalBuffer→MISO；不再补零散平台基础组，不提前Worker/Appender。
