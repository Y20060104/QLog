// Adapted from Tencent BQLog (Apache-2.0), reference 78cbfbef.
#include "qlog/platform/stack_trace.hpp"

#include <cxxabi.h>
#include <execinfo.h>

#include <cassert>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <string>

#include "qlog/platform/macros.hpp"
#include "qlog/utility/utf_conversion.hpp"
namespace qlog::platform {
QLOG_TLS_NON_POD(std::string, stack_trace_current_str_)
QLOG_TLS_NON_POD(std::u16string, stack_trace_current_str_u16_)
void get_stack_trace(uint32_t skip_frame_count, const char*& out_str_ptr,
                     uint32_t& out_char_count) {
    if (!stack_trace_current_str_) {
        out_str_ptr = nullptr;
        out_char_count = 0;
        return;  // This occurs when program exit in Main thread.
    }
    std::string& stack_trace_str_ref = stack_trace_current_str_.get();
    stack_trace_str_ref.clear();
    void* buffer[128];
    char** stacks;
    auto stack_count = backtrace(buffer, 128);
    stacks = backtrace_symbols(buffer, stack_count);
    uint32_t valid_frame_count = 0;
    if (stacks) {
        for (decltype(stack_count) i = 0; i < stack_count; i++) {
            if (valid_frame_count == 0) {
                if (strstr(stacks[i], "get_stack_trace")) {
                    continue;
                }
                valid_frame_count = 1;
            }
            if (valid_frame_count++ <= skip_frame_count) {
                continue;
            }
            stack_trace_str_ref.push_back('\n');
            char* demangled_str = nullptr;
            demangled_str = strstr(stacks[i], "_Z");
            if (demangled_str) {
                auto tail_mark = strchr(demangled_str, ' ');
                if (tail_mark) {
                    tail_mark[0] = '\0';
                }
                tail_mark = strchr(demangled_str, '+');
                if (tail_mark) {
                    tail_mark[0] = '\0';
                }
                int32_t status;
                char* demangled = abi::__cxa_demangle(demangled_str, nullptr, nullptr, &status);
                if (status == 0 && demangled) {
                    auto str_len = strlen(demangled);
                    stack_trace_str_ref.append(stacks[i],
                                               static_cast<size_t>(demangled_str - stacks[i]));
                    stack_trace_str_ref.append(demangled, (size_t)str_len);
                    free(demangled);
                } else {
                    demangled_str = nullptr;
                }
            }
            if (!demangled_str) {
                auto str_len = strlen(stacks[i]);
                stack_trace_str_ref.append(stacks[i], (size_t)str_len);
            }
        }
    }
    free(stacks);
    out_str_ptr = stack_trace_str_ref.data();
    out_char_count = (uint32_t)stack_trace_str_ref.size();
}

void get_stack_trace_utf16(uint32_t skip_frame_count, const char16_t*& out_str_ptr,
                           uint32_t& out_char_count) {
    if (!stack_trace_current_str_u16_) {
        out_str_ptr = nullptr;
        out_char_count = 0;
        return;  // This occurs when program exit in Main thread.
    }
    std::u16string& stack_trace_str_ref = stack_trace_current_str_u16_.get();
    const char* u8_str;
    uint32_t u8_char_count;
    get_stack_trace(skip_frame_count, u8_str, u8_char_count);
    stack_trace_str_ref.clear();
    stack_trace_str_ref.resize((u8_char_count << 1) + 1);
    size_t encoded_size = (size_t)qlog::utility::utf8_to_utf16_sw(
        u8_str, u8_char_count, stack_trace_str_ref.data(), (uint32_t)stack_trace_str_ref.size());
    assert(encoded_size < stack_trace_str_ref.size());
    stack_trace_str_ref.resize(encoded_size);
    out_str_ptr = stack_trace_str_ref.data();
    out_char_count = (uint32_t)stack_trace_str_ref.size();
}

}  // namespace qlog::platform
