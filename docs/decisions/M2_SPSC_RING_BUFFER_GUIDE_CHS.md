# QLog 里程碑 2 开发指南：长期读写 Handle 与存储骨架

- 状态：已接受，当前里程碑
- 日期：2026-08-25
- 依据：ADR-003
- 适用目标：Linux x86-64、C++20
- 当前边界：只完成类型、所有权、对齐存储和缓存行布局，不实现并发状态机

## 0. 固定协作分工

本节优先于本指南后续可能出现的旧任务措辞：

- 你只负责 `include/qlog/`、`src/` 和生产目标需要的实现。
- Codex 负责测试设计、`tests/`、测试相关 CMake/脚本、测试执行和结果分析。
- 每个生产任务开始前，Codex 必须先解释设计原因、风险和不变量。
- 你完成实现后只需通知 Codex，不需要提交测试代码或测试输出。
- 若测试暴露生产缺陷，Codex 提供位置、复现和原因；未经明确要求，不修改你的生产实现。

因此，下文所有测试清单都是 Codex 的验收责任，也是生产代码必须满足的外部契约。

## 1. 本次设计更新的结论

里程碑 2 冻结为：

- 删除 SpscProducer 和 SpscConsumer 类型。
- SpscRingBuffer 内嵌唯一的 SpscWriteHandle 与 SpscReadHandle。
- 两个长期 Handle 同时就是各自的 64B 私有状态块，不再另设 ProducerPrivateState 或 ConsumerPrivateState。
- 每次写入仍返回一个小型 WriteReservation。
- 每次读取返回一个小型 ReadView。
- Storage 仍是连续字节存储，8B 只是最小分配与对齐单位。
- 不引入 BlockStatus、逐 Block 原子、版本号或 union Block。
- 写端与读端继续使用不同的状态枚举。
- 两个发布游标继续独占各自缓存行。

性能收益来自“长期 Handle 与私有状态合并”，不是来自 Handle 这个名字。

## 2. BQLog 源码核对结果

### 2.1 BQLog SISO

BQLog 的 siso_ring_buffer 没有长期 Producer、Consumer 或普通 Handle 成员。两端直接调用 Ring：

~~~text
alloc_write_chunk
    -> 写 Payload
    -> commit_write_chunk

read_chunk
    -> 使用 Payload
    -> return_read_chunk / discard_read_chunk
~~~

每次 alloc/read 都会按值返回一个临时小 Handle。
具体字段很小：

- 写 Handle：`data_addr + result + low_space_flag`。
- 读 Handle：`data_addr + result + data_size`。
- commit/return 根据 `data_addr` 找回 Frame Header，再读 `block_num` 推进本地游标。
- 基础 Handle 本身不是长期 RAII 对象；BQLog 另有栈上 scope guard 封装配对。


SISO 使用 8B Block：

- BLOCK_SIZE = 8。
- 第一个 8B 保存 block_num 和 data_size。
- 其他 Block 只是 Payload、对齐填充或尾端浪费。
- 没有逐 Block UNUSED/USED/INVALID。
- 没有 union block。
- Frame 可见性由发布写游标控制。

源码位置：

- E:/VisualStudioProject/BqLog/src/bq_log/types/buffer/siso_ring_buffer.h 第 44～64 行。
- E:/VisualStudioProject/BqLog/src/bq_log/types/buffer/siso_ring_buffer.cpp 第 61～149 行。
- E:/VisualStudioProject/BqLog/src/bq_log/types/buffer/log_buffer_defs.h 第 41～51 行。

### 2.2 BQLog MISO

union block 和 block_status 位于 miso_ring_buffer，即多生产者单消费者版本：

- 一个 Block 是一整条缓存行。
- union 把 chunk header 与缓存行原始字节覆盖在同一地址。
- UNUSED/USED/INVALID 用于多个生产者之间的槽位发布和跳过。
- 这些状态不是 SPSC 所需机制。

源码位置：

- E:/VisualStudioProject/BqLog/src/bq_log/types/buffer/miso_ring_buffer.h 第 58～114 行。

因此 QLog V1 不把 MISO 的并发协调成本带入 SPSC。

