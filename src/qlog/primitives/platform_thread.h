#pragma once
#include <cstdint>

#ifdef _WIN32
// Windows headers (no additional includes needed for unsigned long)
#else
#include <pthread.h>
#endif

namespace qlog::platform
{

#ifdef _WIN32
using thread_id = unsigned long; // DWORD in Windows
#else
using thread_id = pthread_t; // pthread_t in POSIX
#endif

class thread
{
public:
    // 禁用拷贝和移动（暂不支持）
    thread(const thread&) = delete;
    thread& operator=(const thread&) = delete;
    thread(thread&&) = delete;
    thread& operator=(thread&&) = delete;

    template<typename F> explicit thread(F&& func); // 启动线程

    ~thread(); // 析构函数

    void join();           // 等待线程完成
    bool joinable() const; // 是否可以加入

    static thread_id current_thread_id(); // 获取当前线程 ID
};

// 平台睡眠函数
void sleep_milliseconds(int64_t ms);
void sleep_microseconds(int64_t us);

} // namespace qlog::platform