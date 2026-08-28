!~1# QLog V1 SPSC 架构评审

- 状态：已接受
- 日期：2026-08-16
- 接受日期：2026-08-16
- 最后修订：2026-08-25
- 关卡已通过：里程碑 1 Geometry；当前仅进入里程碑 2 存储与类骨架

## 已冻结事项

- 一个生产者线程拥有一个 SPSC 通道；一个后台线程消费多个通道。
- 在正常日志路径中，生产者绝不进行格式化、阻塞、稳态分配或共享原子读-改-写操作。
- 每个通道容量有界。准入失败时采用 `drop_new`，且所有游标保持不变。
- 仅保证单个生产者通道内的 FIFO 顺序。
- ADR-002 定义尾端帧头/连续载荷（payload）布局。
- 每接受一条记录，生产者以一次 release-store 发布。消费者可以批量 release 发布回收进度。

## 已冻结的分层

```text
业务线程
    -> TLS 长期写 Handle
    -> 通道拥有的 SPSC 环形缓冲区
                                    \
业务线程                              -> 一个后台调度器
    -> TLS 长期写 Handle                 /      -> 可复用的 fmt 输出缓冲区
    -> 通道拥有的 SPSC 环形缓冲区            -> sink 写缓存 -> 文件 I/O
```

### SPSC 环形缓冲区

环形缓冲区类型冻结为 `SpscRingBuffer`。它仅拥有存储、游标状态以及预留/读取机制，不知道 fmt、文件、线程注册、休眠或 mmap。

已冻结的所有权：

- 容量在构造时选择，之后固定且不可变；
- 环形缓冲区拥有按 64 字节对齐的存储；
- 不可复制且不可移动，从而保证两个长期 Handle 及单次对象引用的地址保持稳定；
- 内嵌唯一的 `SpscWriteHandle` 和 `SpscReadHandle`，并只通过左值 accessor 返回引用；
- 两个长期 Handle 不可复制、不可移动，持有非拥有型 Ring 指针和本侧私有游标缓存；
- 长期 Handle 自身就是对应角色的 64B 私有状态块，不再另设 PrivateState；
- 由地址稳定的 Channel 对象通过 `unique_ptr` 持有；
- 外部内存和 mmap 构造函数推迟到 V1 之后。

已冻结的缓存行分组：

```text
长期写 Handle 缓存行：ring, current_write_cursor_, cached_read_cursor_, reservation_pending
写发布缓存行：       CursorSet::write_cursor_
读发布缓存行：       CursorSet::read_cursor_
长期读 Handle 缓存行：ring, current_read_cursor_, cached_write_cursor_, reclaim counters, read_pending
冷数据缓存行：       capacity, mask, storage pointer, statistics
```

`SpscWriteHandle`、`CursorSet`、`SpscReadHandle` 是三个 Ring 成员对象；
其中 `CursorSet` 占两条缓存行，因此总计仍是四条相互隔离的热缓存行。必须检查
两个 Handle 各为 64B、`CursorSet` 为 128B，并保证
`std::atomic<std::uint64_t>` 始终无锁。

### 长期写 Handle

已接受的 API 使用只可移动的两阶段预留：

```cpp
enum class ReserveStatus : std::uint8_t {
    ok,
    full,
    payload_too_large,
    reservation_pending,
};

class WriteReservation {
public:
    std::span<std::byte> payload() noexcept;
    void commit() noexcept;
    void abort() noexcept;
};

class SpscWriteHandle {
public:
    [[nodiscard]] WriteReservation
    try_reserve(std::size_t exact_payload_bytes) noexcept;
};
```

- 成功的预留只暴露一个连续的载荷（payload）span。
- `try_reserve()` 计算暂定布局，但不推进已提交的生产者游标。
- `commit()` 写入 8 字节帧头，推进 `current_write_cursor_`，然后对 `cursors_.write_cursor_` 执行 release-store。
- 未提交即析构会中止预留：不发布任何内容，也不推进任何游标。下一次预留可以覆盖相同的未发布字节。
- 同一时刻只允许一个未完成的预留。该检查仅涉及生产者本地状态，不需要原子操作。
- `commit()` 不能改变预留的精确大小，因为不同大小可能意味着不同的物理载荷（payload）位置。

### 长期读 Handle

已接受的 API 返回一个只可移动的读取句柄：

```cpp
enum class ReadStatus : std::uint8_t {
    ok,
    empty,
    corrupted,
};
    read_pending,

class ReadView {
public:
    std::span<const std::byte> payload() const noexcept;
    void consume() noexcept;
    void abandon() noexcept;
};

class SpscReadHandle {
public:
    [[nodiscard]] ReadView try_peek() noexcept;
    void publish_reclaimed() noexcept;
};
```