## 3. 最终对象关系

~~~text
SpscRingBuffer（不可复制、不可移动）
├── SpscWriteHandle          64B，长期写端 + 写端私有状态
├── PublishedWriteState      64B，atomic<uint64_t>
├── SpscReadHandle           64B，长期读端 + 读端私有状态
├── PublishedReadState       64B，atomic<uint64_t>
├── ColdState                配置与 Storage owner
└── 64B 对齐的字节 Storage
~~~

单次对象：

~~~text
SpscWriteHandle::try_reserve()
└── WriteReservation         一条尚未 commit 的记录

SpscReadHandle::try_peek()
└── ReadView                 一条尚未 consume 的记录
~~~

生命周期：

1. Ring 拥有两个长期 Handle。
2. 长期 Handle 不拥有 Ring，只保存稳定的非拥有型 Ring 指针。
3. Ring 必须比两个长期 Handle及所有单次对象活得更久。
4. 长期 Handle 不可复制、不可移动。
5. WriteReservation 与 ReadView 不可复制、可 noexcept 移动。
6. moved-from 单次对象必须 inactive。
7. Ring 析构前不得有并发访问或 active 单次对象。

## 4. 为什么选择长期 Handle 与状态合并

旧草案中 Endpoint 和私有状态是两层对象：

~~~text
SpscProducer -> Ring -> ProducerPrivateState
SpscConsumer -> Ring -> ConsumerPrivateState
~~~

新方案把角色入口和该角色唯一写入的状态合并：

~~~text
SpscWriteHandle == 写端私有缓存行
SpscReadHandle  == 读端私有缓存行
~~~

这样：

- 不再为 Endpoint 额外保存一组对象。
- TLS 将来可以直接缓存 SpscWriteHandle*。
- 写端本地游标通过 this 直接访问。
- 读端本地游标通过 this 直接访问。
- 每个长期 Handle 本来就需要独占一个缓存行，Ring 总体布局不增加热缓存行。
- 类型仍然限制写端只能调用写 API、读端只能调用读 API。

当前不能声称它一定快于直接 Ring API。M3 完成后仍需比较 Release 汇编和 benchmark；但从对象布局看，它比“Endpoint + 独立 PrivateState”更紧凑。

## 5. 8B 分配单位的冻结方式

采用：

~~~text
kAllocationUnitBytes = 8
kFrameAlignment      = 8
sizeof(FrameHeader)  = 8
~~~

保持：

- capacity、payload、frame、offset 都以 byte 为单位。
- logical cursor 使用 uint64_t 字节计数。
- Storage 使用 std::byte。
- Frame 起点和 frame_bytes 始终 8B 对齐。
- 每个 Frame 只有一个 Header。
- 物理位置仍是 logical_cursor & mask。

不采用：

- Block 类数组。
- Block 游标。
- union 在 Header 与数组之间切换。
- 每个 8B 单元的状态字节。
- 每 Block atomic、ready、version 或 result。

byte cursor 的物理寻址少一次 block-to-byte 换算，也保留现有 Geometry 与测试结论。

## 6. API 形状

下面是声明边界，不是函数体答案。

~~~cpp
namespace qlog::detail {

struct SpscRingBufferConfig {
    std::size_t capacity_bytes{64 * 1024};
    std::size_t max_payload_bytes{8 * 1024};
};

enum class ReserveStatus : std::uint8_t {
    ok,
    full,
    payload_too_large,
    reservation_pending,
};

enum class ReadStatus : std::uint8_t {
    ok,
    empty,
    corrupted,
    read_pending,
};

class SpscRingBuffer;
class WriteReservation;
class ReadView;

class alignas(64) SpscWriteHandle final {
public:
    SpscWriteHandle(const SpscWriteHandle&) = delete;
    SpscWriteHandle& operator=(const SpscWriteHandle&) = delete;
    SpscWriteHandle(SpscWriteHandle&&) = delete;
    SpscWriteHandle& operator=(SpscWriteHandle&&) = delete;

