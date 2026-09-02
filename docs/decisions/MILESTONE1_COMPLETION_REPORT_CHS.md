# QLog 里程碑一完成报告：SPSC RingBuffer

- 状态：本地开发完成
- 完成日期：2026-09-02
- 范围：V1 每线程 SPSC Channel 的底层 RingBuffer
- 后续入口：[里程碑二设计讨论指南](./MILESTONE2_DESIGN_GUIDE_CHS.md)

## 1. 完成定义

里程碑一交付一个固定容量、无锁、单生产者/单消费者的字节 RingBuffer。它只负责
连续 payload 的预留、发布、读取和回收，不负责日志字段定义、参数编码、格式化、文件 I/O
或线程注册。

本里程碑“本地开发完成”不等于已经发布正式性能结论。原生 Linux TSan 与 clean commit
完整重复性能矩阵仍是发布门禁。

## 2. 最终公开操作

```cpp
[[nodiscard]] WriteHandle try_reserve(std::size_t payload_bytes) noexcept;
void commit(const WriteHandle& handle) noexcept;
void abort(const WriteHandle& handle) noexcept;

[[nodiscard]] ReadHandle try_read() noexcept;
void release(const ReadHandle& handle) noexcept;
void abandon(const ReadHandle& handle) noexcept;

void publish_reclaimed() noexcept;
```

正常热路径固定为：

```text
Producer: try_reserve -> 直接写 payload -> commit
Consumer: try_read    -> 读取 payload   -> release
```

`abort/abandon` 是冷路径。Handle 为 16B 被动令牌，可平凡复制和析构，不保存 Ring owner，
不通过析构函数隐式改变 Ring 状态。

## 3. 已冻结不变量

- Capacity 为 2 的幂；Frame 以 8B 对齐。
- Ring 内部 `FrameHeader` 为 8B，记录 `frame_bytes` 与 `payload_bytes`。
- Header 留在物理尾端；尾部不足时 payload 连续放在物理头部，tail waste 计入当前 Frame。
- `try_reserve()` 使用精确 payload 长度，只写内部 FrameHeader 并返回目标地址。
- 调用者必须在 `commit()` 前把完整 payload 写入 Ring；commit 不复制 payload。
- 每侧最多一个成功且尚未终结的 Handle；pending 契约在 Debug/Release 均生效。
- `commit()` 通过 write cursor 的 release-store 发布记录；Consumer 通过 acquire-load 获得
  Header 和 payload 可见性。
- Consumer 处理完成后才推进本地读游标；共享 read cursor 按记录数、字节数或快照排空
  条件批量发布。
- Producer 热路径不使用锁、CAS、`fetch_add`、逐 Block 原子、动态分配或等待。
- 完整 Header/Geometry 损坏检查只在 Debug；Release 保留避免协议误用的 pending 检查。

## 4. 内存与所有权布局

- `ColdState`：配置与固定 Storage owner。
- `WriterState`：Producer 私有当前写游标、缓存读游标和 reservation pending。
- `CursorSet::write_cursor_`：独占一条缓存行的跨线程发布游标。
- `CursorSet::read_cursor_`：独占一条缓存行的跨线程回收游标。
- `ReaderState`：Consumer 私有当前读游标、缓存写游标、批量回收预算和 read pending。
- 两个 Handle 均固定为 16B。

## 5. 最终本地门禁

2026-09-02 的最终结果：

- clang-format：通过；
- `git diff --check`：通过；
- Debug：47/47 通过；
- Release：46 项通过，故意损坏 Frame 的 Debug-only 测试预期跳过 1 项；
- ASan/UBSan：47/47 通过；
- Release 汇编：`try_read()` 194B，`release()` 138B；pending 位于 `0x118` 的比较、
  置位和清零均存在，完整 Geometry 校验未进入 Release；
- quick benchmark：QLog/BQLog 共 10 个样本全部 `valid=true`，无 unexpected 或
  validation failure；非零 dropped 只来自设计中的 `drop_new` 场景。

quick benchmark 只有一轮，只是 harness 与数量统计烟雾测试，不能作为性能排名依据。

## 6. 被否决的实验

R1 曾尝试让 `read_pending_` 只在 Debug 生效。Release 汇编确实缩短，但同场交替纯读
before/after 仅从 50.13M/s 变为 50.51M/s，改善 0.77%，低于预设 3% 且落在 MAD
噪声内，因此已回退。R2 不再执行。

详细证据见 [读侧 R1 实验记录](./NEXT_IMPLEMENTATION_GUIDE_CHS.md)。

## 7. 尚未完成的发布门禁

- 在原生 Linux 或 CI 运行 TSan；WSL2 当前因地址空间映射兼容问题不能提供有效结果。
- 在 clean QLog/BQLog commit 上运行完整 7 次重复矩阵。
- 固定编译器、CPU affinity、容量、payload、计时边界，并报告中位数与 MAD。
- 所有比较继续检查 `attempted == accepted + dropped`；排空后检查
  `accepted == consumed/processed`。

这些事项可以与里程碑二的协议设计并行，但在 README 对外声明正式性能数据前必须完成。

## 8. 关键文件

- `include/qlog/detail/ring_geometry.hpp`
- `include/qlog/detail/spsc_ring_buffer.hpp`
- `src/spsc_ring_buffer.cpp`
- `tests/spsc_ring_buffer_operation_test.cpp`
- `tests/spsc_ring_buffer_payload_test.cpp`
- `tests/spsc_ring_buffer_wrap_concurrency_test.cpp`
- `benchmarks/README_CHS.md`

