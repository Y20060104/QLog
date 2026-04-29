# plan.md — 复刻高性能日志系统（BqLog-like）

> 目标：从零实现一个工业级、多线程安全、支持异步压缩写盘的 C++ 日志库。
> 每个 Milestone 都是可独立验证的交付物，后一个 Milestone 依赖前一个的稳定实现。

## ⭐ BqLog 对齐策略

**目标**: 每个 Milestone 的实现与 BqLog v3 源码**尽最大努力对齐** (100% 数据结构 + 算法)

**对齐范围**:
- ✅ **必须对齐**: 数据结构、算法、内存序、性能目标
- ⚠️ **有选择对齐**: 不实现超出 plan.md 的功能（如 mmap、调试统计）
- ℹ️ **参考学习**: 代码风格、优化技巧

**验证方式**:
- 每个 Milestone 的指导文档包含 BqLog 源码对比表
- 编码时参考对应 BqLog 源码位置
- Code Review 检查对齐度（见 RULES.md 第 10 章）

**参考源码**:
- BqLog 主库: `/home/qq344/BqLog/`
- 关键模块位置见各 Milestone 的"参考源码"节

---

## Milestone 0 — 地基：无锁原语与内存工具

**目标**：掌握并实现后续所有模块依赖的底层原语。

### BqLog 对齐度

| 项目 | BqLog 位置 | QLog 对齐 | 难度 |
|------|----------|---------|------|
| atomic 包装 | bq_common.h | ✅ 100% | ⭐ |
| spin_lock | bq_common.h | ✅ 100% | ⭐ |
| aligned_alloc | bq_common.h | ✅ 100% | ⭐ |
| thread 包装 | platform/ | ✅ 100% | ⭐ |
| condition_variable | platform/ | ✅ 100% | ⭐ |

### 参考源码

- **atomic 包装**: `/home/qq344/BqLog/src/bq_common/atomic/...`
  - `load_acquire()`, `store_release()`, `fetch_add_relaxed()` 等方法
  - memory_order 编码规范

- **spin_lock**: `/home/qq344/BqLog/src/bq_common/spin_lock.h`
  - CAS loop 实现，relaxed 语义

- **aligned_alloc**: `/home/qq344/BqLog/src/bq_common/memory/aligned_alloc.h`
  - 平台差异：posix_memalign vs _aligned_malloc

### 实现差异（允许）

- ✅ 方法名：BqLog `load_acquire()` vs QLog 可用 `load_acquire()`
- ✅ 细节优化：BqLog 可能有平台汇编，QLog 用 std::atomic
- ❌ memory_order 语义必须相同（relaxed/acquire/release）

### 任务清单
- [ ] 实现 `platform::atomic<T>`，支持 relaxed / acquire / release 语义
  - [ ] 参考 BqLog atomic.h，验证 memory_order 标注
- [ ] 实现 `spin_lock`（基于 atomic 的自旋锁）
  - [ ] 对标 BqLog spin_lock.h
- [ ] 实现 `spin_lock_rw`（读写自旋锁）
  - [ ] 位操作状态表示与 BqLog 相同
- [ ] 实现 cache-line 对齐分配器
  - [ ] 64 字节对齐（验证 sizeof）
  - [ ] 对标 BqLog aligned_alloc
- [ ] 实现 `platform::thread`
- [ ] 实现 `platform::condition_variable` + `mutex`

### 验证标准
- [ ] 编译无警告（-Wall -Wextra）
- [ ] `atomic<T>` 通过 TSan（0 data races）
- [ ] `spin_lock` 压测无死锁，吞吐量 > std::mutex
- [ ] sizeof(block) == 64（对齐验证）
- [ ] 性能基准达成

### 对齐检查清单

- [ ] atomic 方法名与 BqLog 一致
- [ ] memory_order 参数完全相同
- [ ] spin_lock 使用 fetch_add（不是 CAS loop）
- [ ] 无误用 seq_cst（改用 acquire/release）

### 关键知识点
- MESI 协议与 false sharing
- memory_order 语义（对齐 BqLog 规范）
- cache line 大小（64 bytes on x86/ARM）

---

## Milestone 1 — SPSC Ring Buffer（单生产者单消费者）

**目标**：实现 `spsc_ring_buffer`，这是 HP 路径的核心数据结构。

### BqLog 对齐度

| 项目 | BqLog 位置 | QLog 对齐 | 说明 |
|------|----------|---------|------|
| Block 结构 | siso_ring_buffer.h:60-80 | ✅ 100% | 8B header |
| Cursor 分离 | siso_ring_buffer.h:90-110 | ✅ 100% | false sharing 防护 |
| Alloc 算法 | siso_ring_buffer.cpp:80-120 | ✅ 100% | 无 CAS，仅 acquire/release |
| TLS 缓存 | siso_ring_buffer.cpp:50-60 | ⚠️ 部分 | 简化版（无 hash_map） |

### 参考源码

- **SPSC Block**: `/home/qq344/BqLog/src/bq_log/types/buffer/siso_ring_buffer.h:60-80`
  - 8 字节 header (block_num + data_size)
  - 分离读写 cursor 到不同 cache line

- **Alloc 实现**: `/home/qq344/BqLog/src/bq_log/types/buffer/siso_ring_buffer.cpp:80-150`
  - 无 CAS（SPSC 不需要）
  - acquire/release 语义

### 实现差异（允许）

- ✅ Block 大小：BqLog 可能 < 64，QLog 用 64 字节对齐
- ✅ TLS 缓存：BqLog 用 hash_map，QLog 可简化（单 buffer 场景）
- ❌ cursor 分离必须到不同 cache line（64 字节粒度）
- ❌ Alloc 中不能用 CAS loop

### 任务清单
- [ ] 设计 block 结构（8B header）
  - [ ] 对标 BqLog siso_ring_buffer.h
