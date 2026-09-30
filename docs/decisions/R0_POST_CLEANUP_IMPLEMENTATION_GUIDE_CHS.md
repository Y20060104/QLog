# R0 补全实现指南：从当前头文件到完整 BQLog Layout

> **2026-10-01 最新决定与进度：** 参数命名按 BQLog 实际含义对齐，代码格式沿用 QLog 当前要求；扩容按参考 layout.cpp:1100，不增加 layout_failed_ 或通用防御性错误协议。Layout/UTF cpp 已接入 CMake，但扩容仍为空，完整布局尚未实现；Debug/Release 库与现有 smoke 可执行文件构建通过，保留空扩容函数警告；未运行测试，未验证完整 Layout 调用方链接。详见 [构建记录](../validation/LAYOUT_CMAKE_20261001_CHS.md)。详见 [命名与扩容补充](./LAYOUT_ALIGNMENT_20261001_CHS.md)。本条优先于下方历史进度。

> **2026-09-29构建接入完成（优先于下方历史快照）：** qlog现编译version.cpp、buffer/spsc_ring_buffer.cpp、record/log_entry_handle.cpp。QLOG_DEBUG通过target_compile_definitions(qlog PUBLIC $<$<CONFIG:Debug>:QLOG_DEBUG>)传播：仅Debug定义，Release/RelWithDebInfo/MinSizeRel及空配置不定义；标准assert仍由NDEBUG控制，不得在调用方单独定义/取消宏造成类布局不一致。Debug/Release实际构建与调用方链接均通过，两种配置现有version smoke各1/1通过；已核对库及调用方编译命令的宏状态。此次未执行Record/Ring行为、并发、恢复或BQLog差分测试。参数序列化保留log_*接口及CR，普通UTF-32参数转换为UTF-16，custom四字节字符仍为显式未决项。下一模块为TimeZone，目前未创建实现，进入前重新检查实际文件。详见[TIMEZONE_HANDOFF_20260929_CHS.md](./TIMEZONE_HANDOFF_20260929_CHS.md)。

> **2026-09-27更新：** RecordHeader/limits/LogEntryHandle位于record；argument_tag.hpp、argument_serialization.hpp已由detail迁入record，checked_size.hpp迁入utility并使用qlog::utility；视图cpp位于src/record/log_entry_handle.cpp。修正const getter、validate空指针/类型/返回值、int16分支及扩展线程名长度校验。已做严格编译和独立链接，无运行测试。Ring已由用户加入CMake；Record cpp尚未加入，QLOG_DEBUG尚无PUBLIC配置。序列化头目前只有pragma once，未代写实现。


> **2026-09-26最新进度（优先于下方2026-09-23快照）：** 第三、四批已填写；本轮补全try_recover_from_exist_memory_map并修正batch结果、断言/拼写、线程身份判断和Release开关方法定义。恢复以外部存储重算有效块数，memcpy读取普通游标快照，用remaining有界验证u32回绕与chunk几何，验证后建立Head/原子并恢复游标；坏快照返回false，非法外部存储仍是assert前置条件。QLOG_DEBUG和NDEBUG/O2两种配置以-Werror编译，并通过独立共享检查库的--no-undefined链接；未接入项目CMake，未运行行为/并发/恢复差分或性能测试。性能CR保留。
>
> 下一轮先做Ring的CMake接入与宏PUBLIC传播，再以Record为一个模块：补全指南第2至4节，配合完整Layout指南第4、5节。TimeZone/Layout排在之后；不要把本次链接检查记为完整Ring验收。


> **2026-09-23 Ring补充决定：旧字节Ring/配置及相关测试基准、过期Ring文档已删除。** 当前库仅版本基础；新SISO按8字节block、外部内存、32位游标重建，见 [Ring实现指南](./R0_BQLOG_BLOCK_RING_GUIDE_CHS.md) 与 [清理记录](./R0_RING_CONFIG_CLEANUP_20260923_CHS.md)。此前47项测试只是本次Ring删除前的历史结果。


