# MemoryMap 对齐决定与 FileManager 实际收口

> **2026-10-12 实际实现收口（本段最新）：** 用户已经填写六文件并把公共及对应native cpp注册进CMake。此前“六文件为空”的记录仅是上一轮快照。

## 本轮实际修正与验证

QLog feat/spsc-ring-opt@a1ba6b4d45a25d60242327b68894c23d56c027e0；BQLog main@78cbfbef4f87e7558335f0c04bf6dd6894b46451。九个采用参考（MemoryMap五文件、NormalBuffer两文件、log_buffer_defs及utility_types）相对BQ HEAD无差异，BQ整树仍有MISO/构建等本地修改。

助手仅修改4个已写生产文件：公共头与公共cpp统一get_min_size_of_memory_map_file；公共头直接包含cstdint；POSIX补real_min_file_size、written +=及结尾endif；Windows转包含头补公共头。Windows cpp、POSIX原msync条件及其余流程保持原样。全部原注释和两个CR保留，size_t*/const size_t*/HANDLE&、__memory_map_size_unit及CreateFileMapping保留。容量断言与已记录的对齐/对象访问、Windows粒度、错误和32位边界不变。

180个生产/测试/CMake文件中4个改变，其余176个SHA-256不变。用户已正确注册MemoryMap，本轮未改CMake或FileManager；实际源数Linux29/Windows30。

实际Linux/GCC和原生MSVC x64 Debug/Release严格库构建、两个独立包含公开头的调用方编译链接通过；四配置现有CTest各12/12，库及调用方PUBLIC QLOG_DEBUG仅Debug定义。MSVC只在仓库外审查项目沿用既有C4324/C4146排除，没有修改生产警告或增加别名绕过选项。新MemoryMap调用方未运行；这些CTest不计映射行为、文件物化、故障、恢复、并发或性能验收。

修正前失败日志、备份及本轮验证：D:/qq344/Documents/qlog-memory-map-review-normal-buffer-guidance-20261012-231506。下一组为[NormalBuffer决定](NORMAL_BUFFER_ALIGNMENT_20261012_CHS.md)，完整实现指导留对话、用户填写新函数。

## 上一轮原指针指导快照（历史）

> **2026-10-12 最新规则：** 用户明确选择私有字节区按BQLog原指针转换访问。POSIX用size_t*/const size_t*，Windows用HANDLE&绑定HANDLE*；上一轮memcpy指导被本次决定替代。完整函数指导留对话，六个生产草稿不代写。

## 本次指针存取确认

QLog feat/spsc-ring-opt@a1ba6b4d45a25d60242327b68894c23d56c027e0；当前六个MemoryMap文件已创建但均为空、未跟踪。BQLog main@78cbfbef4f87e7558335f0c04bf6dd6894b46451；五个采用的MemoryMap参考文件相对HEAD无差异，整树仍有MISO与构建等本地修改。

原始指针访问完整六文件候选仅在仓库外准备：Linux/GCC和原生MSVC x64 Debug/Release严格编译、公开调用方链接四配置通过，候选库源数Linux29/Windows30，库及调用方PUBLIC Debug宏核对。没有新增-fno-strict-aliasing或其他别名绕过选项；MSVC仅外部沿用既有C4324/C4146排除。候选未运行、没有执行CTest、映射/退出/恢复/并发或性能检查；不把上轮memcpy验证冒充本次结果。

180个生产/测试/CMake文件SHA-256全不变，六个MemoryMap草稿仍为空，根CMake不变且MemoryMap未注册。此次只更新决策、AGENTS和路线；生产新定义由用户填写。证据：D:/qq344/Documents/qlog-memory-map-direct-guidance-20261012-200217。

日期：2026-10-12。完整声明、函数实现指导及设计讲解留对话，本文只记录决定与验证边界。

## 上一轮 FileManager 实际收口（历史）

QLog feat/spsc-ring-opt@f24f3afcd8e9418ea48ad5a6b05680c622008145；BQLog main@78cbfbef4f87e7558335f0c04bf6dd6894b46451。两树仍有既有未提交修改。采用 MemoryMap 五文件、FileManager 两文件及 NormalBuffer 两文件相对 BQ HEAD 均无差异，不称整树干净。

用户已填写 FileManager 剩余47定义，但写在公共头尾部；助手把这些现有定义与静态 thread_local 错误槽移回 src/io/file_manager.cpp，保留所有原CR/注释。修正描述符构造遗漏 seq=0、句柄比较误用赋值、is_invalid/get_file_lats_modified_epoch_ms 与声明不一致，并同步 real_path/temp_files 及 flush_file 的参数 file。现有公共头含模板/简单getter，其余定义在cpp。没有新增生产函数或改原子内存序、断言、默认参数及已确认的参考边界。