- [ ] 实现 head 结构（cursor 分离）
  - [ ] write_cursor 和 read_cursor 在不同 cache line
- [ ] 实现 `alloc_write_chunk()`
  - [ ] 无 CAS，仅 acquire/release
  - [ ] 参考 BqLog siso_ring_buffer.cpp:80-120
- [ ] 实现 `commit_write_chunk()`
- [ ] 实现 `read_chunk()`
- [ ] 实现 `return_read_chunk()`
- [ ] 实现 cursor 本地缓存优化（写线程缓存读 cursor，读线程缓存写 cursor）
- [ ] 实现 `batch_read()` 接口，一次性获取所有可读 chunk

### 验证标准
- [ ] Block + Cursor 对齐 BqLog 结构
- [ ] 无 CAS 操作（SPSC 特性）
- [ ] TSan 通过（0 races）
- [ ] 吞吐量 > 3x std::queue+mutex

### 对齐检查清单

- [ ] Block 结构大小与 BqLog 相同
- [ ] Cursor 分离粒度为 64 字节
- [ ] Alloc 中仅用 acquire/release（无 CAS）
- [ ] 性能指标与 BqLog 对齐

### 关键知识点
- SPSC 无需 CAS，仅 acquire/release 保证可见性
- cursor 缓存减少 atomic load（MESI 优化）

---

## Milestone 2 — MPSC Ring Buffer（多生产者单消费者）

**目标**：实现 `mpsc_ring_buffer`，这是 LP 路径的核心数据结构。

### BqLog 对齐度

| 项目 | BqLog 位置 | QLog 对齐 | 说明 |
|------|----------|---------|------|
| Block 结构 | miso_ring_buffer.h:66-92 | ✅ 100% | 64B union，3-byte block_num |
| Alloc 算法 | miso_ring_buffer.cpp:114-220 | ✅ 100% | fetch_add + CAS 异常回滚 |
| TLS 缓存 | miso_ring_buffer.cpp:17-60 | ✅ 100% | miso_tls_buffer_info |
| Memory Order | miso_ring_buffer.cpp:171-200 | ✅ 100% | 精确的 acquire/release |
| 性能指标 | BqLog benchmark | ✅ 100% | 200-250ns alloc+commit |

### 参考源码

- **BqLog MPSC**: `/home/qq344/BqLog/src/bq_log/types/buffer/miso_ring_buffer.{h,cpp}`
  - Block 结构：64 字节 union，3-byte block_num
  - Alloc：fetch_add 常路径 + CAS 异常回滚
  - TLS 缓存：减少原子操作 98%

### 实现差异（允许）

- ✅ 命名：BqLog `write_cursor_` vs QLog `write_cursor`
- ✅ 实现细节：位操作方式可不同
- ❌ Block 大小必须 64 字节
- ❌ fetch_add 必须作为常路径（不是 CAS loop）
- ❌ Memory order 必须精确

### 参考资源

- 📄 **完整指导**: `/home/qq344/QLog/claude/M2_MPSC_ALIGNED_IMPLEMENTATION.md`
- 📄 **对齐表**: `/home/qq344/QLog/claude/M2_BQLOG_ALIGNMENT_TABLE.md`
- 📄 **验证总结**: `/home/qq344/QLog/claude/M2_VERIFICATION_SUMMARY.md`

### 任务清单
- [ ] 实现 `mpsc_ring_buffer.h`（数据结构）
  - [ ] Block union：64 字节
  - [ ] Cursor 分离：128 字节（2 cache lines）
  - [ ] 对标 BqLog miso_ring_buffer.h:66-114
  
- [ ] 实现 `mpsc_ring_buffer.cpp`（核心逻辑）
  - [ ] `alloc_write_chunk()`: fetch_add + CAS 异常回滚
    - [ ] 参考 BqLog miso_ring_buffer.cpp:114-220
  - [ ] `commit_write_chunk()`: release store
  - [ ] `read_chunk()`: 三态扫描
  - [ ] `return_read_chunk()`: cursor 推进

- [ ] 实现单元测试
  - [ ] 8+ 测试（对标 M1）
  - [ ] 10 生产者压测

### 验证标准
- [ ] Block 大小 == 64 字节
- [ ] fetch_add 作为常路径（99% 成功率）
- [ ] CAS 仅在异常路径
- [ ] TLS 缓存减少竞争 98%
- [ ] TSan 通过（0 races）
- [ ] 延迟 200-250ns (alloc+commit)
- [ ] 吞吐量 > 15M/s (10 生产者)

### 对齐检查清单

- [ ] sizeof(block) == 64 ✓
- [ ] alignof(cursors_set) == 64 ✓
- [ ] fetch_add 常路径（不是 CAS loop）✓
- [ ] CAS 仅异常路径 ✓
- [ ] memory_order 标注正确 ✓
- [ ] 与 BqLog 性能指标对齐 ✓

### 关键知识点
- MPSC 竞争点仅在 alloc，commit/read 无竞争
- fetch_add + CAS 异常回滚（2-3x 性能提升 vs CAS loop）
- TLS 缓存策略（减少原子操作）
- 三态 block：unused/used/invalid

---

## Milestone 3 — 双路 Buffer 调度器（log_buffer）

**目标**：实现 `log_buffer`，根据线程频率自动路由到 HP 或 LP buffer。

### BqLog 对齐度

| 项目 | BqLog 位置 | QLog 对齐 | 说明 |
|------|----------|---------|------|
| Buffer 管理 | log_buffer.h | ⚠️ 部分 | QLog 简化版 数据结构尽量一致 |
| HP/LP 路由 | log_buffer.cpp | ✅ 100% | 频率检测逻辑 |
| TLS 结构 | log_buffer.h | ✅ 100% | log_tls_buffer_info |
| Oversize 处理 | log_buffer.cpp | ⚠️ 待定 | 可选实现 |