日期：2026-09-23。参考 BQLog 固定提交 `60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`。QLog 为 `/home/qq344/QLog`。

**当前状态以顶部2026-10-01记录为准：TimeZone已接入，Layout/UTF cpp本轮接入但Layout主链仍未完成。下方带日期记录为历史证据。**

本文接续 [完整 Layout 指南](./R0_BQLOG_LAYOUT_REBUILD_GUIDE_CHS.md)，补齐当前树的落地顺序、具体文件、可复制声明/基础实现、构建连接和验收样本。旧 R0 六参数方案不再使用。

## 0. Ring之后的模块顺序与文档范围（2026-09-23源码复核）

截至2026-09-29，SpscRingBuffer单条/批量/恢复/Debug已填写，Record序列化及视图已填写；Ring和Record视图均已接入库，Debug宏已PUBLIC传播。构建/链接通过不等于行为验收；下一开发模块为TimeZone。

| 接续模块 | 具体工作 | 现有文档覆盖 |
|---|---|---|
| Ring收口 | 第三、四批后静态审查、CMake接入、实际调用者链接；QLOG_DEBUG两种配置一致 | Ring指南；行为/并发/恢复测试暂缓但仍是最终验收项 |
| Record | RecordHeader/ArgumentTag/限制迁入record；LogEntryHandle；参数测量、序列化和扩展信息 | 本文第3、4节及完整Layout指南第4、5节 |
| TimeZone与Layout | 完整前缀和正文、UTF转换、内部缓冲借用、逐目标完整布局 | 本文第5、6节及完整Layout指南第6至10节 |
| 配置与运行态骨架 | 配置树、Logger/Manager持有关系、目标过滤和工厂 | V1路线R1，尚非完整逐函数实施稿 |
| Buffer集成 | 独立MISO协议、block_list、oversize_buffer，再接LogBuffer/TLS/LP-HP/序号/满策略 | V1路线R2及BQLog源码；尚缺同等细度逐函数指南 |
| Worker/Appender/生命周期 | 三种模式、Console/TextFile与文件层、reset/flush/退出 | V1路线R3至R5，进入模块前继续源码核对 |

这是QLog的开发顺序，不是声称BQLog要求先写哪个文件。Record/Layout可用固定fixture解耦；不能仅为读取一条记录而提前扩展Worker。

本次选定Windows参考HEAD 60ef4d3并确认采用的源码文件相对HEAD无差异：
- src/bq_log/types/buffer/siso_ring_buffer.h/.cpp：单条/批读、恢复；不能代替多生产者协议。
- src/bq_log/types/buffer/block_list.h：block_node_head内嵌SISO实例；不是仅有一个裸Ring的最终拓扑。
- src/bq_log/types/buffer/log_buffer.h/.cpp：alloc_write_chunk按TLS频率选HP节点或共享LP MISO；block_when_full映射err_wait_and_retry；oversize另有路径。
- src/bq_log/api/bq_log_api.cpp::__api_log_write_begin：唤醒/等待重试位于上层，不能塞入SISO。
- src/bq_log/log/log_types.h/.cpp：借用记录及validate；格式tag可接受UTF-32不表示Layout支持UTF-32格式串。
- src/bq_log/log/layout.cpp::do_layout：前缀后正文，不自动调用完整validate，也不返回I/O状态。
- src/bq_log/log/appender/appender_file_text.cpp::log_impl：先文件基类准备，完整布局，复制到文件缓存并追加换行，tidy后归还缓存。
- src/bq_log/log/log_imp.cpp::process：成功读取后建立记录视图，在作用域读句柄结束时归还Buffer，不能强制提前归还再I/O。

本轮只核对上述接续边界和相关文档，不代表所有数值/UTF/时区边界已运行差分。初始化assert合同、QLOG_DEBUG和保留性能CR以Ring指南第0节为准。

## 1. 清理前发现的问题与本轮处理结果