    [[nodiscard]] WriteReservation
    try_reserve(std::size_t exact_payload_bytes) noexcept;

private:
    friend class SpscRingBuffer;
    explicit SpscWriteHandle(SpscRingBuffer& ring) noexcept;

    SpscRingBuffer* ring_{};
    std::uint64_t current_write_cursor_{};
    std::uint64_t cached_read_cursor_{};
    bool reservation_pending_{};
};

class alignas(64) SpscReadHandle final {
public:
    SpscReadHandle(const SpscReadHandle&) = delete;
    SpscReadHandle& operator=(const SpscReadHandle&) = delete;
    SpscReadHandle(SpscReadHandle&&) = delete;
    SpscReadHandle& operator=(SpscReadHandle&&) = delete;

    [[nodiscard]] ReadView try_peek() noexcept;
    void publish_reclaimed() noexcept;

private:
    friend class SpscRingBuffer;
    explicit SpscReadHandle(SpscRingBuffer& ring) noexcept;

    SpscRingBuffer* ring_{};
    std::uint64_t current_read_cursor_{};
    std::uint64_t cached_write_cursor_{};
    std::uint32_t records_since_publish_{};
    std::uint32_t bytes_since_publish_{};
    bool read_pending_{};
};

class SpscRingBuffer final {
public:
    explicit SpscRingBuffer(SpscRingBufferConfig config = {});
    ~SpscRingBuffer() noexcept;

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;
    SpscRingBuffer(SpscRingBuffer&&) = delete;
    SpscRingBuffer& operator=(SpscRingBuffer&&) = delete;

    [[nodiscard]] SpscWriteHandle& write_handle() & noexcept;
    [[nodiscard]] SpscReadHandle& read_handle() & noexcept;

    SpscWriteHandle& write_handle() && = delete;
    SpscReadHandle& read_handle() && = delete;
};

}  // namespace qlog::detail
~~~

实际声明顺序必须满足完整类型规则。Handle 构造期间只能保存 Ring 地址，不能读取尚未完成构造的 Ring 成员。

## 7. 四个热缓存行

| 缓存行 | 字段 | 唯一写线程 |
|---|---|---|
| SpscWriteHandle | ring、current_write_cursor_、cached_read_cursor_、reservation_pending | 写线程 |
| PublishedWriteState | atomic write_cursor_ | 写线程 |
| SpscReadHandle | ring、current_read_cursor_、cached_write_cursor_、回收计数、read_pending | 读线程 |
| PublishedReadState | atomic read_cursor_ | 读线程 |

要求：

~~~cpp
static_assert(sizeof(SpscWriteHandle) == 64);
static_assert(alignof(SpscWriteHandle) == 64);
static_assert(sizeof(SpscReadHandle) == 64);
static_assert(alignof(SpscReadHandle) == 64);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
~~~

M2 中 atomic 只初始化为零，不调用 load/store。

ColdState 和统计不得与两个 PublishedState 共用缓存行。

## 8. 单次 Reservation/View 的性能目标

BQLog 每次写和读同样会创建小 Handle。QLog 的单次对象也必须：

- 不分配。
- 不复制 Payload。
- 无虚函数。
- 无引用计数。
- 按值返回并允许复制消除。
- 失败对象 inactive。
- 使用 owner 指针是否为空表示 active，避免额外 active bool。
- hot 函数在 M3 放入 header/inl，便于标量替换。

建议的 M3 字段目标：

~~~text
WriteReservation
  owner pointer
  payload pointer
  next logical write
  payload bytes
  ReserveStatus

ReadView
  owner pointer
  payload pointer
  next logical read
  payload bytes
  ReadStatus
~~~


BQLog 的 Handle 没有保存 next cursor，因此更紧凑；代价是 commit/return 会从
`data_addr` 找回 Header 并再次读取 `block_num`。QLog M3 的正确性基线先保留
显式 next cursor，并在 commit 写 Header，避免 commit 再读 Header。如果 Release
汇编出现 sret 或明显 spill，再建立“Header 恢复跨度”的受控微基准后决定是否改
ADR；不能只为把对象压到 16B 就引入指针哨兵、指针标记或 union。
目标大小不超过 32B，但这是性能目标，不是持久 ABI。最终以 Release sizeof、汇编和 benchmark 为准。

