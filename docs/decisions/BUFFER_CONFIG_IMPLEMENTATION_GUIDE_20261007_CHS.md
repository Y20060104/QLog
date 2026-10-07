# Buffer 运行态配置完整实现指导

日期：2026-10-07。用户手写生产实现，本轮只给指导、临时验证与文档更新；没有创建下面的新生产声明/定义。现有过滤模块先修复审查记录的三处问题，再填写本组；详见 [实际过滤审查](../validation/FILTER_CODE_REVIEW_20261007_CHS.md)。

QLog HEAD ec3f72b27784b3b1a9047391117395b4848afb8d，分支 feat/spsc-ring-opt；BQLog HEAD 78cbfbef4f87e7558335f0c04bf6dd6894b46451。本组采用 enum、log_buffer_defs、log_imp、log_buffer、string_def/impl、type_tools、平台 memory_map 文件均无 HEAD 差异；参考整树仍有 MISO/构建本地修改，后续 MISO 采用前单独审计实际差异。

## 本组职责、出处与归属

本组把已有配置树转换成之后创建 Buffer 所需的数据，建立恢复文件的身份校验值。仍不创建 LoggerImpl/Manager/Worker/Appender/LogBuffer 或 LP/HP/TLS 对象。

- LogMemoryPolicy 放现有 include/qlog/buffer/log_buffer_defs.hpp，namespace qlog::buffer；对应 include/bq_log/misc/bq_log_def.h:33，顺序 discard/block/expand，默认枚举底层类型与参考一样，不额外指定 uint8。
- LogBufferConfig 位于 include/qlog/buffer/log_buffer_config.hpp / src/buffer/log_buffer_config.cpp，namespace qlog::buffer；对应 src/bq_log/types/buffer/log_buffer_defs.h:76-116。struct 的字段与 calculate_check_sum 都 public，没有 private 状态或自定义构造，使用成员默认值；普通成员定义写 cpp。
- get_log_buffer_config_by_config 位于 include/qlog/config/buffer_config.hpp / src/config/buffer_config.cpp，namespace qlog::config。本函数是 log_imp::init:162-188 那一段转换的组织性抽取，不是 BQLog 已有的同名 helper，不改变字段、分支、默认与转换规则。
- equals_ignore_case 提升到已有 utility/string_utils，runtime 和 config 共用；保留原有 CR。这里只迁移已写比较过程，不建立 detail/Manager/拥有型包装。

memory_map_supported 参数是平台能力事实，对应 memory_map::is_platform_support，不是另一项用户配置或默认 false 降级开关。之后由真正的平台层/运行态提供；BQLog 当前 Linux 与 Windows 返回 true。本组声明、实现与验证不调用尚未实现的 QLog MemoryMap，注入能力参数完整保留 requested && supported 计算。

当前 QLog 范围为 Windows/Linux 桌面，因此默认 64KiB，对齐参考非 BQ_MOBILE_PLATFORM 分支；参考移动平台是32KiB。未来进入移动平台需接实际平台定义，本组不伪造新的公共平台宏，也不声称已验证移动端。

## 成员不变量与实际规则

字段名字与顺序保留 BQLog：log_name、log_categories_name、default_buffer_size、need_recovery、policy、high_frequency_threshold_per_second。名称/类别为值拷贝，保持原类别顺序，不排序、不插入默认类别。运行态将类别索引对应记录 category_idx，顺序同时参与恢复校验。配置对象不是 Ring 外部存储拥有者；真实 Buffer 创建后其运行配置必须按后续生命周期规则保持稳定。

default_buffer_size 是请求字节数，不保证等于最终所有存储之和或每个内部块的实际容量。此层不调用 round_pow_of_two 或 SpscRingBuffer::calculate_min_size。BQLog log_buffer.cpp:138 在 Buffer 构造阶段做 max(16*BQ_CACHE_LINE_SIZE, roundup_pow_of_two(...))；LP/HP成员先在初始化列表构造，进入实际 Buffer 模块时要保留它们的真实顺序，不提前把这个动作搬入解析器。

非 object 的 log_config 仍返回名称/类别已填写的默认配置。整数 buffer_size 转 uint32，布尔 recovery 做请求&&平台能力，策略字符串完整忽略大小写匹配 discard/block/expand、不trim、未知保持block；频率字段按 int64→uint64，0转UINT64_MAX。错误类型忽略、不新增通用拒绝协议或范围clamp。负整数转换成无符号属于定义良好的模转换；文本 -1 在当前实际解析器会先成为字符串，此层忽略它，不能把“程序构造负整数节点”与“负数文本”混淆。当前数值溢出备选合同仍暂不选定。

