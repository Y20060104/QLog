# QLog V1 开发指南：SPSC 基础设施

> 2026-09-17 format覆盖：[ADR-016](./ADR-016-v1-bqlog-worker-format.md)优先于本文旧的严格花括号、参数数目匹配、默认文本表示和解析缓存合同。Producer原样copy/hash；worker按BQLog当前UTF-8顺序扫描。当前起点与剩余实施见[剩余V1指南](./V1_REMAINING_IMPLEMENTATION_GUIDE_CHS.md)。本文未被覆盖的wire/参数/长度规则继续有效，历史验收记录不改写为当前实现状态。

> 历史指南：其中长期 Handle 相关内容已由 ADR-004 取代，ADR-004 中的 RAII
> 短期 Handle 又由 ADR-005 取代。当前实现只以
> `V1_TWO_MILESTONES_GUIDE_CHS.md`、ADR-005、ADR-007～ADR-010 与里程碑二实现设计指南为准。
> 本文早期的“格式化后 release、再把输出交给 Sink”后台顺序也已由 ADR-010 取代；当前合同是
> 私有 64KiB scratch 形成完整行、交给 Sink 内存 batch、release Frame，最后才执行可能阻塞的
> `write`/`fdatasync`。

- 状态：历史详细参考
- 日期：2026-08-16
- 最后修订：2026-09-05（同步 ADR-010 Backend/Sink 所有权边界）
- 适用范围：QLog V1 的字节型 SPSC Ring、Channel 与最小后台消费链路
- 目标读者：项目实现者与代码评审者

> 后续唯一生效的执行计划是
> [QLog V1 后续两里程碑实现指南](./V1_TWO_MILESTONES_GUIDE_CHS.md)。
> 本文的旧阶段和旧里程碑仅用于保留设计原理、测试细节与历史记录。

## 1. 如何使用这份指南

这份指南提供设计契约、文件边界、测试目标、性能门禁和评审问题，
不提供可直接粘贴的完整实现。开发顺序固定为：

```text
共同冻结可观察契约
    -> Codex 讲清设计原因、风险和必须保持的不变量
    -> 你编写最小生产实现
    -> Codex 编写并运行验收测试
    -> Codex 用失败证据评审，生产代码由你修正
    -> 运行 Sanitizer 与 Release 基准
    -> 一次只优化一个变量
```

前一个里程碑没有达到退出条件，不进入下一个里程碑。Ring 正确之前，
不得开始 Backend formatter、文件 Sink、TSC、mmap 或 MPSC。

### 1.1 固定协作分工

你的职责：

- 专注 `include/qlog/`、`src/` 以及生产目标所需的实现。
- 根据已冻结契约编写生产代码，不负责手写测试、注册测试或整理测试输出。
- 实现完成后只需告知 Codex 可以评审。

Codex 的职责：

- 每个实现阶段开始前，先说明要实现什么、为什么这样设计以及不可破坏的不变量。
- 负责 `tests/`、测试相关 CMake/脚本、测试夹具、模型与压力测试。
- 负责运行 Debug、Release、ASan/UBSan，并保存可复核的失败或通过证据。
- 发现生产代码问题时给出精确位置、复现和原因；未经你明确要求，不代替你修改生产实现。
- 门禁全部通过后更新计划，并给出下一项生产代码任务。

因此，本指南后续所有“必须测试”“测试”“测试输出”均表示 Codex 的验收职责，
不是要求你编写测试。测试需求仍是生产实现必须满足的契约。

## 2. V1 目标与非目标

### 2.1 使用场景

- Linux x86-64、C++20。
- 长生命周期线程较多的游戏服务器和实时仿真服务。
- 业务线程优先保证低尾延迟，日志系统过载时不得阻塞主业务。
- 每个 `(生产线程, AsyncLogger)` 使用独立 SPSC Channel，一个后台线程消费多个 Channel。
- V1 输出人类可读文本，自研 `c20_format` 只在后台线程执行。

### 2.2 Producer 热路径契约

- 不格式化。
- steady state 不进行堆分配。
- 不获取锁、不等待、不重试。
- 正常路径没有共享原子 RMW：无 CAS、无 `fetch_add`。
- Channel 满时 `drop_new`，返回明确状态并计数。
- 每个 Channel 内 FIFO；不承诺跨线程全局顺序。

### 2.3 V1 暂不解决

- 机器断电后的审计级持久化。
- mmap 队列恢复、跨进程共享内存。
- 任意插件热卸载。
- 多后台线程同时消费一个 Channel。
- MPSC 竞争场景。
- 离线二进制解码器、VLQ、字符串驻留。
- 严格跨线程时间排序。

这些不是“永远不做”，而是不允许它们污染第一版能够证明正确的核心。

### 2.4 V1 总体开发路线

```text
里程碑一：完成可用且高性能的 SPSC RingBuffer
    -> 里程碑二：完成可演示、可公平基准的异步日志 V1
```

