# 平台文件 I/O 实际代码审查

> **2026-10-09 最新源码覆盖旧问题：** 当前Windows read循环为<、截断API为SetFileInformationByHandle，历史两处问题均由用户修正；最新实际两系统Debug/Release编译链接及I/O各31定向检查通过，原生Windows也实际运行通过。根target仍未编译I/O，不能计正式库集成。本轮没有改生产代码，最新UTF及路径接续见[本轮实际审查](UTF8_PLATFORM_IO_CODE_REVIEW_20261009_CHS.md)。下方问题和编译失败均按2026-10-08快照保留。
> **2026-10-08 UTF接续更新：** 用户授权的UTF-16通用核心修改已落实，Layout按字节→码元适配并保留专用扫描；UTF-8函数仍待用户填写。最新实际构建/回归见[核心对齐记录](UTF_CORE_ALIGNMENT_20261008_CHS.md)。下方I/O审查结果与两处问题是原阶段记录，本次未改I/O源码或接入根CMake；下方UTF候选81/117结果也只属于此前接口。

日期：2026-10-08。QLog feat/spsc-ring-opt@f24f3afcd8e9418ea48ad5a6b05680c622008145；BQLog main@78cbfbef4f87e7558335f0c04bf6dd6894b46451。参考整树仍有 MISO/构建等本地修改；采用的平台源码和 util.h/util.cpp 相对 HEAD 无差异。

## 当前结论与两处问题

用户已填写两系统八函数及公共声明。助手未修改生产源码、CMake、测试或 CR。审查期间用户自行修正 POSIX __APPle__ 为 __APPLE__：已复核当前 Apple 分支选择 fsync；修正前后 Linux 预处理文本相同，保留实际 Linux 所测结果。模拟宏预处理不是 Apple SDK 编译。

| 优先级 | 当前源码位置 | 问题、影响与修正 |
| --- | --- | --- |
| P1 | src/platform/windows/file_io.cpp:27 | 读取循环 > 改 <；输出先清零，正长度读取当前都跳过 ReadFile，返回0且实际量0 |
| P1 | src/platform/windows/file_io.cpp:115 | SetFileInfoByHandle 改为完整名称 SetFileInformationByHandle；最新实际MSVC C2039/C3861 |

当前共2处/2项改动待用户修正。审查期间用户已修好Apple宏、GetLastError、seek低32位掩码、FileEndOfFileInfo枚举和解锁长度；重新核对后不再列为当前缺陷。以上是抄写问题，不是参考缺陷或新合同。POSIX读写累计、read返回EINTR/write原位重试、未知seek默认current、truncate不附带seek等正常规则与参考一致。

## 编译与行为证据

- 实际POSIX：Debug/Release C++20全警告-Werror语法、独立编译链接及定向各29/29通过。涵盖大小、短读/EOF、游标、截断缩短/扩展/游标不变、flush、同进程锁/解锁、零长度、无效句柄、非regular错误码。未验证信号EINTR、强制部分写、跨进程锁或并发。
- 最新实际Windows：本机MSVC14.51.36231/Windows SDK，Debug/Release /std:c++20 /W4 /WX编译均失败，仅剩SetFileInfoByHandle名称错误；两次编译前后源码哈希均一致。最初实际源码的三个名称错误属于本轮早期快照，已保存原日志。
- 早期临时Windows名称修正版：只改三个名称以便编译；Debug/Release各12检查中3失败：读取、256 MiB定位、精确解锁（原生错误158）。
- 临时Windows完整候选：再改循环及两个掩码；原生Debug/Release编译链接及同组各12/12通过。该候选不是生产源码，不能记为实际Windows I/O已修好，也非整个QLog Windows构建。
- 根库仍14cpp，没有编译任一file_io.cpp；没有改常驻测试/重跑根CTest。修好上述两处后再接两个系统CMake分支、实际公开调用方及宏传播。

Linux审计：/tmp/qlog-platform-io-review-20261008-gf5i4gt4。
Windows审计：D:\qq344\Documents\qlog-io-review-20261008-bf1db3a4。
保存源码校验值、用户修正记录、实际编译日志、两种候选和定向测试。未提交。

## CR逐项回答

1. struct stat是文件元数据结构，不是文件内容；st_mode含类型/权限，st_size是字节长度。fstat(fd,&buf)查询已打开fd指向的对象，成功0，失败-1并设置errno；stat(path,&buf)则按路径查询。见[stat(2)](https://man7.org/linux/man-pages/man2/stat.2.html)、[stat类型](https://man7.org/linux/man-pages/man3/stat.3type.html)。
2. S_ISREG(buf.st_mode)判断普通文件。EISDIR是“是目录”的错误码宏；BQLog在此对所有非regular对象统一返回该值，不意味着管道等也是目录，本组保留其规则。
3. read请求量是上限。文件剩余不足、管道当前数据较少、部分读取后信号打断等均可能导致短读；0表示EOF，正的短读不能一概解释为EOF。BQLog此层遇短读就返回。见[read(2)](https://man7.org/linux/man-pages/man2/read.2.html)。
4. SEEK_CUR相对当前游标、SEEK_SET相对开头、SEEK_END相对当前文件长度。lseek调整并返回新游标，失败-1；QLog本层仅保留错误返回。off_t是平台文件长度/偏移类型，不跨平台写死为long或假定总是64位。见[lseek(2)](https://man7.org/linux/man-pages/man2/lseek.2.html)。
5. ftruncate把文件总长度设为参数值，缩短丢弃尾部，扩展区域读为零，当前游标不变。例如长度100/游标80，截断到20后游标仍80；高层额外seek留给FileManager。见[truncate(2)](https://man7.org/linux/man-pages/man2/truncate.2.html)。
6. struct flock是fcntl记录锁参数结构，与flock()函数不同。F_WRLCK/F_UNLCK为加写锁/解锁；起点0、长度0到EOF及后续增长区域；F_SETLK立即尝试、不等待。这与同进程exclusive登记不同。见[fcntl锁](https://man7.org/linux/man-pages/man2/fcntl_locking.2.html)。
7. DWORD是Windows32位无符号整数，64位Windows上也不是64位。ReadFile/WriteFile本轮量用它，所以先限制到UINT32_MAX再转换，外层size_t累计。见[Windows类型](https://learn.microsoft.com/en-us/windows/win32/winprog/windows-data-types)。

原生名称及解锁范围依据：[SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)、[UnlockFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-unlockfile)。

## 下一组与已确认决定

用户最新改为完全对齐 BQLog 软件转换，撤销“遇到截断尾部就停止，保留已转换前缀”。不新增多字节尾部不足 break 或容量截断；正常调用以完整有效 UTF-8 和足够目标空间为前提。参考本身对残缺输入可能越界，不将取消检查描述为残缺输入的安全处理。修订指导候选 Linux Debug/Release 及 ASan+UBSan 各 81 项空/合法输入检查通过；旧 117 项仅属于旧候选，本次未写生产新函数、未重跑原生 Windows。

先补现有utility::utf8_to_utf16_sw，作为Windows路径依赖。完整手写指导见[UTF-8到UTF-16](../decisions/UTF8_TO_UTF16_IMPLEMENTATION_GUIDE_20261008_CHS.md)。未自动写生产新函数；路径~user拼接/账户查询和exclusive登记寿命进入相应模块再讨论。整数越界double/原字符串仍仅记录、暂不选定。