校验输入精确为 log_name + "/" + 逐个category_name + "/"，仅名字和类别顺序参与；policy/size/recovery/threshold不参与。调用对应 string::hash_code → string_impl.h:424 的算法：初值0，先乘1099511628211，再异或字符转换后的uint64。不是标准FNV-1a，不可换乘/异或顺序、初值、std::hash或 util::get_hash_64（参考注释明确后者不同）。

保留 static_cast<uint64_t>(char) 的参考语义，不能静默先转 unsigned char；高位字节结果会受 char 是否有符号影响，此处转换不是UB。uint64乘法溢出按2^64取模。嵌入NUL按完整字符串长度处理。该值不是加密身份认证，也不能保证唯一；"/" 分隔未转义，例如名字"a/b"无类别与名字"a"类别["b"]得到同一拼接文本，按参考保留，不本轮改变恢复格式。

## 一次完整填写

先将 src/runtime/log_level_bitmap.cpp 内 equals_ignore_case 迁到 utility（CR随函数保留）。string_utils.hpp 增加 string_view include 和以下声明：

```cpp
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace qlog::utility {
std::string trim(const std::string& value);

std::vector<std::string> split_nonempty(const std::string& value, char delimiter);
bool equals_ignore_case(const std::string& value, std::string_view expected);
}  // namespace qlog::utility
```

string_utils.cpp 原有 trim/split_nonempty 保持，追加以下定义（cpp 已有 cctype/cstddef）：

```cpp
bool equals_ignore_case(const std::string& value, std::string_view expected) {
    // CR：这个函数作用是什么 为什么value不与expected相等就false
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
```

runtime cpp 包含 qlog/utility/string_utils.hpp，移除旧匿名 helper，七处调用改成 qlog::utility::equals_ignore_case；保留所有 CR。移除已不用的 cctype/string_view include，不修改其他函数规则。已有缺失 clear 仍需按审查补定义。

log_buffer_defs.hpp 在 namespace qlog::buffer 内加入下列 enum；现有 Result/Handle/MemoryMapBufferState 原样保留：

```cpp
enum class LogMemoryPolicy {
    discard_when_full,
    block_when_full,
    auto_expand_when_full,
};
```

### include/qlog/buffer/log_buffer_config.hpp

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "qlog/buffer/log_buffer_defs.hpp"

namespace qlog::buffer {
struct LogBufferConfig {
    std::string log_name;
    std::vector<std::string> log_categories_name;
    std::uint32_t default_buffer_size = 1024U * 64U;
    bool need_recovery = false;
    LogMemoryPolicy policy = LogMemoryPolicy::block_when_full;
    std::uint64_t high_frequency_threshold_per_second = 1000;
    std::uint64_t calculate_check_sum() const;
};
}  // namespace qlog::buffer
```

### src/buffer/log_buffer_config.cpp

```cpp
#include "qlog/buffer/log_buffer_config.hpp"

namespace qlog::buffer {
std::uint64_t LogBufferConfig::calculate_check_sum() const {
    std::string verify_str = log_name + "/";
    for (const auto& category_name : log_categories_name) {
        verify_str += category_name + "/";
    }

    std::uint64_t check_sum = 0;
    for (char ch : verify_str) {
        check_sum *= 1099511628211ULL;
        check_sum ^= static_cast<std::uint64_t>(ch);
    }
    return check_sum;
}
}  // namespace qlog::buffer
```

### include/qlog/config/buffer_config.hpp

```cpp
#pragma once
#include <string>
#include <vector>
#include "qlog/buffer/log_buffer_config.hpp"
#include "qlog/config/property_value.hpp"

namespace qlog::config {
qlog::buffer::LogBufferConfig get_log_buffer_config_by_config(
    const std::string& log_name,
    const std::vector<std::string>& log_categories_name,
    const PropertyValue& log_config,
    bool memory_map_supported);
}  // namespace qlog::config
```

### src/config/buffer_config.cpp

```cpp
#include "qlog/config/buffer_config.hpp"
#include <limits>
#include "qlog/utility/string_utils.hpp"

