# V1 决策日志

> 本目录记录新日志项目的设计。它不属于 BQLog 实现；BQLog 仅作为参考实现。

## 项目范围

- 状态：已接受
- 日期：2026-08-15
- 最后修订：2026-09-02
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
14. [已由决策 31 取代] `SpscRingBuffer` 内嵌唯一的 `SpscWriteHandle` 和 `SpscReadHandle`。两个长期 Handle 同时充当各自的 64B 私有状态块，不再另设 Producer/Consumer endpoint 或独立 PrivateState。每次写入返回 `WriteReservation`，每次读取返回 `ReadView`；单次对象只可移动，热路径操作均为 `noexcept`。
15. 后台线程直接从环形缓冲区视图格式化到其可复用输出缓冲区，然后在可能较慢的 sink I/O 之前回收环形缓冲区空间。
16. 唤醒是调度器的职责，而不是 Ring 的职责。V1 首先实现自适应轮询加短时定时等待，并且不进行生产者侧唤醒访问；保留休眠标志变体供后续基准测试。
17. 所有生产者线程都在 Logger 关闭之前停止。V1 中 Channel 地址保持稳定，其存储由 Logger 保留到关闭时。
18. [已由决策 27 取代] Debug 模式下遇到损坏会快速失败。Release 模式下隔离受损通道，并在不递归调用 QLog 的情况下报告；绝不使用不可信的 `frame_bytes` 跳过记录。
19. 类型名冻结为 `SpscRingBuffer`，文件名冻结为 `spsc_ring_buffer.hpp/.cpp`。`ring_geometry.hpp` 继续保持与并发策略无关的纯几何职责。
20. 构造配置错误使用冷路径 `std::invalid_argument`，分配失败保留 `std::bad_alloc`。配置项 `max_payload_bytes` 必须大于零，但后续 `try_reserve(0)` 仍可表示合法的空载荷记录。
21. 8B 仅作为 Frame 对齐和最小分配单位。SPSC V1 不使用 `union block`、`BlockStatus`、逐 Block 原子或版本号；BQLog 的 union/status 属于 MISO 多生产者实现，不能移植到 SPSC。
22. [对象术语由决策 31 修订] 写端使用 `ReserveStatus`，读端使用 `ReadStatus`。结果属于单次操作对象，Ring 不保存共享 `last_result`。
23. SPSC 配置不静默向上取整。合法 capacity 范围为 `[16, 2^31]` 且必须是 2 的幂；`max_payload_bytes` 必须非零并满足 `align_up_8(max_payload_bytes) <= capacity / 2`。实现必须先用 `max_payload_bytes <= capacity / 2` 拒绝极值，再进行对齐计算，避免无符号回绕。Header 不额外计入半容量约束。
24. 阶段 B 使用私有 `ValidatedConfig`、`ColdState` 和唯一 `StorageOwner`。Storage 通过 C++ aligned scalar `operator new` 一次性申请 64B 对齐内存，并由自定义 deleter 调用匹配的 aligned scalar `operator delete`；不清零、不扩容、不公开 mutable 地址。校验必须先于分配，构造成功的 Ring 不存在半初始化状态。
25. [对象术语由决策 31 修订] 阶段 B 已于 2026-08-26 完成：固定 Storage 的配置校验、64B 对齐分配与 RAII 释放通过 Debug、Release 和 ASan/UBSan 门禁。随后建立私有读写 State 与发布状态的四缓存行布局，不提前加入状态机或内存序行为。
26. [共享状态类型名已由决策 28、29 连续修订] 阶段 C 的游标命名冻结为：本侧权威进度使用 `current_write_cursor_` / `current_read_cursor_`，对端缓存使用 `cached_read_cursor_` / `cached_write_cursor_`，共享发布状态使用 `write_cursor_` / `read_cursor_`。原 Ring 类型方案为 ColdState、WriteHandle、PublishedWriteState、ReadHandle、PublishedReadState，四个热状态块各占 64B。
27. 性能优先校验策略取代决策 18 的 Release 隔离方案：Debug 执行完整 Header、长度、对齐和 Geometry 检查；Release 只保留避免越界和未定义行为的最小检查。损坏隔离、详细统计和紧急报告推迟到 V1 之后，不进入 Producer 或 Consumer 热路径。
28. [共享游标容器已由决策 29 修订] 阶段 C 曾将两个发布缓存行统一为 `SharedCursor` 类型，内部原子字段为 `value_`，Ring 分别内嵌 `write_cursor_` / `read_cursor_`。
29. [成员类型名由决策 31 修订] 阶段 C 的共享游标命名与布局最终修订为：私有 `CursorSet cursors_` 统一收纳 `write_cursor_` / `read_cursor_`，两个原子成员分别使用 `alignas(kCacheLineSize)` 并各占一条缓存行，不手写 padding。四条热缓存行及 SPSC 所有权、缓存策略和后续 acquire/release 语义保持不变。
30. 从 2026-08-28 起，后续执行计划只保留两个里程碑：一是完成可用且高性能的 SPSC RingBuffer，二是完成可演示、可公平基准的异步日志 V1。旧指南中的阶段 A～D、里程碑 0～7 和 M3～M7 保留为历史任务与设计证据，不再作为独立的后续里程碑。该合并不改变决策 1～29 已冻结的语义。
31. [Handle 所有权和终结语义由决策 35 修订] ADR-004 取代 ADR-003 的对象模型。`SpscRingBuffer` 不再公开长期
    `SpscWriteHandle` / `SpscReadHandle`；长期热状态改为 Ring 私有且各占 64B 的
    `WriterState` / `ReaderState`。Ring 直接提供 `try_reserve()` / `try_peek()`；
    每次操作按值返回短期、只可移动的 `WriteHandle` / `ReadHandle`。短期 Handle
    保存 owner、连续 payload 地址、预计算的下一逻辑游标、payload 长度和本次状态；
    `commit/abort` 属于 WriteHandle，`consume/abandon` 属于 ReadHandle。TLS 后续缓存
    `ThreadLogger` 或稳定 Channel，不缓存短期 Handle。该修订删除长期 Handle 到 Ring
    的父指针依赖，但不改变四缓存行布局、尾端 Header/头部 payload、drop_new 或
    acquire/release 协议。
