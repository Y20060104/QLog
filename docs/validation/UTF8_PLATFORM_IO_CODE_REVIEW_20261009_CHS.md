# UTF-8与平台I/O最新实际源码审查

> **2026-10-09 后续接入更新：** I/O及路径基础现已接入实际根target，每个平台16cpp；实际Linux和原生Windows Debug/Release CTest各13/13，后者临时排除C4324对齐提示。TimeZone平台调用同步和测试常量改名、源码保护范围及下一组见[最新实际记录](PLATFORM_PATH_REVIEW_CMAKE_20261009_CHS.md)。下方“14cpp/I/O未接入”仅描述本记录原阶段。

日期：2026-10-09。QLog feat/spsc-ring-opt@f24f3afcd8e9418ea48ad5a6b05680c622008145；BQLog main@78cbfbef4f87e7558335f0c04bf6dd6894b46451。采用的util.h/.cpp、platform_misc.h/.cpp、posix_misc.cpp、win64_misc.cpp相对参考HEAD无差异；BQLog整树仍有MISO/构建等本地修改。

## 最新结论

用户在审查过程中已修正utf8_to_utf16_sw三字节头掩码、拼码点的逻辑或、四字节头移位，以及Windows SetFileInformationByHandle拼写；读取循环已经为小于。以最后重新读取和实际验证的源码为准，不再将这些历史抄写问题列为当前缺陷。当前审查范围未发现新的阻塞缺陷。

- UTF-8输入长度计字节、输出及返回计char16_t码元；ASCII紧密循环、2/3/4字节拼接、代理对与BQLog软件核心一致。当前两/三字节分支的static_cast只包住首项，但或运算的有效值不超16位，实际GCC和MSVC严格编译通过；不能将其误报为编译失败。
- 零长度提前返回、非零指针assert保留已确认差异；容量参数按参考保留不用，不增加尾部停止、替换字符或容量截断协议。完整有效输入、足够目标空间、非重叠与对齐仍是正常调用前提。
- POSIX/Windows已打开句柄的八函数正常流程和参考一致。当前Windows截断名称和读取循环均正确。
- 根库仍编译14cpp，utf_conversion.cpp已在target中；两个file_io.cpp尚未接根target。本次通过临时调用方直接编译实际系统I/O源码，不等于根库已导出这些符号。

## 实际验证

全部使用最新实际生产源码，不是修正版候选：

| 对象 | 结果 | 范围 |
| --- | --- | --- |
| Linux GCC13.3 Debug/Release实际库及临时公开调用方 | 严格构建链接通过，外部CTest各13/13 | 常驻11项加临时实际UTF和实际POSIX I/O两项；PUBLIC QLOG_DEBUG仅Debug、NDEBUG仅Release已核对 |
| Linux UTF定向检查 | Debug/Release通过 | 16组独立预期输入及空指针零长度；精确源分配、输出哨兵、嵌入零、紧密ASCII、编码边界、回转 |
| Unicode标量遍历 | Linux/原生Windows Debug/Release均通过 | U+0000..U+10FFFF除代理区的1,112,064个有效标量；对照当前BQLog软件函数源码摘录，并回转到UTF-8 |
| Linux ASan+UBSan | 上述UTF定向与标量遍历通过 | 不输入残缺UTF-8，不据此声称异常输入安全 |
| 实际POSIX I/O | Debug/Release各31检查通过 | 大小、读写、短读/EOF、seek、truncate缩短/扩展/游标、flush、同进程lock/unlock、无效句柄与零长度 |
| 实际Windows I/O与UTF | MSVC原生Debug/Release严格编译链接及运行通过，I/O各31检查通过 | /std:c++20 /W4 /WX /permissive-；只是对应源文件和定向调用方，不是整个QLog原生Windows构建 |

BQLog对照为软件核心源码摘录，不是实际BQLog双库或SIMD差分。I/O未覆盖信号EINTR、强制部分写、跨进程锁、并发、崩溃/磁盘故障。常驻UTF测试目前仍只有UTF-16方向；本轮新检查保存在临时审查工程，没有添加常驻测试。

最初有一次Windows编译环境初始化失败（cl不可用），已用一次初始化、顺序编译重跑成功；环境失败不列为源码缺陷。早期抄写错误和旧候选结果不计入上述最新结果。

审计目录：D:\\qq344\\Documents\\qlog-review-20261009-current-6db480（WSL：/mnt/d/qq344/Documents/qlog-review-20261009-current-6db480）。保存145个生产/include/src/tests及根CMake文件的前后SHA-256、最新源码快照、参考摘录和哈希、测试程序、Linux与原生Windows日志、宏证据。审查收口时145文件无助手改动，原CR保留。原/tmp记录按历史文档解读，不依赖它们作为本轮新结果。

## 下一组及用户最新决定

用户回复“严格对齐BQLog”：路径~user拼接和账户失败边界不采用助手提出的修正/保留未展开路径方案。参考~user/sub在通常不带末尾斜杠的pw_dir下会缺少分隔符；HOME缺失且getpwuid失败会空指针解引用。明确保留参考来源和缺陷说明，不将其宣称为安全回退合同。本组只指导独立的is_absolute及Windows编码适配器，不实现该账户分支，不添加新错误协议。

完整下一组指导见[平台路径基础](../decisions/PLATFORM_PATH_FOUNDATION_IMPLEMENTATION_GUIDE_20261009_CHS.md)。之后补词法路径/基准目录的真实依赖，再补open/close/exclusive登记、FileHandle/FileManager、MemoryMap、NormalBuffer、MISO。整数越界double/原字符串仍“仅记录、暂不选定”。本次不修改生产源码、根CMake或常驻测试。
