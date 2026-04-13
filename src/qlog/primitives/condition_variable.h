#pragma once
#include <cstdint>

namespace qlog::platform
{

class mutex
{
private:
    void* platform_data_;

public:
    void lock();
    void unlock();
    bool try_lock();

    friend class scoped_lock;
    friend class condition_variable;
};

class scoped_lock
{
private:
    mutex& m_;

public:
    explicit scoped_lock(mutex& m);
    ~scoped_lock(); // RAII 自动解锁

    scoped_lock(const scoped_lock&) = delete;
    scoped_lock& operator=(const scoped_lock&) = delete;
};

class condition_variable
{
private:
    void* platform_data_;

public:
    condition_variable();
    ~condition_variable();

    condition_variable(const condition_variable&) = delete;
    condition_variable& operator=(const condition_variable&) = delete;

    // 基础 wait（需平台实现）
    void wait(scoped_lock& lock);

    // 带谓词的 wait（推荐用法）
    template<typename Pred> void wait(scoped_lock& lock, Pred&& pred)
    {
        while (!pred())
            wait(lock);
    }

    // 超时 wait（返回 true 表示被通知，false 表示超时）
    bool wait_for(scoped_lock& lock, int64_t ms);

    // 带谓词的超时 wait
    template<typename Pred> bool wait_for(scoped_lock& lock, int64_t ms, Pred&& pred);

    void notify_one();
    void notify_all();
};

// 模板实现
template<typename Pred>
bool condition_variable::wait_for(scoped_lock& lock, int64_t ms, Pred&& pred)
{
    // 简化实现：循环检查谓词直到超时
    // 完整实现需要平台支持高精度时间
    while (!pred())
    {
        if (!wait_for(lock, ms))
        {
            return false; // 超时
        }
    }
    return true; // 被谓词满足
}

} // namespace qlog::platform