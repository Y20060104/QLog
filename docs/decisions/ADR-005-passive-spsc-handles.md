# ADR-005：被动且平凡可复制的 SPSC Handle

- 状态：已接受
- 日期：2026-09-02
- 部分取代：ADR-004 的 Handle 所有权、字段布局、RAII 终结与成员接口
- 影响范围：`WriteHandle`、`ReadHandle` 与 `SpscRingBuffer` 两阶段接口
- 不改变：ADR-002 Geometry、固定 Storage、四缓存行布局、单次 pending、
  `drop_new`、发布游标和 acquire/release 协议

## 背景

ADR-004 正确地把长期热状态移入 Ring，但仍让每个短期 Handle 保存 Ring owner 和
下一逻辑游标，并通过自定义移动、析构和成员函数终结操作。这造成读写 Handle
所有权模型不一致，也扩大了写 Handle，并使 Release 热路径产生不必要的生命周期代码。

BQLog SISO 的普通单条读写 Handle 都是按值返回的被动描述符：Ring 负责
alloc/read 与 commit/return，Handle 本身不拥有 Ring，也不在析构中改变队列。
QLog 学习这种职责分层，但保留自己的字节 Geometry、连续回绕 payload、私有字段、
标准原子和批量读游标发布策略。

## 决策

### 1. 对称对象模型

`WriteHandle` 与 `ReadHandle` 都是一次操作的非拥有、被动令牌：

- 不保存 `SpscRingBuffer*`；
- 不保存 `next_write_cursor_` / `next_read_cursor_`；
- 没有自定义移动、赋值或析构；
- 可平凡复制、可平凡析构，固定目标大小为 16B；
- 析构、复制和移动都不得改变 Ring 状态。

两个类型保持独立，避免把 ReadHandle 传给写侧接口；不通过公共基类、继承或
`std::variant` 合并。

### 2. 字段布局

两个 Handle 都使用以下物理形状：

```cpp
payload pointer                  // 8B
std::uint32_t frame_and_status_  // 4B
std::uint32_t payload_bytes_     // 4B
```

成功 Frame 的 `frame_bytes` 按 8B 对齐，低 3 位为零，因此
`frame_and_status_` 的低 3 位复用为失败状态。成功 Handle 保存非零
`frame_bytes`；失败 Handle 保存零 Frame 大小和具体 status。

```cpp
static_assert(sizeof(WriteHandle) == 16);
static_assert(sizeof(ReadHandle) == 16);
static_assert(std::is_trivially_copyable_v<WriteHandle>);
static_assert(std::is_trivially_copyable_v<ReadHandle>);
static_assert(std::is_trivially_destructible_v<WriteHandle>);
static_assert(std::is_trivially_destructible_v<ReadHandle>);
```

`operator bool()` 根据解码后的 `frame_bytes != 0` 判断本次操作是否成功；
零长度 payload 仍至少占用 8B Frame，因此仍为成功。`status()` 读取低 3 位。

### 3. Ring 拥有状态转换

写侧正常路径：

```cpp
auto handle = ring.try_reserve(payload_bytes);
if (!handle) {
    return handle.status();
}

encode_or_memcpy(handle.data(), handle.size());
ring.commit(handle);
```

读侧最终接口：

```cpp
auto handle = ring.try_read();
if (!handle) {
    return handle.status();
}

process(handle.data(), handle.size());
ring.release(handle);
```

正常路径每侧都只有“取得令牌 + Ring 终结”两阶段。冷路径保留：

```cpp
ring.abort(write_handle);    // 不发布写游标
ring.abandon(read_handle);   // 不推进读游标，下次仍读同一条
```

`return` 是 C++ 关键字，因此读侧不使用 `return()`。最终命名已经收口为
`try_read()` / `release()`，生产 API 不保留 `try_peek()` / `consume()` 兼容别名。
benchmark runner 中的 `consume` 只是跨 QLog/BQLog 的适配动作名；QLog adapter 内部转调
`ring.release(handle)`，不属于 Ring 的公开接口。

### 4. commit/release 的推进信息

