# 平台模块按 POSIX / Windows 拆分

日期：2026-10-08。用户授权助手拆分头文件及已有函数；未实现函数仍由用户完成。

## 实际核对与来源

QLog 为 feat/spsc-ring-opt@f24f3afcd8e9418ea48ad5a6b05680c622008145；权威目录 /home/qq344/QLog。BQLog 为 main@78cbfbef4f87e7558335f0c04bf6dd6894b46451，参考整树仍有 MISO、构建及未跟踪文件修改；本次采用的下列平台文件相对 HEAD 无差异：

- src/bq_common/platform/platform_misc.h:14-25：按系统包含原生类型头，随后提供公共枚举及函数声明。
- src/bq_common/platform/posix_misc.h:19：POSIX int32 句柄、-1 哨兵、>=0 快速检查。
- src/bq_common/platform/win64_misc.h:18：Windows void* 句柄、-1 哨兵、仅比较哨兵的快速检查。
- src/bq_common/platform/posix_misc.cpp 与 win64_misc.cpp：系统函数体独立，分配算法分别为 posix_memalign/free 与 _aligned_malloc/_aligned_free。

QLog 保留自己的 platform 模块及 aligned_memory/file_io 文件职责，按系统分目录；不引入新 Manager、拥有型包装或全局 detail。

## 当前文件职责

| 文件 | 职责与状态 |
| --- | --- |
| include/qlog/platform/aligned_memory.hpp | 两个公共分配声明，未修改 |
| include/qlog/platform/file_io.hpp | 用户的公共 I/O 声明；拆分开始时仅有 get_file_size/read_file，过程中用户补齐到 8 个声明，助手保留 |
| include/qlog/platform/file_defs.hpp | 公共打开模式、位运算、seek 枚举；按 _WIN32 选择原生类型头 |
| include/qlog/platform/posix/file_defs.hpp | POSIX 句柄别名、哨兵与 inline 检查 |
| include/qlog/platform/windows/file_defs.hpp | Windows 句柄别名、哨兵与 inline 检查 |
| src/platform/posix/aligned_memory.cpp | 从旧 cpp 搬移 POSIX 的两个已有函数 |
| src/platform/windows/aligned_memory.cpp | 从旧 cpp 搬移 Windows 的两个已有函数 |
| src/platform/posix/file_io.cpp | 只有 include 与 namespace 骨架，用户填写 8 个函数定义 |
| src/platform/windows/file_io.cpp | 只有 include 与 namespace 骨架，用户填写 8 个函数定义 |

旧 src/platform/aligned_memory.cpp 已搬移；旧空 src/platform/file_io.cpp 已换成两个系统骨架。调用方继续包含公共头。分配 API 的类型签名在两系统相同，无需再复制一对系统声明头；文件句柄类型确有差异，因此独立为原生类型头。

公共 file_defs.hpp 负责系统头选择；原生头上的系统条件也避免错误系统定义和重复别名。不改变 fd=0 有效、Windows 仅检查 -1 哨兵等参考规则，也不额外排除 Windows nullptr。

## CMake 与定义唯一性

根库保留 13 个共用 cpp，再按 WIN32 选择一个系统 aligned_memory.cpp；每个平台仍为 14 个 cpp。两个系统实现提供相同的 qlog::platform 函数，必须只编译当前系统那一份，避免重复定义及错误平台 API。

```cmake
if(WIN32)
    target_sources(qlog PRIVATE
        src/platform/windows/aligned_memory.cpp
    )
else()
    target_sources(qlog PRIVATE
        src/platform/posix/aligned_memory.cpp
    )
endif()
```

两个未实现 file_io.cpp 不加入目标。写完后分别加到同一 WIN32/else 分支，而不是把两个系统 cpp 同时加入。QLOG_DEBUG 的 Debug-only PUBLIC 传播保持不变，NDEBUG 仍按构建配置处理。

## 搬移与修正范围

