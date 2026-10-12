# 线程平台基础完整实现指导

> **2026-10-09最新实际进度：** 用户已填写这组生产函数并接库；Windows文件名及四处抄写已纠正，CR保留。两系统Debug/Release实际库/调用方及CTest各12/12，根库17cpp。下方“仅候选/未写生产”是原指导时点。下一组见[平凡构造原子完整指导](PLATFORM_ATOMIC_PRIMITIVES_IMPLEMENTATION_GUIDE_20261009_CHS.md)，证据见[最新记录](../validation/THREAD_OPS_REVIEW_ATOMIC_GUIDANCE_20261009_CHS.md)。

日期：2026-10-09。下一组由用户填写四个新函数；本轮只给文档和临时候选，没有新增QLog生产线程声明/定义或修改根CMake。

## 当前依据与为什么先做这一组

QLog feat/spsc-ring-opt@f24f3afcd8e9418ea48ad5a6b05680c622008145。BQLog main@78cbfbef4f87e7558335f0c04bf6dd6894b46451；thread.h、thread_posix.cpp、thread_win64.cpp以及已核对的spin_lock/mutex文件相对HEAD无差异，参考整树仍有MISO和构建文件修改。

用户已填写Windows get_lexically_path。实际审查并复现..未抵消前段，助手仅把!result.empty()纠正为!result_split.empty()，保持CR。实际Windows根库和调用方Debug/Release各1071路径场景通过；现有public get_lexically_path声明的POSIX定义还未写。

BQLog POSIX词法路径的账户失败分支调用util::log_device_console，基准目录访问common_global_vars并持spin_lock_zero_init。mutex错误分支又引用诊断输出和线程名。这些不是QLog现存函数，不能先写虚构调用。先抽出真实thread静态方法中能独立完成的当前线程操作；只是模块归属调整，保留原系统调用、TLS缓存和睡眠规则。

对应参考：src/bq_common/platform/thread/thread.h:50、:76-86；thread_posix.cpp:378-420；thread_win64.cpp:118-145、:192-199。本组不涉及创建/取消/join/detach线程对象。

## 文件与完整公共声明

生产目标（由用户新建）：include/qlog/platform/thread_ops.hpp、src/platform/posix/thread_ops.cpp、src/platform/windows/thread_ops.cpp。命名空间qlog::platform；公共自由函数在头文件声明，定义在系统cpp。每线程缓存和POSIX转换模板只在对应cpp匿名命名空间内可见。无需系统类型公共头，因为接口仅用uint64_t；无需新Manager或拥有线程句柄的包装。

```cpp
#pragma once

#include <cstdint>

namespace qlog::platform {
using thread_id = std::uint64_t;

thread_id get_current_thread_id();
void yield();
void cpu_relax();
void sleep(std::uint64_t millsec);
}  // namespace qlog::platform
```

## POSIX完整实现

```cpp
#include "qlog/platform/thread_ops.hpp"

#include <cerrno>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <type_traits>

namespace qlog::platform {
namespace {
thread_local thread_id current_thread_id_ = 0;

template <typename Handle>
thread_id convert_thread_id(Handle handle) {
    if constexpr (std::is_pointer_v<Handle>) {
        return static_cast<thread_id>(reinterpret_cast<std::uintptr_t>(handle));
    } else {
        return static_cast<thread_id>(handle);
    }
}
}  // namespace

thread_id get_current_thread_id() {
    if (current_thread_id_ == 0) {
        current_thread_id_ = convert_thread_id(::pthread_self());
    }
    return current_thread_id_;
}

void yield() {
    ::sched_yield();
}

void cpu_relax() {
#if defined(__arm__) || defined(__aarch64__)
    __asm__ __volatile__("yield");
#else
    __asm__ __volatile__("pause");
#endif
}

void sleep(std::uint64_t millsec) {
    timespec req;
    timespec rem;
    req.tv_sec = static_cast<time_t>(millsec / 1000U);
    req.tv_nsec = static_cast<long>((millsec % 1000U) * 1000000U);

    while (::nanosleep(&req, &rem) == -1) {
        if (errno == EINTR) {
            req = rem;
        } else {
            break;
        }
    }
}
}  // namespace qlog::platform
```

convert_thread_id是本文件完整提供的内部模板：Linux当前pthread_t是整数，某些POSIX平台为指针；模板分别采用static_cast或经uintptr_t的reinterpret_cast，表达BQLog原C式转换的含义。它只转换标识，不解引用、不哈希、不调用gettid。不能在非模板函数内用if constexpr假定另一分支一定不会做类型检查。

get_current_thread_id第一次从pthread_self取得值，之后读当前线程自己的TLS缓存。yield忽略sched_yield返回值，与参考一致。cpu_relax在ARM用yield指令，在参考支持的x86路径用pause；这里没有给其他架构增加新回退。

sleep的millsec是毫秒；秒部分millsec/1000，纳秒部分(millsec%1000)*1000000。nanosleep只在返回-1且errno为EINTR时，用rem更新req再试；其他失败直接结束，接口不返回错误码。不要重新等待原始完整时长，也不要改成绝对截止时间协议。

## Windows完整实现

