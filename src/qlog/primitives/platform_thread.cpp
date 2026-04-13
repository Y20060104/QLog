/*
 * Copyright (c) 2026 QLog Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "platform_thread.h"

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>

#include <chrono>
#endif

namespace qlog::platform
{

// ============================================================================
// thread 类实现
// ============================================================================

thread::~thread()
{
    // TODO: 清理资源（M0 阶段先空实现）
}

void thread::join()
{
    // TODO: 实现平台相关的 join 逻辑（M0 阶段先空实现）
}

bool thread::joinable() const
{
    // TODO: 实现平台相关的 joinable 检查（M0 阶段先空实现）
    return false;
}

thread_id thread::current_thread_id()
{
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return pthread_self();
#endif
}

// ============================================================================
// 睡眠函数实现
// ============================================================================

void sleep_milliseconds(int64_t ms)
{
#ifdef _WIN32
    Sleep(static_cast<DWORD>(ms));
#else
    usleep(static_cast<useconds_t>(ms * 1000));
#endif
}

void sleep_microseconds(int64_t us)
{
#ifdef _WIN32
    // Windows Sleep 最小粒度是 1ms，使用 Sleep(1) 近似
    if (us < 1000)
    {
        Sleep(1);
    }
    else
    {
        Sleep(static_cast<DWORD>(us / 1000));
    }
#else
    usleep(static_cast<useconds_t>(us));
#endif
}

} // namespace qlog::platform
