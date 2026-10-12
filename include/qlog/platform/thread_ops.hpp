#pragma once

#include <cstdint>
#include <string>

namespace qlog::platform {
using thread_id = std::uint64_t;

thread_id get_current_thread_id();
void yield();
void cpu_relax();
void sleep(std::uint64_t millsec);
std::string get_current_thread_name();
}  // namespace qlog::platform