M3 语义：

- WriteReservation 析构默认 abort，绝不自动 commit。
- ReadView 析构默认 abandon，绝不自动 consume。
- commit/abort/consume/abandon 后 owner 置空。
- Header 在 commit 中通过 memcpy 写入。
- 发布游标才是提交标记。

## 9. Result 设计

删除统一的 ResultStatus。

写端使用 ReserveStatus，读端使用 ReadStatus。两者底层都是 uint8_t，因此分开不会增加对象大小。

统一的是调用风格：

~~~cpp
if (!reservation) {
    inspect(reservation.status());
}

if (!view) {
    inspect(view.status());
}
~~~

禁止在 Ring 或长期 Handle 中保存共享 last_result。每次调用的结果只属于本次 Reservation/View。

## 10. 当前执行计划

### 阶段 A：单次对象生命周期骨架（已完成）

为什么先做：

- Reservation/View 表示一条尚未结束的独占借用，复制会制造两个“所有者”。
- 默认移动将来会复制 owner，使源对象和目标对象同时 active，可能重复 abort/abandon。
- 先只冻结特殊成员，可避免在 M3 前过早固定字段布局和返回 ABI。

你的生产代码任务：

- 前置声明 `SpscWriteHandle` 与 `SpscReadHandle`。
- 将 `WriteReservation` 与 `ReadView` 定义为完整 `final` 类型。
- 删除复制构造与复制赋值。
- 公开声明 `noexcept` 析构、移动构造和移动赋值；只声明，不 `= default`，不写函数体。
- 默认构造保持私有，并 friend 对应的长期 Handle。
- 暂不加入字段和操作 API。

Codex 的验收任务：

- 检查两个类型不可默认构造、不可复制。
- 检查两个类型可 `noexcept` 移动和析构。
- 检查 `try_reserve()` 与 `try_peek()` 的精确返回类型。
- 执行头文件独立编译以及 Debug、Release 测试。

阶段 A 已于 2026-08-26 通过头文件独立编译、Debug 和 Release 门禁。后续阶段
不得改变已经冻结的 move-only 生命周期契约；只有进入 M3 时才能为单次对象加入
字段和操作 API。

### 阶段 B：配置校验与 64B 对齐 Storage RAII（已完成）

阶段 B 已于 2026-08-26 通过头文件独立编译、Debug、Release 与
ASan/UBSan 门禁，共 17 项测试全部通过。

为什么这样做：

- 所有非法配置必须在分配前拒绝，避免无效配置触发大内存申请或产生半初始化对象。
- 固定容量和 64B 对齐为后续缓存行布局和掩码寻址提供稳定地址。
- 私有 `ColdState` 集中保存冷数据，阶段 C 加入热状态块时不必再次重排这些字段。
- `unique_ptr` 与匹配的 aligned deleter 将释放责任绑定到对象生命周期。
- Storage 只拥有原始字节，不提前承担任何并发协议。

冻结的私有结构：

~~~text
SpscRingBuffer
└── ColdState
    ├── ValidatedConfig
    │   ├── capacity_bytes
    │   ├── capacity_mask
    │   └── max_payload_bytes
    └── StorageOwner
        └── std::unique_ptr<std::byte, AlignedStorageDeleter>
~~~

`ValidatedConfig`、`ColdState`、`AlignedStorageDeleter` 和 `StorageOwner`
都只是 `SpscRingBuffer` 的私有实现细节，不进入生产公共 API。

构造关系必须是：

~~~text
SpscRingBuffer(config)
    -> validate_config(config)       // 只校验，不分配
    -> ValidatedConfig
    -> ColdState(validated_config)
    -> 唯一一次 64B aligned Storage 分配
~~~

配置契约：

~~~text
capacity_bytes >= 16
capacity_bytes 是 2 的幂
capacity_bytes <= (std::size_t{1} << 31)
max_payload_bytes != 0
align_up_8(max_payload_bytes) <= capacity_bytes / 2
~~~

