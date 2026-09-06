# ADR-004：短期 Handle 与 Ring 私有读写状态

- 状态：部分被 ADR-005 取代，保留为历史决策
- 日期：2026-08-28
- 最后修订：2026-09-02
- 取代：ADR-003
- 被取代部分：Handle 保存 owner/next cursor、只可移动、RAII 析构终结、Handle 成员 `commit/abort/consume/abandon`、终结后自动失效
- 影响范围：QLog V1 `SpscRingBuffer` 对象模型与命名
- 不改变：ADR-002 Frame Geometry、四缓存行布局、`drop_new` 与 acquire/release 协议

> 当前 Handle 所有权、字段布局和终结接口只以
> [ADR-005：被动且平凡可复制的 SPSC Handle](./ADR-005-passive-spsc-handles.md)
> 为准。本文关于 Ring 私有 `WriterState` / `ReaderState`、`CursorSet`、固定 Storage、
> 单次 pending 和 Frame 职责分层的结论仍然有效；其余 Handle 生命周期描述仅用于
> 解释演进过程，不再指导实现。

## 背景

BQLog 的普通 `log_buffer_write_handle` / `log_buffer_read_handle` 是每次
alloc/read 按值返回的短期描述符。长期热状态实际保存在 SISO Ring 的本侧游标、
对端游标缓存和共享发布游标中。Quill 同样由长期 Queue 保存游标，并为单次写入
返回小型 `WriteReservation`。因此，QLog 原来的长期
`SpscWriteHandle` / `SpscReadHandle` 命名会把“长期端点状态”和“单次借用”混在一起。

## 决策

### 长期状态

`SpscRingBuffer` 私有内嵌：

- `WriterState`：`current_write_cursor_`、`cached_read_cursor_`、
  `reservation_pending_`；
- `ReaderState`：`current_read_cursor_`、`cached_write_cursor_`、批量回收计数、
  `read_pending_`；
- `CursorSet`：分别占一条缓存行的原子 `write_cursor_` / `read_cursor_`。

`WriterState` 和 `ReaderState` 各占一条 64B 缓存行，`CursorSet` 占两条，仍形成
四条相互隔离的热缓存行。State 不保存 Ring 父指针，也不公开 accessor。

### 短期对象

Ring 直接提供：

```cpp
[[nodiscard]] WriteHandle try_reserve(std::size_t exact_payload_bytes) noexcept;
[[nodiscard]] ReadHandle try_peek() noexcept;
void publish_reclaimed() noexcept;
```

每次写入返回 `WriteHandle`，每次读取返回 `ReadHandle`。两个 Handle：

- 按值返回，不分配、不拥有 payload；
- 不可复制，可 `noexcept` 移动；
- owner 为空表示 inactive；
- 析构分别默认 `abort()` / `abandon()`；
- 终结后立即失效。

`WriteHandle` 保存本次 owner、连续 payload 地址、预计算的
`next_write_cursor_`、payload 长度和 `ReserveStatus`；`commit()` / `abort()`
属于该对象。

`ReadHandle` 保存本次 owner、连续只读 payload 地址、预计算的
`next_read_cursor_`、payload 长度和 `ReadStatus`；`consume()` / `abandon()`
属于该对象。

`abort` / `abandon` 是终结操作，不是结果状态。Ring 每一侧最多只有一个
未终结的短期 Handle。

### 地址与游标

Geometry 继续只返回环内 offset。运行时地址统一由：

```text
header = storage_base + layout.header_offset
payload = storage_base + layout.payload_offset
next_cursor = current_cursor + layout.frame_bytes
```

得到。逻辑游标不做 mask；只有从逻辑游标映射 Storage offset 时使用
`capacity_mask`。

空间准入必须最终使用跨尾后的 `layout.frame_bytes`。实现可以实验“最小跨度
提前失败 + 完整布局检查”，但第一次粗判只能拒绝，不能据此接受记录；该顺序由
Release 汇编和微基准决定，不在本 ADR 中提前宣称更快。

### FrameHeader、FrameLayout 与 Handle 的单一职责

- `FrameHeader` 是 Ring 内的持久元数据，随记录保留到 Consumer 完成读取；
- `FrameLayout` 是 `try_reserve()` / `try_peek()` 的函数局部计算结果，函数返回前
  即可销毁；
- `WriteHandle` / `ReadHandle` 是一次未终结操作的短期借用令牌。

`FrameHeader::frame_bytes` 与 `FrameLayout::frame_bytes` 数值相同，但不是两份
持久状态：前者跨线程留在 Ring 中，后者通常只存在于寄存器。Header 中继续同时
保存：

```text
frame_bytes   = 逻辑游标推进距离，包含 Header、payload 对齐和跨尾浪费
payload_bytes = 有效 payload 长度，不包含对齐与尾端浪费
```

Header 固定为 8B；删除其中一个 `uint32_t` 不会减少一个 8B Frame 对齐单元，
反而会迫使 Consumer 重算推进距离。因此 V1 保留两个字段。

`WriteHandle` 不保存 `FrameHeader` 或完整 `FrameLayout`，其 V1 字段冻结为：

```cpp
SpscRingBuffer* ring_{};
std::byte* payload_{};
std::uint64_t next_write_cursor_{};
std::uint32_t payload_bytes_{};
ReserveStatus status_{ReserveStatus::full};
```

目标大小不超过 32B。保存预计算的 `next_write_cursor_`，使 `commit()` 不必重新
读取 Ring Header 或重新计算 Geometry。BQLog 风格的 24B、commit 回读 Header
方案只保留为后续微基准对照，不作为 V1 默认实现。

