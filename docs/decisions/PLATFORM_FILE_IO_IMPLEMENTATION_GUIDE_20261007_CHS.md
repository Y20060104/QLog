# 平台文件 I/O：原生类型与已打开句柄操作完整指导

> **2026-10-09 路径基础审查及实际库接入（最新）：** 已实现路径函数算法与BQLog一致；助手按此前授权整理系统文件、将Windows内部头移到src并同步namespace，保留函数体/CR。I/O和路径cpp已在WIN32/else接根target，当前每个平台16cpp。原生整库验证发现TimeZone仍用POSIX日历调用，已按BQLog补_s/_r平台宏及Windows名称localtime分支，偏移/缓存算法不重写；现有Layout测试预期prefix改名expected_prefix消除MSVC遮蔽提示。最终Linux及原生Windows实际库/调用方Debug/Release CTest各13/13（常驻11+临时实际路径/I/O两项），两平台源列表和PUBLIC宏已核对。Windows临时验证/WX仅排除既有C4324对齐提示，根警告配置未变，不称无例外严格通过；143个无关原文件哈希不变。下一组由用户写Windows完整get_lexically_path，指导候选Linux两配置各1071场景与BQLog摘录及真实string类型一致，仅算候选。POSIX账户/诊断及基准目录真实依赖接续后再写，不造helper/Manager；严格对齐及整数越界暂不选定决定继续有效。见[实际接入记录](../validation/PLATFORM_PATH_REVIEW_CMAKE_20261009_CHS.md)和[下一组完整指导](WINDOWS_LEXICAL_PATH_IMPLEMENTATION_GUIDE_20261009_CHS.md)。下方待填写/待接入文字按原阶段保留。

> **2026-10-09 UTF与平台I/O最新审查：** 用户已填写UTF-8软件函数并在审查期间修正位运算和Windows API抄写；重新核对最新实际源码，当前范围无新增阻塞缺陷。Linux实际Debug/Release严格库/调用方及外部CTest各13/13（常驻11+临时实际UTF/I/O两项），原生Windows对应源码严格编译运行通过；UTF对1,112,064个有效Unicode标量与BQLog软件源码摘录一致并回转，LinuxASan+UBSan同组通过，实际两系统I/O各31检查通过。根库仍14cpp，I/O仍未接根target；生产145文件SHA-256及CR未改，不把临时验证写成正式I/O集成或完整系统验收。下一组由用户填写路径判定和Windows内部编码适配器。用户最新回复“严格对齐BQLog”：不选择~user分隔符修正或账户查询失败回退；参考缺陷须明确说明，不宣称失败路径安全。整数越界两方案仍暂不选定。见[本轮记录](../validation/UTF8_PLATFORM_IO_CODE_REVIEW_20261009_CHS.md)和[完整下一组指导](PLATFORM_PATH_FOUNDATION_IMPLEMENTATION_GUIDE_20261009_CHS.md)。下方旧阶段结果按原日期解读。
> **2026-10-08 UTF通用核心生产对齐（最新）：** 用户授权助手修改代码。utf16_to_utf8_sw已统一为const char16_t*及码元数量、BQLog直接读取/代理项规则；容量参数保留未使用，调用方保证空间。Layout在insert_str_utf16处把字节数转成码元数，专用花括号扫描留在Layout，格式串主函数未改，CR保留。实际库/调用方Debug/Release严格构建和CTest各12/12（常驻11+临时参考摘录1）通过；参考软件核心65761组对照一致，ASan+UBSan定向7/7通过，PUBLIC宏传播已核对。根CMake未改，生产仍14cpp；UTF-8新函数仍待用户填写，平台I/O接入及其他验收边界不变。详见[实际对齐记录](../validation/UTF_CORE_ALIGNMENT_20261008_CHS.md)。下方旧字节utility接口/仅指导进度及81、117项结果按历史快照解读。