- 每个长期读 Handle 最多只能有一个未完成的 ReadView。
- `try_peek()` 仅在缓存快照耗尽后才 acquire-load `cursors_.write_cursor_`。
- ReadView 不拥有载荷（payload）字节。它不得存活到对应空间被回收之后。
- `consume()` 推进 `current_read_cursor_`；在累计 32 条记录、4 KiB、切换通道或观察到通道为空时，以 release 发布回收进度。
- 未消费的句柄析构时会放弃该视图，并保留记录以供后续读取。后台线程的错误路径必须明确决定是消费一条有问题的日志，还是隔离该通道。

## 后台调度

后台线程以轮询方式扫描活动通道。每次访问都有记录数/字节数预算，避免一个高噪声生产者使其他线程饥饿。对于每条记录：

```text
查看环形缓冲区中的连续载荷（payload）
    -> 直接解码并格式化到后台线程可复用的输出缓冲区
    -> 消费环形缓冲区记录，并在满足条件时发布回收进度
    -> 稍后将输出缓冲区写入 sink
```

因此，同步格式化读取载荷（payload）期间必须保留环形缓冲区中的记录，但在可能较慢的文件 I/O 之前释放它。格式化器或 sink 都不得保留指向环形缓冲区存储的 `string_view`。

唤醒属于调度器，而不属于 SPSC 环形缓冲区。这样可使队列正确性独立于 Linux `eventfd`、信号量、条件变量或定时等待。

## 通道注册表与生命周期

- 线程第一次记录日志时，在注册表锁保护下执行冷路径通道注册；之后 TLS 缓存一个无所有权的 `SpscWriteHandle*`。
- Logger 拥有的 Channel 地址保持稳定。
- V1 要求所有生产者线程在 Logger 关闭之前停止并完成 join。
- 线程退出标记允许后台线程继续排空已退役通道。
- 对于 V1 面向的长生命周期线程场景，退役 Channel 的存储可以继续由 Logger 持有直至关闭。这样可以从第一版实现中消除删除竞态；回收高度短生命周期线程的通道是后续功能。
- 关闭过程会排空已接受的帧，在选定的 sink 边界执行 flush，停止并 join 后台线程，最后才销毁通道。

## 代码评审中必须证明的不变量

```text
unsigned(current_write_cursor_ - cached_read_cursor_) <= capacity
unsigned(cached_write_cursor_ - current_read_cursor_) <= capacity
current_write_cursor_ % 8 == 0
current_read_cursor_  % 8 == 0
```

`cached_read_cursor_` / `cached_write_cursor_` 分别是从
`cursors_.read_cursor_` / `cursors_.write_cursor_` 取得的本地快照。

- 只有生产者写入生产者本地状态和 `cursors_.write_cursor_`。
- 只有消费者写入消费者本地状态和 `cursors_.read_cursor_`。
- 无符号减法计算游标距离，包括游标接近 `UINT64_MAX` 时；绝不使用 `write < read` 判断游标顺序。
- 过期的对端游标缓存只会低估可用空间或可处理工作量。
- `cursors_.write_cursor_` 是提交标记；消费者绝不检查未发布的帧头/载荷（payload）字节。
- 在生产者 acquire-load 到包含相应字节的已发布读游标之前，已回收字节绝不被复用。

## 已接受的评审决策

1. **存储所有权。** 环形缓冲区拥有对齐存储，其运行时选择的容量不可变。外部存储和 mmap 存储不属于 V1。
2. **后台线程边界。** 后台线程直接从环形缓冲区格式化到可复用输出缓冲区，在格式化之后回收空间，随后才执行文件 I/O。
3. **空闲唤醒策略。** 第一版实现采用自适应轮询加短时定时等待，不增加任何生产者侧的唤醒访问。带调度器休眠标志和低频通知路径的方案保留为后续测量选项。两种情况下，唤醒均位于 Ring 之外。
4. **生命周期约定。** 生产者线程在 Logger 关闭前停止；V1 中地址稳定的通道存储保留到关闭时。
5. **损坏处理策略。** Debug 模式完整检查 Header、长度、对齐和 Geometry。Release 模式只保留避免越界和未定义行为的最小检查；损坏隔离、详细统计与紧急报告推迟到 M6，不进入 M3 热路径。任何模式都绝不使用不可信的 `frame_bytes` 定位下一条记录。

## 接受后的实现顺序

1. 实现纯布局几何计算，并在小容量环形缓冲区中穷举测试每个对齐位置。
2. 建立 `SpscRingBuffer` 的存储所有权、长期读写 Handle、四缓存行布局和单次对象声明，不实现状态机。
3. 实现单线程 reserve/commit/peek/consume/abort，并与 deque 模型比较。
4. 添加发布游标和 acquire/release；在 TSan 下运行双线程序列与校验和测试。
5. 添加对端游标缓存和批量回收，并在每项优化前后进行基准测试。
6. 添加 Channel 注册和后台线程轮询调度。
7. 对唤醒策略进行基准测试，然后选择 V1 调度器默认值。
