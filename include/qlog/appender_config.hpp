#pragma once

#include <cstddef>
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

enum class ConsoleStream : std::uint8_t {
    stdout_stream,
    stderr_stream,
};

struct TimeZoneConfig {
    std::int16_t offset_minutes{0};
};

struct ConsoleConfig {
    ConsoleStream stream{ConsoleStream::stdout_stream};
};

struct TextFileConfig {
    std::string path;
    std::uint32_t retry_interval_us{100000U};
};

struct TextOutputConfig {
    TimeZoneConfig time_zone;
    std::size_t batch_bytes{256 * 1024U};
    std::uint32_t flush_interval_us{100000U};
};

struct AppenderConfig {
    std::string name;
    AppenderType type{AppenderType::Console};
    bool enabled{true};

    FilterConfig filter;
    TextOutputConfig text;

    ConsoleConfig console;
    TextFileConfig file;
};

}  // namespace qlog