> **2026-10-08 文件I/O实际审查与UTF接续（此前阶段快照）：** 用户已填两系统8函数及公共声明。实际POSIX Debug/Release严格编译链接、定向各29检查通过；Apple宏已由用户在审查期间修正，Linux预处理不变。Windows最新原生MSVC/SDK编译失败，尚有2处：SetFileInfoByHandle应为SetFileInformationByHandle，以及读取循环>应为<；其他本轮发现已由用户修正并复核。助手未改生产源码/CMake/CR，I/O未接根target，根库仍14cpp；临时Windows完整候选各12/12仅算候选。用户最新要求UTF软件转换完全对齐BQLog，撤销尾部不足时停止并保留前缀的规则；不加多字节尾部break或容量截断，正常调用要求完整有效输入及足够目标空间。修订指导候选Linux Debug/Release及ASan+UBSan各81项空/合法输入检查通过，未写生产新函数；旧117项只属于已被替代的前缀候选。见[实际审查](../validation/PLATFORM_IO_CODE_REVIEW_20261008_CHS.md)及[UTF完整指导](UTF8_TO_UTF16_IMPLEMENTATION_GUIDE_20261008_CHS.md)。参考整树仍脏；路径/登记边界与整数越界两方案继续未决。

> **2026-10-08 清理核对与填写起点：** 当前 include/qlog/platform 与 src/platform 共 9 个文件，均为公共接口、系统类型、已实现分配或待填写 I/O 的必要文件；旧平铺 src/platform/aligned_memory.cpp、src/platform/file_io.cpp 已不存在，本轮没有额外文件可删除。生产源码及用户草稿未改。下一步只填写两个系统 file_io.cpp 的 8 个函数，公共声明已齐；write_file 的长度参数在声明/定义中同步为 write_size。下面分别给出两个完整 cpp，写完后再审查及接 CMake。

> **2026-10-08 按系统拆分（本段最新）：** 用户授权助手拆分已有头文件/函数，现采用公共 API、POSIX/Windows 原生类型头和系统 cpp。两个已实现分配函数仅搬移，CMake 按 WIN32 二选一，根库每个平台仍为 14 个 cpp；公共 API/命名空间不变。file_io.hpp 当前已由用户补到 8 个声明，两个系统 file_io.cpp 只有待填写骨架、未接库；助手未写 I/O 函数体。实际 Linux Debug/Release 严格构建与公开调用方链接、CTest 各 11/11（常驻 10 + 临时拆分调用方 1）通过；Windows 仅做模拟宏头文件语法检查，未编译运行原生 Windows 实现。详见 [系统拆分记录](PLATFORM_SYSTEM_SPLIT_20261008_CHS.md)，后续仍按 [文件 I/O 指导](PLATFORM_FILE_IO_IMPLEMENTATION_GUIDE_20261007_CHS.md) 由用户完成。下方原日期结果保留为历史证据。
日期：2026-10-07。QLog f24f3afc；BQLog 78cbfbef。本组采用 platform_misc.h、posix_misc.h/.cpp、win64_misc.h/.cpp，相关文件相对 HEAD 无差异，参考整树仍有MISO/构建修改。用户填写生产函数；本文件的完整代码只作为指导，候选仅位于临时目录。

## 本组边界与依赖

目标文件：

- include/qlog/platform/file_defs.hpp：共用打开模式、位操作、seek 选项，并选择当前系统的类型头。
- include/qlog/platform/posix/file_defs.hpp：POSIX 原生句柄别名、无效哨兵、快速检查。
- include/qlog/platform/windows/file_defs.hpp：Windows 原生句柄别名、无效哨兵、快速检查。
- include/qlog/platform/file_io.hpp：8 个共用普通自由函数声明。
- src/platform/posix/file_io.cpp：8 个 POSIX 函数定义，当前仅有骨架，由用户填写。
- src/platform/windows/file_io.cpp：8 个 Windows 函数定义，当前仅有骨架，由用户填写。

调用方只包含公共头；两平台函数名、类型签名和 qlog::platform 命名空间相同。系统 cpp 内直接写本系统步骤，不再在每个函数中嵌套 _WIN32 分支。尚未实现的两个 cpp 不接 CMake，完成后才加入对应的 WIN32/else 分支。

namespace qlog::platform，无class、public/private划分或拥有型包装。所有声明是命名空间可调用接口。只有类型/常量/两种位操作和简单检查在头内inline，其余在cpp。普通标量句柄的复制只是复制fd/HANDLE，不是dup、增引用或所有权转移；无析构关闭。