32. [Handle 字段布局由决策 35 修订] ADR-004 的 Storage/Handle 协议冻结为：`FrameHeader` 写入 Ring 并随记录保留，
    `FrameLayout` 只作为 `try_reserve()` / `try_peek()` 的局部计算结果；短期
    `WriteHandle` 不保存 Header 或完整 Layout，只保存 Ring 指针、连续 payload
    地址、预计算的下一逻辑写游标、有效 payload 长度和本次状态，目标大小不超过
    32B。`try_reserve()` 使用 `cold_state_.storage_.get() + layout.header_offset` 与
    `+ layout.payload_offset` 绑定真实地址，空间确认后写入尚未发布的 Header，再
    返回 Handle。失败 Handle 的 Ring/payload 指针为空，且不得写 Header、设置
    pending 或推进游标；commit 才以 release-store 发布，abort 不清零未发布字节。
33. [owner、终结位置和 bool 语义由决策 35 修订] Handle 的 payload 访问接口修订为底层裸指针风格：内部继续保存 private 的
    `payload_` / `payload_bytes_`，公有热接口为内联 `data()` / `size()`，不在 Ring 基础 API
    中返回 `std::span`，也不公开可修改的状态字段。`WriteHandle::data()` 返回
    `std::byte*`，`ReadHandle::data()` 返回 `const std::byte*`；`operator bool()` 以 Ring
    owner 指针非空判定 active，`status()` 返回本次结果，`deactivate()` 保持 private。
    `try_reserve()` 只执行空间准入、Geometry、写入 Ring 私有 `FrameHeader` 并返回 payload
    目标地址；调用者在 commit 前将日志 `RecordHeader` 与参数直接编码到该地址，或把已有连续
    数据一次 `memcpy` 进入 Ring。commit 不复制 payload，只推进写游标并以 release-store 发布。
    该选择学习 BQLog 的 alloc/fill/commit 职责分层，但不复制其 public 可写 Handle 字段，也不
    宣称 `std::span` 本身必然更慢。
34. [已由决策 35 取代] Handle 查询状态冻结为两组正交语义：`operator bool()` 表示当前是否仍持有 active 借用，
    `status()` 始终保留本次 `try_reserve()` / `try_peek()` 的结果。成功且尚未终结时为
    `bool == true`、`status == ok`、`data != nullptr`、`size == exact_payload_bytes`；失败 Handle
    为 `false`、具体失败 status、`nullptr`、`0`；moved-from 以及 commit/abort/consume/abandon
    后的 Handle 为 `false`、保留原 status、`nullptr`、`0`。零长度 payload 是合法成功记录，
    因而以 bool 而不是 size 区分成功与失败。移动操作通过 `deactivate()` 规范化源对象，不增加
    active 标志。存在未终结写预留时，新的 `try_reserve()` 必须优先返回
    `reservation_pending`，不再根据新请求的 payload 大小返回其他状态。
35. ADR-005 将两侧 Handle 统一为 16B、非拥有、平凡可复制且平凡可析构的被动令牌。
    Handle 不保存 Ring owner 或 64 位 next cursor，只保存 payload 指针、低 3 位编码 status
    的 `frame_and_status_` 和 `payload_bytes_`。Ring 负责全部状态转换：写侧为
    `try_reserve/commit`，冷路径 `abort`；读侧最终命名为 `try_read/release`，冷路径
    `abandon`。成功 Handle 终结后不会自动清空，物理副本仍保持原始结果，但所有副本
    共享一次性的逻辑终结权；重复终结、跨 Ring 终结和使用旧副本违反底层契约。
    Debug 写终结路径校验 pending、Geometry 和地址归属，读取得路径校验 Header、Geometry
    与可用范围；Release 删除这些完整检查。该修订不改变
    Ring 私有 State、单次 pending、Header/payload 布局、批量回收阈值或内存序协议。
