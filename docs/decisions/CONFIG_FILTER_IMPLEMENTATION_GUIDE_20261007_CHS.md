# 等级位图与配置过滤 helper 完整实现指导

日期：2026-10-07。本文保留完整填写指导；截至本次实际审查用户已写生产声明/定义，但有 mask 拼写、当前类别引用与 clear 缺定义三处问题，实际过滤尚未编译/链接通过。详见 [本次实际审查](../validation/FILTER_CODE_REVIEW_20261007_CHS.md)。实际 Property/PropertyValue 已修正三处编译问题，Debug/Release 实际源码及公开调用方检查通过；配置 cpp 尚未接根 qlog target。见 [生产编译修正记录](../validation/CONFIG_TREE_COMPILE_20261007_CHS.md)。

QLog 实际 HEAD：ec3f72b27784b3b1a9047391117395b4848afb8d，分支 feat/spsc-ring-opt，工作树有草稿。
BQLog 参考 HEAD：78cbfbef4f87e7558335f0c04bf6dd6894b46451。本文涉及的 log_level_bitmap.h/.cpp、log_utils.h/.cpp、log_imp.cpp、appender_base.cpp 和底层字符串/数组实现已核对；被采用的参考文件无 HEAD 差异，不能称整树干净。

## 2026-10-07 用户确认：保留两套类别规则

用户明确要求对齐 BQLog 的 Logger/Snapshot 与 Appender 类别规则差异。以下为已接受兼容方向，不把两套规则统一为一个 matcher 或新增策略参数。

源码来源：log_utils.cpp:16-55；log_imp.cpp:150-159、435-446；log_snapshot.cpp:117-133；appender_base.cpp:120-149。已重新核对参考 HEAD 78cbfbef4f87e7558335f0c04bf6dd6894b46451，上述文件与 HEAD 无差异，整树仍有 MISO/构建等本地修改。

| 项目 | Logger/Snapshot helper | Appender set_basic_configs |
| --- | --- | --- |
| mask=net | net、net.http，排除 network | net、net.http、network |
| mask=* | 没有通配语义，只按普通名称/点号规则 | 允许全部类别 |
| mask=*default | 另外允许索引0，仍保留普通匹配 | 无特殊含义，只按普通前缀 |
| 大小写与两端空白 | 区分大小写，helper不trim | 区分大小写，不trim |
| 无字符串mask | 全部允许 | 全部允许 |
| 单个空字符串mask | 空名称及以点号开头的名称 | 全部允许 |

BQ string::begin_with 在 string_impl.h:296 使用长度检查与 memcmp，c_str() 对空串返回非空 empty_str，因此上述空串前缀行为也成立；QLog 的 string::compare 对应完整长度前缀比较，不按 C 字符串 NUL 截断。两套规则均没有一般 glob/regex 语义，net* 不会自动表示 net 前缀。数组 [] 与 [ ] 也有区别：后者经构树成为含空字符串的数组。

后台处理先在 log_imp::log 检查 Logger 类别，拒绝时直接返回；通过后逐个 Appender 检查自身类别/等级/enabled，再进入 log_impl；Snapshot 启用时另检查其等级/类别。故 Appender 的*和 Snapshot 的允许规则均不能绕过 Logger 类别拒绝。Producer 粗等级位图仍是所有 Appender 等级的 OR，不合并 Appender 类别，也不排除 disabled 目标。

等级 helper 在三处复用，fallback 仍在调用方：log.print_stack_levels 对非数组保留helper清零结果；Appender levels 非数组时调用方补all；Snapshot buffer_size非零且levels非数组时调用方补all。snapshot 配置是根 config["snapshot"]，与 config["log"] 并列，见 log_imp.cpp:208/284。本组只建立等级位图和 config helper，Appender/Snapshot 对象后续按路线逐模块实现。

## 本组目标与归属

| 生产目标文件（当前已由用户建立） | 归属与用途 | BQLog 对应 |
| --- | --- | --- |
| include/qlog/runtime/log_level_bitmap.hpp | qlog::runtime::LogLevelBitmap，运行态等级集合 | src/bq_log/log/log_level_bitmap.h |
| src/runtime/log_level_bitmap.cpp | 普通成员与 cpp 内大小写比较 | src/bq_log/log/log_level_bitmap.cpp |
| include/qlog/config/filter_config.hpp | qlog::config 的两个过滤转换函数声明 | src/bq_log/utils/log_utils.h:125/127 |
| src/config/filter_config.cpp | 配置树转等级位图/类别 byte 数组 | src/bq_log/utils/log_utils.cpp:16/58 |