这是文件操作模块的句柄操作组。下一组按实际平台依赖同时补路径、Windows UTF转换、open/close及进程内exclusive登记；之后再做对应BQLog真实file_manager的FileHandle/FileManager和MemoryMap。当前不得用不存在的get_base_dir、get_lexically_path、add/remove_file_execlusive_check替代真实实现。也不得先给close_file写无登记版本，后续遗漏关闭时清理。

## 源码与参数映射

| QLog函数 | BQLog POSIX位置 | BQLog Windows位置 | 参数与返回 |
| --- | --- | --- | --- |
| get_file_size(handle) | posix_misc.cpp:109 | win64_misc.cpp:122 | native句柄、size_ref字节输出；int32错误码 |
| read_file | posix_misc.cpp:469 | win64_misc.cpp:405 | target_addr可写空间、read_size请求字节、out_real_read_size实际字节 |
| write_file | posix_misc.cpp:489 | win64_misc.cpp:426 | src_addr只读空间、write_size请求字节、out_real_write_size累计字节 |
| seek_file | posix_misc.cpp:523 | win64_misc.cpp:456 | opt起点、offset有符号字节距离；int32错误码 |
| truncate_file | posix_misc.cpp:299 | win64_misc.cpp:515 | offset是新文件总长度，不是相对距离；int32错误码 |
| flush_file | posix_misc.cpp:546 | win64_misc.cpp:482 | 请求原生文件同步；int32错误码 |
| lock_file | posix_misc.cpp:185 | win64_misc.cpp:196 | 原生整文件范围写锁；bool |
| unlock_file | posix_misc.cpp:203 | win64_misc.cpp:204 | 原生解锁；bool |

int32接口0表示该原生操作未报错，非0沿用平台errno/GetLastError。read/write返回值不是字节数，字节数由输出引用传出；返回0不保证读写满请求。不要加throw、统一错误对象或失败自动重开协议。

POSIX句柄是int32，-1无效、0可能有效；Windows是void*，-1对应INVALID_HANDLE_VALUE。BQLog快速检查Windows只比较该哨兵，不额外排除nullptr；快速检查不证明句柄仍在OS中有效。

## 不变量、调用次序与边界

get_file_size/read/write每次首先清零输出，即便后续失败也保留此前成功累计量。调用方保证非零请求的指针指向至少请求长度的可用存储，源/目标符合对应读写权限；本层不管理内存或FileHandle引用寿命。读写按当前原生文件游标执行，本层不隐含seek，组合seek+write不会因包装成函数就成为线程原子操作。

原生调用次序：后续open获得句柄→按需seek/get_size→read/write/truncate→按需flush→后续close；open/close尚未在本组提供。当前候选探针使用原生mkstemp/close建立测试fixture，生产函数不依赖未提供的QLog helper。

1. get_file_size：清零size_ref；POSIX fstat，失败errno，成功但非S_ISREG则EISDIR，最后st_size转size_t；Windows GetFileSizeEx，失败GetLastError，成功QuadPart转size_t。不把POSIX非regular改成统一invalid_argument，也不擅自为Windows新增同一regular过滤。
2. read_file：清零实际读量，max chunk POSIX SSIZE_MAX/Windows UINT32_MAX；逐次min(上限,剩余)，原生read/ReadFile，失败返回错误，成功推进目标和累计量。读到少于本轮请求立即返回0，包括EOF0；只有本轮读满且总量仍不足时继续。POSIX此处EINTR直接返回errno，不能照write擅自重试。Linux内核单次实际上限可能小于SSIZE_MAX；由短读规则决定本层结束，不发明“read_exact”合同。
3. write_file：清零实际写量，current_src与remaining跟踪进度；分段write/WriteFile。POSIX负返回且errno=EINTR不推进状态并重试，其他错误返回；成功推进累计/指针/剩余，written0结束返回0。部分进度后的错误同时保留错误码与前缀字节量。调用方只能重试未写后缀，不能整批重新写造成重复。
4. seek_file：默认平台选项current，switch映射begin/end/current，未知枚举保留默认；POSIX lseek，负返回errno；Windows保持参考SetFilePointer拆64位，SetLastError(NO_ERROR)后同时检查INVALID_SET_FILE_POINTER和GetLastError非0。offset为负可用于end/current向前移动，只要最终位置原生允许。不可只见0xFFFFFFFF就认定失败。
5. truncate_file：POSIX ftruncate，Windows SetFileInformationByHandle(FileEndOfFileInfo)；offset为新长度，可缩短也可扩展，默认转换沿用参考、不增加新的跨平台范围错误合同。原生truncate不由本组额外seek。参考FileManager层再核对文件长度并seek到end，留到该模块。
6. flush_file：Linux fdatasync，Apple fsync，Windows FlushFileBuffers；错误直接返回。不是fflush，不是write缓存刷新，也不是mmap刷新。未来public force_flush是否调用本函数要按真实调用链判断，不能仅凭flush命名承诺持久化。
7. lock_file/unlock_file：先快速检查哨兵；POSIX零初始化flock，l_type写锁/解锁，SEEK_SET/start0/len0/pid=getpid，F_SETLK；Windows LockFile/UnlockFile覆盖0到最大范围。原生文件锁与mutex、同进程exclusive登记不同，不能替换未来登记。flock{}避免无关未初始化字节，不改变参考有效字段和算法。