里程碑内仍按依赖顺序实现和测试，但不再把状态机、内存序、编码器、Backend
或 Sink 单独命名为新里程碑。SPSC 通过之前不并行开发 codec。

## 3. 参考项目的学习边界

QLog 采用“问题 -> 原理 -> 自己的规格 -> 自己的测试 -> 自己实现”的
研究流程。参考项目不能成为改名后的代码模板。

| 项目 | 可以学习 | QLog V1 不照搬 |
|---|---|---|
| BQLog | SISO 的 8B 分配单位、小型单次 Handle、peer cursor 缓存与两阶段提交；Producer `size_seq`/type copy 和 Backend `layout::c20_format` 的职责分离 | SISO 的 reinterpret/packed/mmap 技巧；4B type cell、align4 参数 ABI、未对齐 typed-pointer 访问；MISO 的 union block、逐 Block status 和多生产者协调 |
| spdlog | Logger/Sink/Formatter 分层；异步消息类型；明确区分 block、overwrite-oldest、discard-new；后台异常边界与 shutdown | 它的异步核心是带 mutex/CV 的共享 MPMC 队列，不符合 QLog 每线程 SPSC 热路径；不复制其 async message、thread pool 或 shared ownership 路径 |
| Quill | 每前端线程一个 SPSC；前后端分离；静态 metadata、参数类型特化 decode；后台统一 fmt；对队列模式、时间戳和 shutdown 的完整工程化处理 | 不复制其宏、metadata 布局、codec、队列代码、全局排序算法或完整功能面；V1 不为排序引入 grace period |
| NanoLog | 静态内容与动态参数分离；把工作移出运行时热路径；延迟格式化；文本和二进制 benchmark 必须分榜 | 不引入源码预处理器和构建期代码生成；V1 不要求离线解码；不把二进制吞吐冒充在线文本吞吐 |
| `{fmt}` | 可作为非生产的语法/性能对照 | V1 生产代码不依赖；不把其 parser、dynamic argument store 或内部 API 引入 Producer/Backend |

学习源码时必须留下自己的研究笔记，至少回答：

1. 它解决的具体问题是什么？
2. 它成立依赖哪些使用场景和不变量？
3. QLog 的约束是否相同？
4. 有哪些替代方案？
5. 如何用测试和 benchmark 决定是否采用？

完成笔记后，从 QLog ADR 和测试重新实现，不对着参考函数逐行翻译。任何
确实需要移植的第三方代码都必须单独评估许可证并注明来源；V1 核心默认
不做这种移植。

## 4. 已冻结的总体架构

```text
(线程 A, Logger X) -> ProducerHandle AX -> Channel AX --\
(线程 A, Logger Y) -> ProducerHandle AY -> Channel AY ----> Backend
(线程 B, Logger X) -> ProducerHandle BX -> Channel BX --/      |
                                                          v
                                      c20_format 到 Backend 私有 64KiB scratch
                                                          |
                                           交给 Sink 的内存 batch
                                                          |
                                                    consume/reclaim Ring
                                                          |
                                                可能阻塞的文件 I/O
```

职责边界：

- `SpscRingBuffer`：存储、游标、reserve/commit、peek/consume。
- `Channel`：Ring 加线程/Logger 冷元数据、Frame/Record 版本、状态和统计。
- `SpscWriteHandle`：长期写入口，同时拥有写端私有状态。
- `SpscReadHandle`：长期读入口，同时拥有读端私有状态。
- `Backend`：公平扫描 Channel、解码、`c20_format`、批量写 Sink。
- `Logger/Registry`：冷路径注册、生命周期和 shutdown。

Ring 不知道 format grammar、文件、TLS、条件变量、eventfd、mmap 或 Logger。

## 5. 推荐文件边界

```text
include/qlog/detail/
├── ring_geometry.hpp       # 纯 constexpr 几何，无 atomics、无 storage
├── spsc_ring_buffer.hpp    # 类型、API、状态布局、契约
└── spsc_ring_buffer.inl    # M3 创建；小型 hot-path 定义

src/
└── spsc_ring_buffer.cpp    # 构造析构、对齐分配、配置校验等 cold path

tests/
├── geometry_test.cpp
├── spsc_ring_buffer_storage_test.cpp
├── spsc_ring_buffer_model_test.cpp
├── spsc_ring_buffer_stress_test.cpp
└── spsc_ring_buffer_fault_test.cpp

bench/
└── spsc_ring_buffer_bench.cpp
```

规则：

- `ring_geometry.hpp` 只回答“给定逻辑位置和精确长度，如何布局”。
- hot path 可以放 `.inl` 以便跨调用内联，但先保证可读性和测试覆盖。
- `.cpp` 放不会按日志频率执行的 cold path。
- 暂不创建类似 `bq_common.h` 的巨型公共头。真正有三个以上稳定使用点时，
  才提取小而单一职责的公共组件。