已有 qlog::LogLevel、config::PropertyValue 和 utility::trim 直接复用。bitmap 不依赖 PropertyValue；config helper 依赖 bitmap。等级位图后续同时服务 Producer 粗筛选、Appender 和堆栈等级，因此放 runtime。只有纯字符串比较是本 cpp 私有辅助，不复制整个 log_utils 或建立 Manager/拥有型配置包装。

## 明确规则

- bitmap_ 是一个 uint32 值，默认/clear 为 0，构造/复制/赋值保留全部位。all 写 0xFFFFFFFF，不收缩成六个命名等级的 0x3F；枚举操作前置条件为索引 [0,31]，log_level_max=32 是哨兵。用无符号移位与 Debug assert 明确前置条件，不对非法枚举增加 clamp/返回默认值。
- add_level(string) 本身不 trim；忽略大小写匹配 all/verbose/debug/info/warning/error/fatal，未知字符串诊断并保持原位图，不接受 trace/warn 别名。
- get_log_level_bitmap_by_config 对非数组清零并 false；对数组逐项忽略非字符串并诊断，字符串 trim 后加入临时位图，最后替换输出并 true。空数组和全部无效项的数组均 true/0。
- get_categories_mask_by_config 保留参考 categories_name 的 const 按值参数。输出必须事先与名称数组等长；assert 前置条件，不在 helper 内 resize。仅收集字符串 mask，不 trim、不忽略大小写。无字符串 mask 时全部允许；否则匹配名称完全相同，或以 mask 开头且后续第一字符为点号；*default 另外允许第 0 个类别。* 没有通配语义，返回始终 true。
- tmp.reserve(categories_name.size()) 对应参考 set_capacity 的增长意图，不改变已有元素 count；不把 capacity 当成 size。
- Logger/Snapshot 使用上述点号边界 helper，Appender 的 set_basic_configs 当前是 * 或普通前缀，二者实际规则不同。不要将 helper 直接套到 Appender 来消除差异。Appender levels 缺失/非数组时的 all fallback 在调用方；print_stack_levels 调用方不补 all。
- 参考注释 make sure atomic 不构成 C++ 线程同步。临时构建再普通赋值可减少中间业务状态，但普通 uint32/指针和类别 vector 仍需后续运行态锁/发布协议；本组不引入 atomic 或重新定义线程合同。
- BQ 字符串 equals_ignore_case 在 string_impl.h:362 起使用 toupper(char)，负 char 实参违反 ctype 前置条件；下面先转换 unsigned char，在定义良好的输入上保持同一比较过程。不承诺 Unicode case folding。
- 诊断沿用当前 TimeZone 已有 fprintf(stderr) 路径，对应参考设备控制台的警告意图。输出通道/文字与 BQ 不相同；此处不新建日志后端，不改变无效项的位图处理。

## 按依赖顺序填写的完整候选

以下代码只作为指导，普通成员仍放 cpp。先填 bitmap 头/cpp，再填 filter_config 头/cpp。

### include/qlog/runtime/log_level_bitmap.hpp

```cpp
#pragma once
#include "qlog/log_level.hpp"
#include <cassert>
#include <cstdint>
#include <string>

namespace qlog::runtime {
class LogLevelBitmap {
   public:
    LogLevelBitmap();
    LogLevelBitmap(std::uint32_t init_bitmap_value);
    LogLevelBitmap(const LogLevelBitmap& rhs);
    LogLevelBitmap& operator=(const LogLevelBitmap& rhs);
    void clear();
    bool have_level(qlog::LogLevel level) const {
        const auto index = static_cast<std::int32_t>(level);
        assert(index >= 0 && index < 32);
        return (bitmap_ & (std::uint32_t{1} << static_cast<std::uint32_t>(index))) != 0;
    }
    void add_level(qlog::LogLevel level);
    void add_level(const std::string& level_string);
    void del_level(qlog::LogLevel level);
    std::uint32_t* get_bitmap_ptr();

   private:
    std::uint32_t bitmap_;
};
}  // namespace qlog::runtime
```

### src/runtime/log_level_bitmap.cpp