namespace qlog::config {
qlog::buffer::LogBufferConfig get_log_buffer_config_by_config(
    const std::string& log_name,
    const std::vector<std::string>& log_categories_name,
    const PropertyValue& log_config,
    bool memory_map_supported) {
    qlog::buffer::LogBufferConfig buffer_config;
    buffer_config.log_name = log_name;
    buffer_config.log_categories_name = log_categories_name;
    if (log_config.is_object()) {
        if (log_config["buffer_size"].is_integral()) {
            buffer_config.default_buffer_size =
                static_cast<std::uint32_t>(log_config["buffer_size"]);
        }
        if (log_config["recovery"].is_bool()) {
            buffer_config.need_recovery =
                static_cast<bool>(log_config["recovery"]) && memory_map_supported;
        }
        if (log_config["buffer_policy_when_full"].is_string()) {
            const auto policy_config =
                static_cast<std::string>(log_config["buffer_policy_when_full"]);
            if (qlog::utility::equals_ignore_case(policy_config, "discard")) {
                buffer_config.policy = qlog::buffer::LogMemoryPolicy::discard_when_full;
            } else if (qlog::utility::equals_ignore_case(policy_config, "block")) {
                buffer_config.policy = qlog::buffer::LogMemoryPolicy::block_when_full;
            } else if (qlog::utility::equals_ignore_case(policy_config, "expand")) {
                buffer_config.policy = qlog::buffer::LogMemoryPolicy::auto_expand_when_full;
            }
        }
        if (log_config["high_perform_mode_freq_threshold_per_second"].is_integral()) {
            buffer_config.high_frequency_threshold_per_second =
                static_cast<std::uint64_t>(static_cast<std::int64_t>(
                    log_config["high_perform_mode_freq_threshold_per_second"]));
            if (buffer_config.high_frequency_threshold_per_second == 0) {
                buffer_config.high_frequency_threshold_per_second =
                    std::numeric_limits<std::uint64_t>::max();
            }
        }
    }
    return buffer_config;
}
}  // namespace qlog::config
```

## 调用顺序、技术点与验收

未来调用顺序：Property文本去重/解析→PropertyValue树→稳定name/category列表→本转换函数(传config["log"]与真实平台能力)→构造LogBuffer→Buffer内部容量/存储/恢复初始化。checksum供后续group/MISO/recovery识别文件，不代替SISO恢复快照的块几何和游标验证。

calculate_check_sum 无参数、返回uint64、const不修改字段；内部字符串构造可能分配，因此不标 noexcept。get_log_buffer_config_by_config 四个参数分别为名称、类别列表、log子树、平台支持事实，返回值对象，不返回借用配置树节点/临时名称的指针。它没有bool“验证成功”结果，缺字段/类型不符对应参考保留默认。

面试要区分：配置文本模型与运行态配置字段；请求容量与有效存储容量；频率阈值与Ring容量；值拷贝与借用引用；字符串比较语义与线程同步。默认block是写入满缓冲时的未来行为，不是当前解析函数会阻塞。0频率哨兵用UINT64_MAX抑制HP升级，不意味着阈值0立刻进入HP。checksum选择明确算法用于对齐恢复身份，不因std::hash能调用就满足参考兼容；其碰撞/字符符号限制必须如实说明。

本组候选仅在 /tmp/qlog-filter-review-20261007-ggvxthqk/buffer-candidate 写入。Debug/Release严格构建链接与运行各48个定向检查，UBSan + float-cast-overflow 同组48个通过；另-funsigned-char同组48个通过。每轮12组checksum直接调用参考实际log_buffer_config::calculate_check_sum（头文件调用方，不是只复制公式），覆盖空名称/空类别、顺序、大小写、中文字节、嵌入NUL、长串和分隔歧义。解析映射按源码规则检查，未直接调用BQLog log_imp::init。utility迁移候选再连接前一组过滤61个场景的UBSan检查通过。不是生产Buffer/CMake、完整BQLog日志双库差分、MISO并发/恢复/Windows/移动端/性能验收。

实际过滤三处先由用户补好，正式审查通过后接Property/PropertyValue/bitmap/filter四个cpp；本组写好再接log_buffer_config/buffer_config两个新cpp。utility已有cpp在target中，不创建空cpp、不改变QLOG_DEBUG PUBLIC仅Debug的规则。此轮未改CMake。

这组完成后核对MISO实际HEAD与本地补丁，再给独立MISO声明/成员不变量及分配提交/消费/恢复指导；之后才接存储、LP/HP、TLS、切换与完整LogBuffer，不提前推进Worker/Appender。
