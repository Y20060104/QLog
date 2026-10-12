# 平台存储接续：对齐内存分配完整实现指导

> **2026-10-08 按系统拆分（本段最新）：** 用户授权助手拆分已有头文件/函数，现采用公共 API、POSIX/Windows 原生类型头和系统 cpp。两个已实现分配函数仅搬移，CMake 按 WIN32 二选一，根库每个平台仍为 14 个 cpp；公共 API/命名空间不变。file_io.hpp 当前已由用户补到 8 个声明，两个系统 file_io.cpp 只有待填写骨架、未接库；助手未写 I/O 函数体。实际 Linux Debug/Release 严格构建与公开调用方链接、CTest 各 11/11（常驻 10 + 临时拆分调用方 1）通过；Windows 仅做模拟宏头文件语法检查，未编译运行原生 Windows 实现。详见 [系统拆分记录](PLATFORM_SYSTEM_SPLIT_20261008_CHS.md)，后续仍按 [文件 I/O 指导](PLATFORM_FILE_IO_IMPLEMENTATION_GUIDE_20261007_CHS.md) 由用户完成。下方原日期结果保留为历史证据。
> **2026-10-07 平台分配实际收口（本段最新）：** 实际aligned_memory已审查、名称同步并接根库，当前14个cpp；Debug/Release实际库/公开调用方严格构建链接、外部CTest各13/13（常驻10+临时3）、实际Linux分配85检查及定向UBSan通过。Windows/移动端、完整Buffer/并发/恢复/性能不计通过。下一组是 [文件原生类型及8个句柄操作](PLATFORM_FILE_IO_IMPLEMENTATION_GUIDE_20261007_CHS.md)，随后路径/open-close/exclusive与FileHandle/FileManager→MemoryMap→NormalBuffer→MISO及完整LP/HP依赖。整数越界两方案仍暂不选定。详见 [实际证据](../validation/PLATFORM_ALIGNED_MEMORY_CMAKE_20261007_CHS.md)。下方未接库/草稿快照按原阶段理解。

> **2026-10-07 当前实施状态：** 已重新核对QLog HEAD `f24f3afcd8e9418ea48ad5a6b05680c622008145`，实际已新增 `include/qlog/platform/aligned_memory.hpp` 与 `src/platform/aligned_memory.cpp` 声明/函数体草稿，待实际审查、接CMake与验证；下方代码仍是供用户对照填写的指导，不能把此前候选结果计作新文件通过。本次静态快照中头文件释放名为 `align_free`、cpp定义为 `aligned_free`，cpp命名空间为 `qlog::platfrom`、头文件为 `qlog::platform`，后续应核对这两处声明/定义名称；本次仅记录，不修改草稿。既有Linux候选验证按原记录保留，本次没有重跑测试；Windows分支未验。文件操作和MemoryMap随后独立核对，参考实际位置为 `src/bq_common/platform/io/`，不在本组提前声明或调用尚未实现的辅助函数。

日期：2026-10-07。用户确认先按实际依赖补平台存储基础，再实现MISO。本组只有平台对齐堆内存分配/释放这一完整模块，由用户手写生产函数；助手只准备临时指导与验证。当前配置CMake收口见 [BUFFER_CONFIG_CMAKE_20261007_CHS.md](../validation/BUFFER_CONFIG_CMAKE_20261007_CHS.md)。

## 源码核对与路线

QLog审查起点HEAD 1614ea56ff2d525313e3bd0399b96e6ff252e721（当时有助手修正/CMake及用户未跟踪配置文件），最终源码复核HEAD f24f3afcd8e9418ea48ad5a6b05680c622008145；BQLog HEAD 78cbfbef4f87e7558335f0c04bf6dd6894b46451。平台分配、NormalBuffer、config/hash采用文件均无HEAD差异，整树仍脏。没有直接把参考本地MISO补丁当作官方基线。

