#include "qlog/buffer/log_buffer_config.hpp"

namespace qlog::buffer {
std::uint64_t LogBufferConfig::calculate_check_sum() const {
    std::string verify_str = log_name + "/";

    for (const auto& category_name : log_categories_name) {
        verify_str += category_name + "/";
    }

    std::uint64_t check_sum = 0;
    for (char ch : verify_str) {
        check_sum *= 1099511628211ULL;  // CR:这个常数可以更换嘛 ？为什么要选这个？ 有什么考量？
        check_sum ^= static_cast<std::uint64_t>(ch);
    }

    return check_sum;
}
}  // namespace qlog::buffer