- ArgumentTag 已按参考改为 0–20，保留。
- RecordHeader 已改为 40 字节并有字段偏移断言；RecordExtHeader 已增加，保留。get_head_size_without_format_str 当前写在类外，应移为类内 static 成员，与参考对应。
- LogLevel 已改 int32_t，但 `log_level_max=32.` 是语法错误，应为 `log_level_max = 32,`；valid_level 使用 int32 值与 0、5 比较。
- record_limits 仍有 8192 格式字节/32 参数上限；这些旧额外限制退出，头长改为 sizeof(RecordHeader)。
- text_formatter.hpp 仍是旧六参数接口且包含已删除头，需要删除后按本指南重建。
- 旧 Logger、Channel、Producer、codec 的剩余声明/定义和测试仍在；不要让它们引用新 Record 后勉强编译。

上面列的是清理前问题：现在枚举语法、静态函数归属、记录限制已修正；旧格式器、Logger/Channel/Producer、codec 和相关测试已删除。当前CMake库仅包含version.cpp；新的Ring源码尚未接入；其构建结果不能报告为“新 R0 完成”。

## 2. 最终文件图

| 文件 | 放什么 | 对应 BQLog |
|---|---|---|
| include/qlog/log_level.hpp | int32 等级枚举 | basic_types.h::log_level |
| include/qlog/record/argument_tag.hpp | uint8 参数 tag | bq_log_def.h::log_arg_type_enum |
| include/qlog/record/record_header.hpp | 40 字节头和 1 字节扩展头 | _log_entry_head_def / _log_entry_ext_head_def |
| include/qlog/record/log_entry_handle.hpp | 记录借用、偏移访问 | log_types.h::log_entry_handle |
| src/record/log_entry_handle.cpp | validate | log_types.cpp |
| include/qlog/record/argument_serialization.hpp | 类型识别、string helper、size_seq、参数复制模板 | bq_log_wrapper_tools.h / bq_log_impl.h 的填参函数 |
| include/qlog/layout/time_zone.hpp | 时区对象与缓存声明 | time_zone.h |
| src/layout/time_zone.cpp | 解析、日历转换、时间缓存 | time_zone.cpp |
| include/qlog/layout/layout.hpp | Layout 与内部 FormatInfo/enum_layout_result | layout.h |
| src/layout/layout.cpp | 前缀、两种扫描器、格式解析、标量插入 | layout.cpp |
| include/qlog/utility/utf_conversion.hpp与src/utility/utf_conversion.cpp | 内部 UTF helper 函数，非额外对象层 | bq_common/utils/util.cpp 的当前 UTF 路径 |
| tests/layout/layout_record_test.cpp | fixture、完整输出、视图生命周期与差分 | 新测试，不能套旧 32 字节期望 |

命名空间：公开等级在qlog；记录类型在qlog::record，TimeZone/Layout在qlog::layout，通用UTF转换在qlog::utility。上表为后续目标，当前RecordHeader/tag仍在detail，实际迁移前检查目标并同步include；本轮未移动这些源码。模板定义放对应 hpp；普通方法定义放 cpp；同一个结构只定义一次。不新增 Session、拥有型布局结果或 DecodedArg 数组。

## 3. 第一步：写 LogEntryHandle

放入 include/qlog/record/log_entry_handle.hpp。这是可复制的接口与内联基础实现，不依赖旧 codec。

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include "qlog/record/record_header.hpp"
#include "qlog/log_level.hpp"

namespace qlog::record {
class LogEntryHandle {
public:
    LogEntryHandle(const std::uint8_t* data, std::uint32_t size)
        : data_ptr_(data), data_len_(size) {}

    const std::uint8_t* data() const { return data_ptr_; }
    std::uint32_t data_size() const { return data_len_; }