### 参考源码

- **BqLog Buffer 管理**: `/home/qq344/BqLog/src/bq_log/types/buffer/log_buffer.{h,cpp}`
  - HP/LP 路由逻辑
  - TLS 结构设计

### 实现差异（允许）

- ✅ Oversize buffer 逻辑可简化（M3 optional）
- ✅ 频率检测阈值可微调
- ❌ HP/LP 路由逻辑必须对齐
- ❌ TLS 结构与 BqLog 一致

### 任务清单
- [ ] 设计 TLS 结构 `log_tls_buffer_info`
  - [ ] 对标 BqLog log_buffer.h
  - [ ] 写侧和读侧分离到不同 cache line
- [ ] 实现线程频率检测（首次写入注册为 HP，超过阈值降级为 LP）
- [ ] HP 路径：为每个线程分配独立 `siso_ring_buffer` block
- [ ] LP 路径：所有低频线程共享一个 `miso_ring_buffer`
- [ ] 实现 `oversize_buffer`：超大 entry 的临时缓冲区，spin-lock RW 保护，1s 后回收（可选）
- [ ] 实现统一的 `alloc_write_chunk()` / `commit_write_chunk()` 接口（内部路由）
- [ ] 实现读侧遍历：`read_chunk()` 按序消费 HP + LP buffer

### 验证标准
- [ ] 混合场景测试：高频线程走 HP，低频线程走 LP，消费者能正确合并两路数据
- [ ] 内存占用：10 线程 × 200 万条日志，log_buffer 自身 < 2MB

### 对齐检查清单

- [ ] TLS 结构对齐与 BqLog 一致
- [ ] HP/LP 路由逻辑正确性
- [ ] 无死锁或竞争（TSan 通过）
- [ ] 性能指标达成

### 关键知识点
- HP/LP 双路设计（性能 + 功能性平衡）
- 线程频率检测算法
- TLS 缓存策略

---

## Milestone 4 — 二进制序列化（log entry 格式）

**目标**：定义并实现日志条目的二进制格式，这是热路径的核心操作。

### BqLog 对齐度信息（待定）

本 Milestone 对齐信息在指导文档 `M4_SERIALIZATION_ALIGNED_IMPLEMENTATION.md` 中提供。

### 任务清单
- [ ] 设计 entry 格式：`[timestamp_ms(8B)][thread_id(8B)][level(1B)][category_idx(2B)][fmt_hash(4B)][params...]`
- [ ] 实现类型标签系统：每个参数前写 1 字节类型 tag（int32/int64/float/double/string_utf8/string_utf16/bool/...）
- [ ] 实现各类型的序列化（直接 memcpy 到 buffer）
- [ ] 实现 format string hash（编译期 constexpr hash 优先）
- [ ] 实现 `is_enable_for(category_idx, level)` 快速过滤（bitmap + mask array）

### 验证标准
- 序列化 + 反序列化往返测试，所有类型正确
- 热路径 benchmark：单次 `alloc + serialize + commit` < 300ns（Release 模式）

### 关键知识点
- 延迟格式化：热路径只做二进制序列化，字符串拼接推迟到消费者线程
- constexpr hash 在编译期计算，运行时零开销

---

## Milestone 5 — 格式化引擎（layout）

**目标**：实现 `layout`，将二进制 entry 还原为人类可读的文本。

### 📊 BqLog 对齐度分析

