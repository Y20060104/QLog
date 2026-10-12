# Windows词法路径完整实现指导

> **2026-10-09最新源码更新：** 用户已写入公共声明及完整Windows定义；..分支容器条件已纠正为result_split，保留CR。实际Windows库/调用方两配置各1071对照通过；本文下方“仅候选/未写生产”指此前阶段，不再表示当前Windows函数未实现。POSIX依赖仍待补；下一组见[线程基础完整指导](PLATFORM_THREAD_OPS_IMPLEMENTATION_GUIDE_20261009_CHS.md)，证据见[最新记录](../validation/WINDOWS_LEXICAL_THREAD_GUIDANCE_20261009_CHS.md)。

日期：2026-10-09。用户手写未实现函数。当前路径基础已整理到POSIX/Windows目录并接根库，本组只把完整新函数写在本文和临时审查目录，没有新增生产声明/定义。

## 参考与依赖

BQLog main@78cbfbef4f87e7558335f0c04bf6dd6894b46451；src/bq_common/platform/win64_misc.cpp:212-250的get_lexically_path，以及include/bq_common/types/string_impl.h:377的split、:401的trim相对HEAD无差异；参考整树仍脏。

本组可以直接复用已存在的qlog::utility::trim和split_nonempty。POSIX词法函数还包含账户查询和util::log_device_console；后者依赖共同缓冲、锁与common_global_vars。get_base_dir同样依赖spin_lock_zero_init和common_global_vars的base_dir_initializer。先写这个独立完整Windows函数，再按真实依赖补POSIX/基准目录，不给不存在helper的假调用或新增Manager。

## 文件、接口与归属

公共声明追加到include/qlog/platform/file_path.hpp的qlog::platform内：

```cpp
std::string get_lexically_path(const std::string& original_path);
```

定义放src/platform/windows/file_path.cpp现有is_absolute之后、qlog::platform的结束大括号之前。它是公开平台自由函数，不放进qlog::platform::windows；后者只放Windows内部适配器。公共声明先覆盖接口，POSIX定义在其依赖齐备后单独实现，不把Windows规则装进POSIX cpp。

当前cpp保留所有已有函数和CR，补充所需include：

```cpp
#include <cstddef>
#include <vector>

#include "qlog/utility/string_utils.hpp"
```

## 完整函数

```cpp
std::string get_lexically_path(const std::string& original_path) {
    std::string result;
    result.reserve(original_path.size());

    std::string trans_path = original_path;
    std::replace(trans_path.begin(), trans_path.end(), '\\', '/');

    const bool end_with_slash = !trans_path.empty() && trans_path.back() == '/';
    std::vector<std::string> split = qlog::utility::split_nonempty(trans_path, '/');
    std::vector<std::string> result_split;
    result_split.reserve(split.size());

    for (std::size_t i = 0; i < split.size(); ++i) {
        split[i] = qlog::utility::trim(split[i]);

        if (split[i] == "..") {
            if (!result_split.empty() && result_split.back() != "..") {
                result_split.pop_back();
            } else {
                result_split.push_back(split[i]);
            }
        } else if (split[i] == ".") {
        } else {
            result_split.push_back(split[i]);
        }
    }

    for (std::size_t i = 0; i < result_split.size(); ++i) {
        if (i != 0) {
            result += '/';
        }
        result += result_split[i];
    }

    if (result.empty()) {
        result = "./";
    }
    if (!original_path.empty() && original_path[0] == '/') {
        result = "/" + result;
    }
    if (end_with_slash) {
        result += '/';
    }

    return result;
}
```

## 依赖顺序与步骤

