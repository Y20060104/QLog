#include "qlog/detail/text_formatter.hpp"

#include <array>
#include <cstddef>
#include <cstring>

int main() {
    using namespace qlog::detail;
    std::array<std::byte, 16> output{};
    const auto* literal = reinterpret_cast<const std::byte*>("abc");
    auto result = render_message_utf8(literal, 3U, nullptr, 0U, output.data(), output.size());
    if (result.failure || result.size != 3U || std::memcmp(output.data(), "abc", 3U) != 0) return 1;

    DecodedArg arg{};
    arg.tag = ArgumentTag::UInt32;
    arg.value.bits = 42U;
    const auto* format = reinterpret_cast<const std::byte*>("value={}");
    result = render_message_utf8(format, 8U, &arg, 1U, output.data(), output.size());
    if (result.failure || result.size != 8U || std::memcmp(output.data(), "value=42", 8U) != 0) return 2;

    std::array<std::byte, 4> guarded{};
    guarded.fill(std::byte{0x5a});
    result = render_message_utf8(literal, 3U, nullptr, 0U, guarded.data() + 1U, 2U);
    if (!result.failure || result.failure->error != FormatError::text_output_limit_exceeded ||
        result.size != 0U || guarded.front() != std::byte{0x5a} || guarded.back() != std::byte{0x5a}) return 3;

    result = render_message_utf8(nullptr, 1U, nullptr, 0U, output.data(), output.size());
    if (!result.failure || result.failure->error != FormatError::invalid_format_metadata) return 4;
    return 0;
}