```cpp
#include "qlog/runtime/log_level_bitmap.hpp"
#include <cctype>
#include <cstdio>
#include <string_view>

namespace qlog::runtime {
namespace {
bool equals_ignore_case(const std::string& value, std::string_view expected) {
    if (value.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto lhs = static_cast<unsigned char>(value[i]);
        const auto rhs = static_cast<unsigned char>(expected[i]);
        if (lhs != rhs && std::toupper(lhs) != std::toupper(rhs)) {
            return false;
        }
    }
    return true;
}
}  // namespace

LogLevelBitmap::LogLevelBitmap() : bitmap_(0) {}
LogLevelBitmap::LogLevelBitmap(std::uint32_t init_bitmap_value)
    : bitmap_(init_bitmap_value) {}
LogLevelBitmap::LogLevelBitmap(const LogLevelBitmap& rhs)
    : bitmap_(rhs.bitmap_) {}
LogLevelBitmap& LogLevelBitmap::operator=(const LogLevelBitmap& rhs) {
    bitmap_ = rhs.bitmap_;
    return *this;
}
void LogLevelBitmap::clear() {
    bitmap_ = 0;
}
void LogLevelBitmap::add_level(qlog::LogLevel level) {
    const auto index = static_cast<std::int32_t>(level);
    assert(index >= 0 && index < 32);
    bitmap_ |= std::uint32_t{1} << static_cast<std::uint32_t>(index);
}
void LogLevelBitmap::add_level(const std::string& level_string) {
    if (equals_ignore_case(level_string, "all")) {
        bitmap_ = 0xFFFFFFFFU;
        return;
    }
    if (equals_ignore_case(level_string, "verbose")) {
        add_level(qlog::LogLevel::verbose);
    } else if (equals_ignore_case(level_string, "debug")) {
        add_level(qlog::LogLevel::debug);
    } else if (equals_ignore_case(level_string, "info")) {
        add_level(qlog::LogLevel::info);
    } else if (equals_ignore_case(level_string, "warning")) {
        add_level(qlog::LogLevel::warning);
    } else if (equals_ignore_case(level_string, "error")) {
        add_level(qlog::LogLevel::error);
    } else if (equals_ignore_case(level_string, "fatal")) {
        add_level(qlog::LogLevel::fatal);
    } else {
        std::fprintf(stderr, "qlog warning: invalid level mask was found:\"%s\"\n",
                     level_string.c_str());
    }
}
void LogLevelBitmap::del_level(qlog::LogLevel level) {
    const auto index = static_cast<std::int32_t>(level);
    assert(index >= 0 && index < 32);
    bitmap_ &= ~(std::uint32_t{1} << static_cast<std::uint32_t>(index));
}
std::uint32_t* LogLevelBitmap::get_bitmap_ptr() {
    return &bitmap_;
}
}  // namespace qlog::runtime
```

### include/qlog/config/filter_config.hpp

```cpp
#pragma once
#include "qlog/config/property_value.hpp"
#include "qlog/runtime/log_level_bitmap.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace qlog::config {
bool get_categories_mask_by_config(
    const std::vector<std::string> categories_name,
    const PropertyValue& categories_mask_config,
    std::vector<std::uint8_t>& out_categories_mask);
bool get_log_level_bitmap_by_config(
    const PropertyValue& log_level_bitmap_config,
    qlog::runtime::LogLevelBitmap& out_level_bitmap);
}  // namespace qlog::config
```

### src/config/filter_config.cpp

