// clang-format off
// DbgHelp requires Windows declarations before its own header.
#include <windows.h>
#include <dbghelp.h>
// clang-format on
// Adapted from Tencent BQLog (Apache-2.0), reference 78cbfbef.
#include "qlog/platform/stack_trace.hpp"


#include <cassert>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

#include "qlog/global/common_vars.hpp"
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
    const char16_t* u16_str;
    uint32_t u16_str_len;
    get_stack_trace_utf16(skip_frame_count, u16_str, u16_str_len);
    stack_trace_str_ref.clear();
    stack_trace_str_ref.resize(((u16_str_len * 3) >> 1) + 1);
    size_t encoded_size =
        (size_t)qlog::utility::utf16_to_utf8_sw(u16_str, u16_str_len, stack_trace_str_ref.data(),
                                                (uint32_t)stack_trace_current_str_.get().size());
    assert(encoded_size < stack_trace_str_ref.size());
    stack_trace_str_ref.resize(encoded_size);
    out_str_ptr = stack_trace_str_ref.data();
    out_char_count = (uint32_t)stack_trace_current_str_.get().size();
}

void get_stack_trace_utf16(uint32_t skip_frame_count, const char16_t*& out_str_ptr,
                           uint32_t& out_char_count) {
    if (!stack_trace_current_str_u16_) {
        out_str_ptr = nullptr;
        out_char_count = 0;
        return;  // This occurs when program exit in Main thread.
    }
    std::u16string& stack_trace_str_ref = stack_trace_current_str_u16_.get();
    HANDLE thread = GetCurrentThread();
    CONTEXT context;

    stack_trace_str_ref.clear();
    qlog::platform::scoped_mutex lock(common_global_vars::get().stack_trace_mutex_);
    if (!common_global_vars::get().stack_trace_sym_initialized_.load(
            qlog::platform::memory_order::relaxed)) {
        SymInitialize(common_global_vars::get().stack_trace_process_, NULL, TRUE);
        common_global_vars::get().stack_trace_sym_initialized_.store(
            true, qlog::platform::memory_order::relaxed);
    }
    RtlCaptureContext(&context);

    STACKFRAME64 stack;
    memset(&stack, 0, sizeof(STACKFRAME64));
    DWORD machine_type;

#ifdef _M_ARM64
    machine_type = IMAGE_FILE_MACHINE_ARM64;
    stack.AddrPC.Offset = context.Pc;
    stack.AddrPC.Mode = AddrModeFlat;
    stack.AddrStack.Offset = context.Sp;
    stack.AddrStack.Mode = AddrModeFlat;
    stack.AddrFrame.Offset = context.Fp;
    stack.AddrFrame.Mode = AddrModeFlat;
#elif defined(_M_ARM)
    machine_type = IMAGE_FILE_MACHINE_ARM;
    stack.AddrPC.Offset = context.Pc;
    stack.AddrPC.Mode = AddrModeFlat;
    stack.AddrStack.Offset = context.Sp;
    stack.AddrStack.Mode = AddrModeFlat;
    stack.AddrFrame.Offset = context.R11;
    stack.AddrFrame.Mode = AddrModeFlat;
#elif defined(_M_X64)
    machine_type = IMAGE_FILE_MACHINE_AMD64;
    stack.AddrPC.Offset = context.Rip;
    stack.AddrPC.Mode = AddrModeFlat;
    stack.AddrStack.Offset = context.Rsp;
    stack.AddrStack.Mode = AddrModeFlat;
    stack.AddrFrame.Offset = context.Rbp;
    stack.AddrFrame.Mode = AddrModeFlat;
#else
    static_assert(false, "Unsupported architecture on Windows");
#endif

    bool sym_refreshed = false;
    bool effective_stack_started = false;
    uint32_t current_frame_idx = 0;
    while (StackWalk64(machine_type, common_global_vars::get().stack_trace_process_, thread, &stack,
                       &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
        static_assert(sizeof(void*) == 8, "Only 64bit windows is supported");

        DWORD64 address = stack.AddrPC.Offset;
        char buffer[sizeof(SYMBOL_INFOW) + MAX_SYM_NAME * sizeof(WCHAR)];
        PSYMBOL_INFOW symbol = (PSYMBOL_INFOW)buffer;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
        symbol->MaxNameLen = MAX_SYM_NAME;
        DWORD64 displacement64 = 0;
        bool symbo_found = SymFromAddrW(common_global_vars::get().stack_trace_process_, address,
                                        &displacement64, symbol);
        if (!symbo_found && !sym_refreshed) {
            int32_t error_code = static_cast<int32_t>(GetLastError());
            (void)error_code;
            sym_refreshed = true;
            SymRefreshModuleList(common_global_vars::get().stack_trace_process_);
            symbo_found = SymFromAddrW(common_global_vars::get().stack_trace_process_, address,
                                       &displacement64, symbol);
        }
        if (symbo_found) {
            if (!effective_stack_started) {
                if (wcsstr(symbol->Name, L"qlog::platform::get_stack_trace") == symbol->Name ||
                    wcsstr(symbol->Name, L"qlog::api::__api_get_stack_trace") == symbol->Name) {
                    continue;
                }
                effective_stack_started = true;
            }
        }
        if (effective_stack_started && (++current_frame_idx <= skip_frame_count)) {
            continue;
        }

        stack_trace_str_ref.push_back(u'\n');
        stack_trace_str_ref.resize(stack_trace_str_ref.size() + 16);
        static_assert(sizeof(wchar_t) == sizeof(WCHAR) && sizeof(WCHAR) == sizeof(char16_t),
                      "windows wchar_t should be 2 bytes");
        swprintf((wchar_t*)stack_trace_str_ref.data() + stack_trace_str_ref.size() - 16, 16 + 1,
                 L"%016" PRIx64 "", (uintptr_t)address);
        stack_trace_str_ref.push_back(L'\t');

        DWORD64 module_base =
            SymGetModuleBase64(common_global_vars::get().stack_trace_process_, address);
        if (!module_base && !sym_refreshed) {
            sym_refreshed = true;
            SymRefreshModuleList(common_global_vars::get().stack_trace_process_);
            module_base =
                SymGetModuleBase64(common_global_vars::get().stack_trace_process_, address);
        }

        if (module_base) {
            HMODULE h_module = (HMODULE)module_base;
            constexpr DWORD default_max_module_name_len = 1024;
            wchar_t module_file_name[default_max_module_name_len];
            const wchar_t* module_file_name_ptr = module_file_name;
            DWORD get_module_length =
                GetModuleFileNameW(h_module, module_file_name, default_max_module_name_len);
            if (!get_module_length && !sym_refreshed) {
                sym_refreshed = true;
                SymRefreshModuleList(common_global_vars::get().stack_trace_process_);
                get_module_length =
                    GetModuleFileNameW(h_module, module_file_name, default_max_module_name_len);
            }
            module_file_name[default_max_module_name_len - 1] = (wchar_t)0;
            if (get_module_length) {
                const wchar_t* last_slash = wcsrchr(module_file_name, L'\\');
                if (last_slash != nullptr) {
                    module_file_name_ptr = last_slash + 1;
                }
                stack_trace_str_ref += (const char16_t*)module_file_name_ptr;
                stack_trace_str_ref.push_back(u'\t');
            }
        } else {
            stack_trace_str_ref += u"unknown module\t";
        }

        if (symbo_found) {
            stack_trace_str_ref += (const char16_t*)symbol->Name;
            stack_trace_str_ref.push_back(u' ');
            DWORD displacement = 0;
            IMAGEHLP_LINEW64 line;
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            if (SymGetLineFromAddrW64(common_global_vars::get().stack_trace_process_, address,
                                      &displacement, &line)) {
                stack_trace_str_ref.push_back(u'(');
                stack_trace_str_ref += (const char16_t*)line.FileName;
                stack_trace_str_ref.push_back(u':');
                char tmp[32];
                auto num_len =
                    snprintf(tmp, sizeof(tmp), "%" PRIu32, static_cast<uint32_t>(line.LineNumber));
                for (decltype(num_len) i = 0; i < num_len; ++i) {
                    stack_trace_str_ref.push_back(static_cast<char16_t>(tmp[i]));
                }
                stack_trace_str_ref.push_back(u')');
            }
        } else {
            stack_trace_str_ref += u"(unknown function and file)";
        }
    }
    out_str_ptr = stack_trace_str_ref.data();
    out_char_count = (uint32_t)stack_trace_str_ref.size();
}

}  // namespace qlog::platform