- 不把所有内容继续堆进 `ring_geometry.hpp`。

## 6. 帧协议与几何契约

### 6.1 常量与 Header

```cpp
inline constexpr std::size_t kFrameAlignment = 8;

struct FrameHeader {
    std::uint32_t frame_bytes;
    std::uint32_t payload_bytes;
};
```

必须用编译期检查固定：

```text
sizeof(FrameHeader) == 8
FrameHeader trivially copyable
atomic<uint64_t> always lock-free on the V1 target
```

Header 通过 `memcpy` 写入和读出；不要在 byte buffer 上直接
`reinterpret_cast<FrameHeader*>`。

### 6.2 构造约束

```text
capacity 是 2 的幂
capacity >= 16
capacity <= 2^31
capacity % 8 == 0
capacity >= 2 * align_up_8(max_payload_bytes)
storage 地址按 64B 对齐
```

默认配置：

```text
capacity          = 64 KiB
max_payload_bytes = 8 KiB
```

### 6.3 布局公式

令：

```text
H        = 8
A        = align_up_8(payload_bytes)
physical = logical_write & (capacity - 1)
tail     = capacity - physical
normal   = H + A
```

普通布局：

```text
条件           normal <= tail
header_offset  physical
payload_offset physical + H
frame_bytes    normal
tail_waste     0
```

回绕布局：

```text
条件           normal > tail
header_offset  physical
payload_offset 0
frame_bytes    tail + A
tail_waste     tail - H
```

因为 capacity、frame start 和 frame bytes 都按 8B 对齐，所以尾端至少剩
8B，Header 永远连续。消费者用同一公式推导 payload 位置，不额外存
`wrapped` 标志。

不要为了担心 `std::min` 就写晦涩表达式。性能依据是 Release 汇编、cycles
和基准，而不是源码里函数名的数量。本布局本身只需要一个清晰的普通/回绕
分支。

## 7. 状态所有权和内存序

建议用四个彼此独立的 64B 热状态块，并对其类型大小做静态检查：

| Cache line | 字段 | 唯一写线程 |
|---|---|---|
| Long-lived write handle | `ring`, `current_write_cursor_`, `cached_read_cursor_`, reservation state | Writer |
| `CursorSet::write_cursor_` | `atomic<uint64_t>` | Writer |
| `CursorSet::read_cursor_` | `atomic<uint64_t>` | Reader |
| Long-lived read handle | `ring`, `current_read_cursor_`, `cached_write_cursor_`, reclaim counters | Reader |

冷元数据和统计不得与 published cursor 共用 cache line。Producer 统计由
Producer 写，Consumer 统计由 Consumer 写；运行中跨线程读取统计需要另行
设计快照，不能随手把所有计数都变成共享 atomic。

核心不变量：

```text
unsigned(current_write_cursor_ - cached_read_cursor_) <= capacity
unsigned(cached_write_cursor_ - current_read_cursor_) <= capacity
current_write_cursor_ % 8 == 0
current_read_cursor_  % 8 == 0
```

`cached_read_cursor_` / `cached_write_cursor_` 是从共享原子取得的本地快照。
只用无符号减法计算逻辑距离，禁止用 `write < read` 判断先后。

发布链：

```text
Producer 写 Payload
    -> memcpy Header
    -> cursors_.write_cursor_.store(new_write, release)
    -> Consumer cursors_.write_cursor_.load(acquire)
    -> Consumer 才能读 Header/Payload
```

回收链：

```text
Consumer 完成最后一次 Ring 数据读取
    -> cursors_.read_cursor_.store(new_read, release)
    -> Producer cursors_.read_cursor_.load(acquire)
    -> Producer 才能覆盖已回收字节
```

peer cursor 缓存只能更旧：它可以导致保守的少写/少读，但绝不能扩大可写
或可读范围。

## 8. API 契约草案

以下是单次操作对象的接口形状，不是函数体答案。长期角色入口已冻结为
`SpscWriteHandle` 和 `SpscReadHandle`；写入返回 `WriteReservation`，读取
返回 `ReadView`。

```cpp
enum class ReserveStatus : std::uint8_t {
    ok,
    full,
    payload_too_large,
    reservation_pending,
};

class WriteReservation {
public:
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] ReserveStatus status() const noexcept;
    [[nodiscard]] std::byte* data() noexcept;
    [[nodiscard]] std::uint32_t size() const noexcept;
    void commit() noexcept;
    void abort() noexcept;
};

enum class ReadStatus : std::uint8_t {
    ok,
    empty,
    corrupted,
};

class ReadView {
public:
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] ReadStatus status() const noexcept;
    [[nodiscard]] const std::byte* data() const noexcept;
    [[nodiscard]] std::uint32_t size() const noexcept;
    void consume() noexcept;
    void abandon() noexcept;
};
```

强制语义：