174个生产/测试/CMake文件仅 include/qlog/io/file_manager.hpp 与 src/io/file_manager.cpp 改变，其余172个SHA-256不变；两文件原12条行注释均保留。根CMake原已注册 FileManager，无需重复加入，实际源数Linux27/Windows28不变。修正前实际 GNU 严格语法失败日志已保存。

实际Linux/GCC和原生MSVC x64 Debug/Release严格库构建、两个独立包含公共头的调用方编译/链接通过，现有CTest四配置各12/12。库及调用方QLOG_DEBUG只在Debug出现。MSVC只在仓库外验证项目排除既有C4324/C4146；没有更改根警告配置。新FileManager调用方未运行，既有CTest不是FileManager行为/并发/生命周期验收。

## MemoryMap 模块决定

采用原 memory_map_handle 与 memory_map 两类、namespace qlog；公共声明放 include/qlog/platform/memory_map.hpp，POSIX/Windows分别有仅转包含的native头及 src/platform/posix/memory_map.cpp、src/platform/windows/memory_map.cpp。三项普通对齐计算定义移到公共 src/platform/memory_map.cpp，四个简单getter类内；保持原get_memory_map_alignedment拼写、参数、返回值、字段顺序和8字节platform_data_。不增加Manager、拥有包装、析构释放、自定义复制/移动或新错误接口。

用户最新明确要求“对齐BQLog，把私有字节数组直接转换成size_t*或HANDLE*后访问”。本组采用原POSIX的*(size_t*)写、*(const size_t*)读，以及Windows的HANDLE&绑定*(HANDLE*)；上一轮memcpy适配撤回。保留原__memory_map_size_unit局部名和CreateFileMapping宏调用，参数/字段/错误/default/断言不变。系统宏使用现有编译器宏，namespace/std名称与当前模块归属照常适配。容量static_assert不等于全部对齐/对象访问证明；C++20字节数组的隐式对象创建规则需要与实际类型和布局一起判断，不能笼统称指针转换必然UB或已证明跨平台安全。原双下划线名的保留标识符边界明确保留。

POSIX对齐、先0到最小文件长度以4096字节pread/pwrite物化、EINTR/短读补零/部分写、MAP_SHARED读写映射、MS_SYNC及munmap按当前原参考。不能换成只ftruncate；对已有数据读错时不得用零覆盖。Windows按原GetFileSizeEx、AllocationInfo（<=）、EndOfFileInfo（<）、CreateFileMapping、MapViewOfFile、FlushViewOfFile及释放顺序，保留MapView失败的原return。

## 明示的参考边界

- Windows仍用common_global_vars::page_size_（dwPageSize）作对齐单位；MapViewOfFile偏移要求dwAllocationGranularity，两者不保证相等。某些非零偏移会失败；原NormalBuffer使用offset=0。本轮不擅自换粒度。
- 映射句柄复制/移动不会创建独立OS映射或转移并清空全部地址；不允许把副本各自release。析构file_只管理文件引用，不自动unmap；最终由NormalBuffer明确释放。
- POSIX失败real_data_可为MAP_FAILED，判断成功用mapped_data_；size_为请求长度，平台区记录真正映射长度。release无映射直接返回、不清失败error_code；有映射按原清字段，即使底层释放失败也不新设恢复状态。
- 物化/扩容可能已经修改文件后再失败，不提供回滚。POSIX原pwrite==0不推进；零长度、size_t溢出、off_t范围及Windows32位右移32未增加新合同。本轮只验证x64，不计32位/ARM/移动端。
- WindowsMapView失败仍先CloseHandle再读GetLastError，可能影响原错误号；flush/release仍原void接口，Windows原忽略系统flush/unmap/close返回值。不擅自加FlushFileBuffers或持久化承诺。

## 上一轮 memcpy 候选验证（历史）

仓库外完整MemoryMap候选（6文件）加到当前实际库后，Linux/GCC与原生MSVC Debug/Release严格编译及公开调用方链接四配置通过，候选源数Linux29/Windows30，PUBLIC Debug宏核对。候选未运行、未执行映射行为或新增常驻测试；格式化前后语义token一致。生产MemoryMap文件仍未创建，根CMake不引用不存在的文件；用户填写后同轮审查并注册公共cpp与对应native cpp。

证据、修正前备份和完整指导候选：D:/qq344/Documents/qlog-memory-map-guidance-20261012-131329。

下一轮审查MemoryMap，随后直接NormalBuffer→MISO→LP/HP/TLS/LogBuffer，之后才Worker/Appender及完整运行态。整数越界double/原字符串两方案继续仅记录、暂不选定；上述构建/CTest不计Ring/Record恢复、存储/并发/完整日志或性能验收。
