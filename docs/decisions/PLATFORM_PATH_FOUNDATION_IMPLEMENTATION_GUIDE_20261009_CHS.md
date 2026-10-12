# 平台路径基础：完整实现指导

> **2026-10-09 路径基础审查及实际库接入（最新）：** 已实现路径函数算法与BQLog一致；助手按此前授权整理系统文件、将Windows内部头移到src并同步namespace，保留函数体/CR。I/O和路径cpp已在WIN32/else接根target，当前每个平台16cpp。原生整库验证发现TimeZone仍用POSIX日历调用，已按BQLog补_s/_r平台宏及Windows名称localtime分支，偏移/缓存算法不重写；现有Layout测试预期prefix改名expected_prefix消除MSVC遮蔽提示。最终Linux及原生Windows实际库/调用方Debug/Release CTest各13/13（常驻11+临时实际路径/I/O两项），两平台源列表和PUBLIC宏已核对。Windows临时验证/WX仅排除既有C4324对齐提示，根警告配置未变，不称无例外严格通过；143个无关原文件哈希不变。下一组由用户写Windows完整get_lexically_path，指导候选Linux两配置各1071场景与BQLog摘录及真实string类型一致，仅算候选。POSIX账户/诊断及基准目录真实依赖接续后再写，不造helper/Manager；严格对齐及整数越界暂不选定决定继续有效。见[实际接入记录](../validation/PLATFORM_PATH_REVIEW_CMAKE_20261009_CHS.md)和[下一组完整指导](WINDOWS_LEXICAL_PATH_IMPLEMENTATION_GUIDE_20261009_CHS.md)。下方待填写/待接入文字按原阶段保留。

日期：2026-10-09。用户填写未实现生产声明/函数体；助手本轮只审查、验证临时候选及维护文档。生产路径文件截至本轮核对尚不存在，不能把本文当作生产代码已写入。

## 本组边界及参考

本组完整提供：两系统is_absolute，以及Windows内部trans_to_windows_wide_string。namespace qlog::platform保留共用接口，Windows专用函数放qlog::platform::windows。没有class或Manager；普通函数头文件声明、cpp定义。

BQLog参考main@78cbfbef4f87e7558335f0c04bf6dd6894b46451，相关文件相对HEAD无差异，整树仍脏：

| 函数 | 参考位置 | 本组归属 |
| --- | --- | --- |
| POSIX is_absolute | src/bq_common/platform/posix_misc.cpp:294 | src/platform/posix/file_path.cpp |
| Windows is_absolute | src/bq_common/platform/win64_misc.cpp:253 | src/platform/windows/file_path.cpp |
| trans_to_windows_wide_string | src/bq_common/platform/win64_misc.cpp:32 | Windows cpp及src内部头 |

先把这组独立依赖写完整。get_lexically_path、get_base_dir、force_to_abs_path、open/close和exclusive登记不在本组中调用或留下空定义；后续按真实依赖补齐。

用户最新回复“严格对齐BQLog”：不采用~user分隔符修正或账户查询失败时保留未展开路径的建议。后续词法函数须保留参考行为说明：~user/sub可缺少分隔符，HOME与getpwuid均不能提供目录时参考会空指针解引用。该失败路径不是安全保证；本组不复制或执行账户查询分支。未知用户诊断、账户接口和基准目录全局状态进入后续组时核对完整依赖。

## 文件及可见性

- include/qlog/platform/file_path.hpp：跨系统公开声明，调用方只包含此头。
- src/platform/posix/file_path.cpp：POSIX定义。
- src/platform/windows/file_path_internal.hpp：Windows源文件之间共享的内部声明，不移到公开include。
- src/platform/windows/file_path.cpp：Windows公开函数与内部适配器定义。

BQLog适配器是同一cpp的static函数；QLog已按系统/职责拆分，后续file_io和file_path两个cpp都要使用它，因此使用src内部头及Windows命名空间共享。只改变代码放置与链接可见性，不改变函数算法、参数含义或路径规则。不要在两个cpp各复制一份转换代码，也不增加拥有型包装。

## 完整文件

include/qlog/platform/file_path.hpp

```cpp
#pragma once

#include <string>

namespace qlog::platform {
bool is_absolute(const std::string& path);
}  // namespace qlog::platform
```

src/platform/posix/file_path.cpp

```cpp
#include "qlog/platform/file_path.hpp"

namespace qlog::platform {
bool is_absolute(const std::string& path) {
    return !path.empty() && (path[0] == '/' || path[0] == '~');
}
}  // namespace qlog::platform
```

src/platform/windows/file_path_internal.hpp

```cpp
#pragma once

#include <string>

namespace qlog::platform::windows {
std::u16string trans_to_windows_wide_string(const std::string utf8_str);
}  // namespace qlog::platform::windows
```

src/platform/windows/file_path.cpp

