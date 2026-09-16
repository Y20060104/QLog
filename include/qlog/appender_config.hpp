#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qlog {

enum class AppenderType : std::uint8_t {
    Console,
    TextFile,
};

struct FilterConfig {
    // 六个合法等级默认全部启用。
    std::uint32_t levels{0x3FU};

    // public 配置中的空数组表示：
    // 规范化时按 Logger category 数量展开为全 1。
    std::vector<std::uint8_t> category_enabled;
};

struct AppenderConfig {
    std::string name;
    AppenderType type{AppenderType::Console};
    bool enabled{true};
    FilterConfig filter;
};

}  // namespace qlog