- `try_reserve(exact_payload_bytes)` 使用最终精确长度。
- `commit()` 不接受另一个 actual size。
- 同一 `SpscWriteHandle` 最多一个未完成 Reservation。
- reserve 只计算 tentative layout；commit 前不推进 committed write cursor。
- 未 commit 的 Reservation 析构等价于 abort，不发布、不移动游标。
- 同一 `SpscReadHandle` 最多一个未结束 ReadView。
- ReadView 默认 abandon，不隐式丢掉日志。
- `consume()` 后先前取得的 `data()` 与 `size()` 范围立即失效。
- Ring 与长期 Handle 均不能复制或移动；单次对象不能复制；Ring 地址从构造到析构保持稳定。

## 9. 里程碑 0：建立代码契约

### 任务

- 建立第 5 节的文件骨架和最小声明。
- 在状态字段旁注明唯一 writer。
- 在头文件注释写出四条不变量。
- Ring 禁止复制和移动；hot-path API 标为 `noexcept`、查询返回值标为
  `[[nodiscard]]`。

### 退出条件

- 所有声明能够编译。
- 还没有 reserve/commit 函数体、并发、formatter 或后台线程。
- 你能逐字段回答“谁写、谁读、为什么是否需要 atomic”。

## 10. 里程碑 1：纯 Geometry

### 任务

- 实现 `is_power_of_two`、`align_up_8`、`compute_frame_layout`。
- `FrameLayout` 至少表达 header offset、payload offset、frame bytes、
  tail waste 和 wrapped。
- 函数只计算数值，不访问真实 buffer。

### 先写的测试

- 对齐：0、1、7、8、9、15、16。
- 普通记录、恰好贴合尾端、只多 1B 触发回绕。
- Header 位于最后 8B，payload 从物理 offset 0 开始。
- payload 为 0、1、7、8、最大值。
- 对 16/32/64/128/256/512B 小 Ring，穷举所有对齐起点和所有合法
  payload 长度。
- 测试 Oracle 使用独立、可读的除法公式计算对齐，不能调用生产实现的
  `align_up_8`，防止测试与实现犯同一个错误。
- 注入接近 `UINT64_MAX` 的逻辑游标。

### 退出条件

- Header 始终连续，payload 始终为一个连续区间。
- `frame_bytes` 非零、按 8B 对齐、不超过 capacity。
- 下一帧位置仍按 8B 对齐。
- wrap waste 被包含在 `frame_bytes`。
- 此阶段禁止 branch hint、branchless 技巧、prefetch、SIMD 和汇编。

### 面试自检

- 为什么 Header 不可能跨尾？
- 为什么回绕 Frame 必须把剩余尾部计入 `frame_bytes`？
- 为什么 `capacity >= 2 * align_up_8(max_payload)` 能保证任意位置可布局？

## 11. 里程碑 2：存储和类骨架

- 状态：当前里程碑
- 专项指南：[里程碑 2：SpscRingBuffer 存储与类骨架](./M2_SPSC_RING_BUFFER_GUIDE_CHS.md)

### 任务

- 构造时一次性申请 64B 对齐内存，析构时通过
  `std::unique_ptr<std::byte, AlignedStorageDeleter>` 调用匹配的 aligned delete。
- Ring 使用私有 `ValidatedConfig` 保存 capacity、mask 和 max payload，并使用
  私有 `ColdState` 保存已验证配置与唯一 Storage owner。
- 无分配的私有 `validate_config()` 必须在 Storage 申请前完成全部校验。
- capacity、mask、max payload 构造后不可变。
- 非法配置抛出 `std::invalid_argument`；分配失败保持 `std::bad_alloc`。
- 将 `src/spsc_ring_buffer.cpp` 接入生产 `qlog` target。
- 通过最小私有测试 friend 提供只读配置、Storage 地址和纯校验访问；不公开
  mutable Storage。
- Ring 后续内嵌唯一 `SpscWriteHandle` 和 `SpscReadHandle`，但它们属于阶段 C。
- 保持 `WriteReservation` 和 `ReadView` 的 move-only 语义，不实现 M3 状态转换。
- 8B 只作为对齐/分配单位，不创建 union block 或逐 Block 状态。
- Logger 未来用 `unique_ptr` 持有稳定 Channel；不要把 Ring 对象直接放进可能
  扩容并搬迁元素的容器。
- 本阶段不实现 reserve/commit/peek/consume、游标推进或 acquire/release。

### 测试

- 0、小于 16、非 2 次幂、超过 `2^31` 的 capacity 被拒绝。
- `max_payload_bytes == 0` 被拒绝。
- `max_payload_bytes <= capacity / 2` 的合法边界被接受，只超过 1B 时被拒绝。
- `SIZE_MAX` 形式的 max payload 必须被拒绝，校验不能发生对齐回绕。
- `capacity == 2^31` 通过私有纯校验入口验收，不实际申请 2 GiB。
- 64 KiB/8 KiB 默认配置成功。
- Storage 地址满足 64B 对齐。
- 构造失败和析构在 ASan/UBSan 下无泄漏、无 delete mismatch。
- 编译期验证 Ring 不可复制、不可移动。
- 生产公共 API 不暴露 Storage 地址或修改入口。

