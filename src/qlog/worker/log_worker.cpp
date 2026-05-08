/*
 * Copyright (c) 2026 QLog Contributors
 * Licensed under the Apache License, Version 2.0
 */

#include "qlog/worker/log_worker.h"

#include <chrono>

namespace qlog
{

namespace
{
// 使用 steady_clock 避免系统时间跳变影响 watch dog
inline uint64_t steady_now_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}
} // namespace

// ============================================================================
// 生命周期
// ============================================================================

log_worker::~log_worker()
{
    stop();
}

void log_worker::start(process_func_t process_func)
{
    process_func_  = std::move(process_func);
    should_stop_.store_release(false);
    running_.store_release(true);
    last_process_ms_.store_release(steady_now_ms());

    thread_           = std::thread([this] { run(); });
    watch_dog_thread_ = std::thread([this] { watch_dog_run(); });
}

void log_worker::stop()
{
    if (!running_.load_acquire())
        return;

    should_stop_.store_release(true);
    do_wakeup(false); // 唤醒 worker 让其检查 should_stop_

    if (thread_.joinable())
        thread_.join();
    if (watch_dog_thread_.joinable())
        watch_dog_thread_.join();
}

// ============================================================================
// 唤醒接口
// ============================================================================

void log_worker::do_wakeup(bool force_flush_flag)
{
    platform::scoped_lock lock(wakeup_mutex_);
    if (force_flush_flag)
        force_flush_.store_release(true);
    wakeup_pending_.store_release(true);
    wakeup_cv_.notify_one();
}

void log_worker::awake()
{
    do_wakeup(false);
}

void log_worker::awake_and_wait_join()
{
    // 记录目标 epoch（当前 epoch + 1 意味着至少完成一个新的处理周期）
    const uint64_t target = process_epoch_.load_acquire() + 1;
    do_wakeup(true);

    // 等待 epoch 推进（worker 完成处理后通过 sync_cv_ 通知）
    platform::scoped_lock lock(sync_mutex_);
    sync_cv_.wait(
        lock,
        [this, target]() -> bool
        {
            return process_epoch_.load_acquire() >= target ||
                   !running_.load_acquire();
        }
    );
}

// ============================================================================
// Worker 主循环
// ============================================================================

void log_worker::run()
{
    while (!should_stop_.load_acquire())
    {
        // ── 等待阶段：66ms 超时唤醒 OR 主动信号唤醒（BqLog 66ms 对齐）────
        {
            platform::scoped_lock lock(wakeup_mutex_);

            // 仅在没有待处理信号时才真正等待
            if (!wakeup_pending_.load_acquire() && !should_stop_.load_acquire())
            {
                wakeup_cv_.wait_for(lock, k_process_interval_ms);
            }
            wakeup_pending_.store_release(false);
        }

        if (should_stop_.load_acquire())
            break;

        // ── 处理阶段：执行回调，消费 ring buffer 中的日志条目 ─────────────
        // exchange(false) 原子性地清除 force_flush_ 并获取当前值
        const bool is_force = force_flush_.exchange(false, std::memory_order_acq_rel);

        if (process_func_)
        {
            process_func_(is_force);
        }

        // ── 完成阶段：更新心跳，递增 epoch，通知等待方 ───────────────────
        last_process_ms_.store_release(steady_now_ms());
        process_epoch_.fetch_add(1, std::memory_order_release);

        {
            platform::scoped_lock lock(sync_mutex_);
            sync_cv_.notify_all();
        }
    }

    // Worker 退出时唤醒所有等待中的 awake_and_wait_join 调用方
    running_.store_release(false);
    {
        platform::scoped_lock lock(sync_mutex_);
        sync_cv_.notify_all();
    }
}

// ============================================================================
// Watch Dog 监控线程
// ============================================================================

/**
 * Watch Dog 设计原理：
 *   每 200ms 采样一次 last_process_ms_
 *   如超过 k_watch_dog_timeout_ms 未完成处理周期，则强制唤醒 worker
 *
 *   在实际生产环境中，这里还应触发告警上报（metrics/logging）
 *   当前实现：仅强制唤醒，让 worker 在下一周期恢复
 */
void log_worker::watch_dog_run()
{
    platform::mutex    wd_mutex;
    platform::condition_variable wd_cv;

    while (!should_stop_.load_acquire())
    {
        // 每 200ms 检查一次心跳（比 k_process_interval_ms 更密集）
        {
            platform::scoped_lock lock(wd_mutex);
            wd_cv.wait_for(lock, 200);
        }

        if (should_stop_.load_acquire())
            break;

        const uint64_t now  = steady_now_ms();
        const uint64_t last = last_process_ms_.load_acquire();

        if (last > 0 && now > last + static_cast<uint64_t>(k_watch_dog_timeout_ms))
        {
            // Worker 可能卡住，强制唤醒（non-force，避免重复 flush）
            // 生产环境：此处添加告警日志或 metrics 上报
            do_wakeup(false);
        }
    }
}

} // namespace qlog