## 填写顺序与返回规则

先完成 POSIX cpp，再按 Windows cpp 对照本平台 API；两个文件均为 qlog::platform 中的自由函数定义。建议顺序为 get_file_size → read_file → write_file → seek_file → truncate_file → flush_file → lock_file → unlock_file。普通函数不移入头文件，不调用尚未提供的 QLog open/close 或路径 helper。

read/write 的输出量在进入函数时清零；每轮成功后立即累计。返回错误时保留此前已完成的前缀量，返回 0 仍可能是短读/零进展。POSIX read 的 EINTR 直接返回，write 的 EINTR 原位重试；Windows read 按参考使用本轮实际量 != 请求量退出，正常 API 范围下与小于判断等价，此处保留参考写法。未知 seek 枚举沿用 current 默认，不新增错误合同。

POSIX flock 使用 fl{} 是相对参考仅写字段的局部初始化差异，锁种类、范围及 F_SETLK 算法不变。参考 write_file 的 BQ_UNIT_TEST 故障注入属于测试设施，本组对照正常生产分支；没有新增 QLog 测试注入宏或错误协议。

本轮重新核对参考五个平台文件相对 HEAD 均无差异；BQLog 整树仍脏。当前 POSIX 指导候选严格语法检查通过，未运行新的 I/O 行为测试，Windows cpp 未做原生编译运行；既有拆分验证不等于尚未填写的 I/O 已验收。

## 拆分后的声明与完整实现指导

下面三个类型头是当前实际源码；公共 I/O 声明和两个完整 cpp 是填写指导。生产 cpp 目前仍只有 include 与 namespace 骨架，不能把文档代码算作生产实现。

### include/qlog/platform/file_defs.hpp

```cpp
#pragma once

#include <cstdint>

#if defined(_WIN32)
#include "qlog/platform/windows/file_defs.hpp"
#else
#include "qlog/platform/posix/file_defs.hpp"
#endif

namespace qlog::platform {
    enum class file_open_mode_enum{
        read=1,
        write=1<<1,
        read_write=3,
        auto_create=1<<2,
        exclusive=1<<3
    };

    enum class file_seek_option:std::uint8_t{
        current,
        begin,
        end
    };

    inline file_open_mode_enum operator|(file_open_mode_enum lhs, file_open_mode_enum rhs){
        return static_cast<file_open_mode_enum>(static_cast<std::int32_t>(lhs)|static_cast<std::int32_t>(rhs));
    }

    inline file_open_mode_enum operator&(file_open_mode_enum lhs,file_open_mode_enum rhs){
        return static_cast<file_open_mode_enum>(static_cast<std::int32_t>(lhs)& static_cast<std::int32_t>(rhs));
    }
}  // namespace qlog::platform
```

### include/qlog/platform/posix/file_defs.hpp

```cpp
#pragma once

#include <cstdint>

#if !defined(_WIN32)
namespace qlog::platform {
using platform_file_handle=std::int32_t;

inline constexpr platform_file_handle invalid_platform_file_handle=-1;

inline bool is_platform_handle_valid(const platform_file_handle& file_handle){
    return file_handle>=0;
}
}  // namespace qlog::platform
#endif
```