实现校验时，必须在调用 `align_up_8()` 前先检查
`max_payload_bytes <= capacity_bytes / 2`。合法 capacity 的一半一定按 8B
对齐，因此这个直接检查与对齐后的约束等价，并能先拒绝 `SIZE_MAX` 一类输入，
避免 `value + 7` 无符号回绕。

半容量约束不额外包含 Header。令 `P = align_up_8(payload)`；跨尾时尾部长度
`T <= P`，所以 `frame_bytes = T + P <= 2P <= capacity`。Header 已包含在
尾部 `T` 中。

你的生产代码任务：

- 保留已经存在的 64 KiB / 8 KiB 默认配置。
- 加入上述四个私有类型，以及私有、无分配的 `validate_config()`。
- 配置错误抛出 `std::invalid_argument`；不新增公开 `ConfigError` 枚举，
  也不冻结异常文本。
- aligned allocation 失败时原样传播 `std::bad_alloc`。
- `StorageOwner` 使用
  `::operator new(bytes, std::align_val_t{64})` 分配，并由 deleter 调用匹配的
  `::operator delete(pointer, std::align_val_t{64})`。
- 不得混用 `new[]`、`delete[]`、普通 `operator delete`、`free` 或
  `std::aligned_alloc`。
- Storage 不清零、不值初始化、不扩容，也不公开 mutable data。
- 在命名空间中前置声明 `SpscRingBufferTestAccess`，并仅将其声明为 Ring 的
  私有 friend；测试访问器由 Codex 在测试代码中定义。
- 将 `src/spsc_ring_buffer.cpp` 加入生产 `qlog` target。
- 只定义 Ring 构造、析构、配置校验、ColdState 构造和 Storage 释放；不要定义
  Handle、Reservation/View 或 accessor 的运行时行为。

Codex 的验收任务：

- 覆盖 capacity 为 0、小于 16、非 2 次幂以及超过 `2^31` 的情况。
- 覆盖 `max_payload_bytes` 为 0、合法边界、超过边界 1B 和 `SIZE_MAX`。
- 验证 `capacity=16/max_payload=8` 合法，`capacity=16/max_payload=9` 非法。
- 通过私有纯校验入口验证 `capacity=2^31/max_payload=2^30` 合法，不实际申请
  2 GiB Storage。
- 验证默认 64 KiB / 8 KiB 配置能够构造。
- 验证配置错误类型为 `std::invalid_argument`，但不检查异常文本。
- 验证 Storage 地址按 64B 对齐，配置和地址在 Ring 生命周期内保持稳定。
- 验证重复构造析构以及异常路径无泄漏、无 delete mismatch。
- 验证生产公共 API 没有暴露 Storage。
- 运行头文件独立编译、Debug、Release 与 ASan/UBSan。

### 阶段 C：长期 Handle 与发布状态布局（当前）

本阶段冻结三类游标名称：

~~~text
本线程权威进度：
  SpscWriteHandle::current_write_cursor_
  SpscReadHandle::current_read_cursor_

共享发布进度：
  PublishedWriteState::write_cursor_
  PublishedReadState::read_cursor_

对端进度缓存：
  SpscWriteHandle::cached_read_cursor_
  SpscReadHandle::cached_write_cursor_
~~~

数据流固定为：

~~~text
current_write_cursor_
    -> PublishedWriteState::write_cursor_
    -> cached_write_cursor_

current_read_cursor_
    -> PublishedReadState::read_cursor_
    -> cached_read_cursor_
~~~

`current_*` 只能由本侧线程推进，是本侧权威位置；`cached_*` 是对端发布游标
的本地快照，只允许过期并造成保守判断，不能扩大可读或可写范围。

四个热状态块的成员顺序冻结为：

~~~text
ColdState cold_state_
SpscWriteHandle write_handle_
PublishedWriteState published_write_state_
SpscReadHandle read_handle_
PublishedReadState published_read_state_
~~~

其中两个发布状态的形状冻结为：

~~~cpp
struct alignas(64) PublishedWriteState {
    std::atomic<std::uint64_t> write_cursor_{0};
};

struct alignas(64) PublishedReadState {
    std::atomic<std::uint64_t> read_cursor_{0};
};
~~~