    const RecordHeader& get_log_head() const {
        return *reinterpret_cast<const RecordHeader*>(data_ptr_);
    }
    RecordHeader& get_log_head() {
        return *const_cast<RecordHeader*>(
            reinterpret_cast<const RecordHeader*>(data_ptr_));
    }
    const char* get_format_string_data() const {
        return reinterpret_cast<const char*>(data_ptr_ + sizeof(RecordHeader));
    }
    std::size_t get_log_args_offset() const {
        const auto n = static_cast<std::size_t>(get_log_head().log_format_data_len);
        return sizeof(RecordHeader) + ((n + 3U) & ~std::size_t{3});
    }
    const std::uint8_t* get_log_args_data() const {
        return data_ptr_ + get_log_args_offset();
    }
    std::uint32_t get_log_args_data_size() const {
        return static_cast<std::uint32_t>(
            get_log_head().ext_info_offset - get_log_args_offset());
    }
    const RecordExtHeader& get_ext_head() const {
        return *reinterpret_cast<const RecordExtHeader*>(
            data_ptr_ + get_log_head().ext_info_offset);
    }
    LogLevel get_level() const {
        return static_cast<LogLevel>(get_log_head().level);
    }
    std::uint32_t get_category_idx() const {
        return get_log_head().category_idx;
    }
    bool validate() const;

private:
    const std::uint8_t* data_ptr_;
    std::uint32_t data_len_;
};
} // namespace qlog::record
```

前置条件：内存覆盖 size 字节；引用头的接口要求头存储满足 8 字节对齐和对象存储要求；非 const getter 只能用于确实可写的记录。validate 可用 memcpy 读取局部头来检查字节，但检查通过不等于给任意未对齐外部字节授予引用访问资格。实际 Buffer 与 fixture 必须提供正确对齐。返回指针/引用不延长记录生命周期。下文fixture中的无前缀名称在测试局部用using qlog::record::RecordHeader、RecordExtHeader、ArgumentTag、LogEntryHandle以及qlog::layout::TimeZone、Layout引入；不要在公共头中写using namespace。

参数区偏移使用 size_t；当前目标为 64 位。若以后支持 32 位，必须专门处理 fmt_len+3 溢出，不声称这段能直接覆盖所有 ABI。

### 3.1 validate 的实现步骤

在src/record/log_entry_handle.cpp的namespace qlog::record中定义validate，包含上述头、record/argument_tag.hpp、cstring。全部长度加法在 uint64_t 中完成，再决定是否缩小；按剩余长度比较，避免 cursor+step 溢出。

```cpp
bool LogEntryHandle::validate() const {
    if (!data_ptr_ || data_len_ < sizeof(RecordHeader) + sizeof(RecordExtHeader))
        return false;

    RecordHeader h;
    std::memcpy(&h, data_ptr_, sizeof(h));
    const auto fmt = static_cast<ArgumentTag>(h.log_format_str_type);
    if (fmt != ArgumentTag::string_utf8_type &&
        fmt != ArgumentTag::string_utf16_type &&
        fmt != ArgumentTag::string_utf32_type)
        return false;

    const std::uint64_t args_begin = sizeof(RecordHeader) +
        ((std::uint64_t{h.log_format_data_len} + 3U) & ~std::uint64_t{3});
    const std::uint64_t ext = h.ext_info_offset;
    if (args_begin > data_len_ || ext < args_begin || ext + 1U > data_len_)
        return false;
    if (ext + 1U + data_ptr_[static_cast<std::size_t>(ext)] > data_len_)
        return false;

    std::uint64_t cursor = args_begin;
    while (cursor < ext) {
        const auto remaining = ext - cursor;
        if (remaining < 4U) return false;
        const auto* p = data_ptr_ + static_cast<std::size_t>(cursor);
        const auto tag = static_cast<ArgumentTag>(*p);
        std::uint64_t step;
        switch (tag) {
        case ArgumentTag::null_type:
        case ArgumentTag::bool_type:
        case ArgumentTag::char_type:
        case ArgumentTag::char16_type:
        case ArgumentTag::int8_type:
        case ArgumentTag::uint8_type:
        case ArgumentTag::int16_type:
        case ArgumentTag::uint16_type:
            step = 4; break;
        case ArgumentTag::char32_type:
        case ArgumentTag::int32_type:
        case ArgumentTag::uint32_type:
        case ArgumentTag::float_type:
            step = 8; break;
        case ArgumentTag::pointer_type:
        case ArgumentTag::int64_type:
        case ArgumentTag::uint64_type:
        case ArgumentTag::double_type:
            step = 12; break;
        case ArgumentTag::string_utf8_type:
        case ArgumentTag::string_utf16_type: {
            if (remaining < 8U) return false;
            std::uint32_t n;
            std::memcpy(&n, p + 4, sizeof(n));
            step = 8U + ((std::uint64_t{n} + 3U) & ~std::uint64_t{3});
            break;
        }
        default:
            return false;
        }
        if (step > remaining) return false;
        cursor += step;
    }
    return cursor == ext;
}
```

这是保持参考结构检查规则的安全读值映射：null 指针先返回 false，局部 memcpy 避免校验过程依赖未对齐 load；不新增旧 bool 规范化、时间状态位或字符编码策略。UTF-32 格式 tag 仍可通过结构检查但不能由当前 Layout 正常处理，需单独修复决定，不混淆两个函数的职责。

## 4. 第二步：参数序列化，替代旧 PreparedRecord

全部模板放 argument_serialization.hpp。保留参考分层，不恢复 NormalizedArgument/PreparedRecord/MeasureResult。

| 函数/模板 | 输入与返回 | 必须实现的内容 |
|---|---|---|
| get_log_param_type_enum<T>() | 无运行参数；返回 ArgumentTag | nullptr、普通指针、POD、字符串字符类型、自定义格式协议逐项对应参考；wide 普通字符串映射 UTF-16 |
| get_storage_data_size_constexpr<WithTag,T>() | 返回 size_t | 固定类型：null 为 WithTag?4:0；指针为8+(WithTag?4:0)；POD 为sizeof(T)+(WithTag?(sizeof(T)<=2?2:4):0) |
| get_storage_data_size<WithTag>(const T&) | 返回动态原始大小 | 字符串为(WithTag?4:0)+4+存储字节；UTF-32 先测转 UTF-16 后的字节数 |
| size_seq_element<H,V,true> | get_value/get_aligned_value | 常量大小放模板参数，实例不重复存储 |
| size_seq_element<H,V,false> | value 与两个 getter | 动态大小在测量时保存 |
| size_seq<WithTag,Types...> | get_element/get_next/get_total | 当前节点+余下节点；总大小累加每项 align4(raw_size) |
| make_size_seq<WithTag>(const Args&...) | 返回 size_seq | 只为动态节点填写 value；常量节点不读取参数求大小 |
| type_copy<WithTag>(const T&,uint8_t*,size_t raw_size) | void | 写首字节 tag；按类型偏移写值；字符串写原始字节长度再写内容；不把补齐长度当内容长度 |
| do_log_args_fill(uint8_t*,const Seq&,const Args&...) | void | 对当前值 type_copy，地址+=aligned_size，递归到下一项 |

size_seq 的空参数实例 get_total()==0；调用端的零参数路径无需调用依赖 FIRST 的 fill_size_seq 重载。1 字节 POD 原始大小是 3，不是 4；只是分配步长补齐为 4。这是 raw_size 与 aligned_size 必须分开的实例。

普通字符串 helper 的参数是原对象；返回存储字节数和可读源地址。C 字符串按终止符测量；数组和带长度对象按参考各自规则处理；UTF-32 需要计算代理对后的 UTF-16 字节数。自定义格式成员/全局函数分派也必须对应参考，不能用“任意 ostream 可写类型”扩大输入协议。

源码补充（2026-09-27）：普通1/2字节字符数组只去掉末尾NUL，保留内部NUL；字符指针按首个NUL测量，空字符指针写文本null；4字节字符的普通数组/类/view测量在首个NUL停止，并计算转换后的UTF-16字节。自定义格式的测量使用size()*sizeof(char_type)，自定义char32写入路径与UTF-16参数tag存在疑点，不能套普通UTF-32字符串转换公式后宣称完全对齐；进入该分支时单列复现与修复决定。

当前轮先用下面手工 fixture 解耦 Layout 开发；这不意味着完整 serializer 可以省略。通过 fixture 只能说明消费者工作，完整 R0 还必须完成 producer 侧测量/填充及往返。

## 5. 第三个文件组：TimeZone

头放 include/qlog/layout/time_zone.hpp，定义放 src/layout/time_zone.cpp。字符串直接 std::string，字符缓存是 char[129]，无需新配置包装类。

成员：bool use_local_time_；int32_t gmt_offset_hours_/minutes_/time_zone_diff_to_gmt_ms_；std::string time_zone_str_；char time_cache_[129]；size_t time_cache_len_；uint64_t last_time_epoch_cache_。

公有签名按原完整指南的逐函数表落地。关键实现顺序：reset → parse_by_string → get_tm_by_epoch → inner_refresh_time_string_cache → 缓存 getter → get_time_str_by_epoch/配置 getter。

parse_by_string：字符串 trim/大写；local 别名、UTC/GMT/Z、带符号时差分别处理；strtol 解析小时/分钟并检查尾部；小时 [-12,14]、分钟 [0,59]；按参考错误诊断/default local 路径，不额外返回强事务错误对象。

get_tm_by_epoch(epoch_ms,out)：先 ms/1000；local 分支用 localtime_r；固定偏移分支加时差后 gmtime_r；成功返回 true。Windows 对应安全日历接口另做平台映射，本轮 Linux 为权威目标。

inner_refresh_time_string_cache：日历转换后生成“时区 年-月-日 时:分:秒.”，保存有效长度和 epoch_ms；毫秒不在此函数追加。初次 epoch0 和 -00:30 疑点见完整指南，不能悄悄定义与参考不同的结果。

## 6. 第四个文件组：具体 Layout 声明

在 text_formatter.hpp 中使用以下真实容器声明，替换旧指南占位符；这些是现有容器，不另建拥有型类。

```cpp
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "qlog/record/log_entry_handle.hpp"
#include "qlog/layout/time_zone.hpp"