```cpp
#include "qlog/platform/thread_ops.hpp"

#include <windows.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace qlog::platform {
namespace {
thread_local thread_id current_thread_id_ = 0;
}  // namespace

thread_id get_current_thread_id() {
    if (current_thread_id_ == 0) {
        current_thread_id_ = static_cast<thread_id>(::GetCurrentThreadId());
    }
    return current_thread_id_;
}

void yield() {
    ::SwitchToThread();
}

void cpu_relax() {
#if defined(_MSC_VER)
#if defined(_M_IX86) || defined(_M_X64)
    _mm_pause();
#elif defined(_M_ARM) || defined(_M_ARM64)
    __yield();
#else
    _ReadWriteBarrier();
#endif
#else
#if defined(__arm__) || defined(__aarch64__)
    __asm__ __volatile__("yield");
#else
    __asm__ __volatile__("pause");
#endif
#endif
}

void sleep(std::uint64_t millsec) {
    ::SleepEx(static_cast<DWORD>(millsec), TRUE);
}
}  // namespace qlog::platform
```

get_current_thread_id缓存GetCurrentThreadId的原生DWORD值，扩大到uint64_t。yield调用SwitchToThread，不增加失败后的Sleep(0)回退。MSVC x86/x64 cpu_relax为_mm_pause，ARM为__yield，其他MSVC目标沿参考_ReadWriteBarrier；非MSVC沿参考ARM yield/其他pause。

sleep严格沿参考SleepEx(DWORD(millsec),true)，是可被APC等唤醒的alertable等待，返回值被忽略，不保证补满剩余时间。uint64_t转DWORD保留低32位，不钳制、不分段；0xFFFFFFFF是Windows INFINITE特殊值。巨大毫秒值和无限等待语义不能被解释成跨平台统一的有限等待合同。

## 成员不变量与接口边界

本组没有拥有线程的实例成员。cpp内部current_thread_id_每线程初始为0，首次获取后保存原生标识；0只是参考采用的未缓存标记。缓存不在不同线程间共享，因此不需要原子。线程ID可能在线程结束后复用，不是跨进程/跨运行永久标识。现有SISO std::thread::id调试字段本轮不改。

yield进入调度层，不保证有线程接手或发生上下文切换；cpu_relax是处理器等待提示，不是内存屏障或线程安全协议。未来自旋锁的同步来自原子的acquire/release，不能只靠pause、yield或volatile。

睡眠输入仍按参考转换，没有新溢出错误协议。POSIX在time_t范围外的转换及非EINTR错误不作新修复；Windows保持缩窄和alertable提前返回。非法线程身份/其他CPU平台及精确等待时长不在本轮验收内。

## 实现和构建接入顺序

1. 建公共头和两个系统cpp；先填TLS/get_current_thread_id，再填yield、cpu_relax、sleep，完整代码与内部模板见上。
2. 用户填完后审实际源并做独立编译/实际公开调用方链接；当前根库仍16cpp。本组写完后在既有WIN32/else target_sources中分别追加对应thread_ops.cpp，二选一，共17cpp。
3. POSIX分支find_package(Threads REQUIRED)，target_link_libraries(qlog PUBLIC Threads::Threads)，让静态库调用方获得所需线程链接选项。QLOG_DEBUG继续现有Debug-only PUBLIC，不在调用方重新定义，不设置为0。
4. 下一组核对自旋锁与必要原子表示，再协调mutex/诊断共享状态的真实初始化依赖；POSIX账户路径/基准目录及open-close/exclusive在依赖齐备后闭合。之后FileHandle/FileManager、MemoryMap、NormalBuffer、MISO；不提前进入Worker/Appender。

## 对应面试问题与答案

- pthread_self就是Linux内核TID吗？不是。本组保留pthread_t的数值表示；它与gettid及std::thread::id不是可直接混用的合同。
- 为什么thread_local缓存不加atomic？每线程访问自己的副本，当前没有跨线程访问；TLS不能让其他共享数据自动安全。
- yield与cpu_relax区别？前者请求系统调度，后者为当前CPU等待循环提供提示。都不承诺公平或一定发生线程切换。
- pause能代替acquire/release吗？不能。数据发布和读取的happens-before需要原子/锁协议，CPU等待提示不建立它。
- EINTR为什么使用rem？只继续等待本次尚未完成的请求；从头再睡会重复已经睡过的部分，参考没有采用绝对截止时间来扣除信号处理时长。
- Windows与POSIX sleep保证相同吗？不保证。Windows是alertable且参数缩窄到DWORD，POSIX在EINTR时继续剩余请求；本组保留实际差异。
- 为什么不把完整Thread对象一次写完？创建/生命周期和线程名有额外依赖；四项静态操作就是当前锁/初始化所需的独立底座，没有另设线程管理器。

## 本轮验证边界

临时候选的Linux GCC和原生Windows MSVC各Debug/Release严格编译链接、12项线程标识检查及四个操作定向调用通过，包括两个同时存活线程的TLS和原生值一致性。只有临时候选，不计QLog生产线程完成。

实际Windows根库及调用方使用/W4 /WX /permissive-，审查全局额外/utf-8 /EHsc并仅排除既有对齐提示C4324；BQLog参考头标为SYSTEM，不以第三方头警告作为QLog警告验收。根CMake及生产警告策略未改。未执行ARM/Apple/BSD、EINTR注入、APC、巨大毫秒值、性能、自旋锁或完整并发验收；未重跑常驻CTest。

审查目录：D:\qq344\Documents\qlog-thread-foundation-guidance-20261009-6c0e2f。实际路径修正与指导证据见[本轮记录](../validation/WINDOWS_LEXICAL_THREAD_GUIDANCE_20261009_CHS.md)。