SPSC 每侧同时最多有一个成功且未终结的操作，因此不需要在 Handle 中保存完整
64 位 next cursor：

```cpp
current_write_cursor_ += write_handle.frame_bytes();
current_read_cursor_ += read_handle.frame_bytes();
```

`try_reserve()` 已经完成空间准入、Geometry 和未发布 FrameHeader 写入；调用者写入
payload 后，`commit()` 只推进本地写游标并对共享写游标执行 release-store。

`try_read()` 从 FrameHeader 得到 payload 与推进距离；`release()` 推进本地读游标，
继续采用累计 32 条、4 KiB、观察到快照耗尽或显式 flush 时发布回收游标的策略。

### 5. 一次性逻辑契约

Handle 在物理上可复制，但成功令牌在逻辑上只能终结一次：

- `commit/release/abort/abandon` 后不得再次终结同一令牌或其副本；
- 终结后不得继续依赖 `data()` 指向内容的生命周期；
- Handle 不会自动变成空对象；`operator bool()` 仍描述原始操作结果，而不是
  “是否仍拥有终结权”；
- 把 Handle 交给错误的 Ring、重复终结或在新操作开始后使用旧副本都违反底层契约。

Debug 写终结路径检查 pending、Frame 大小、Geometry 和 payload 地址是否对应当前
reservation；读取得路径继续校验 Header、Geometry 与可用范围。Release 删除这些完整
校验，只保留失败 Handle 的快速无操作分支；底层 `detail` API 以显式契约换取更短热路径。

如未来公共 API 需要异常安全，可在日志层外包一层 scope guard；不把 RAII owner
重新塞回 Ring 的 16B 基础 Handle。

## 实现状态

- 写侧与读侧均已完成：两个 Handle 都是 16B 被动令牌，Ring 提供
  `try_reserve/commit/abort` 与 `try_read/release/abandon`。
- 旧生产接口 `try_peek/consume` 已删除；测试名和 QLog benchmark adapter 已同步。
- Debug 46/46 通过；Release 45 项通过且损坏帧测试按校验配置跳过；
  ASan/UBSan 46/46 通过。TSan 在 GoogleTest 枚举阶段受 WSL2
  `unexpected memory mapping` 限制，必须在原生 Linux 或 CI 补跑。
- Release 对象中没有 Handle 自定义析构符号；`try_reserve/commit/try_read/release`
  热函数内未生成 `call`、`lock`、`xadd` 或 `cmpxchg`。
- WSL2 的 7 次重复开发矩阵全部满足数量守恒和校验。纯读中位数约为
  QLog 3774 万条/秒、BQLog 4615 万条/秒，下一轮性能分析聚焦读侧；两个仓库均为
  dirty 且环境为 WSL2，因此不作为公开性能结论。
- Ring V1 的接口、对象模型和语义已经冻结；原生 Linux TSan、clean commit 基准和
  读侧差距解释完成前，不宣布性能验收最终冻结。

## 被废弃的旧结论

以下内容不再指导实现：

- Handle 保存 Ring owner，以 owner 是否为空表示 active；
- Handle 保存 64 位 next cursor；
- Handle 只可移动，移动后源对象自动失效；
- `handle.commit()` / `handle.abort()` / `handle.consume()` / `handle.abandon()`；
- 析构自动 abort/abandon；
- 终结后清空 Handle 字段；
- 通过 Handle 自身状态保证重复终结幂等。

ADR-004 其余关于 Ring 私有 State、固定 Storage、Frame/Header/Layout 职责和
每侧单次 pending 的结论继续有效。

## 后果

- 写 Handle 从不超过 32B 收紧到固定 16B，读写 ABI 形状对称。
- Release 中不再生成 Handle 自定义构造、移动、析构和 owner 回调符号。
- 调用点必须显式把 Handle 交回产生它的 Ring。
- 底层接口更快、更接近硬件，但错误使用不再由 RAII 自动修复；Debug 测试和上层
  封装承担易用性检查。
- benchmark 和测试适配器必须持有 Ring，不能再用静态函数只操作 Handle。
