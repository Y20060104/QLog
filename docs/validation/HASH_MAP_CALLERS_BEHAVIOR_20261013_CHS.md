# 原 HashMap 调用迁移验证记录

日期：2026-10-13。QLog 权威目录为 /home/qq344/QLog；本轮用户并发填写 MISO，助手只调整其他容器调用。

## 基准及修改范围

QLog feat/spsc-ring-opt@a1ba6b4d45a25d60242327b68894c23d56c027e0；BQLog main@78cbfbef4f87e7558335f0c04bf6dd6894b46451。两树均有本地改动。采用的 Layout、property/property_ex、common_vars、POSIX/Windows misc 及 string_impl 共十个参考文件相对提交均无差异；不采用 BQ 的本地 MISO 草稿。

实际修改十个生产文件：Layout、Property、PropertyValue 的头/cpp，common_vars.hpp，两 native file_io.cpp，以及 type_tools.hpp 的 std::string 哈希适配。另调整 tests/CMakeLists.txt 和现有 Layout 测试，新增 hash_map_integration_test.cpp。根 CMakeLists.txt 和原 HashMap 实现不改，原 CR 保留。

## 实际工作区编译

GNU 与原生 MSVC x64 Debug/Release 各严格编译七个实际 cpp（含本轮调用方测试）及五个独立公开头，合计每配置十二个翻译单元。公开头分别为 type_tools.hpp、layout.hpp、property.hpp、property_value.hpp、common_vars.hpp。模块检查直接读取 qlog 目标的 INTERFACE_INCLUDE_DIRECTORIES/INTERFACE_COMPILE_DEFINITIONS；Debug 定义 QLOG_DEBUG，Release 不定义，compile_commands 已核对。外部严格检查使用 GNU -Werror 和 MSVC /WX；MSVC 仅沿用已有 /wd4324 /wd4146，未放宽生产警告。

本轮首次实际整库构建遇到用户仍在填写的 MISO：TLS 草稿的 & 表达式及尚无返回的 alloc_write_chunk 导致失败。没有修改用户草稿，没有把当前整库构建记作通过。以下运行结果使用隔离副本。

## 隔离副本运行及差分

其余代码复制本轮实际工作区；只将 miso_ring_buffer.hpp、miso_ring_buffer.cpp、platform/macros.hpp 固定为上一轮已验证的备份。原生产文件不被覆盖。验证副本中本轮十三个修改文件与权威目录最终 SHA-256 全部一致。

| 配置 | 实际模块编译 | 隔离库 cpp 数 | 隔离常驻 CTest | 外部 BQ 参考 CTest |
| --- | --- | --- | --- | --- |
| GNU Debug | 12/12 | 31 | 19/19 | 4/4 |
| GNU Release | 12/12 | 31 | 19/19 | 4/4 |
| MSVC x64 Debug | 12/12 | 32 | 19/19 | 4/4 |
| MSVC x64 Release | 12/12 | 32 | 19/19 | 4/4 |

隔离库及公开 HashMap 两翻译单元调用方编译、链接通过；库和调用方 PUBLIC Debug 宏已核对。每配置合计 23/23 CTest。参考头由 git archive 固定提交取得，未经本地修改；MSVC 参考目标继续用外部 /FIintrin.h 满足原 type_tools 的 intrinsic 声明。

新增四组常驻回归：

- qlog.layout.thread_cache：768 个线程 ID 首次插入与 768 次改名后查询，验证扩容及 tidy_memory 后仍使用首次名称，当前 QLog 前缀格式不变。
- qlog.hash_map_integration.strings：777 个不同键，含空串、内嵌 NUL、全 256 字节值和 UTF-8；覆盖哈希、扩容后查找、遍历顺序、erase、重新插入、复制/移动。2855 次检查。
- qlog.hash_map_integration.properties：重复键、key_list 顺序、末尾未完成项跳过、转义序列化、字典扩容、构树、子节点地址稳定、深复制及树移动。1559 次检查；不触碰尚未选定的 int64_t 越界方案。
- qlog.hash_map_integration.registry：真实平台 open_file/close_file，独占登记、竞争写打开失败、无效句柄、关闭移除及重新打开。9 次检查；仅单进程顺序场景。

从四配置原始 Testing/Temporary/LastTest.log 对照 QLog/BQLog basic、collision、lifetime、strings 四组，检查次数及 digest 全部一致。strings 的四配置结果均为 checks=2855、digest=1939170642177708656；共十六对输出吻合。实际执行和日志保存发生在自动审批中断前；后续复用已有日志完成比较，没有把未执行的重复调用记作证据。

## 保护及验收边界

193 个原生产/测试/CMake 文件指纹核对：允许本轮命名文件和用户 MISO 三个文件变化，未发现其他变化；本轮十三个修改文件与已验证副本无漂移。用户只在本轮改变 MISO cpp，助手不写 MISO 头/cpp或平台宏。根 CMake 与原 CR 保留。未提交或推送。

本轮未运行 sanitizer；此前原 HashMap 三组的 ASan/UBSan 记录属于此前范围。上述结果不接受当前 MISO 草稿、TLS 退出、MISO 并发/恢复、MemoryMap/NormalBuffer、完整 FileManager 生命周期或性能；文件登记只完成所列顺序回归。

完整产物：D:\qq344\Documents\qlog-hash-map-callers-alignment-20261013-174155。包含 before 备份、reference-head、validation-source/指纹、首次实际构建失败日志、四配置实际模块编译及隔离运行日志、差分原始日志比较、validation-matrix.json 和 final-preservation.json。