### 固定 Storage 与 WriteHandle 地址绑定

`ColdState::storage_` 唯一拥有固定容量的原始字节区。Ring 构造完成后不扩容，
`SpscRingBuffer` 也不可移动，因此在 Ring 生命周期内：

```cpp
std::byte* const storage_base = cold_state_.storage_.get();
```

保持稳定。Storage 不预先保存任何“当前写地址”；每次 `try_reserve()` 根据当前
逻辑写游标和本次 payload 长度计算 offset，再绑定真实地址：

```cpp
const FrameLayout layout = compute_frame_layout(
    writer_state_.current_write_cursor_,
    exact_payload_bytes,
    cold_state_.config_.capacity_bytes);

std::byte* const header_address =
    storage_base + layout.header_offset;
std::byte* const payload_address =
    storage_base + layout.payload_offset;
```

只有 offset 映射到 Storage 时才涉及物理环位置；`next_write_cursor_` 继续保存
单调逻辑值：

```cpp
const std::uint64_t next_write_cursor =
    writer_state_.current_write_cursor_ + layout.frame_bytes;
```

空间确认后，Producer 先把尚未发布的 Header 写入 Ring：

```cpp
const FrameHeader header{
    static_cast<std::uint32_t>(layout.frame_bytes),
    static_cast<std::uint32_t>(exact_payload_bytes),
};

std::memcpy(header_address, &header, sizeof(header));
```

随后由 `SpscRingBuffer` 作为 `WriteHandle` 的 friend 填入：

```cpp
write_handle.ring_ = this;
write_handle.payload_ = payload_address;
write_handle.next_write_cursor_ = next_write_cursor;
write_handle.payload_bytes_ =
    static_cast<std::uint32_t>(exact_payload_bytes);
write_handle.status_ = ReserveStatus::ok;
writer_state_.reservation_pending_ = true;
```

这里 `payload_bytes_` 必须是 `exact_payload_bytes`，不能是
`layout.frame_bytes`。V1 不在 Ring 基础接口中构造 `std::span`；Handle 通过内联
`data()` / `size()` 访问连续 payload：

```cpp
[[nodiscard]] explicit operator bool() const noexcept {
    return ring_ != nullptr;
}

[[nodiscard]] ReserveStatus status() const noexcept {
    return status_;
}

[[nodiscard]] std::byte* data() noexcept {
    return payload_;
}

[[nodiscard]] std::uint32_t size() const noexcept {
    return payload_bytes_;
}
```

`ReadHandle` 提供同名查询接口，但 `data()` 返回 `const std::byte*`，`status()` 返回
`ReadStatus`。`payload_` / `payload_bytes_` 继续保持 private，`deactivate()` 也必须是
private；外部只能通过 `commit()` / `abort()` 或 `consume()` / `abandon()` 终结操作。
选择裸指针加长度，是为了让 Ring API 保持底层并便于上层直接编码；这不是在宣称
`std::span` 本身一定更慢。

失败 Handle 保持 `ring_ == nullptr`、`payload_ == nullptr`，只设置对应 status；
失败路径不得设置 pending、写 Header 或推进任何游标。

调用者必须在 reserve 成功后、commit 前完成 payload 写入：

```cpp
auto handle = ring.try_reserve(exact_payload_bytes);
if (!handle) {
    return false;
}

encode_or_memcpy(handle.data(), handle.size());
handle.commit();
```

`try_reserve()` 只负责空间准入、Geometry、写入 Ring 私有 `FrameHeader` 并返回目标地址；
它不接收也不复制用户 payload。已有连续字节时，上层执行一次 `memcpy` 进入 Ring；结构化日志
应精确计算长度后直接编码到该地址，避免“中间完整 Record + 第二次 memcpy”。`commit()` 不复制
payload，只推进本地写游标并通过 release-store 发布。

必须区分两层 Header：

```text
[ Ring 私有 FrameHeader ][ payload: RecordHeader | UTF-8 format | padding | tagged arguments ]
```

`FrameHeader` 由 Ring 在 reserve 时写；`RecordHeader` 和自包含 payload 由 ADR-007 冻结，属于
Ring payload，并由日志编码层在
commit 前写。WriteHandle 的地址仅在 `commit()` / `abort()` 前有效，ReadHandle 的地址仅在
`consume()` / `abandon()` 前有效。

`commit()` 使用 `next_write_cursor_` 更新 WriterState，随后对共享写游标执行一次
release-store，再清除 pending 并使 Handle inactive。`abort()` 只清除 pending
并使 Handle inactive；reserve 阶段留下的旧 Header 没有被发布，对 Consumer
不可见，也不需要清零。

## 理由

- Handle 生命周期与 BQLog 的普通 handle 语义一致，名称不再误导。
- 删除长期 endpoint 到 Ring 的父指针依赖，Ring 直接访问本侧热状态。
- 短期 Handle 明确承载 pending 操作、预计算 next cursor 和 RAII 终结语义。
- 四缓存行结构、单生产者/单消费者所有权和内存序协议不变。
- 后续 `ThreadLogger` / Channel 才是线程长期入口，TLS 不缓存短期 Handle。

## 后果

- 删除长期 Handle accessor 及相关类型测试。
- 布局测试改为验证 `WriterState -> write_cursor -> read_cursor -> ReaderState`。
- ADR-003 和旧阶段指南保留为历史记录，但不再指导实现。
- QLog 仍需通过 Release 汇编和公平基准验证短期 RAII Handle 是否被标量替换，
  不能仅凭类型结构宣称零成本。