来源：
- src/bq_common/platform/platform_misc.h:137-138，分配/释放声明；
- src/bq_common/platform/posix_misc.cpp:676-690，至少指针大小对齐，posix_memalign/free；
- src/bq_common/platform/win64_misc.cpp:727-737，同样提升对齐，_aligned_malloc/_aligned_free；
- src/bq_log/types/buffer/normal_buffer.cpp:23-53，映射失败后使用对齐堆内存，NormalBuffer后续用它。
- src/bq_log/types/buffer/miso_ring_buffer.cpp:67-99/350-356，先创建NormalBuffer，再建立Head/Block并初始化或恢复。

正确依赖顺序为：配置数据已完成→平台对齐分配→文件句柄/文件操作→MemoryMap→NormalBuffer→MISO→LP/HP/TLS/LogBuffer。上一轮“配置后直接进入完整MISO”的路线遗漏了真实存储依赖，本次按用户选择补齐；不把完整MISO改成与SISO相同的外部内存所有权，也不关闭recovery来绕过依赖。

已核对MISO本地diff：header将head的alignas(cache line)改alignas(4)，cpp删除多处统计增量、消费者线程/借用检查及几何assert；其他生产算法块基本仍为HEAD代码。本轮只记录，不移植该补丁，也不在平台分配组决定未来MISO布局/原子访问细节。正式MISO指导前需再核对当时HEAD/diff和参考类型重解释/状态发布/恢复边界。

## 模块归属与合同

| 依赖组 | 当前状态 | 完成后的接续 |
| --- | --- | --- |
| aligned_memory | 已实现、接库，两个函数按 POSIX/Windows 搬移；Linux 实际构建/调用方通过 | 保持分配/释放配对，供后续 NormalBuffer 使用；Windows 原生验证后置 |
| 文件句柄/文件操作 | 8 个公共声明已填写，系统 cpp 仍只有骨架 | 按已核对的指导由用户填写两平台函数体，再接库 |
| MemoryMap | 尚未实现；已纳入存储依赖 | 保留实际平台能力、映射失败与释放规则，再进入NormalBuffer |
| NormalBuffer | 尚未实现 | 对齐映射/堆内存所有权与失败回退，再建立MISO |
| MISO / LP-HP / TLS / LogBuffer | 尚未实现 | 独立核对并发、发布/回收、恢复与满策略；Worker/Appender后置 |

include/qlog/platform/aligned_memory.hpp：公共声明。
src/platform/posix/aligned_memory.cpp：POSIX 定义。
src/platform/windows/aligned_memory.cpp：Windows 定义。
namespace qlog::platform，两个自由函数，无class/Manager/拥有型包装；CMake 选择一份系统实现。

aligned_alloc的参数顺序严格为alignment,size，返回void*；alignment/size用size_t。小于sizeof(void*)时提高到sizeof(void*)，包括输入0/1/4等，这一行为来自参考。正常使用方给出符合平台要求的2的幂对齐，例如Ring用64。size不是对齐值，不要求此层补成alignment的整数倍。请求非整数倍字节也是POSIX参考支持的情况；不要改用标准aligned_alloc的size倍数要求。失败按本平台原生分配行为处理，不引入throw、统一errno返回结构、assert/clamp或分配重试。

POSIX先初始化p=nullptr，调用posix_memalign(&p,alignment,size)；返回码非0返回nullptr，成功返回p。不要用errno替代函数返回码。Windows调用顺序是_aligned_malloc(size,alignment)，与自己的接口参数顺序相反。

aligned_free按同一个平台配对释放；POSIXfree、Windows_aligned_free。不能把对齐Windows分配结果交给普通free，也不能对这块未由new[]创建的存储用delete[]。函数负责原始存储分配/释放，不调用对象构造/析构、不清零内存、不管理Buffer生命周期。创建实际对象时由后续使用方执行构造与析构。