### 退出条件

- 分配只发生在 cold path。
- 长期 Handle 和单次对象都不拥有 Ring 生命周期。
- 没有 TLS、注册表、后台线程、formatter 或 mmap。
- 源码中没有任何 M3 状态机行为。

## 12. 里程碑 3：单线程状态机

### 实现顺序

```text
try_reserve
    -> 调用者写 payload
    -> commit 或 abort
    -> try_peek
    -> consume 或 abandon
```

### 测试

- 空 Ring 返回 empty。
- 普通记录、回绕记录和多记录 FIFO round-trip。
- abort 后消费者不可见，下一次 reserve 复用同一逻辑位置。
- 存在 Reservation 时第二次 reserve 返回 `reservation_pending`。
- full、oversize、pending 都不改变 committed cursor。
- abandon 后再次 peek 得到同一条记录。
- Ring 用满时物理读写位置相同，但逻辑距离为 capacity。
- `required == free` 成功，`required > free` 失败。
- 使用 `deque<vector<byte>>` 作为 Oracle，固定随机种子执行至少十万次
  reserve/commit/abort/peek/consume。

### 退出条件

- Header 只通过 `memcpy` 访问。
- commit 前的字节永远不可见。
- drop 原因互斥且完整，`attempted == accepted + dropped`。
- full 路径没有等待、循环重试、分配或 fallback I/O。

### 面试自检

- 为什么 commit 必须使用 reserve 时的精确大小？
- 为什么 abort 不需要清零已写字节？
- 为什么 published cursor 而不是 Header 是提交标志？

## 13. 里程碑 4：双线程与内存序

### 任务

- 一个 Producer 线程、一个 Consumer 线程。
- 先实现每次都加载 peer cursor 的清晰版本。
- Producer commit 使用 release store；Consumer 读取前使用 acquire load。
- Consumer 完成读取后 release store；Producer 复用前 acquire load。

### 压力 Payload

每条测试记录包含：

```text
sequence + payload_size + 可重现字节模式 + checksum
```

测试参考结果在线程 join 后比较；不要在被测 hot path 外围用 mutex 掩盖
错误。

### 负载

- 生产消费平衡。
- Producer 持续过载，频繁 full/drop。
- Consumer 更快、Producer 间歇产生数据。
- 极小 Ring，强制高频回绕和复用。
- 游标从 `UINT64_MAX` 附近开始并跨整数回绕。

### 退出条件

- 数百万条 accepted 与 consumed 内容和顺序完全一致。
- 看不到部分写、abort 记录、重复记录或覆盖中的记录。
- TSan 无 data race，ASan/UBSan 无错误。
- 能逐条解释 publish 和 reclaim 两条 happens-before 链。
- 没有 CAS、`fetch_add`、slot-ready 或版本号。

不要把“x86 上一直正常”当成放松内存序的理由；后续跨平台阶段需要原生
ARM64 CI。

## 14. 里程碑 5：游标缓存与批量回收

### 任务

- Producer 先用 `cached_read_cursor_`，只有缓存判断空间不足时才 acquire-load
  `cursors_.read_cursor_`；刷新后仍不足才 drop。
- Consumer 用 `cached_write_cursor_`，本地 snapshot 耗尽后才 acquire-load
  `cursors_.write_cursor_`。
- Consumer 在 32 条、4 KiB、切换 Channel 或观察 empty 时发布 reclaim。
- 四条热缓存行保持相互独立。

### 测试

- 陈旧缓存只能保守，不能越界。
- Producer 在真正返回 full 前刷新一次 read cursor。
- 31/32 条、4095/4096B 边界。
- empty、channel switch、shutdown 强制发布剩余 reclaim。
- `publish_reclaimed()` 可重复调用。
- 之前所有模型和并发测试原样继续通过。

### 性能门禁

- 在加入缓存前保存 Release 基线。
- 加入后分别比较成功热路径的 atomic load、cycles/record、P99 和吞吐。
- 没有实测收益的复杂化不保留。

## 15. 里程碑 6：校验、故障与统计

### Consumer 校验顺序

在形成任何可能越界的指针-长度范围之前，重新计算布局并检查：

- payload 不超过配置上限。
- frame bytes 至少为 8、按 8B 对齐、不超过 capacity。
- frame bytes 等于从 offset/payload 重新计算的结果。
- 完整 frame 位于 acquire 得到的 snapshot 内。

### 故障注入

- `frame_bytes` 为 0、小于 8、未对齐、超过 capacity/available。
- payload 超限。
- ordinary/wrapped 的 frame bytes 与公式不一致。
- Header 合法位置但推导 payload 将越界。
- 只写部分 payload 后 abort。
- Header 已写但 published write 尚未发布。