```cpp
#include "qlog/platform/file_path.hpp"
#include "file_path_internal.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>

#include "qlog/utility/utf_conversion.hpp"

namespace qlog::platform {
bool is_absolute(const std::string& path) {
    const char first_char = path.empty() ? '\0' : path[0];
    if (first_char == '/' || first_char == '\\') {
        return true;
    }
    if (path.size() >= 2 && path[1] == ':' &&
        ((first_char >= 'A' && first_char <= 'Z') ||
         (first_char >= 'a' && first_char <= 'z'))) {
        return true;
    }
    return false;
}
}  // namespace qlog::platform

namespace qlog::platform::windows {
std::u16string trans_to_windows_wide_string(const std::string utf8_str) {
    std::u16string result;
    if (utf8_str.empty()) {
        return result;
    }

    result.resize(utf8_str.size() + 1U);
    const std::uint32_t trans_size = qlog::utility::utf8_to_utf16_sw(
        utf8_str.data(), static_cast<std::uint32_t>(utf8_str.size()),
        result.data(), static_cast<std::uint32_t>(result.size()));
    assert(trans_size < result.size());
    result.resize(trans_size);
    std::replace(result.begin(), result.end(), u'/', u'\\');
    return result;
}
}  // namespace qlog::platform::windows
```

## 参数、步骤与不变量

is_absolute(path)输入UTF-8路径文本，返回BQLog采用的路径分类，不查询磁盘、不展开~、不消除点号，也不证明路径存在。空路径false。POSIX只判断首字节/或~；Windows判断首字节/或反斜杠，或者ASCII字母加冒号。

| 输入 | POSIX | Windows |
| --- | --- | --- |
| 空或abc | false | false |
| /tmp/a | true | true |
| 反斜杠开头 | false | true |
| C:relative或C: | false | true |
| ~或~user/a | true | false |
| 1:a | false | false |

C:relative在BQLog中为true。它不同于严格Windows路径语义；不能未经用户确认替换成std::filesystem::path::is_absolute。POSIX把~归为true也是为后续路径处理服务，当前函数自身不展开它。

trans_to_windows_wide_string(utf8_str)按参考保留const std::string值参数，返回拥有自己存储的std::u16string。调用顺序：空串返回 → 分配输入字节数+1个char16_t → 调用已实现utility软件核心 → 断言实际数量小于预分配长度 → resize到实际码元数 → /替换为反斜杠 → 返回。

不变量：

- 正常输入完整有效、不重叠，输入长度与预分配长度能由uint32_t表示。不是任意长度/异常编码校验器。
- 有效UTF-8每个ASCII字节最多变为一个UTF-16码元，多字节字符占用更多输入字节，故输入字节数个码元足够，再多分配一个单元对应参考。
- 核心返回的trans_size计char16_t码元，不是字节，不乘2。
- resize(trans_size)删除未用空间，不丢弃已转换内容；不是恢复被撤销的容量/尾部截断规则。
- std::u16string管理自己的结尾零，转换核心不额外写终止符；嵌入零按显式长度保留。
- std::replace只改分隔符。a/../b变为a反斜杠..反斜杠b，点号处理不属于此函数。
- 不在此处追加Windows长路径前缀，也不补成绝对路径；参考在更上层另外组织。

std::u16string::resize会初始化存储，与BQLog fill_uninitialized的分配细节不同；这是标准字符串的存储适配，不新增安全回退/错误协议。参数名保持path、first_char、utf8_str、result、trans_size；格式沿用QLog。

## 构建接续与验证

生产实现由用户填写后再审查，系统cpp在根CMake的WIN32/else分支分别选择，同一目标不能同时编译两个is_absolute定义。内部头不需要作为编译单元。现有file_io.cpp已通过本轮实际定向验证但仍未接根target；正式接入时一并明确库源列表。不要为头文件新建空cpp，也不在调用方单独改变QLOG_DEBUG。

本轮临时指导候选：Linux GCC Debug/Release的POSIX判定各14检查，Windows算法各21检查通过；后者还在原生MSVC Debug/Release严格编译运行各21检查通过，链接实际已修正UTF软件核心。只是候选验证，未写生产新文件，不计生产路径模块验收。

后续组顺序：词法路径与基准目录（先核同步/全局状态/诊断依赖） → open/close/exclusive登记 → FileHandle/FileManager → MemoryMap → NormalBuffer → MISO。先完成已确认基础，不提前进入Worker/Appender。

## 面试设计问题

1. 为什么平台接口相同却拆两个cpp？调用方不关心原生差异，构建时选择实现；如果同时加入，会违反一个外部函数只有一个定义的要求。
2. 为什么不用通用转换函数同时整理路径？UTF编码转换与路径语义属于不同职责；同一核心可被Layout等模块复用，Windows分隔符只属于平台适配器。
3. 为什么内部头放src？后续两个Windows源文件需要共享，普通用户不需要这个接口；目录组织和C++链接属性分别解决不同问题。
4. 为什么UTF-8输入N字节可以分配N个UTF-16单元？ASCII为1对1，2/3字节字符为1单元，4字节为2单元；有效输入下输出单元总数不超过输入字节数。
5. 返回std::u16string会不会马上失效？不会，它拥有存储，返回值移动或消除复制；从它取得的c_str指针仅在拥有字符串仍存在且未发生使指针失效的修改时可用。
6. char16_t和wchar_t是否相同？是不同C++类型；本组使用固定16位的char16_t表达UTF-16。后续到Windows W接口才按实际平台ABI适配，不在跨平台公共头假定wchar_t恒为16位。
7. is_absolute是否证明安全路径或真实绝对路径？不能；它只复现BQLog的分支选择。目录存在、符号链接、权限、点号和长路径前缀都需要分别看实际后续函数。

实际审查和候选证据见[本轮记录](../validation/UTF8_PLATFORM_IO_CODE_REVIEW_20261009_CHS.md)。