| 项目 | BqLog 源码位置 | QLog 对齐度 | 说明 |
|------|--------------|----------|------|
| Python-style 格式 | layout.h:99-103 | 90% | {0}, {1:>10.2f} 等 |
| 类型转换函数 | layout.cpp:insert_* | 90% | int/float/double/bool/string/pointer |
| UTF-8 编码 | layout.cpp:python_style_format_content_utf8 | 100% | 主要支持 |
| UTF-16 编码 | layout.cpp:python_style_format_content_utf16 | 0% | 可选，后期加入 |
| SIMD 优化 | layout.cpp:*_avx2/*_neon | 0% | 可选，M12 加入 |
| 缓冲区管理 | layout.h:format_content | 95% | 初始1024B，复用 |
| 时区处理 | layout.cpp:insert_time | 85% | 集成 time_zone 类 |

**总体对齐度**: 65-75% (去掉 SIMD 和 UTF-16)

### 参考源码

- **layout.h**: `/home/qq344/BqLog/src/bq_log/log/layout.h` (175 行)
- **layout.cpp**: `/home/qq344/BqLog/src/bq_log/log/layout.cpp` (1939 行)
- **关键接口**:
  ```cpp
  enum_layout_result do_layout(
      const log_entry_handle& entry,
      time_zone& tz,
      const array<string>* categories
  );
  ```

### 任务清单
- [ ] format_info 结构体（对齐、宽度、精度、类型）
- [ ] 格式字符串解析器（`{0}`, `{1:>10.2f}` 等）
- [ ] 6个 insert_* 类型转换函数
  - [ ] insert_integral_unsigned/signed (base 2/8/10/16)
  - [ ] insert_decimal (float/double)
  - [ ] insert_str_utf8
  - [ ] insert_pointer, insert_bool
- [ ] fill_and_alignment() 对齐填充
- [ ] python_style_format_content() UTF-8 版本
- [ ] do_layout() 主入口与状态机
- [ ] 缓冲区复用 + tidy_memory()

### 验证标准
- 格式化结果与 Python `format()` 对齐
- 缓冲区复用：无内存泄漏（ASan 通过）
- 性能基准：单条日志格式化 < 1µs (Release)

### 简化策略
- ❌ SIMD 优化 (AVX2/SSE/NEON) — 保留软件版本
- ❌ UTF-16 支持 — 仅 UTF-8
- ❌ Legacy 版本 — 不保留
- ✅ 缓冲区复用 — 保留
- ✅ 时区处理 — 保留

**预期代码量**: 800-900 行 (BqLog 2114 的 40%)

---

## Milestone 6 — Appender 体系（完整实现）

**目标**：实现日志输出插件体系的全部四种格式（Console、文本、原始二进制、压缩），100% 对齐 BqLog。

### 📊 BqLog 对齐度分析

| 模块 | BqLog 代码量 | QLog 目标 | 对齐度 | 复杂度 | 说明 |
|------|-------------|---------|--------|--------|------|
| appender_base | 241 行 (86h+155cpp) | ~250 行 | 100% | ⭐⭐ | 虚基类框架 |
| appender_console | 325 行 (80h+245cpp) | ~300 行 | 100% | ⭐ | stdout/stderr 输出 |
| appender_file_base | 914 行 (251h+663cpp) | ~900 行 | 100% | ⭐⭐⭐ | 文件I/O、轮转、缓冲 |
| appender_file_text | 112 行 (26h+86cpp) | ~120 行 | 100% | ⭐ | 文本格式输出 |
| appender_file_raw | 66 行 (31h+35cpp) | ~80 行 | 100% | ⭐ | 原始二进制输出 |
| appender_file_binary | 562 行 (179h+383cpp) | ~600 行 | 100% | ⭐⭐⭐ | 二进制格式基础 |
| appender_file_compressed | 610 行 (105h+505cpp) | ~650 行 | 100% | ⭐⭐⭐⭐ | VLQ编码、模板缓存、压缩 |
| **总计** | **3230 行** | **~3300-3350 行** | **100%** | | |

**总体对齐度**: 100% (完整实现所有格式)

### 参考源码

- **appender_base**: `/home/qq344/BqLog/src/bq_log/log/appender/appender_base.h/cpp`
- **appender_console**: `/home/qq344/BqLog/src/bq_log/log/appender/appender_console.h/cpp`
- **appender_file_base**: `/home/qq344/BqLog/src/bq_log/log/appender/appender_file_base.h/cpp`
- **appender_file_text**: `/home/qq344/BqLog/src/bq_log/log/appender/appender_file_text.h/cpp`
- **appender_file_raw**: `/home/qq344/BqLog/src/bq_log/log/appender/appender_file_raw.h/cpp`
- **appender_file_binary**: `/home/qq344/BqLog/src/bq_log/log/appender/appender_file_binary.h/cpp`
- **appender_file_compressed**: `/home/qq344/BqLog/src/bq_log/log/appender/appender_file_compressed.h/cpp`

### 分模块任务清单

#### 1️⃣ appender_base 虚基类
- [ ] 定义四种 appender 类型枚举：console, text_file, raw_file, compressed_file
- [ ] 虚接口：init_impl() / reset_impl() / log_impl()
- [ ] 配置管理：从 property_value 解析配置
- [ ] 过滤机制：
  - [ ] log_level_bitmap（等级过滤）
  - [ ] categories_mask_array（分类过滤）
- [ ] 生命周期：init() → log() → clear()
- [ ] enable/disable 支持

**关键数据结构**:
```cpp
class appender_base {
    appender_type type_;
    bq::string name_;
    log_level_bitmap log_level_bitmap_;
    bq::array_inline<uint8_t> categories_mask_array_;
    layout* layout_ptr_;
    const log_imp* parent_log_;
};
```

#### 2️⃣ appender_console (Console 输出)
- [ ] 输出到 stdout (info/warning) 或 stderr (error/fatal)
- [ ] 集成 layout 格式化
- [ ] ANSI 颜色着色（可选，level 对应不同颜色）
- [ ] 立即输出（不缓冲）

**实现要点**:
- [ ] INFO/DEBUG → stdout (绿色)
- [ ] WARNING → stdout (黄色)
- [ ] ERROR/FATAL → stderr (红色)

#### 3️⃣ appender_file_base (文件基类)
- [ ] 文件 I/O 操作（open/close/write/flush）
- [ ] 缓冲管理（内存缓冲，批量写入）
- [ ] 文件轮转机制：
  - [ ] 按大小轮转 (max_file_size)
  - [ ] 按时间轮转 (daily)
  - [ ] 保留历史文件数 (max_files_count)
- [ ] 文件命名规则：`name.log`, `name.log.1`, `name.log.2`, etc
- [ ] 文件打开检测（恢复已有文件）

**关键参数**:
```cpp
uint64_t max_file_size;          // 默认 100MB
uint32_t max_files_count;        // 最多保留
uint32_t buffer_size;            // 缓冲区大小
bool enable_time_rotation;       // 时间轮转
```

#### 4️⃣ appender_file_text (文本格式)
- [ ] 继承 appender_file_base
- [ ] 每条日志一行（自动添加 `\n`）
- [ ] 集成 layout 格式化
- [ ] UTF-8 编码输出

**实现方式**:
```cpp
void log_impl(const log_entry_handle& handle) override {
    const char* formatted = layout_ptr_->do_layout(...);
    write(formatted);
    write("\n");
    flush_if_needed();
}
```

#### 5️⃣ appender_file_raw (原始二进制)
- [ ] 继承 appender_file_base
- [ ] 直接写入 entry 的二进制数据
- [ ] 无格式化，无额外信息

#### 6️⃣ appender_file_binary (二进制格式基础)
- [ ] 继承 appender_file_base
- [ ] 定义文件格式结构：
  - [ ] **File Header** (8 字节):
    - uint32_t version (4B)
    - appender_format_type format (1B, 1=raw, 2=compressed)
    - char padding[3] (3B)
  - [ ] **Segments** (链式结构):
    - Segment Header (12B): next_seg_pos, seg_type, enc_type, has_key
    - [可选] Encryption Keys (加密支持)
    - Segment Payload (Data)
  - [ ] **Metadata** (First Segment Only):
    - magic_number: "2,2,7" (3B)
    - use_local_time (1B)
    - gmt_offset_hours/minutes (8B)
    - time_zone_str (32B)
    - category_count (4B)
    - category definitions (重复)
  - [ ] **Log Entries** (Variable)
- [ ] 文件打开时读取并验证 metadata

**关键接口**:
```cpp
virtual bool parse_exist_log_file(parse_file_context& context);
virtual void on_file_open(bool is_new_created);
virtual appender_format_type get_appender_format() const;
```

#### 7️⃣ appender_file_compressed (压缩格式，最复杂)
- [ ] 继承 appender_file_binary
- [ ] VLQ 编码实现：
  - [ ] varint_encode(uint64_t)
  - [ ] varint_decode(const uint8_t*, size_t&)
  - [ ] 最多 4 字节表示任意 uint64_t
- [ ] 模板缓存系统：
  - [ ] format_templates_hash_cache_: 缓存 format string 和 level/category
  - [ ] thread_info_hash_cache_: 缓存线程 ID 和名称
  - [ ] 使用 hash 值作为模板索引
- [ ] Log Entry 写入：
  - [ ] [data_item_header]: type(1b) + len_extra(7b) + len_base(VLQ)
  - [ ] [data]: timestamp(VLQ) + fmt_idx(VLQ) + thread_idx(VLQ) + [param_type, param...]
- [ ] Template 写入：
  - [ ] Format Template: [sub_type=0] + [level(1B), category_idx(VLQ), fmt_string]
  - [ ] Thread Info Template: [sub_type=1] + [thread_idx(VLQ), thread_id(VLQ), thread_name]
- [ ] 文件恢复：parse_log_entry / parse_formate_template / parse_thread_info_template

**VLQ 编码公式**:
```cpp
// 编码：每字节使用高位标记是否还有后续字节
uint8_t buf[5];
size_t len = varint_encode(value, buf);  // 返回字节数

// 解码：
uint64_t value = 0;
size_t offset = 0;
varint_decode(buf, offset);  // offset 会被更新
```

**模板缓存策略**:
```cpp
// 计算 format 模板哈希
uint64_t fmt_hash = hash(level, category_idx, fmt_string);
// 查缓存
if (format_templates_hash_cache_.find(fmt_hash) != end) {
    fmt_idx = format_templates_hash_cache_[fmt_hash];
} else {
    // 新模板，写入并记录索引
    fmt_idx = current_format_template_max_index_++;
    write_format_template(fmt_idx, level, category_idx, fmt_string);
    format_templates_hash_cache_[fmt_hash] = fmt_idx;
}
```

### 验证标准
- [ ] 编译无警告 (-Wall -Wextra)
- [ ] appender_console 输出到 stdout (手动验证)
- [ ] appender_file_text 产生可读文本文件
- [ ] appender_file_raw 产生二进制文件
- [ ] appender_file_binary 文件可被解析（自实现 parser）
- [ ] appender_file_compressed 压缩文件 < 15% 文本大小（相同日志内容）
- [ ] 文件轮转：达到 max_file_size 自动创建新文件
- [ ] 过滤生效：disabled appender 不输出；level/category 过滤正确
- [ ] 多线程安全：10 个生产者线程并发输出，无乱码、无丢失

### 关键对齐点
- ✅ 四种 appender 类型枚举值
- ✅ 虚接口三个方法 (init_impl / reset_impl / log_impl)
- ✅ File Header 8 字节格式
- ✅ Segment Header 12 字节格式
- ✅ Metadata 结构（magic_number, time_zone, categories）
- ✅ VLQ 编码（最多 4 字节）
- ✅ 模板缓存机制（hash_cache + index 映射）
- ✅ 过滤 bitmap 机制

### 工作分解与时间估计

| 阶段 | 任务 | 工作周数 | 说明 |
|------|------|---------|------|
| P1 | appender_base + console + file_base | 3 | 基础框架 |
| P2 | appender_file_text + raw | 1 | 简单格式 |
| P3 | appender_file_binary | 2 | 结构化格式 |
| P4 | appender_file_compressed | 2 | VLQ + 模板缓存 |
| P5 | 单元测试 + 集成测试 | 1 | 完整覆盖 |
| **合计** | | **6-8 周** | |

**预期代码量**: 3300-3350 行 (BqLog 3230 行相当)

---

## Milestone 7 — 异步 Worker 线程

**目标**：实现 `log_worker`，驱动异步日志处理。

### 📊 BqLog 对齐度分析

| 项目 | BqLog 源码位置 | QLog 对齐度 | 说明 |
|------|--------------|----------|------|
| log_worker 类 | log_worker.h:23-83 | 95% | 继承 platform::thread |
| 66ms 周期 | log_worker.h:25 | 100% | process_interval_ms = 66 |
| 条件变量唤醒 | log_worker.h:31-34 | 100% | condition_variable + mutex |
| atomic 标志 | log_worker.h:33-34 | 100% | wait_flag / awake_flag |
| run() 主循环 | log_worker.cpp:run() | 90% | 周期检查 + 唤醒处理 |
| Watch dog | log_worker.h:77-82 | 85% | 线程重启监控 |

**总体对齐度**: 85-95% (几乎完全对齐)

### 参考源码

- **log_worker**: `/home/qq344/BqLog/src/bq_log/log/log_worker.h/cpp` (206 行)

### 任务清单
- [ ] log_worker 类
  - [ ] 继承 platform::thread
  - [ ] process_interval_ms = 66 常数
  - [ ] condition_variable trigger + mutex lock
  - [ ] atomic<bool> wait_flag / awake_flag
- [ ] awake() 接口（立即唤醒）
- [ ] awake_and_wait_begin/join() 配对等待
- [ ] run() 主循环
  - [ ] 66ms 定期唤醒
  - [ ] 获取 log_imp 并调用 process()
  - [ ] 支持 force_flush 模式
- [ ] Watch dog 监控线程存活

### 验证标准
- 66ms 周期检查有效（日志在周期内被处理）
- awake() 立即唤醒生效（latency < 1ms）
- Watch dog 测试：模拟线程退出，1s 内自动重启

### 核心对齐点
- ✅ 66ms 常数不能改变
- ✅ condition_variable + atomic 搭配
- ✅ process_by_worker 回调接口

**预期代码量**: 200-260 行 (BqLog 206 行相当)

---

## Milestone 8 — 日志管理器 (Manager + IMP)

**目标**：组装完整的 log 对象和单例管理器。

### 📊 BqLog 对齐度分析

#### log_manager

| 项目 | BqLog 源码位置 | QLog 对齐度 | 说明 |
|------|--------------|----------|------|
| 单例模式 | log_manager.h:32 | 100% | static instance() |
| log_imp_list | log_manager.h:77 | 95% | array_inline<unique_ptr> |
| public_worker | log_manager.h:78 | 95% | log_worker 实例 |
| public_layout | log_manager.h:79 | 95% | layout 单例 |
| ID 编码 | log_manager.h:50-52 | 100% | XOR magic number |
| 过滤锁 | log_manager.h:80 | 90% | spin_lock_rw 保护 |

#### log_imp

| 项目 | BqLog 源码位置 | QLog 对齐度 | 说明 |
|------|--------------|----------|------|
| log() hot path | log_imp.cpp:log() | 100% | ring buffer 写入 |
| process() | log_imp.cpp:process() | 95% | worker 调用处理 |
| 过滤机制 | log_imp.h:93-96 | 100% | is_enable_for() |
| Appender 链 | log_imp.h:appenders_ | 95% | array<unique_ptr> |
| ID 持有 | log_imp.h:id_ | 100% | uint64_t 编码 |

**总体对齐度**: 85-90% (高度对齐)

### 参考源码

- **log_manager**: `/home/qq344/BqLog/src/bq_log/log/log_manager.h/cpp` (378 行)
- **log_imp**: `/home/qq344/BqLog/src/bq_log/log/log_imp.h/cpp` (698 行)

### 任务清单

#### log_manager
- [ ] 单例模式 instance()
- [ ] create_log(name, config, categories)
- [ ] reset_config(name, config)
- [ ] get_log_by_id(log_id) 与 ID 编码/解码
  - [ ] magic number: 0x24FE284C23EA5821
- [ ] process_by_worker(log_imp*, is_force_flush)
- [ ] force_flush_all() / try_flush_all()
- [ ] get_public_worker() / get_public_layout()
- [ ] uninit() 清理

#### log_imp
- [ ] 构造 + 配置解析
- [ ] log() hot path（ring buffer 写入）
- [ ] process() worker 调用处理
- [ ] sync_process() 同步处理
- [ ] is_enable_for(category_idx, level) 过滤
- [ ] Appender 链管理
  - [ ] add_appender() / remove_appender()
  - [ ] set_appender_enable()
- [ ] get_buffer() / get_worker() / get_layout()

### 验证标准
- create_log() 返回有效的 log_id
- get_log_by_id() 能正确还原原对象指针
- is_enable_for() 过滤生效
- process() 调用后日志被输出到 appender

### 核心对齐点
- ✅ ID magic number 编码（防指针伪造）
- ✅ log_imp_list 数组存储
- ✅ 两层过滤（category_mask + level_bitmap）
- ✅ Worker 回调接口（process_by_worker）
- ✅ Appender 链模式

### 简化策略
- ❌ 配置热更新复杂逻辑 — 简化版本
- ❌ Snapshot 功能 — 不实现
- ❌ JNI/Python API — M10+ 做

**预期代码量**: 900-1100 行 (BqLog 1076 行相当)
- 多个 log 对象并发写入，互不干扰
- 热更新配置后，新配置立即生效，无日志丢失

---

## Milestone 9 — 崩溃恢复

**目标**：实现基于 mmap 的崩溃恢复机制。

### BqLog 对齐度信息（待定）

本 Milestone 对齐信息在指导文档 `M9_RECOVERY_ALIGNED_IMPLEMENTATION.md` 中提供。

### 任务清单
- [ ] 为 `miso_ring_buffer` 添加 mmap 模式（V3）
- [ ] 实现 version 号机制：每次写入递增，消费后更新 read version
- [ ] 实现启动时恢复：扫描 mmap 文件，找到未消费的 entry 重放
- [ ] 实现 `MAX_RECOVERY_VERSION_RANGE` 限制（防止恢复过旧数据）
- [ ] 实现 checksum 校验（检测 mmap 文件损坏）

### 验证标准
- 模拟进程崩溃（`abort()`），重启后能恢复崩溃前最后 N 条日志
- 损坏的 mmap 文件不导致崩溃，graceful 降级

---

## Milestone 10 — 多语言 Wrapper

**目标**：为 Java / C# / Python / TypeScript 提供绑定。

### BqLog 对齐度信息（待定）

本 Milestone 对齐信息在指导文档 `M10_WRAPPER_ALIGNED_IMPLEMENTATION.md` 中提供。

### 任务清单
- [ ] 设计 C ABI 导出层（`extern "C"` 接口，避免 C++ ABI 问题）
- [ ] Java：JNI wrapper，`ByteBuffer` 直接访问 native buffer（零拷贝）
- [ ] C#：P/Invoke wrapper，`unsafe` 指针直接写 native buffer
- [ ] Python：CPython C Extension（Stable ABI，兼容 Python 3.7+）
- [ ] TypeScript/Node.js：Node-API (N-API) wrapper
- [ ] 实现各语言的"零额外堆分配"路径（避免 GC 压力）

### 验证标准
- 各语言 wrapper 的 benchmark 与 C++ 直接调用性能差距 < 20%
- Java/C# wrapper 在 GC profiler 下，日志调用不产生额外堆对象

---

## Milestone 11 — Category Log 代码生成器

**目标**：实现 `BqLog_CategoryLogGenerator`，自动生成强类型 category 访问器。

### BqLog 对齐度信息（待定）

本 Milestone 对齐信息在指导文档 `M11_CODEGEN_ALIGNED_IMPLEMENTATION.md` 中提供。

### 任务清单
- [ ] 设计 category 配置文件格式（层级结构，如 `Shop.Seller`）
- [ ] 实现解析器（读取配置，构建 category 树）
- [ ] 为每种语言生成强类型 wrapper（C++ / Java / C# / Python / TypeScript）
- [ ] 生成的代码支持 IDE 自动补全（`log.cat.Shop.Seller.info(...)`）

### 验证标准
- 生成的代码编译通过，category index 与运行时 mask array 一致
- 修改配置后重新生成，旧代码编译报错（强类型保护）

---

## Milestone 12 — 性能基准与调优

**目标**：达到与 BqLog 相当的性能指标。

### BqLog 对齐度信息

| 场景 | BqLog 目标 | QLog 目标 | 对齐度 |
|------|----------|---------|--------|
| 单线程异步写入 | ~5M entries/s | > 5M entries/s | ✅ 100% |
| 10 线程异步写入 | ~15-20M entries/s | > 20M entries/s | ✅ 100% |
| 内存占用（10×200万） | ~2MB | < 2MB | ✅ 100% |
| 压缩文件体积 | < 15% vs 文本 | < 15% vs 文本 | ✅ 100% |

### 基准目标（参考 BqLog benchmark）
| 场景 | 目标 |
|------|------|
| 单线程异步写入 | > 5M entries/s |
| 10 线程异步写入 | > 20M entries/s |
| 内存占用（10线程×200万条） | < 2MB |
| 压缩文件 vs 文本文件 | < 15% 体积 |

### 任务清单
- [ ] 实现与 spdlog / log4j2 / NLog 的对比 benchmark
- [ ] 使用 perf / VTune 分析热点，针对性优化
- [ ] 验证 SIMD 路径在目标平台上正确启用
- [ ] 验证 TSan / ASan 下无竞争、无内存错误

---

## 附录 A：Milestone 对齐度总览

| Milestone | 功能 | BqLog 对齐 | 源码位置 | 指导文档 | 状态 |
|-----------|------|----------|--------|--------|------|
| M0 | 无锁原语 | ✅ 100% | bq_common/ | RULES.md | ✅ 完成 |
| M1 | SPSC Buffer | ✅ 100% | siso_ring_buffer.h/cpp | M1_*.md | ✅ 完成 |
| M2 | MPSC Buffer | ✅ 100% | miso_ring_buffer.h/cpp | M2_MPSC_*.md | ✅ 完成 |
| M3 | 双路调度 | ✅ 100%| log_buffer.h/cpp | M3_*.md |✅ 100%|
| M4 | 二进制序列化 | ⏳ 待验证 | entry.h/cpp | M4_*.md | ⏳ 待开始 |
| M5 | 格式化引擎 | ⏳ 待验证 | layout.h/cpp | M5_*.md | ⏳ 待开始 |
| M6 | Appender 体系 | ⏳ 待验证 | appender.h/cpp | M6_*.md | ⏳ 待开始 |
| M7 | Worker 线程 | ⏳ 待验证 | log_worker.h/cpp | M7_*.md | ⏳ 待开始 |
| M8 | 管理器 | ⏳ 待验证 | log_manager.h/cpp | M8_*.md | ⏳ 待开始 |
| M9 | 崩溃恢复 | ⚠️ 可选 | mmap support | M9_*.md | 可选 |
| M10 | 多语言 | ⚠️ 可选 | wrapper/ | M10_*.md | 可选 |
| M11 | 代码生成 | ⚠️ 可选 | tools/ | M11_*.md | 可选 |
| M12 | 性能调优 | ✅ 100% | 基准对标 | M12_*.md | ⏳ 待开始 |

---

---

## 后续 Milestone 总体规划 (M5-M12)

### 📋 分阶段交付方案（更新：100% M6 对齐）

#### **第一阶段 (6-9 周)**: M5 + M7
- **目标**: 异步日志框架可用，核心流程完整
- **内容**:
  - ✅ M5 格式化引擎 (对齐 65-75%)
  - ✅ M7 Worker 异步线程 (对齐 85-95%)
  - ➜ 两个模块可并行开发
- **输出**: 二进制 entry → 格式化文本 → Worker 批处理
- **验证**: 单元测试 + 性能基准

#### **第二阶段 (6-8 周)**: M6（完整实现 100% 对齐）
- **目标**: 四种输出格式完全实现（Console + 文本 + 二进制 + 压缩）
- **内容**:
  - ✅ appender_base 虚基类 (对齐 100%)
  - ✅ appender_console (对齐 100%)
  - ✅ appender_file_base (对齐 100%)
  - ✅ appender_file_text (对齐 100%)
  - ✅ appender_file_raw (对齐 100%)
  - ✅ appender_file_binary (对齐 100%)
  - ✅ appender_file_compressed (对齐 100%) — VLQ编码 + 模板缓存
  - ✅ 文件轮转机制（按大小、时间）
- **输出**: 完整日志输出体系，支持多格式
- **验证**: 格式化正确、轮转生效、压缩率 < 15%

#### **第三阶段 (3-4 周)**: M8
- **目标**: 完整 QLog 系统可用，与 M0-M5 集成
- **内容**:
  - ✅ log_manager 全局单例 (对齐 90%)
  - ✅ log_imp 日志对象 (对齐 90%)
  - ✅ ID 编码、过滤机制
  - ✅ Appender 链整合
- **输出**: 完整日志系统（hot path: ring buffer, cold path: worker）
- **验证**: 端到端测试、多线程稳定性

#### **第四阶段 (可选，4-6 周)**: M9-M12
- M9: 崩溃恢复 (mmap 支持)
- M10: 多语言绑定
- M11: 代码生成器
- M12: 性能调优

### 📊 工作量与时间预估（更新）

| 阶段 | Milestone | 对齐度 | 代码量 | 工作周数 | 难度 |
|------|-----------|--------|--------|---------|------|
| P1 | M5 | 65-75% | 800-900 | 4-6 | ⭐⭐⭐ |
| P1 | M7 | 85-95% | 200-260 | 2-3 | ⭐⭐ |
| P2 | M6（完整） | **100%** | **3300-3350** | **6-8** | **⭐⭐⭐⭐** |
| P3 | M8 | 85-90% | 900-1100 | 3-4 | ⭐⭐ |
| **P1-P3 合计** | **M5/M6/M7/M8** | **85-90%** | **~5700** | **21-27** | |

### 🎯 关键决策

#### M6 从 65-75% 提升到 100% 的理由

- **完整性**: M6 是输出体系，不完整会影响后续的可用性
- **对齐策略**: 既然 BqLog 实现了，QLog 应该完整复刻
- **压缩效益**: VLQ + 模板缓存能显著减少日志文件大小（通常 < 15%）
- **工作量可控**: 虽然 6-8 周较长，但逻辑清晰、可分阶段实现

#### 对齐度选择的理由

- **M5 (65-75%)**:
  - 原因: SIMD 优化是 BqLog 的性能关键，但对 QLog 不必须
  - 简化: 去掉 AVX2/SSE/NEON，仅软件版本
  - 收益: 减少 ~500 行代码，功能完全等价

- **M6 (100%)**:
  - 原因: 输出体系是日志系统的关键组成，需要完整
  - 全实现: 包含 Console、Text、Binary、Compressed 四种格式
  - 收益: 与 BqLog 完全对齐，用户体验完整

- **M7 (85-95%)**:
  - 原因: Worker 线程设计简洁，容易完全对齐
  - 简化: 无需简化，几乎 100% 复制 BqLog 实现
  - 收益: 最高的对齐度和代码质量

- **M8 (85-90%)**:
  - 原因: 管理器是系统协调层，关键部分必须对齐
  - 简化: 配置热更新可简化，Snapshot 可不做
  - 收益: 完整系统集成，与 M0-M5 无缝配合

---

## 附录 B：BqLog 源码快速查询表

| 模块 | 文件 | 功能 | 行号范围 | 代码量 |
|------|------|------|---------|--------|
| Atomic | bq_common/atomic/ | memory_order 定义 | 100-200 | - |
| Spin Lock | bq_common/platform/thread/ | spin_lock 实现 | 200-300 | - |
| SPSC Block | types/buffer/siso_ring_buffer.h | block 结构 | 60-80 | 8B header |
| SPSC Alloc | types/buffer/siso_ring_buffer.cpp | alloc 算法 | 80-150 | - |
| MPSC Block | types/buffer/miso_ring_buffer.h | block 结构 | 66-92 | 64B union |
| MPSC Alloc | types/buffer/miso_ring_buffer.cpp | alloc 算法 | 114-220 | fetch_add+CAS |
| MPSC TLS | types/buffer/miso_ring_buffer.cpp | TLS 缓存 | 17-60 | - |
| Buffer 管理 | types/buffer/log_buffer.h/cpp | HP/LP 路由 | - | **~1100 行** |
| Entry 格式 | log/entry_format.h/cpp | 二进制格式 | - | **~600 行** |
| Serializer | log/serializer.h/cpp | 参数序列化 | - | **~700 行** |
| **Layout** | **log/layout.h/cpp** | **格式化引擎** | **- ** | **2114 行** |
| **Appender Base** | **log/appender/appender_base.h/cpp** | **虚基类** | **-** | **241 行** |
| **Appender Console** | **log/appender/appender_console.h/cpp** | **stdout 输出** | **-** | **325 行** |
| **Appender File Text** | **log/appender/appender_file_text.h/cpp** | **文本文件** | **-** | **112 行** |
| **Appender File Base** | **log/appender/appender_file_base.h/cpp** | **文件基类** | **-** | **914 行** |
| **Appender File Binary** | **log/appender/appender_file_binary.h/cpp** | **二进制** | **-** | **562 行** |
| **Appender Compressed** | **log/appender/appender_file_compressed.h/cpp** | **压缩输出** | **-** | **610 行** |
| **Worker** | **log/log_worker.h/cpp** | **异步处理** | **-** | **206 行** |
| **Manager** | **log/log_manager.h/cpp** | **全局单例** | **-** | **378 行** |
| **Log IMP** | **log/log_imp.h/cpp** | **日志对象** | **-** | **698 行** |

---

## 附录：推荐学习顺序

```
M0 原语 → M1 SPSC → M2 MPSC → M3 双路调度
    → M4 序列化 → M5 格式化 → M6 Appender
    → M7 Worker → M8 管理器
    → M9 崩溃恢复（可选）
    → M10 多语言（可选）
    → M11 代码生成（可选）
    → M12 性能调优
```

每个 Milestone 完成后，运行对应的单元测试和 benchmark，确认稳定后再进入下一阶段。