### 冻结策略

- Debug：断言并 fail-fast。
- Release：当前 Channel 进入 quarantine，只报告一次 corruption。
- emergency reporter 不得递归调用 QLog。
- 不相信损坏的 frame bytes 去寻找下一条记录。

### 统计

至少区分：

```text
accepted
full_drop
oversize_drop
reservation_pending
corruption
wrap_count
tail_waste_bytes
```

统计不能污染 published cursor cache line，也不能为了便于读取给成功热路径
加入共享 RMW。

## 16. 里程碑 7：Channel 与最小 Backend

Ring 单体门禁全部通过后才实现：

```text
Channel
    -> TLS Producer 冷路径注册
    -> Backend round-robin 扫描
    -> 直接格式化到 BackendWorker 私有 64KiB scratch
    -> 仅把完整行交给 Sink 内存 batch
    -> consume/reclaim Ring
    -> 可能阻塞的批量 Sink I/O
```

Backend 每次访问一个 Channel 使用记录数/字节预算，避免高流量线程饿死
低流量线程。V1 不做跨线程时间排序。

测试：

- 多个 Producer 各用独立 SPSC。
- 高流量 Channel 不饿死低流量 Channel。
- 每个 Channel 内 FIFO。
- 格式化异常被捕获，并明确 consume 或 quarantine。
- `c20_format` 在私有 scratch 中完成；只有完整成功的行才交给 Sink 内存 batch，batch 接收完成后
  release Ring，可能阻塞的 `write`/`fdatasync` 位于 release 之后。
- formatter 和 Sink 不保留 Ring 内的 `string_view`；Sink batch 接收返回后也不得借用 Backend scratch。
- 业务线程全部 join 后再 shutdown Logger；shutdown 排空 accepted records，
  join Backend，最后释放 Channel。

初版调度使用 adaptive poll 加短暂 timed wait，Ring 内不包含任何唤醒代码。
sleeping-flag/double-check 方案留到有基线后对比。

## 17. Sanitizer 与构建门禁

至少维护：

```text
Debug
ASan + UBSan
TSan（单独构建，不能和 ASan 混用）
Release
```

要求：

- GCC、Clang 至少各完成一次普通构建和测试。
- ASan/UBSan 跑单元、状态机和 fault tests。
- TSan 跑缩短版双线程压力测试。
- Release 跑完整长时间压力测试。
- 并发测试必须有 watchdog，失败时输出随机种子、容量、游标和最近操作。

TSan 通过只说明未检测到 data race，不代替内存序证明。

## 18. Benchmark 纪律

### 18.1 必报指标

```text
attempted messages/s
accepted messages/s
consumed messages/s
accepted bytes/s
producer P50/P99/P99.9
consumer lag / drain time
full/oversize/fragmentation drop
wrap_count / tail_waste_bytes
cycles per accepted record
branch misses / cache misses（平台允许时）
```

只报 attempted 会把“快速丢日志”伪装成高吞吐，禁止这样比较。

### 18.2 场景

- Payload：16、64、256、1024、8192B。
- Ring：16 KiB、64 KiB、1 MiB，并确保 max payload 配置合法。
- 无回绕、强制回绕、稳定流量、突发流量、持续 2 倍过载。
- 单测 geometry、纯 Ring、Ring 加 Consumer、编码/`c20_format`、Null Sink、页缓存
  文件、`fdatasync` 分层测试。

### 18.3 跨项目公平性

- QLog Text 只和 BQLog Text、Quill Text 等同工作量路径比较。
- BQLog Compress、NanoLog binary 必须放到独立二进制榜，并报告解码成本。
- spdlog 的 producer 完成时间不能冒充 backend 排空时间。
- 消息内容、时间精度、输出字节数、线程数、容量、drop 策略和 drain
  边界必须一致。
- 校验输出条数，保证 `drop == 0` 后才能声称 guaranteed throughput。
- `drain queue`、`write 到 OS page cache`、`fdatasync` 分开命名。

跨项目绝对数字是报告，不是公共 CI 门禁。稳定专用机器建立基线后，可以
用以下初始回归阈值：

```text
accepted throughput 下降 > 5%  -> 需要解释或回退
cycles/accepted 上升 > 5%      -> 需要解释或回退
P99 上升 > 10%                 -> 需要解释或回退
```

## 19. 性能优化规则

在第一份稳定基准之前禁止：

- `[[likely]]`/`[[unlikely]]`、prefetch、SIMD、手写汇编。
- 为了“lock-free”引入 CAS/`fetch_add`。
- 将 acquire/release 改为 relaxed。
- Producer 批量 publish，导致孤立日志不可见。
- Consumer 仍持有 view 时提前 publish read。
- 动态扩容、覆盖旧日志、commit 缩小 payload。
- mmap、Direct I/O、TSC、VLQ、字符串驻留。
- 为跑分关闭 Header 校验或不统计 drop。

