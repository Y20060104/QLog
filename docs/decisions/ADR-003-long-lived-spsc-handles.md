# ADR-003：长期读写 Handle 合并私有状态

- 状态：已接受
- 日期：2026-08-25
- 影响范围：QLog V1 SpscRingBuffer 对象模型
- 不改变：ADR-002 的 Frame Geometry 与连续回绕 Payload

## 背景

最初设计让 SpscRingBuffer 同时拥有：

- SpscProducer endpoint；
- ProducerPrivateState；
- SpscConsumer endpoint；
- ConsumerPrivateState；
- 两个发布游标状态。

Endpoint 只保存 Ring 指针，而私有游标保存在另一对象中。该分层类型安全，但可能让每个 Channel 多出 Endpoint 对象和一次状态查找。

项目目标优先考虑生产者吞吐量、尾延迟和固定内存占用，因此重新核对 BQLog 的 SISO/MISO 实现。

## BQLog 证据

BQLog SISO：

- 不存在长期 Producer/Consumer endpoint。
- alloc_write_chunk/read_chunk 每次按值返回临时小 Handle。
- 使用 8B Block 作为分配单位。
- 每个 Frame 只有起始 Block 保存 Header。
- 没有 union block 和逐 Block 状态。
- 可见性由发布游标控制。

BQLog MISO：

- 使用缓存行大小的 union block。
- Header 与原始缓存行字节覆盖同一地址。
- 使用 UNUSED/USED/INVALID 协调多个生产者。
- 该状态机服务 MPSC，不属于 SPSC 必需成本。

因此不能把 MISO 的 union/status 当成 SISO 性能技巧照搬。

## 决策

### 长期对象

删除 SpscProducer 和 SpscConsumer。

SpscRingBuffer 内嵌：

- 唯一 SpscWriteHandle；
- 唯一 SpscReadHandle。

两个长期 Handle：

- 不可复制、不可移动；
- 不拥有 Ring；
- 只能由 Ring 构造；
- 通过左值限定 accessor 返回稳定引用；
- 自身就是对应角色的 64B 私有状态块。

SpscWriteHandle 保存：

- Ring 指针；
- current_write_cursor_；
- cached_read_cursor_；
- reservation_pending。

SpscReadHandle 保存：

- Ring 指针；
- current_read_cursor_；
- cached_write_cursor_；
- 批量回收计数；
- read_pending。

### 单次对象

每次写入仍返回 WriteReservation，每次读取返回 ReadView。

它们：

- 不拥有 Payload；
- 不分配；
- 不复制；
- 可 noexcept 移动；
- 按值返回；
- 结果状态属于本次操作；
- 终结操作后立即失效。

WriteReservation 默认 abort，ReadView 默认 abandon。

长期 Handle 不能保存 current payload/current layout 来代替单次对象，因为异常或提前返回会让 pending 状态持续到 Channel 销毁。

### 发布状态

继续使用两个独立缓存行，它们是同一个 `SharedCursor` 类型的两个实例：

- `write_cursor_`，由写线程发布；
- `read_cursor_`，由读线程发布。

`SharedCursor` 内部只保存初始化为零的 `atomic<uint64_t> value_`。两个长期
Handle 与两个 `SharedCursor` 实例构成四个热缓存行；写端与读端可变状态
绝不合并到同一缓存行。

### 8B 单位

8B 只作为 Frame 对齐与最小分配单位：

- Storage、容量、游标和长度继续以 byte 表示；
- 不定义逐 Block 状态；
- 不使用 union type-punning；
- Header 通过 memcpy 访问；
- 发布游标是唯一提交标记。

### Result

不使用统一 ResultStatus：

- 写端使用 ReserveStatus；
- 读端使用 ReadStatus。

分离错误域没有额外内存成本，并避免不可能状态。

## 性能理由

相对“Endpoint + 独立 PrivateState”：

- 长期 Handle 与私有状态合并，减少对象层次。
- TLS 可直接缓存写 Handle 地址。
- 本侧游标通过 Handle 的 this 指针访问。
- 不增加额外热缓存行。
- 单次 Reservation/View 可由编译器复制消除或标量替换。

该决策只说明结构上更紧凑，不提前宣称吞吐量一定更高。M3 后必须检查：

- sizeof Ring、长期 Handle 和单次对象；
- Release 汇编；
- producer P50/P99/P99.9；
- 单线程 admission throughput；
- consumer drain throughput；
- 低占用、突发和接近满载场景。

## 考虑过的替代方案

### 保留 SpscProducer/SpscConsumer 与独立私有状态

优点是术语标准、职责直观；缺点是 Endpoint 与状态分层可能增加对象和一次查找。新方案把两者合并。

### 直接在 Ring 暴露读写 API

这最接近 BQLog SISO，可能少一个父指针读取，但无法通过类型限制线程角色。保留为 M3 benchmark 对照，不作为当前默认 API。

### 长期 Handle 不返回单次对象

这会把 tentative layout 和 active 状态留在长期可别名对象中。异常或 early return 不能在当前作用域自动 abort/abandon，拒绝采用。

### 使用 BQLog MISO union block

它解决多生产者槽位发布，不是 SPSC 问题；会增加状态、缓存写和 C++ 对象生命周期复杂度，拒绝采用。

## 后果

优点：

- 符合用户偏好的长期 Write/Read Handle。
- 角色私有状态与 API 入口成为同一缓存行对象。
- 保留单次借用的 RAII 和精确生命周期。
- 没有逐 Block 状态或额外原子操作。

代价：

- 术语不如 Producer/Consumer 标准，需要文档明确 Handle 是长期角色入口。
- Handle 保存 Ring 指针；是否值得改成直接状态指针必须由 benchmark 判断。
- 相比直接 Ring API，多一个对象边界，但可内联。

若后续基准证明直接 Ring API 在公平条件下稳定领先，并且差异足以影响目标指标，可以新增 ADR 替换本决策。