36. ADR-006 冻结 benchmark 分层：Ring 微基准只横向比较相同职责的 QLog SPSC 与
    BQLog SISO。spdlog 当前异步队列是带 `mutex/condition_variable` 的
    `mpmc_blocking_queue<async_msg>`，不存在可直接加入该排行榜的并发 SPSC Ring；其
    `circular_q` 不能被两个线程无同步并发访问。spdlog 在里程碑二以真实 async logger
    身份加入单 Producer/单 Backend 与多 Producer/单 Backend 系统级对照，容量单位、
    overflow policy、复制边界、格式化、accepted/dropped/processed 和内存占用必须分别披露。
37. 2026-09-02，读侧 R1（仅在 Debug 保留 `read_pending_` 校验）实验未通过并决定回退。
    Debug、Release、ASan/UBSan 与 Release 汇编门禁均通过，64KiB/64B transfer 未发现
    可测回归；但同场成对交替纯读的 before/after 中位数仅从 50.13M/s 变为 50.51M/s，
    改善 0.77%，低于预设 3% 且落在 MAD 噪声内。机器码更短不足以交换 Release 的
    API 误用诊断能力。恢复四个 pending 读侧操作并重跑基线之前，不进入 R2。
38. 2026-09-02，R1 回退与里程碑一的本地开发门禁完成。读写两侧的单次 pending
    契约均在 Debug/Release 生效；完整 Header/Geometry 损坏校验仍为 Debug-only。
    Debug 47/47、Release 46 项通过且仅 1 项预期跳过、ASan/UBSan 47/47 通过；
    Release 汇编与冻结契约一致，quick benchmark 的 10 个样本全部有效。R2 不再执行。
    里程碑一标记为“开发完成”；原生 Linux TSan 与 clean commit 正式性能矩阵仍是
    发布门禁，可与里程碑二的协议设计并行补齐。
39. 为跨窗口继续开发，新增里程碑一完成报告和里程碑二设计讨论指南。前者汇总最终 API、
    Ring 不变量、门禁证据和原生 Linux 待办；后者冻结 V1 场景、Producer 热路径预算、
    Record ABI 的讨论顺序和最终验收标准。里程碑二中的 Callsite、RecordHeader、时间戳、
    参数类型、字符串所有权和编码方式仍是待商讨项，不因写入指南而视为已冻结决策。

## 重要限制

- 仅使用 `CallsiteId` 并不能保证插件热卸载安全。注册表仍存储可能位于动态模块中的格式化函数。V1 要求模块在卸载前排空其已接受的记录。
- 运行时分配的 ID 在不同进程或构建之间并不稳定。未来的二进制文件格式必须在每条日志流中写入 `ID -> metadata` 表。
- V1 不承诺审计级持久性，也不保证机器断电后日志仍然存在。

## ADR 索引

- [ADR-001：双 span SPSC 帧环形缓冲区（已取代）](./ADR-001-spsc-frame-ring.md)
- [ADR-002：尾端锚定帧头与连续回绕载荷（payload）](./ADR-002-tail-header-contiguous-payload.md)
- [ADR-003：长期读写 Handle 合并私有状态（已取代）](./ADR-003-long-lived-spsc-handles.md)
- [ADR-004：短期 Handle 与 Ring 私有读写状态（Handle 部分已取代）](./ADR-004-short-lived-spsc-handles.md)
- [ADR-005：被动且平凡可复制的 SPSC Handle](./ADR-005-passive-spsc-handles.md)
- [ADR-006：spdlog 对照的 benchmark 分层](./ADR-006-spdlog-benchmark-layering.md)

## 架构规范

- [QLog V1 SPSC 架构评审（Handle 部分已取代）](./SPSC_ARCHITECTURE_REVIEW.md)

## 开发指南

- [QLog V1 开发指南：SPSC 基础设施](./V1_DEVELOPMENT_GUIDE_CHS.md)
- [里程碑 2：SpscRingBuffer 存储与类骨架](./M2_SPSC_RING_BUFFER_GUIDE_CHS.md)
- [QLog V1 后续两里程碑实现指南（唯一生效计划）](./V1_TWO_MILESTONES_GUIDE_CHS.md)
- [里程碑一完成报告：SPSC RingBuffer](./MILESTONE1_COMPLETION_REPORT_CHS.md)
- [里程碑二设计讨论指南：异步日志 V1](./MILESTONE2_DESIGN_GUIDE_CHS.md)
- [读侧 R1 实验记录：pending 校验仅保留在 Debug（未通过，已回退）](./NEXT_IMPLEMENTATION_GUIDE_CHS.md)