优化阶段每个提交只改变一个变量，并附带：

```text
修改前数据
修改后数据
正确性与 Sanitizer 结果
为什么目标负载会受益
对 P99、drop、内存和空间利用率的影响
```

没有收益的 branch hint、prefetch、cache-line 调整或自定义 memcpy 应删除，
不能因为“理论上更快”永久保留。

## 20. 秋招项目的证据链

最终仓库应当让面试官看到：

- ADR 记录了替代方案、选择和代价，而不只是最终答案。
- 测试先于复杂优化，随机模型和并发证明能够复现。
- 每次优化有独立 commit 和 benchmark 数据。
- 能解释为何 SPSC 不需要 CAS，为何 MPSC 不能照抄这套证明。
- 能解释 release/acquire 保护的具体普通内存访问。
- 能诚实地区分 producer latency、admission throughput、drain、page cache
  和断电持久化。
- 能说明 QLog 从多个项目学习了哪些原则，又主动拒绝了哪些不适合本场景
  的实现。

推荐提交序列：

```text
test: specify frame geometry boundaries
feat: implement frame layout geometry
test: add SPSC state-machine model
feat: add single-thread reservation protocol
test: add two-thread sequence and checksum stress
feat: publish SPSC cursors with acquire-release
perf: cache peer cursors with before-after benchmark
feat: add corruption quarantine and owned statistics
```

## 21. 阶段 B 实现与验收记录（已完成）

阶段 B 已于 2026-08-26 通过头文件独立编译、Debug、Release 与
ASan/UBSan 门禁，共 17 项测试全部通过。以下内容保留为实现与验收记录；
当前进入里程碑 2 阶段 C：长期 Handle 与发布状态布局。

你只修改以下生产文件：

~~~text
include/qlog/detail/spsc_ring_buffer.hpp
src/spsc_ring_buffer.cpp
CMakeLists.txt
~~~

### 21.1 头文件任务

只增加声明与私有数据形状，不写函数体：

- 补充智能指针所需的标准头文件。
- 在 `qlog::detail` 中前置声明 `SpscRingBufferTestAccess`。
- 在 `SpscRingBuffer` 私有区加入：
  - `static constexpr std::size_t kStorageAlignment = 64`；
  - `ValidatedConfig`；
  - `AlignedStorageDeleter`；
  - `StorageOwner`，类型为
    `std::unique_ptr<std::byte, AlignedStorageDeleter>`；
  - `ColdState`；
  - 私有静态 `validate_config(SpscRingBufferConfig)`；
  - `friend struct SpscRingBufferTestAccess`；
  - 唯一 `ColdState` 成员。
- `ValidatedConfig` 保存 `capacity_bytes`、`capacity_mask` 和
  `max_payload_bytes`。
- `ColdState` 保存一个 `ValidatedConfig` 和一个 `StorageOwner`。
- 保持 Ring 构造函数可抛异常，析构函数保持 `noexcept`。
- 不添加公开 `data()`、Storage getter、配置修改器或错误枚举。

声明轮廓如下；这是类型边界，不是函数体答案：

~~~cpp
private:
    static constexpr std::size_t kStorageAlignment = 64;

    struct ValidatedConfig {
        std::size_t capacity_bytes{};
        std::size_t capacity_mask{};
        std::size_t max_payload_bytes{};
    };

    struct AlignedStorageDeleter {
        void operator()(std::byte* pointer) const noexcept;
    };

    using StorageOwner =
        std::unique_ptr<std::byte, AlignedStorageDeleter>;

    struct ColdState {
        explicit ColdState(ValidatedConfig config);

        ValidatedConfig config_{};
        StorageOwner storage_{};
    };

    [[nodiscard]] static ValidatedConfig
    validate_config(SpscRingBufferConfig config);

    friend struct SpscRingBufferTestAccess;
    ColdState cold_state_;
~~~

`AlignedStorageDeleter` 必须是无状态类型，不要标记为 `final`，以免阻碍某些
标准库实现使用空基类优化。不要把 `sizeof(StorageOwner)` 作为跨平台契约；
阶段 B 验证其唯一所有权和释放匹配，不验证标准库内部布局。

### 21.2 源文件任务

仅实现以下冷路径行为：

1. `validate_config()` 无分配地检查：

   ~~~text
   capacity_bytes >= 16
   capacity_bytes 是 2 的幂
   capacity_bytes <= (std::size_t{1} << 31)
   max_payload_bytes != 0
   max_payload_bytes <= capacity_bytes / 2
   ~~~

2. 配置错误抛出 `std::invalid_argument`。必须先完成
   `max_payload_bytes <= capacity_bytes / 2` 检查，再对载荷做对齐计算。
3. 校验成功后产生 `ValidatedConfig`，其中
   `capacity_mask = capacity_bytes - 1`。