### include/qlog/platform/windows/file_defs.hpp

```cpp
#pragma once

#include <cstdint>

#if defined(_WIN32)
namespace qlog::platform {
using platform_file_handle=void*;

inline const platform_file_handle invalid_platform_file_handle=reinterpret_cast<void*>(static_cast<std::intptr_t>(-1));

inline bool is_platform_handle_valid(const platform_file_handle& file_handle){
    return file_handle!=invalid_platform_file_handle;
}
}  // namespace qlog::platform
#endif
```

### include/qlog/platform/file_io.hpp

当前用户已补齐 8 个声明。下面参数名按 BQLog 实际含义给出；write_file 的请求长度应为 write_size，当前草稿暂写 read_size，填写定义时一并同步即可，类型及顺序不变。

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include "qlog/platform/file_defs.hpp"

namespace qlog::platform {
std::int32_t get_file_size(const platform_file_handle& file_handle, std::size_t& size_ref);
std::int32_t read_file(const platform_file_handle& file_handle, void* target_addr,
                      std::size_t read_size, std::size_t& out_real_read_size);
std::int32_t write_file(const platform_file_handle& file_handle, const void* src_addr,
                       std::size_t write_size, std::size_t& out_real_write_size);
std::int32_t seek_file(const platform_file_handle& file_handle, file_seek_option opt,
                      std::int64_t offset);
std::int32_t truncate_file(const platform_file_handle& file_handle, std::size_t offset);
std::int32_t flush_file(const platform_file_handle& file_handle);
bool lock_file(const platform_file_handle& file_handle);
bool unlock_file(const platform_file_handle& file_handle);
}  // namespace qlog::platform
```

### src/platform/posix/file_io.cpp

以下是 POSIX 填写指导，尚未写入生产文件。

```cpp
#include "qlog/platform/file_io.hpp"
#include <algorithm>
#include <cerrno>
#include <climits>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace qlog::platform {
std::int32_t get_file_size(const platform_file_handle& file_handle, std::size_t& size_ref) {
    size_ref = 0;
    struct stat buf;
    if (::fstat(file_handle, &buf) != 0) {
        return errno;
    }
    if (!S_ISREG(buf.st_mode)) {
        return EISDIR;
    }
    size_ref = static_cast<std::size_t>(buf.st_size);
    return 0;
}

std::int32_t read_file(const platform_file_handle& file_handle, void* target_addr,
                      std::size_t read_size, std::size_t& out_real_read_size) {
    out_real_read_size = 0;
    const std::size_t max_size_pertime = static_cast<std::size_t>(SSIZE_MAX);
    char* current_target = static_cast<char*>(target_addr);
    while (out_real_read_size < read_size) {
        const std::size_t need_read_size_this_time =
            (std::min)(max_size_pertime, read_size - out_real_read_size);
        const ssize_t out_size = ::read(file_handle, current_target, need_read_size_this_time);
        if (out_size < 0) {
            return errno;
        }
        out_real_read_size += static_cast<std::size_t>(out_size);
        current_target += out_size;
        if (static_cast<std::size_t>(out_size) < need_read_size_this_time) {
            return 0;
        }
    }
    return 0;
}

std::int32_t write_file(const platform_file_handle& file_handle, const void* src_addr,
                       std::size_t write_size, std::size_t& out_real_write_size) {
    out_real_write_size = 0;
    const char* current_src = static_cast<const char*>(src_addr);
    std::size_t remaining = write_size;
    while (remaining > 0) {
        const std::size_t chunk_size =
            (std::min)(remaining, static_cast<std::size_t>(SSIZE_MAX));
        const ssize_t written = ::write(file_handle, current_src, chunk_size);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return errno;
        }
        out_real_write_size += static_cast<std::size_t>(written);
        current_src += static_cast<std::size_t>(written);
        remaining -= static_cast<std::size_t>(written);
        if (written == 0) {
            break;
        }
    }
    return 0;
}

