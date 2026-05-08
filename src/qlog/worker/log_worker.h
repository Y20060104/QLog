/*
 * Copyright (c) 2026 QLog Contributors
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include "qlog/primitives/atomic.h"
#include "qlog/primitives/condition_variable.h"

#include <cstdint>
#include <functional>
#include <thread>

namespace qlog
{

/**
 * log_worker — 异步日志处理线程
 *
 * BqLog 对标: log_worker.h/cpp (~206 行)
 *
 * 架构设计:
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  Worker Thread                 Watch Dog Thread         │
 *   │  ┌──────────────┐             ┌──────────────────┐      │
 *   │  │ wait_for(66ms│ ←─wakeup── │ check heartbeat  │      │
 *   │  │ or signal)   │             │ every 200ms      │      │
 *   │  ├──────────────┤             └──────────────────┘      │
 *   │  │ process_func_│                                       │
 *   │  ├──────────────┤                                       │
 *   │  │ epoch++      │──────────────────────► sync_cv_       │
 *   │  └──────────────┘                                       │
 *   └─────────────────────────────────────────────────────────┘
 *
 * 同步机制:
 *   - wakeup_cv_ : 主唤醒，66ms 超时 + do_wakeup() 信号
 *   - sync_cv_   : awake_and_wait_begin/join 等待 epoch 推进
 *   - process_epoch_ : 单调递增计数器，每完成一个处理周期 +1
 */
class log_worker
{
public:
    // BqLog: kProcessInterval = 66ms（对标原实现，不可随意修改）
    static constexpr int64_t k_process_interval_ms = 66;

    // Watch dog: 超过此时间未完成一个处理周期则强制唤醒
    static constexpr int64_t k_watch_dog_timeout_ms = 5000;

    /**
     * Worker 每个周期调用一次的处理回调
     * @param force_flush 是否需要 flush 所有 appender 缓冲区到 IO
     */
    using process_func_t = std::function<void(bool force_flush)>;

    log_worker() = default;
    ~log_worker();

    log_worker(const log_worker&) = delete;
    log_worker& operator=(const log_worker&) = delete;

    /**
     * 启动 Worker 线程 + Watch Dog 线程
     * @param process_func 每个处理周期的回调，由 log_manager 注入
     */
    void start(process_func_t process_func);

    /**
     * 停止所有线程（等待当前周期结束后退出）
     */
    void stop();

    /**
     * 立即唤醒 Worker（非阻塞）
     * 适用于新日志写入后的触发，让 appender 尽快输出
     */
    void awake();

    /**
     * 唤醒 Worker 并阻塞等待，直到本次处理周期完全结束
     * Force Flush 路径：保证调用返回时所有 appender 已 flush
     */
    void awake_and_wait_join();

    [[nodiscard]] bool is_running() const noexcept
    {
        return running_.load_acquire();
    }

private:
    void run();
    void watch_dog_run();
    void do_wakeup(bool force_flush_flag);

private:
    std::thread thread_;
    std::thread watch_dog_thread_;

    // ── 主唤醒机制（BqLog: condition_variable trigger + mutex lock）────────
    platform::mutex              wakeup_mutex_;
    platform::condition_variable wakeup_cv_;

    // ── 同步等待机制（awake_and_wait_join）───────────────────────────────────
    platform::mutex              sync_mutex_;
    platform::condition_variable sync_cv_;

    // ── 状态标志（BqLog: atomic<bool> flags）────────────────────────────────
    atomic<bool>     running_        {false};
    atomic<bool>     should_stop_    {false};
    atomic<bool>     wakeup_pending_ {false}; // BqLog: awake_flag_
    atomic<bool>     force_flush_    {false};

    /**
     * 处理轮次计数器
     * 每完成一次 process_func_() 调用后递增
     * awake_and_wait_join 等待 target = epoch + 1 达到
     */
    atomic<uint64_t> process_epoch_ {0};

    // Watch dog 心跳时间（steady_clock ms）
    atomic<uint64_t> last_process_ms_ {0};

    process_func_t process_func_;
};

} // namespace qlog