零字节分配和非法对齐不增加自定义跨平台统一结果；沿用各平台API，合法使用前提和实测平台分开记录。没有Windows验证时不声称非法对齐/size0跨平台都返回nullptr。

两个普通函数在头声明、cpp定义，不加与BQLog不同的noexcept签名，不创建空cpp。平台宏使用_WIN32，已有QLOG_DEBUG/NDEBUG规则保持。

## 拆分后的完整内容

以下为 2026-10-08 实际源码，已实现函数只作系统搬移；不再使用 src/platform/aligned_memory.cpp。

### include/qlog/platform/aligned_memory.hpp

```cpp
#pragma once

#include <cstddef>

namespace qlog::platform {
void* aligned_alloc(std::size_t alignment, std::size_t size);
void aligned_free(void* ptr);
}  // namespace qlog::platform
```

### src/platform/posix/aligned_memory.cpp

```cpp
#include "qlog/platform/aligned_memory.hpp"

#include <cstdlib>

namespace qlog::platform {
void* aligned_alloc(std::size_t alignment, std::size_t size) {
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }

    void* p = nullptr;
    if (::posix_memalign(&p, alignment, size) != 0) {
        return nullptr;
    }
    return p;
}

void aligned_free(void* ptr) {
    std::free(ptr);
}
}  // namespace qlog::platform
```

### src/platform/windows/aligned_memory.cpp

```cpp
#include "qlog/platform/aligned_memory.hpp"

#include <cstdlib>

#include <malloc.h>

namespace qlog::platform {
void* aligned_alloc(std::size_t alignment, std::size_t size) {
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }

    return ::_aligned_malloc(size, alignment);
}

void aligned_free(void* ptr) {
    ::_aligned_free(ptr);
}
}  // namespace qlog::platform
```

## 调用顺序与面试技术点

未来NormalBuffer根据是否需要恢复尝试映射；失败才通过本组分配堆存储。使用方持有这块存储，结束所有读写并析构其中的对象后调用aligned_free。这里只建立两个平台原语，不提前给NormalBuffer构造调用未实现的File/MemoryMap helper。

面试问题：
- alignment和size是什么区别？前者约束起始地址，后者是所需字节数；sizeof(T)描述对象大小，alignof(T)描述类型地址要求。
- 为什么malloc不一定够？它通常满足基本类型对齐，不能据此承诺缓存行64或页4096等更大对齐。
- 对齐能保证没有false sharing吗？不能；还需正确安排对象偏移、字段与缓存行间隔，地址对齐只是其中一项。
- 为什么返回void*而不是对象？本函数只取得原始存储；对象生命周期与资源所有权由调用者管理。
- posix_memalign返回码与errno什么关系？它通过返回值报告失败，不能只看errno。
- 为什么释放方法必须成对？各分配器可能需要自己的元数据与释放规则，Windows_aligned_malloc和_aligned_free为一对。
- 为什么不使用std::aligned_alloc统一？其size必须是alignment整数倍，会改变当前POSIX参考支持的请求范围，此外参考Windows也使用专门接口。

本组临时目录：/tmp/qlog-buffer-config-review-20261007-7o65b58i/aligned-memory-candidate。Linux下Debug/Release严格编译链接、各85个定向检查通过，UBSan同组85个通过。覆盖小alignment提升、8/16/64/4096、1/37/65/1024字节的非倍数请求、指针对齐、完整请求范围可写、配对释放、Linux非法alignment=12返回nullptr；另调用nullptr释放及零字节分配/释放但不规定零字节返回值。Windows分支未编译/运行，未计作生产平台实现、NormalBuffer/MISO/恢复/并发/性能验收。

以上候选结果属于 2026-10-07 指导阶段；实际分配已在随后接库，并于 2026-10-08 按系统拆分，见顶部记录。接续为文件操作/MemoryMap、NormalBuffer、MISO，不重写已完成分配函数。