std::int32_t seek_file(const platform_file_handle& file_handle, file_seek_option opt,
                      std::int64_t offset) {
    int opt_platform = SEEK_CUR;
    switch (opt) {
        case file_seek_option::current: opt_platform = SEEK_CUR; break;
        case file_seek_option::begin: opt_platform = SEEK_SET; break;
        case file_seek_option::end: opt_platform = SEEK_END; break;
        default: break;
    }
    if (::lseek(file_handle, static_cast<off_t>(offset), opt_platform) < 0) {
        return errno;
    }
    return 0;
}

std::int32_t truncate_file(const platform_file_handle& file_handle, std::size_t offset) {
    if (::ftruncate(file_handle, static_cast<off_t>(offset)) != 0) {
        return errno;
    }
    return 0;
}

std::int32_t flush_file(const platform_file_handle& file_handle) {
#if defined(__APPLE__)
    if (::fsync(file_handle) != 0) {
        return errno;
    }
#else
    if (::fdatasync(file_handle) != 0) {
        return errno;
    }
#endif
    return 0;
}

bool lock_file(const platform_file_handle& file_handle) {
    if (!is_platform_handle_valid(file_handle)) {
        return false;
    }
    struct flock fl{};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    fl.l_pid = ::getpid();
    return ::fcntl(file_handle, F_SETLK, &fl) != -1;
}

bool unlock_file(const platform_file_handle& file_handle) {
    if (!is_platform_handle_valid(file_handle)) {
        return false;
    }
    struct flock fl{};
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    fl.l_pid = ::getpid();
    return ::fcntl(file_handle, F_SETLK, &fl) != -1;
}
}  // namespace qlog::platform
```

### src/platform/windows/file_io.cpp

以下是 Windows 填写指导，尚未写入生产文件。

```cpp
#include "qlog/platform/file_io.hpp"
#include <algorithm>

#include <windows.h>