4. `ColdState` 只能接收 `ValidatedConfig`，随后执行唯一一次
   `::operator new(bytes, std::align_val_t{64})`。
5. 裸地址必须立即交给 `StorageOwner`；中间不要插入其他可能抛异常的操作。
6. `AlignedStorageDeleter` 使用匹配的 aligned scalar
   `::operator delete(pointer, std::align_val_t{64})`，并保持 `noexcept`。
7. 定义 Ring 构造与析构；析构可在 `.cpp` 中默认化。
8. 不捕获或转换 `std::bad_alloc`，不清零 Storage。

### 21.3 构建任务

只把 `src/spsc_ring_buffer.cpp` 加入现有 `qlog` 静态库 target：

- 不创建第二个 Ring 库。
- 不把该源文件直接加入测试 executable。
- 不修改 `tests/` 或测试 CMake。

### 21.4 本阶段禁止

- 修改阶段 A 已冻结的 Reservation/View 特殊成员。
- 定义 Handle、Reservation/View 或 accessor 的运行时行为。
- 把长期 Handle、CursorSet 或 atomic 加入 Ring。
- 实现 reserve/commit/peek/consume 或调用布局函数处理真实记录。
- 加入游标推进、acquire/release、CAS 或 `fetch_add`。
- 使用 `vector<std::byte>`、`new[]`、`delete[]`、`aligned_alloc` 或
  `memset`。
- 加入 allocator 注入、工厂 API、自制 expected、PImpl 或 benchmark。
- 为测试公开 Storage 可写地址。

这样做是为了先证明“非法配置一定在分配前被拒绝”和“固定 Storage 被唯一、安全地
释放”，再进入长期 Handle 与四缓存行布局。你完成上述生产代码后只需通知 Codex；
配置、对齐、异常和 Sanitizer 测试全部由 Codex 编写并运行。

严格按照 [更新后的里程碑 2 专项指南](./M2_SPSC_RING_BUFFER_GUIDE_CHS.md) 推进。

## 22. 当前唯一用户任务：阶段 C 长期 Handle 与发布状态布局

当前只修改生产文件：

~~~text
include/qlog/detail/spsc_ring_buffer.hpp
src/spsc_ring_buffer.cpp
~~~

已冻结的游标命名：

- 本侧当前进度：`current_write_cursor_`、`current_read_cursor_`。
- 对端缓存快照：`cached_read_cursor_`、`cached_write_cursor_`。
- 共享发布游标：`SpscRingBuffer::cursors_.write_cursor_`、
  `SpscRingBuffer::cursors_.read_cursor_`。

已冻结的 Ring 成员顺序：

~~~text
ColdState cold_state_
SpscWriteHandle write_handle_
CursorSet cursors_
SpscReadHandle read_handle_
~~~

你的生产代码任务：

- 引入 `<atomic>`，声明一个私有 `CursorSet` 类型。
- `CursorSet` 保存两个初始化为零的 `atomic<uint64_t>`：`write_cursor_` 和
  `read_cursor_`；两个成员分别使用 `alignas(kCacheLineSize)`。
- Ring 只内嵌一个 `CursorSet cursors_`，不手写缓存行 padding。
- Ring friend 两个长期 Handle，并按冻结顺序内嵌三个成员对象，形成四条热缓存行。
- 定义两个 Handle 私有构造，只保存 Ring 地址。
- 定义两个 accessor，只返回内嵌 Handle 的稳定引用。
- 更新 Ring 构造初始化列表，不执行 atomic load/store。

本阶段禁止实现 reserve/commit/peek/consume、游标推进、full/empty、
Header/Payload、acquire/release、CAS、`fetch_add`、锁或线程身份检查。

测试仍由 Codex 负责：提取唯一测试访问器、验证四缓存行布局和稳定地址，并运行
Debug、Release、ASan/UBSan。完成阶段 C 后先执行阶段 D 总门禁，再进入 M3。

详细类型边界与验收要求见
[里程碑 2 专项指南](./M2_SPSC_RING_BUFFER_GUIDE_CHS.md)。

## 23. 一手参考资料

- [BQLog SISO ring source](https://github.com/Tencent/BqLog/blob/main/src/bq_log/types/buffer/siso_ring_buffer.cpp)
- [spdlog async thread-pool dispatch](https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/details/thread_pool-inl.h)
- [spdlog MPMC blocking queue](https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/details/mpmc_blocking_q.h)
- [Quill design overview](https://github.com/odygrd/quill#-design)
- [Quill timestamp trade-offs](https://quillcpp.readthedocs.io/en/latest/timestamp_types.html)
- [NanoLog paper, USENIX ATC 2018](https://www.usenix.org/system/files/conference/atc18/atc18-yang.pdf)
- [NanoLog official repository](https://github.com/PlatformLab/NanoLog)

阅读这些资料的目的是验证原理和设计取舍。QLog 的实现依据始终是本仓库的
ADR、测试与 benchmark。
