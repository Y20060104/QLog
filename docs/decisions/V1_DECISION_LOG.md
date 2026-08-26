# V1 决策日志

> 本目录记录新日志项目的设计。它不属于 BQLog 实现；BQLog 仅作为参考实现。

## 项目范围

- 状态：已接受
- 日期：2026-08-15
- 最后修订：2026-08-26
- 目标：Linux C++20 实时游戏服务器和实时仿真服务。
- V1 拓扑：每个生产者线程一个 SPSC 通道，并由一个后台线程作为消费者。
- 生产者约定：在正常日志路径中，不进行格式化、稳态堆分配、加锁、阻塞或共享原子读-改-写操作。
- 背压：固定内存，通道满时采用 `drop_new`。
- 顺序：仅保证每个线程内 FIFO；不保证严格的跨线程全序。
- 文本格式化：推迟到后台线程，并在 V1 中使用 fmt 实现。

## 已接受的决策

1. 记录帧头保留在靠近物理尾端且按对齐要求确定的帧起点。当完整记录无法放入尾端时，载荷（payload）连续存储在物理偏移零处。尾端帧头之后未使用的字节属于该帧，并计入 `frame_bytes`；不发布单独的 padding/`invalid` 帧。参见 ADR-002。
2. 快速记录格式使用静态参数 schema 和 schema 专用格式化函数。每条记录中不存储逐参数 TLV 标签。
3. 每个调用点在记录中由运行时分配的 `uint32_t` `CallsiteId` 表示，而不是原始指针。
4. 调用点注册是冷路径操作。ID 缓存在调用点；生产者不会为每个事件查询 map 或分配 ID。
5. 使用编译期 `ClockPolicy` 边界。正确性测试可以注入简单/伪时钟；优化后的 Linux x86-64 实现将使用 TSC、后台线程校准和安全回退方案。
6. 后台线程唤醒采用混合方式：有工作流入时主动轮询，空闲后进入等待。唤醒协调不得为每次热路径日志调用增加共享 RMW 操作。
7. 默认 SPSC 容量为 64 KiB。V1 最大载荷（payload）为 8 KiB。过大的记录会作为整体丢弃并计数；绝不截断。
8. 普通和回绕载荷（payload）均连续。消费者直接从环形缓冲区存储中解码和格式化，因此 V1 生产路径不需要为环形回绕执行暂存复制。仅当后台线程读取完毕后，才能释放环形缓冲区视图。
9. 消费者空间回收在累计 32 条记录、4 KiB、切换通道或观察到通道为空时发布，以最先发生者为准。
10. 双 span 设计保留为基准测试备选项，而不是生产回退方案。V1 记录 `wrap_count`、`tail_waste_bytes` 和准入失败计数，以便通过测量而不是假设评估布局权衡。
11. 按最终精确载荷（payload）大小进行预留，并且每个生产者通道同一时刻只能有一个未完成的预留。`commit()` 不能缩小记录，因为这样可能改变其物理布局。
12. 在保持 V1 连续回绕保证的前提下，环形缓冲区配置必须满足 `capacity >= 2 * align_up_8(max_payload_bytes)`。
13. `SpscRingBuffer` 拥有按 64 字节对齐的存储。容量在构造时选择并保持不可变；外部存储和 mmap 存储不属于 V1。
14. `SpscRingBuffer` 内嵌唯一的 `SpscWriteHandle` 和 `SpscReadHandle`。两个长期 Handle 同时充当各自的 64B 私有状态块，不再另设 Producer/Consumer endpoint 或独立 PrivateState。每次写入返回 `WriteReservation`，每次读取返回 `ReadView`；单次对象只可移动，热路径操作均为 `noexcept`。
15. 后台线程直接从环形缓冲区视图格式化到其可复用输出缓冲区，然后在可能较慢的 sink I/O 之前回收环形缓冲区空间。
16. 唤醒是调度器的职责，而不是 Ring 的职责。V1 首先实现自适应轮询加短时定时等待，并且不进行生产者侧唤醒访问；保留休眠标志变体供后续基准测试。
17. 所有生产者线程都在 Logger 关闭之前停止。V1 中 Channel 地址保持稳定，其存储由 Logger 保留到关闭时。
18. Debug 模式下遇到损坏会快速失败。Release 模式下隔离受损通道，并在不递归调用 QLog 的情况下报告；绝不使用不可信的 `frame_bytes` 跳过记录。
19. 类型名冻结为 `SpscRingBuffer`，文件名冻结为 `spsc_ring_buffer.hpp/.cpp`。`ring_geometry.hpp` 继续保持与并发策略无关的纯几何职责。
20. 构造配置错误使用冷路径 `std::invalid_argument`，分配失败保留 `std::bad_alloc`。配置项 `max_payload_bytes` 必须大于零，但后续 `try_reserve(0)` 仍可表示合法的空载荷记录。
21. 8B 仅作为 Frame 对齐和最小分配单位。SPSC V1 不使用 `union block`、`BlockStatus`、逐 Block 原子或版本号；BQLog 的 union/status 属于 MISO 多生产者实现，不能移植到 SPSC。
22. 写端使用 `ReserveStatus`，读端使用 `ReadStatus`。结果属于单次 `WriteReservation`/`ReadView`，Ring 和长期 Handle 不保存共享 `last_result`。
23. SPSC 配置不静默向上取整。合法 capacity 范围为 `[16, 2^31]` 且必须是 2 的幂；`max_payload_bytes` 必须非零并满足 `align_up_8(max_payload_bytes) <= capacity / 2`。实现必须先用 `max_payload_bytes <= capacity / 2` 拒绝极值，再进行对齐计算，避免无符号回绕。Header 不额外计入半容量约束。
24. 阶段 B 使用私有 `ValidatedConfig`、`ColdState` 和唯一 `StorageOwner`。Storage 通过 C++ aligned scalar `operator new` 一次性申请 64B 对齐内存，并由自定义 deleter 调用匹配的 aligned scalar `operator delete`；不清零、不扩容、不公开 mutable 地址。校验必须先于分配，构造成功的 Ring 不存在半初始化状态。
25. 阶段 B 已于 2026-08-26 完成：固定 Storage 的配置校验、64B 对齐分配与 RAII 释放通过 Debug、Release 和 ASan/UBSan 门禁。里程碑 2 当前进入阶段 C，只实现长期 Handle 与发布状态的四缓存行布局，不提前加入状态机或内存序行为。
26. [共享状态类型名已由决策 28 修订] 阶段 C 的游标命名冻结为：本侧权威进度使用 `current_write_cursor_` / `current_read_cursor_`，对端缓存使用 `cached_read_cursor_` / `cached_write_cursor_`，共享发布状态使用 `write_cursor_` / `read_cursor_`。原 Ring 类型方案为 ColdState、WriteHandle、PublishedWriteState、ReadHandle、PublishedReadState，四个热状态块各占 64B。
27. 性能优先校验策略取代决策 18 的 Release 隔离方案：Debug 执行完整 Header、长度、对齐和 Geometry 检查；Release 只保留避免越界和未定义行为的最小检查。损坏隔离、详细统计和紧急报告推迟到 M6，不进入 M3 热路径。
28. 阶段 C 的共享发布状态命名修订决策 26：两个缓存行统一使用 `SharedCursor` 类型，内部原子字段为 `value_`；Ring 成员为 `write_cursor_` / `read_cursor_`。成员顺序修订为 ColdState、WriteHandle、`write_cursor_`、ReadHandle、`read_cursor_`，四个热状态块仍各占 64B。

## 重要限制

- 仅使用 `CallsiteId` 并不能保证插件热卸载安全。注册表仍存储可能位于动态模块中的格式化函数。V1 要求模块在卸载前排空其已接受的记录。
- 运行时分配的 ID 在不同进程或构建之间并不稳定。未来的二进制文件格式必须在每条日志流中写入 `ID -> metadata` 表。
- V1 不承诺审计级持久性，也不保证机器断电后日志仍然存在。

## ADR 索引

- [ADR-001：双 span SPSC 帧环形缓冲区（已取代）](./ADR-001-spsc-frame-ring.md)
- [ADR-002：尾端锚定帧头与连续回绕载荷（payload）](./ADR-002-tail-header-contiguous-payload.md)
- [ADR-003：长期读写 Handle 合并私有状态](./ADR-003-long-lived-spsc-handles.md)

## 架构规范

- [QLog V1 SPSC 架构（已接受）](./SPSC_ARCHITECTURE_REVIEW.md)

## 开发指南

- [QLog V1 开发指南：SPSC 基础设施](./V1_DEVELOPMENT_GUIDE_CHS.md)
- [里程碑 2：SpscRingBuffer 存储与类骨架](./M2_SPSC_RING_BUFFER_GUIDE_CHS.md)