// namespace qlog::layout，class Layout 内：
enum class enum_layout_result { finished, to_be_continue, parse_error };

enum_layout_result do_layout(
    const qlog::record::LogEntryHandle& entry,
    TimeZone& zone,
    const std::vector<std::string>* categories);
const char* get_formated_str();
std::uint32_t get_formated_str_len() const;
void tidy_memory();

// private：完整 FormatInfo 与方法声明见完整指南第 7 节。
TimeZone* time_zone_ptr_ = nullptr;
const std::vector<std::string>* categories_name_array_ptr_ = nullptr;
std::vector<char> format_content;
std::uint32_t format_content_cursor = 0;
std::unordered_map<std::uint64_t, std::string> thread_names_cache_;
FormatInfo format_info_;
```

do_layout 的三个参数分别是借用记录、该目标时区、Logger 类别名称表。绑定后归零 cursor，保证1024字节工作区，layout_prefix 返回非 finished 则直接返回；否则执行正文分派再返回 finished。不把 finished 当成写文件成功，不实现参考尚无实际流程的 to_be_continue 续传。

vector 对应策略：工作区可写长度由 size 管理，必须 resize 后再索引写入；reserve 只增容量不产生可写元素。参考请求按 2 的幂增长；必要大小在宽整数中计算并检查，再 resize。参考默认 precision=UINT32_MAX 的辅助扩容公式含回绕疑点，必须按实际所需输出空间保证安全，不把溢出值直接交给分配器。

```cpp
const char* Layout::get_formated_str() {
    return format_content.empty() ? nullptr : format_content.data();
}
std::uint32_t Layout::get_formated_str_len() const {
    return format_content_cursor;
}
void Layout::tidy_memory() {
    if (format_content.capacity() > 1024U) {
        std::vector<char> small;
        small.reserve(1024U);
        format_content.swap(small);
    }
    format_content_cursor = 0;
}
```

标准容器 reserve 保证至少该容量，不保证精确1024；这里实现释放大工作区并申请小工作区的机制映射，不声称分配器行为逐字节等同 bq::array。若要求精确容量管理，再迁移对应数组实现，不凭空添加缓存策略。tidy 不清线程 map。

### 6.1 cpp 函数排列顺序

1. 匿名 namespace：三位数字表（000–999，共3000字节）、六个三字节等级名、标量 memcpy 读取函数、参考软件扫描 helper。
2. FormatInfo::reset、Layout 构造、扩容、getter/tidy。
3. insert_str_utf8/utf16、insert_char/char16/char32、reverse。
4. insert_integral_unsigned/signed、insert_bool/pointer、两种 insert_decimal。
5. c20_format、fill_and_alignment、fill_e_style。
6. insert_time、insert_thread_info、layout_prefix。
7. python_style_format_content_utf8、utf16、编码分派。
8. do_layout。

这只是定义顺序，不改变调用关系。c20_format 模板只供本 cpp 的 char/char16_t 调用，可在 cpp 内定义。

### 6.2 不可遗漏的实现细节

- FormatInfo 初始 align='>'，reset align='<'；precision 默认 UINT32_MAX；实际做过填充才有参考 memset 清状态的路径。跨调用残留状态列测试。
- 前缀恰为“时间毫秒[tid-ID 名称]\t[等级]\t[非空类别]\t”；毫秒后不额外加空格。Logger 名不在 Layout 内，换行不在 Layout 内。
- UTF-8 无参直接拷贝；UTF-16 无参仍扫描，两种嵌套括号前瞻分别照参考实现。
- string helper 长度是字节，UTF-16 输入单位转换为 len/2；ASCII NUL 按长度保留。孤立代理项的处理跟随当前软件路径，不套 legacy。
- bool 为 TRUE/FALSE；nullptr 文本 null；unsigned 0 与 signed 0 在 '+' 的行为不同；数字取模写入后 reverse 的末端是包含端点。
- float/double 默认精度7/15、逐位截取；不能换成 snprintf/to_chars 就宣布等价。非有限/超界值独立复现。
- c20_format 内部扫描上限与 scanner 的20字符前瞻是两个不同边界；位置文本不等于参数索引功能。

完整逐函数算法、参数和返回值见主指南第8–10节；这一节补的是实际声明、定义落点、容器操作与遗漏项，不新增另一套语法。

## 7. 可独立验证的最小记录

以下 fixture 放 tests/layout/layout_record_test.cpp 中，不放入生产 Record 工厂。格式串 `value={}` 为8字节，单个 int32 参数42，线程名t，类别空：

```cpp
alignas(8) std::uint8_t bytes[58]{};
RecordHeader h{};
h.timestamp_epoch = 1700000000007ULL;
h.ext_info_offset = 56;
h.category_idx = 0;
h.log_thread_id = 123;
h.log_format_str_type = static_cast<std::uint8_t>(ArgumentTag::string_utf8_type);
h.level = static_cast<std::uint8_t>(LogLevel::info);
h.log_format_data_len = 8;
std::memcpy(bytes, &h, sizeof(h));
std::memcpy(bytes + 40, "value={}", 8);
bytes[48] = static_cast<std::uint8_t>(ArgumentTag::int32_type);
std::int32_t value = 42;
std::memcpy(bytes + 52, &value, sizeof(value));
bytes[56] = 1;
bytes[57] = 't';