不使用 `std::hardware_destructive_interference_size`，不手写 padding，也不把
Ring 的精确 `sizeof` 冻结为 ABI。四个热类型各自使用 `alignas(64)`，并通过
静态断言验证 `sizeof == 64`、`alignof == 64` 以及
`atomic<uint64_t>` 始终无锁。

你的生产代码任务：

- 头文件自包含引入 `<atomic>`。
- 按上述名称调整两个 Handle 的当前游标和对端缓存字段。
- 在 Ring 私有区加入两个 PublishedState，并 friend 两个长期 Handle。
- Ring 按冻结顺序内嵌唯一 WriteHandle、PublishedWriteState、ReadHandle 和
  PublishedReadState。
- 两个 Handle 的私有构造函数只保存 `&ring`，不读取尚未构造完成的成员。
- Ring 构造时完成上述成员构造；所有游标、计数和 pending 通过成员初始化为零。
- 在 `.cpp` 定义两个 Handle 构造和两个 accessor；accessor 只返回稳定引用。
- 不定义 `try_reserve()`、`try_peek()`、Reservation/View 或任何状态机行为。

Codex 的验收任务：

- 将唯一测试访问器提取到共享测试头，避免多个测试翻译单元重复定义。
- 验证两个 Handle 和两个 PublishedState 均为 64B 对齐、大小为 64B，且地址
  两两位于不同缓存行。
- 验证两个 accessor 多次调用始终返回同一内嵌 Handle。
- 验证两个共享游标初值为零；测试可以 relaxed-load，生产代码不得 load/store。
- 保留阶段 A/B 全部测试，并运行头文件独立编译、Debug、Release 与
  ASan/UBSan。
- 扫描生产源码，确认没有游标推进、atomic load/store、acquire/release、CAS、
  `fetch_add`、Header memcpy 或并发状态机行为。

性能优先边界：

- 本阶段不加入线程身份检查、锁、引用计数、运行时缓存行检查或防御性分支。
- Debug 完整校验、Release 仅保留避免越界和未定义行为的最小检查；损坏隔离、
  详细统计和紧急报告推迟到 M6。

### 阶段 D：M2 总门禁

本阶段原则上不新增生产功能。阶段 B 已完成生产 source 接入和 Storage 测试访问
契约；阶段 C 已完成长期 Handle 与发布状态布局。

你的生产代码任务：

- 只修复总门禁发现的生产缺陷。
- 不为通过测试增加公开 Storage getter、状态机占位实现或测试专用运行时分支。

Codex 的验收任务：

- 确认所有已有定义的生产源文件均通过 `qlog` target 构建和链接。
- 汇总 Storage、生命周期和四缓存行布局测试。
- 使用相互独立的 Debug、Release、ASan/UBSan 构建目录。
- 检查 M2 源码中没有游标推进、内存序或并发状态机行为。
- 全部门禁通过后才进入 M3。

## 11. 当前阶段禁止出现

- union block。
- BlockStatus 或逐 Block 状态。
- CAS、fetch_add。
- atomic acquire/release。
- Header memcpy。
- Payload span 的实际生成。
- full/empty 判断。
- 游标推进。
- TLS、后台线程、fmt、Sink、mmap。
- 吞吐量 benchmark。

M2 只证明对象布局和生命周期。真正的热路径比较在 M3 状态机可运行后进行。

## 12. M2 退出条件

全部满足后再评审：

- 类型命名与 ADR-003 一致。
- Ring、长期 Handle 和单次对象的特殊成员符合契约。
- 两个长期 Handle 同时充当私有状态缓存行。
- Storage 64B 对齐且由 Ring 独占。
- 四个热块不共享缓存行。
- 配置错误在分配前拒绝。
- Debug、Release、ASan/UBSan 通过。
- 没有任何 M3 状态机行为。

协作交付方式：

- 你只需完成当前阶段的生产实现并通知 Codex。
- Codex 负责测试文件、测试 CMake、构建输出和门禁报告。
- Codex 在每个阶段开始前解释设计原因，在失败时解释具体不变量为何被破坏。
- 未经你明确要求，Codex 不直接修改生产实现。
