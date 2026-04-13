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

#include "condition_variable.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace qlog::platform
{

// ============================================================================
// mutex 类实现
// ============================================================================

void mutex::lock()
{
    // TODO: 实现平台相关的 lock 逻辑（M0 阶段先空实现）
}

void mutex::unlock()
{
    // TODO: 实现平台相关的 unlock 逻辑（M0 阶段先空实现）
}

bool mutex::try_lock()
{
    // TODO: 实现平台相关的 try_lock 逻辑（M0 阶段先空实现）
    return false;
}

// ============================================================================
// scoped_lock 类实现
// ============================================================================

scoped_lock::scoped_lock(mutex& m)
    : m_(m)
{
    m_.lock();
}

scoped_lock::~scoped_lock()
{
    m_.unlock();
}

// ============================================================================
// condition_variable 类实现
// ============================================================================

condition_variable::condition_variable()
{
    // TODO: 初始化平台私有数据（M0 阶段先空实现）
}

condition_variable::~condition_variable()
{
    // TODO: 清理平台私有数据（M0 阶段先空实现）
}

void condition_variable::wait(scoped_lock& lock)
{
    // TODO: 实现平台相关的 wait 逻辑（M0 阶段先空实现）
    (void)lock; // 消除未使用警告
}

bool condition_variable::wait_for(scoped_lock& lock, int64_t ms)
{
    // TODO: 实现平台相关的 wait_for 逻辑（M0 阶段先空实现）
    (void)lock;   // 消除未使用警告
    (void)ms;     // 消除未使用警告
    return false; // 超时
}

void condition_variable::notify_one()
{
    // TODO: 实现平台相关的 notify_one 逻辑（M0 阶段先空实现）
}

void condition_variable::notify_all()
{
    // TODO: 实现平台相关的 notify_all 逻辑（M0 阶段先空实现）
}

} // namespace qlog::platform