```cpp
#include "qlog/config/filter_config.hpp"
#include "qlog/utility/string_utils.hpp"
#include <cassert>
#include <cstdio>
#include <utility>

namespace qlog::config {
bool get_categories_mask_by_config(
    const std::vector<std::string> categories_name,
    const PropertyValue& categories_mask_config,
    std::vector<std::uint8_t>& out_categories_mask) {
    assert(categories_name.size() == out_categories_mask.size());
    std::vector<std::string> tmp;
    if (categories_mask_config.is_array()) {
        for (PropertyValue::array_type::size_type i = 0;
             i < categories_mask_config.array_size(); ++i) {
            if (categories_mask_config[i].is_string()) {
                std::string mask = static_cast<std::string>(categories_mask_config[i]);
                tmp.push_back(std::move(mask));
            }
        }
    }
    tmp.reserve(categories_name.size());
    for (std::size_t i = 0; i < categories_name.size(); ++i) {
        const auto& category_name = categories_name[i];
        std::uint8_t mask = 0;
        if (tmp.empty()) {
            mask = 1;
        } else {
            for (const std::string& mask_config : tmp) {
                if (category_name == mask_config
                    || (category_name.size() > mask_config.size()
                        && category_name.compare(0, mask_config.size(), mask_config) == 0
                        && category_name[mask_config.size()] == '.')) {
                    mask = 1;
                    break;
                }
                if (i == 0 && mask_config == "*default") {
                    mask = 1;
                    break;
                }
            }
        }
        out_categories_mask[i] = mask;
    }
    return true;
}
bool get_log_level_bitmap_by_config(
    const PropertyValue& log_level_bitmap_config,
    qlog::runtime::LogLevelBitmap& out_level_bitmap) {
    qlog::runtime::LogLevelBitmap tmp;
    if (!log_level_bitmap_config.is_array()) {
        out_level_bitmap.clear();
        return false;
    }
    for (PropertyValue::array_type::size_type i = 0;
         i < log_level_bitmap_config.array_size(); ++i) {
        const auto& level_obj = log_level_bitmap_config[i];
        if (!level_obj.is_string()) {
            std::fprintf(stderr, "qlog warning: invalid [log_level] item: %s\n",
                         level_obj.serialize().c_str());
            continue;
        }
        tmp.add_level(qlog::utility::trim(static_cast<std::string>(level_obj)));
    }
    out_level_bitmap = tmp;
    return true;
}
}  // namespace qlog::config
```

## 调用与验收边界

配置文本先经 Property 去重/规范化再由 PropertyValue 工厂构树。运行态提供稳定类别名称顺序，调用方先把 out_categories_mask 的 size 调成同样大小，再传 log.categories_mask；等级 helper 接 print_stack_levels 或未来 Appender levels。helper 自身不提供 Appender all fallback。类别名称、索引和记录 category_idx 的映射必须由后续运行态维持，本组不建立 Logger/Manager。

2026-10-07 接续指导验证目录：/tmp/qlog-filter-rules-guide-j768okgl。位图/过滤模块仍为临时候选，连接的是实际 PropertyValue/Property/string_utils，Debug/Release 严格编译链接、定向场景和 UBSan 通过。在前轮位图/过滤场景上增加9组两套类别规则的矩阵、Logger/Snapshot配置树连通与全局拒绝不能绕过检查。Appender只摘录参考比较条件放入对照探针，未创建/调用生产Appender，也未运行BQLog原库；不是正式配置CMake/CTest、双库差分或并发/Windows/性能验收。见 [指导验证记录](../validation/FILTER_RULES_GUIDE_20261007_CHS.md)。

配置生产 cpp 的三处编译问题现已由助手按用户授权修正，实际源码验证见上方记录；根 qlog target 尚未加入配置 cpp。用户写好本组后，再审查实际文件、完成配置与过滤的 CMake 接入和正式定向测试；保持 QLOG_DEBUG PUBLIC 仅 Debug 定义。之后继续 Buffer 运行态配置/MISO/TLS/LP-HP。

## 学习重点

位图是集合而不是等级阈值，add 是 OR、del 是 AND 补掩码、have 是 AND 判非零；info+error 的值为 0x14。数组为空、字段缺失和数组里全无效项的处理不等价，返回 bool 主要表示配置形状，而非保证有有效项。reserve 只调整 capacity，预先 resize 输出才建立可索引元素。先写临时对象再赋值没有 happens-before，不能消除数据竞争。级别合并的 OR 仅表达某个目标可能需要记录，实际后台仍要按 Logger 类别及各 Appender 类别/等级/enabled 复查；参考 merged 位图不排除 disabled 目标。


## 本次用户实现审查与下一组

本次实际审查优先于上面的临时指导验证快照：生产 filter_config Debug/Release 编译失败，bitmap 独立语法通过但 clear 调用链接失败。助手未修改生产，仅临时修正版的 Debug/Release 与UBSan各61个检查通过，不计实际模块通过/正式CMake集成。已有CPP与CR保持。先按 [实际审查记录](../validation/FILTER_CODE_REVIEW_20261007_CHS.md) 补齐，再按 [Buffer配置完整指导](BUFFER_CONFIG_IMPLEMENTATION_GUIDE_20261007_CHS.md) 建立配置数据/校验值与转换；本轮没有加入CMake。