1. result.reserve预留原输入长度的容量，size仍为0。复制到trans_path，把反斜杠统一成正斜杠；输入original_path保持不变。
2. end_with_slash从转换后的trans_path判断，所以原输入末尾正斜杠或反斜杠都属于这一标记。
3. split_nonempty先按/切分并丢弃原本的空段；之后逐段trim。不能先trim全文，也不能在段trim后再过滤空字符串。
4. result_split作为已处理片段的栈。普通段压入；.不入栈；..在非空且末段不是..时弹出末段，否则保留..。即使已处理段trim后为空，..也照参考弹出它。
5. 按/拼接片段；空结果写./。之后原输入首字符为/时前置/，最后按end_with_slash追加/。不要调整这些步骤的顺序。
6. 返回拥有存储的std::string，没有磁盘查询、账户展开、基准目录拼接、编码转换或长路径前缀处理。

result_split仅代表已处理片段；它可以包含..和trim后产生的空段，不是一个保证已经限制在根目录之下的路径。reserve不是resize，不能把预留空间当作已有元素访问。

## 严格对齐的边界

| 输入 | 当前参考输出 | 原因 |
| --- | --- | --- |
| 空或. | ./ | 空结果规则 |
| ./ | .// | 空结果./后再追加原末尾/ |
| / | /.// | ./前置原首/，然后追加末尾/ |
| 单个反斜杠 | .// | 首字符前置判断看原输入/，末尾标记看转换后路径 |
| 反斜杠a | a | 原首反斜杠不触发前置/ |
| 双反斜杠server反斜杠share | server/share | 参考此函数会丢失该UNC前缀 |
| a///b | a/b | 原空段被split丢弃 |
| a/空格/b | a//b | 原空格段非空，trim后仍压入空段 |
| a/../b | b | ..弹出a |
| /a/../../b | /../b | 参考不会把..限制在根目录 |
| C:relative | C:relative | 不补成完整绝对路径 |
| ~user/a | ~user/a | Windows本函数不展开用户目录 |

这些是BQLog实际输出，不把本函数解释成std::filesystem规范化、真实路径解析或安全根目录校验。按照用户已确认的严格对齐方向保留，不静默调整首反斜杠、UNC、重复斜杠或越过根的规则。

## 面试设计问题

1. 词法路径与真实路径解析有什么区别？前者只处理文本；真实路径解析还受磁盘对象、符号链接、权限等影响。这个函数没有访问文件系统。
2. 为什么用vector当栈？需要push/pop处理..，最后还要按原顺序遍历拼接；vector同时满足这两个需求。
3. 为什么先split再trim？参考先丢弃原始空段，再trim非空段。改变顺序会使a/空格/b的输出不同。
4. reserve和resize的区别是什么？reserve改变容量、不产生元素；resize改变元素数量。result和result_split在reserve后都仍为空。
5. pop_back会释放什么？删除并析构最后一个字符串元素，vector容量通常保留；它不是仅把一个游标减1而不结束元素寿命。
6. 为什么..不能遇到任何前段都直接弹出？此前段如果也是..，两个都表示尚不能抵消的父目录移动；参考保留它们。
7. is_absolute返回true是否意味着此函数一定保留原绝对路径前缀？不能。它们是两段各自独立的参考规则，Windows首反斜杠就是具体反例。
8. 复杂度如何？按总输入与输出长度线性处理；片段数组和中间字符串需要O(n)额外空间。这不是生产者记录热路径上的零分配操作。

## 指导验证与接续

临时完整候选与实际string_utils严格编译链接，Linux Debug/Release各1071场景通过：14个独立预期、33个边界、1024个生成输入；逐项对照从当前BQLog文件摘录的函数，并直接使用BQLog真实string/array模板，不用自造字符串桩。

包含空、点号、正反斜杠、盘符、UNC、空白段、Unicode、嵌入零。只算候选，不计生产新函数、完整Windows文件系统或BQLog双库验收。没有运行本候选的Windows原生或sanitizer测试。

用户填写后先审实际源码及输出，再核对POSIX/基准目录所需同步、诊断和全局状态，接open/close/exclusive登记、FileHandle/FileManager、MemoryMap、NormalBuffer、MISO。现有Windows file_path.cpp已接根库，本组不增加cpp或空实现文件。详见[本轮实际审查与接入](../validation/PLATFORM_PATH_REVIEW_CMAKE_20261009_CHS.md)。