namespace qlog::platform {
std::int32_t get_file_size(const platform_file_handle& file_handle, std::size_t& size_ref) {
    size_ref = 0;
    LARGE_INTEGER file_size;
    if (!::GetFileSizeEx(file_handle, &file_size)) {
        return static_cast<std::int32_t>(::GetLastError());
    }
    size_ref = static_cast<std::size_t>(file_size.QuadPart);
    return 0;
}

std::int32_t read_file(const platform_file_handle& file_handle, void* target_addr,
                      std::size_t read_size, std::size_t& out_real_read_size) {
    out_real_read_size = 0;
    const std::size_t max_size_pertime = static_cast<std::size_t>(UINT32_MAX);
    char* current_target = static_cast<char*>(target_addr);
    while (out_real_read_size < read_size) {
        const std::size_t need_read_size_this_time =
            (std::min)(max_size_pertime, read_size - out_real_read_size);
        DWORD out_size = 0;
        if (!::ReadFile(file_handle, current_target,
                        static_cast<DWORD>(need_read_size_this_time), &out_size, nullptr)) {
            return static_cast<std::int32_t>(::GetLastError());
        }
        out_real_read_size += static_cast<std::size_t>(out_size);
        current_target += out_size;
        if (static_cast<std::size_t>(out_size) != need_read_size_this_time) {
            return 0;
        }
    }
    return 0;
}

std::int32_t write_file(const platform_file_handle& file_handle, const void* src_addr,
                       std::size_t write_size, std::size_t& out_real_write_size) {
    out_real_write_size = 0;
    const char* current_src = static_cast<const char*>(src_addr);
    std::size_t remaining = write_size;
    while (remaining > 0) {
        const DWORD chunk_size = static_cast<DWORD>(
            (std::min)(remaining, static_cast<std::size_t>(UINT32_MAX)));
        DWORD written = 0;
        if (!::WriteFile(file_handle, current_src, chunk_size, &written, nullptr)) {
            return static_cast<std::int32_t>(::GetLastError());
        }
        out_real_write_size += static_cast<std::size_t>(written);
        current_src += static_cast<std::size_t>(written);
        remaining -= static_cast<std::size_t>(written);
        if (written == 0) {
            break;
        }
    }
    return 0;
}

std::int32_t seek_file(const platform_file_handle& file_handle, file_seek_option opt,
                      std::int64_t offset) {
    LONG offset_low = static_cast<LONG>(offset & 0xFFFFFFFFLL);
    LONG offset_high = static_cast<LONG>(offset >> 32);
    DWORD opt_platform = FILE_CURRENT;
    switch (opt) {
        case file_seek_option::current: opt_platform = FILE_CURRENT; break;
        case file_seek_option::begin: opt_platform = FILE_BEGIN; break;
        case file_seek_option::end: opt_platform = FILE_END; break;
        default: break;
    }
    ::SetLastError(NO_ERROR);
    if (::SetFilePointer(file_handle, offset_low, &offset_high, opt_platform) ==
            INVALID_SET_FILE_POINTER && ::GetLastError() != NO_ERROR) {
        return static_cast<std::int32_t>(::GetLastError());
    }
    return 0;
}

std::int32_t truncate_file(const platform_file_handle& file_handle, std::size_t offset) {
    FILE_END_OF_FILE_INFO eof_info;
    eof_info.EndOfFile.QuadPart = static_cast<LONGLONG>(offset);
    if (!::SetFileInformationByHandle(file_handle, FileEndOfFileInfo, &eof_info,
                                      static_cast<DWORD>(sizeof(eof_info)))) {
        return static_cast<std::int32_t>(::GetLastError());
    }
    return 0;
}

std::int32_t flush_file(const platform_file_handle& file_handle) {
    if (!::FlushFileBuffers(file_handle)) {
        return static_cast<std::int32_t>(::GetLastError());
    }
    return 0;
}

bool lock_file(const platform_file_handle& file_handle) {
    if (!is_platform_handle_valid(file_handle)) {
        return false;
    }
    return ::LockFile(file_handle, 0, 0, 0xFFFFFFFFU, 0xFFFFFFFFU) != 0;
}

bool unlock_file(const platform_file_handle& file_handle) {
    if (!is_platform_handle_valid(file_handle)) {
        return false;
    }
    return ::UnlockFile(file_handle, 0, 0, 0xFFFFFFFFU, 0xFFFFFFFFU) != 0;
}
}  // namespace qlog::platform
```

## 设计问题与回答

- 为什么用返回码和输出字节数两个通道？写出部分前缀后仍可能出错，单一bool不能告诉上层后缀从哪里重试；返回0也可能是短读或零进展。
- size_t与ssize_t区别？请求长度不能为负；POSIX结果还需表示-1，必须先检查符号，再转size_t。
- read/write为什么不完全对称？这里对齐实际BQLog：短读退出、读EINTR返回；写EINTR重试、部分写继续。统一成“直到全部完成”会改变参考行为。
- 文件游标是否因为系统调用而免于组合竞争？不能据此保证seek+write这一对操作不可交错。本层没有用户态互斥，也不偷偷替换为pread/pwrite。
- truncate是否将游标放到EOF？本层没有seek动作；BQLog高层FileManager::truncate_file才补这一行为。
- lock_file是否锁住所有不合作的程序？POSIX参考是传统建议性记录锁，不是全局强制mutex；进程内的排他规则还需参考登记表。
- 为什么目前不加入析构关闭？原生别名没有所有权；下一层FileHandle的复制/移动/引用与FileManager描述符表负责资源生命周期，不能在两层同时关闭同一原生句柄。
- get_file_size失败为什么先清零？调用方不能把上次成功值当作本次结果；读写累计量则代表本次已经完成的真实进度。

## 指导验证与后续

候选只在 /tmp/qlog-platform-review-20261007-fpmfnmno/file-io-candidate，Debug/Release严格编译及定向CTest各1/1、UBSan同组1/1，各50检查通过。内容见 [实际审查及验证记录](../validation/PLATFORM_ALIGNED_MEMORY_CMAKE_20261007_CHS.md)。Windows/macOS代码分支只依据参考核对，未编译运行；未跑信号EINTR/跨进程锁/BQLog双库差分。禁止把候选结果记作你尚未填写的生产file_io已通过。

用户在两个系统 cpp 写完全部 8 个函数后，再审实际源码、接对应 CMake 分支和公开调用方；当前根库只选择已实现的 posix/aligned_memory.cpp 或 windows/aligned_memory.cpp。后续路径/open-close/exclusive→FileHandle/FileManager→MemoryMap→NormalBuffer→MISO；memory_pool、miso_linked_list、block_list/group_list和oversize也是正式LogBuffer的实际依赖。