两个分配函数的 API、参数名、原始算法均保持。审计中将旧 cpp 与新 cpp 分别按 POSIX/Windows 分支预处理，所得文本逐字相同；Windows 这里只是宏分支预处理，并非原生编译或运行。

实际旧 file_defs.hpp 有两处拼写错误，导致拆分前严格头文件语法检查失败：

- operator| 中 lhd 改为已声明的 lhs。
- Windows 常量 invalide_platform_file_handle 改为检查函数已引用的 invalid_platform_file_handle。

这两处只修复编译名称，不新增行为。保留用户 file_io.hpp 的后续新声明，未替用户填写 I/O 函数体；当前 write_file 声明中的长度名 read_size 可在手写定义时按指导同步为 write_size，类型/顺序不变。其他生产模块、测试与 CR 保留。

## 实际验证与边界

Linux WSL 上对实际工作树进行：

- Debug / Release：根库与临时公开调用方均以现有完整警告选项及 -Werror 构建、链接通过。
- 两种配置 CTest 各 11/11：常驻 10 项加临时拆分调用方 1 项，没有新增常驻测试；不沿用上一轮临时配置工程的 13/13 数字。
- 临时调用方包含实际公共头，核对原生句柄/标志，并实际链接 qlog::platform 的分配/释放，验证 64 对齐、37 字节可写与配对释放。
- compile_commands 核对每种配置根库恰有 14 个 cpp，其中系统源只有 posix/aligned_memory.cpp 一份，未编译 Windows 实现或 I/O 骨架；库及调用方的 QLOG_DEBUG/NDEBUG 一致。
- nm 核对实际静态库中 aligned_alloc/aligned_free 各一个强定义。
- POSIX 及模拟 _WIN32 的公共头严格语法检查通过；两个 I/O 骨架语法检查通过。模拟宏不等于 Windows SDK/ABI/原生 API 验证。
- 拆分后的 POSIX I/O 文档候选以实际公共头严格语法检查通过；这是指导检查，未将函数体写入生产 cpp 或执行新的 I/O 行为测试。

这是系统拆分及实际公开调用方验证。没有执行新的 8 函数 I/O 行为测试、Windows/macOS 原生编译运行、完整 Buffer/并发/恢复/BQLog 差分或性能验收。旧分配 85 检查与 I/O 指导候选 50 检查仍按其原日期记录，不作为本次重新运行。

审计目录：/tmp/qlog-platform-split-20261008-n3hxybh8。保存拆分前源码/CMake、before-manifest.json、after-split-manifest.json、user-changes-during-split.json、当前源文件校验值、函数体预处理一致性证据、头文件检查及 Debug/Release 构建/CTest 日志。源码未提交。

## 后续清理核对（2026-10-08）

用户要求删除 platform 下无关文件。重新逐一核对当前 9 个文件、include 引用及 CMake：旧平铺两个 cpp 已不存在；公共 API 及公共类型入口仍必需，系统类型头被公共入口选择，分配 cpp 已接目标，两个 I/O cpp 是用户接续填写位置。因此本轮额外删除 0 个文件，生产源码及草稿不变。已将文件 I/O 指导补充为明确的八函数顺序及返回规则；重新严格语法检查的是文档 POSIX 候选，未运行新测试。

## 下一步填写

按 [文件 I/O 完整指导](PLATFORM_FILE_IO_IMPLEMENTATION_GUIDE_20261007_CHS.md) 在 POSIX/Windows 的 file_io.cpp 中依次填写 get_file_size、read_file、write_file、seek_file、truncate_file、flush_file、lock_file、unlock_file。指导中的完整函数体已同步为两个系统文件；公共头只声明，系统 cpp 直接写本平台步骤。

写完后审查实际源码，再接对应 CMake 分支并验证公开调用方、短读/部分写/游标等真实规则。随后路径/Windows UTF 转换/open-close/exclusive → FileHandle/FileManager → MemoryMap → NormalBuffer → MISO；其余 Buffer 依赖及运行态路线保持。整数越界 double/原字符串仍仅记录、暂不选定。