LogEntryHandle entry(bytes, sizeof(bytes));
// entry.validate() 应为 true。
// TimeZone zone("UTC"); std::vector<std::string> categories{ "" };
// layout.do_layout(entry, zone, &categories);
```

时间输入对应 UTC 2023-11-14 22:13:20.007。预期布局字节如下，\t 代表一个 tab，无尾部换行：

```text
UTC0 2023-11-14 22:13:20.007[tid-123 t]\t[I]\tvalue=42
```

这里 format_hash=0 不影响 Layout。本 fixture 不验收生产 hash 编码。再用真实 serializer 编同一输入，与手工记录的有效字段逐项比较；padding 不要求与参考未初始化字节相同。

最小反例：总长度57导致线程名截断；ext_info_offset=47进入格式区；参数 tag=255；格式长度 UINT32_MAX；string 参数声明长度超出扩展起点。validate 应拒绝。级别/类别越界则在 Layout 前缀路径返回 parse_error，不把这两个检查误塞回旧 RecordValidationPolicy。

## 8. 构建如何补回

截至2026-09-29，qlog已编译version.cpp、src/buffer/spsc_ring_buffer.cpp、src/record/log_entry_handle.cpp。
宏配置为target_compile_definitions(qlog PUBLIC $<$<CONFIG:Debug>:QLOG_DEBUG>)。
Debug定义QLOG_DEBUG，其余配置不定义；不能使用QLOG_DEBUG=0，因为头文件用#ifdef判断。PUBLIC同时作用于库和链接QLog::qlog的调用方。assert仍按NDEBUG控制。多配置生成器由实际选择的配置决定宏，不通过configure时判断CMAKE_BUILD_TYPE。

以下仅是未来新增源文件，完成实现后再加入，不提前引用不存在的文件：

```cmake
target_sources(qlog PRIVATE
    src/layout/time_zone.cpp
    src/utility/utf_conversion.cpp
    src/layout/layout.cpp
)
```

argument_serialization.hpp 是模板头，无需空 cpp。UTF转换同时服务Record序列化与Layout，采用qlog::utility独立函数文件；只有仅本cpp使用的扫描/标量辅助放匿名namespace。模板定义仍在头中，不建空cpp。

tests/CMakeLists.txt 增加 qlog_layout_record_test，链接 QLog::qlog 与 GTest::gtest_main，gtest_discover_tests 添加 qlog. 前缀、layout_record 标签。测试必须从库链接非内联 do_layout/validate，不能直接 include cpp 绕过库连接。

在新的构建目录执行，避免旧测试发现文件污染：

```bash
cmake -S . -B build/r0-layout-new -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/r0-layout-new -j 4
ctest --test-dir build/r0-layout-new --output-on-failure
```

以上build/r0-layout-new命令仍是后续Layout实施示例。本轮已在独立临时调用方工程完成Debug/Release构建、Record真实链接及各1/1 smoke；详见[构建证据](../validation/RECORD_CMAKE_20260929_CHS.md)。Debug 通过后做一次 ASan/UBSan 针对记录长度、UTF和数值边界的检查；实际变更再选测试，不用重复跑旧 I1 数量凑验收。

## 9. 与 BQLog 对照的完成标准

将同一组对齐记录样本、目标时区、类别表分别交给固定版本 BQLog layout 和 QLog Layout。记录结果枚举、长度、十六进制字节、调用顺序。对比 fixture/生产编码两条路径；record view 通过不等于 Layout 支持所有 fmt tag。

必须包含：六等级、类别空/非空、线程缓存重用、两种格式编码无参花括号差异、所有正常参数 tag、含NUL/代理对、正负整数极值、不同进制/符号/宽度、float/double、连续格式调用、tidy后重新格式化、目标时区不同的重复完整布局。

对参考疑似缺陷，先复现并明确记录修复边界，再决定是否修复；不照抄 UB、不以旧 QLog 结果代替。完成这些才可称“新记录/Layout 基础实现并验证”；LP/HP、Worker、Appender 文件故障、reset/flush/退出仍归后续阶段。

## 10. 后续主链补全，不回接旧文件

R1：配置树、Logger轻量入口、Manager持有运行态、Appender工厂和过滤归属。R2：LogBuffer/TLS/LP/HP、write_begin/finish、参数序列化接生产缓冲、block/discard/expand。R3：公共/独立/同步处理、单消费者与Layout互斥。R4：逐目标完整Layout、Console/TextFile、缓存/文件层/滚动。R5：reset/flush/退出与实际返回边界。

禁止为了让旧例子恢复编译而重新创建原 AsyncLogger::Impl、ChannelDependencies、LogResult 或 PreparedRecord 兼容层。被删除旧文件的名字不是新设计要求，任何新增类应能指出明确的 BQLog 职责对应。
