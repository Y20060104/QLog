> 历史草案：内部接口已由实际源码和当前同名实施指南替代，不可直接照抄。

# QLog V1 剩余实现指南：按依赖顺序一次接线

日期：2026-09-19；2026-09-20 输出层实现更新。五项先按推荐接受，随后用户明确更正“flush需要更正和BQLog对齐”；flush 以本文件修正版为准。见 [ADR-017](./ADR-017-v1-output-and-completion.md)。

本文件是**剩余 V1 的唯一编码顺序与接口入口**。ADR-015 保留架构理由，ADR-016 保留正文格式合同；旧 `V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md` 的接口片段由本文件统一替代；单次 flush 持续推进短写、write EINTR 重试，按 BQLog 对齐。撤销暂存草案的 IoBudget/FlushCursor/预算让步方案；“只允许等待锁”由新增 Console 输出锁例外覆盖。这里给出新增接口、字段、控制流、故障处理和验收要求；代码块是待实现的接口或算法片段，不是已合入生产源码的完整后端。

权威仓库 `/home/qq344/QLog`；Windows 入口 `\\wsl.localhost\Ubuntu\home\qq344\QLog`。参考库是 `E:\VisualStudioProject\BqLog`，不能在那里开发 QLog 生产文件。核对基线 HEAD `58b6948c33bb2f4b94db7c3e3d77b228eaf5afd7`，工作区含尚未提交的 formatter、诊断及文档；不要 reset 清理。


> 2026-09-20 当前进度：步骤 1 与步骤 2 的输出代码已实现并接入 CMake（仍未接真实 worker）。该日期下一步为 compose_line，具体代码见 [输出层后续指南](./V1_APPENDER_NEXT_IMPLEMENTATION_GUIDE_20260920_CHS.md)，验证见 [实现报告](./V1_APPENDER_IMPLEMENTATION_REPORT_20260920_CHS.md)。以下步骤 1/2 的接口与当前头文件同步，不再抄旧聊天中的大写占位函数。


> 当前更新（完整行复核）：compose_line 与 Context OS tid 已进入生产并补充专项测试；下一步是 [PreparedReset / ControlMailbox](./V1_CONTROL_PREPARATION_NEXT_GUIDE_20260920_CHS.md)。不要再次追加旧的完整行参考代码。本轮结果见 [复核报告](./V1_COMPOSE_REVIEW_REPORT_20260920_CHS.md)。

## 0. 已完成边界与实施纪律

已有：Ring/Record/hash/decoder、TLS ProducerContext、配置规范化、`render_message_utf8` 正文、Producer 诊断专项。已有测试通过并不等于真实异步输出已完成。输出层已在人工驱动环境实现；完整行已接入；仍缺管理、worker、生命周期和端到端验收。

保留如下合同，后续不要反复改动：

- Producer 原样 copy/hash 格式字节，零 brace 解析；worker 正文一次渲染。保留现有 `FormatSpec`、`render_message_utf8` 名称，不重新加 bq 前缀。
- 指针 + 显式长度；不引入 `std::span`，不改变 I1 wire ABI 和 16 字节 Ring Handle。
- `prepare_logger_config`、`prepare_appender_configs`、`merge_appender_levels` 实际位于 `include/qlog/logger_config.hpp` / `src/logger_config.cpp`、`namespace qlog`。不要按照历史 ADR-015 R4 再迁回 detail。
- Config 保持平铺结构；Appender 使用运行期继承；共享 worker 默认、独立 worker 可选；单管理线程、单槽邮箱；drop_new。
- `request_drain` 调用前停止全部 Producer，完成结果领取之前不恢复。`shutdown` 前停止并 join 全部业务使用者、结束其他管理调用。`close_registration` 只阻止新 Context，不停止现有 Context。
- 时间戳保留 admission 时刻原值，不采用 BQLog 的回退钳制；稳态 Producer 不分配；冷初始化和管理准备允许分配。

按下表实施，每步达到检查点再进入下一步；步骤 7 之前不向现有 AsyncLogger 启动真实后台。

| 顺序 | 文件与交付 | 本步检查点 |
|---|---|---|
| 1 | `detail/output_batch.hpp`、`detail/io_result.hpp`、`detail/io_report.hpp`、对应 cpp | 批次偏移、完整写出、固定报告槽单测 |
| 2 | `detail/appender.hpp`、Console/TextFile 派生类、POSIX I/O、工厂 | 受控短写/错误/恢复/最终关闭 |
| 3 | `detail/text_formatter.hpp`、`src/text_formatter.cpp::compose_line` | 行格式、日期边界、字节长度 |
| 4 | `detail/control_mailbox.hpp`、`detail/appender_prepare.hpp`、对应 cpp | 冷准备事务、无 worker 的命令状态推进 |
| 5 | `detail/backend_session.hpp`、`src/backend_session.cpp` | 单线程受控 Record → 批次 → 输出 |
| 6 | `detail/worker_wakeup.hpp`、worker/runtime 头与 cpp | 可独立启动/停止的测试 Session、附着/解绑 |
| 7 | `async_logger.hpp/cpp`、`async_logger_impl.hpp`、Channel/Context/Ring 接线 | 构造/公共管理 API/shutdown/析构同时闭环 |
| 8 | `tests/`、`benchmarks/`、CMake、验收文档 | 完整 V1 正确性、故障、性能与平台证据 |

步骤 1 先写完整接口，明确 cache flush 与持久化同步两层；步骤 2 在人工驱动环境完成全部故障转移；步骤 7 才整体接生命周期。这样不会先启动线程、再补救析构，也不会先实现跨轮预算 flush、再改回 BQLog 行为。

## 1. 基础类型一次写齐

新增内部头均放 `include/qlog/detail/`，实现放 `src/`。公共配置/result 继续放 `include/qlog/`。以下内部类型属于 `namespace qlog::detail`。接口头自行包含需要的 `<cstddef>`、`<cstdint>`、`<memory>`、`<vector>`、`<array>`、`<chrono>`、`<atomic>` 等，不依赖传递包含。

### 1.1 OutputBatch：只有一个尚未写完的连续区间

`output_batch.hpp` 的接口固定为：

```cpp
class OutputBatch final {
 public:
  explicit OutputBatch(std::size_t capacity); // 冷分配，失败可抛出
  OutputBatch(const OutputBatch&) = delete;
  OutputBatch& operator=(const OutputBatch&) = delete;
  const std::byte* pending_data() const noexcept;
  std::size_t pending_size() const noexcept;
  std::size_t append_capacity() const noexcept;
  bool append(const std::byte* data, std::size_t size) noexcept;
  void consume(std::size_t size) noexcept;
  std::size_t discard_pending() noexcept;
 private:
  std::unique_ptr<std::byte[]> storage_;
  std::size_t capacity_{};
  std::size_t used_{};
  std::size_t written_{};
};
```

不变量 `written_ <= used_ <= capacity_`。pending 是 `[written_, used_)`；只有 `written_ == 0` 时可追加。部分写入后 `append_capacity()` 返回 0，禁止把新行插入正在续写的批次。

`append`：先判 `data == nullptr && size != 0`，再用 `size > append_capacity()` 判界；size 为 0 时直接成功，避免对 nullptr 调用 memcpy。成功 memcpy 到 `storage_ + used_`，再增加 used。容量校验用减法，不用可能溢出的 `used + size`。

`consume` 只接受 `size <= pending_size()`；增加 written；完全写完立即令两个偏移为 0。错误输入属于内部不变量损坏，Debug assert，Release 也不能越界；调用处先验证系统调用返回范围。`discard_pending` 返回原剩余长度并复位，不返回已写前缀。不要 memmove 压缩、不要重放前缀、不要在故障时扩容。

### 1.2 flush 结果：按 BQLog 一次调用推进写出

`io_result.hpp` 固定为：

```cpp
enum class FlushStatus : std::uint8_t { completed, incomplete, failed };
struct IoWriteResult final { std::size_t written; int error; };
struct IoCallResult final { int error; };
struct FlushResult final {
  FlushStatus status{FlushStatus::completed};
  std::size_t remaining_bytes{0};
  bool durability_uncertain{false};
};
inline constexpr std::uint32_t kRecoveryNamesPerVisit = 16U;
```

**不定义 IoBudget、FlushCursor、yielded，也不在正常短写之间返回调度器。** 一次 cache flush 持续 write，直到全部写完、出现非 EINTR 错误或 write 返回 0。write 的 EINTR 在这次调用内重试；Linux fdatasync 按 BQLog `flush_file` 只调用一次，EINTR 作为此次 sync 错误报告。

FlushStatus::incomplete 专门表示没有 OS errno 但 write 返回 0 且仍有后缀。它不是成功，也不伪造 EIO，不触发永久错误换文件；后缀保留，下一次正常 flush 再尝试。最终关闭遇到它则报告 system_error=0、unwritten_bytes>0 并丢弃后缀。这一结果分类是 QLog 对 BQLog 已有返回行为的显式表达，不是重新引入预算让步。

POSIX write 包装执行一次 write：成功返回实际 written；-1 立即保存 errno；0 原样返回。上层 write_all 循环负责推进，累计已写长度；SSIZE_MAX 仅限制每次系统调用长度，不是每次 flush 的总字节限额。返回正数大于请求长度是注入器/内部不变量破坏，不能越界 consume。

Record/Session 轮转配额、恢复文件最多16个重名尝试仍保留；这些不是 flush 预算。慢 write 或持续 EINTR 可以占用共享 worker；公平性只能在一次 flush 返回之后继续，不声称可抢占 I/O 或保证 shutdown 超时。

### 1.3 报告与历史：先预分配，再 noexcept 写入

保留现有 `management_result.hpp` 公共枚举和字段。`IoFailure` 的 `system_error=0` 与 write 阶段的 unwritten_bytes>0 表示 write 返回0而未写完，没有 OS errno；不能据此当作成功或伪造 EIO/ETIMEDOUT。

新增 `io_report.hpp`：

```cpp
struct IoErrorSlot final {
  bool present{false};
  IoFailure value; // 名字预填；output_path 预留最大长度
};
class IoReport final {
   public:
    IoReport() = default;
    IoReport(const IoReport&) = delete;
    IoReport& operator=(const IoReport&) = delete;
    IoReport(IoReport&&) noexcept = default;
    IoReport& operator=(IoReport&&) noexcept = default;
    void record(std::size_t target, IoStage stage, int error, std::uint64_t unwritten,
                bool uncertain, const char* path, std::size_t path_size) noexcept;
    bool has_errors() const noexcept;
    std::vector<IoFailure> take_errors() noexcept;
    void clear() noexcept;

   private:
    friend IoReport prepare_io_report(const AppenderConfig* configs, std::size_t count);
    std::vector<std::array<IoErrorSlot, 4>> slots_;
    std::vector<IoFailure> export_;
    bool taken_{false};
};

struct IoHistory final {
  std::uint64_t event_count{0};
  std::uint64_t lost_bytes{0};
  bool first_present{false};
  IoFailure first; // 创建 Appender 时已准备字符串容量
};
```

再声明冷入口 `IoReport prepare_io_report(const AppenderConfig* configs, std::size_t count);`，只对已规范化配置工作。prepare 的 vector 槽数必须是 `resize`，不能仅 reserve 后用下标访问。配置路径的最大运行长度为 `base.size() + sizeof(".recovery.") - 1 + 20`；先检查加法溢出，再 reserve。报告的每一槽、history.first、活动 last_error 均预留相同路径上限。Console 使用空 output_path。

`record` 在当前目标/阶段第一次出现时保存 errno 和路径；后续同槽保留首个 errno/path，unwritten 使用同阶段观测最大值，uncertain 取 OR。这个 unwritten 是该次操作所见未完成量，**不是累计损失计数**。每槽字符串赋值前检查容量，不允许自动扩容；冷准备不足是实现缺陷，测试必须覆盖。不要把 take_errors 调到 worker 循环中。

`IoReport` 禁止复制，clear 仅可在 take 之前使用；take 后只能销毁或以新的冷准备对象替换。`take_errors` 按目标顺序、open/write/sync/close 顺序把 present 的 value move 到预留 export，再 move 返回；缺省分配器的 vector/string move 不做重新分配。public completion 和 shutdown 容器同样在冷路径准备，不在故障出口临时 reserve。

历史 event_count 只在实际失败系统调用（write 的内部 EINTR 重试除外，fdatasync 的 EINTR 属于本次同步失败） 时增加；重复真实 ENOSPC 可加事件，不在每个 tick 重新“报告已知故障”时加事件。lost_bytes 只在 `discard_pending()` 返回正数时增加。使用饱和加法防计数回绕。第一次实际错误填 first，后续不覆盖；最终 write 返回0导致的丢弃可增加 lost_bytes，但不凭空产生 OS 事件。

Session 在每次 Appender 操作后立即 capture_first_error 到同一个 RetiredIoSummary，以保持首错的 worker 观察顺序；对象关闭后再 merge_history_into 累加计数。最终关闭活动目标也合并到同一 `retired_io`，因此它在 shutdown 结果中代表整个 Logger 生命周期历史，不只 reset 退休对象。每个对象只合并一次，显式 `history_merged_` 标志或转移所有权后清空 history 防止重复。

检查点：部分 consume 后禁止 append；两次 ENOSPC 保留同一后缀而 lost=0；最终 discard 只加一次；当前 report 清空与 history 保留互不影响；计数饱和、空指针零长度、路径最大恢复编号。

## 2. Appender 与 I/O：先在无真实 worker 环境完成

### 2.1 唯一基类控制接口

已实现 `detail/appender.hpp`、`src/appender.cpp`。当前接口如下；完整函数体直接阅读源码：

```cpp
enum class OutputState : std::uint8_t {
    active, disk_full, retry_same_fd, reopen_pending, closed,
};

// Single backend owner. Never invoke I/O while a Ring frame is borrowed.
class Appender {
public:
    virtual ~Appender() noexcept;
    bool selects(std::uint32_t category, std::uint8_t level) const noexcept;
    bool ready_for_record() const noexcept;
    bool accept_line(const std::byte*, std::size_t) noexcept;
    FlushResult flush(FlushMode, IoReport&, std::size_t report_index) noexcept;
    void final_close(FlushMode, IoReport&, std::size_t report_index) noexcept;
    void service_recovery(std::chrono::steady_clock::time_point, IoReport&,
                          std::size_t report_index) noexcept;
    void service(std::chrono::steady_clock::time_point, IoReport&,
                 std::size_t report_index) noexcept;
    void apply_compatible_config(AppenderConfig& prepared) noexcept;
    CalendarCache& calendar_cache() noexcept;
    const AppenderConfig& config() const noexcept;
    // Session calls this immediately after each target operation to preserve
    // cross-target observation order; always use the same Session summary.
    void capture_first_error(RetiredIoSummary&) noexcept;
    // Only after final_close; moves the first error without allocating, once.
    void merge_history_into(RetiredIoSummary&) noexcept;
    OutputState state() const noexcept { return state_; }
    // closed has no deadline; caller must exclude it from wakeup scheduling.
    std::chrono::steady_clock::time_point next_service_time() const noexcept;

protected:
    explicit Appender(AppenderConfig prepared);
    // One syscall: an error result must have written == 0.
    virtual IoWriteResult write(const std::byte*, std::size_t) noexcept = 0;
    virtual IoCallResult sync_output() noexcept = 0;
    // Idempotent; invalidate fd before close; keep output_path available.
    virtual IoCallResult close_output() noexcept = 0;
    // Reports every actual failure via note_io_failure below. No duplicate
    // reporting in service_recovery. A failed open uses blocking=true;
    // a secondary cleanup close failure uses blocking=false.
    virtual IoCallResult reopen_output(IoReport&, std::size_t) noexcept = 0;
    virtual std::string_view output_path() const noexcept = 0;
    void note_io_failure(IoReport&, std::size_t, IoStage, int error,
                         std::uint64_t unwritten, bool uncertain,
                         std::string_view path, bool blocking) noexcept;

private:
    AppenderConfig config_;
    OutputBatch batch_;
    OutputState state_{OutputState::active};
    CalendarCache calendar_cache_;
    IoHistory history_;
    IoFailure last_error_;
    IoFailure sync_error_; // independent of a later write/open fault
    bool has_last_error_{false};
    bool sync_failed_{false};
    bool history_merged_{false};
    std::chrono::steady_clock::time_point flush_due_{};
    std::chrono::steady_clock::time_point retry_due_{};

    FlushResult flush_impl(FlushMode, IoReport&, std::size_t, bool final_attempt) noexcept;
    IoWriteResult write_all(const std::byte*, std::size_t) noexcept;
    void schedule_retry(std::chrono::steady_clock::time_point) noexcept;
    void report_unresolved(IoReport&, std::size_t) const noexcept;
    void discard_pending_as_lost() noexcept;
    void close_and_report(IoReport&, std::size_t) noexcept;
};
```

构建前按 §3 定义 CalendarCache 值类型；这一步可先只加头定义，函数实现按步骤 3 完成。由基类集中实现批次、状态、错误、周期；派生类仅负责实际 I/O 与句柄。不要把同一状态机复制到两个派生类。基类析构不能调用纯虚 I/O；TextFile 派生析构只作 fd 未关闭时的 close 兜底，不进行 flush；正常路径的报告和历史合并由显式 final_close 完成。冷准备失败的临时对象也通过这一 RAII 规则释放 fd。

`selects` 使用配置 enabled、filter.levels 和对应 category 字节，先做索引/level 检查。Producer 合并位图仍包含 disabled 配置，沿用既有合同。`ready_for_record` 必须是 active 且 append_capacity >= 65536；不能仅看还有一个字节。这样借用 Record 前即可保证任意合法完整行都能复制进去。

`accept_line` 只检查长度并复制，不调用 write/sync/open，不等待锁。返回 false 是空间/状态不满足；正确的 Session 选中快照应使它不发生，不得转成“返回成功但截断”。

`apply_compatible_config` swap 冷准备配置，保留 batch/fd/recovery/history；时区变更 invalidate CalendarCache；周期更新以当前 steady time 重排 deadline；不复制可能分配的字符串。兼容性不成立时严禁走这个函数。

### 2.2 flush 的完整控制流：write_all + 可选 sync

在 `appender.cpp` 增加非虚私有 `IoWriteResult write_all(const std::byte*, std::size_t) noexcept`。下面给出完整循环主体；派生 write 只调用一次系统调用。

```cpp
IoWriteResult Appender::write_all(const std::byte* data, std::size_t size) noexcept {
  std::size_t total = 0;
  while (total < size) {
    const auto request = std::min(size - total, static_cast<std::size_t>(SSIZE_MAX));
    const auto r = write(data + total, request);
    if (r.error == EINTR) continue;
    if (r.error != 0) return {total, r.error};
    if (r.written > request) std::terminate(); // 内部/测试注入结果破坏协议
    total += r.written;
    if (r.written == 0U) break;
  }
  return {total, 0};
}
```

需要 `<algorithm>`、`<climits>`、`<cerrno>`、`<exception>`。size=0 不触碰 data，不进行空指针算术。data 输入是 batch 的稳定 pending_data，没有并发追加或回收。

`flush(mode, report, target)` 按固定顺序：

1. closed 或 TextFile reopen_pending：复制尚未解除的故障到本 report，返回 failed；不要把“没有 fd”当空 batch 成功。
2. 记录 requested=batch.pending_size；调用 write_all(pending_data,requested)；**先** consume(result.written)，更新准确后缀，再处理 result.error。写成功前缀永不重放。
3. 有错误按 §2.3 处理并返回 failed。没有错误但 pending 非零说明 write 返回0：状态改 retry_same_fd、设置重试deadline；report 写 stage=write,error=0,unwritten=pending，返回 incomplete；不加 OS 事件、不重开文件。
4. 后缀为0则解除旧 write 故障，状态 active。buffered 返回 completed；若仍有 sync_failed，保留 uncertain 并将未解除的 sync 故障写入本 report。durable 对 TextFile 调一次 sync_output；失败保存 sync_failed、report(sync,errno,0,true)，返回 failed；成功清 sync_failed。
5. Console durable 等同完成输出，不调用 fdatasync。TextFile 即便本次 batch 为空，durable 也调用 fdatasync，覆盖之前 buffered 已写出的内容。

将公共 flush 与 final_close 共用逻辑收敛到私有 `FlushResult flush_impl(FlushMode, IoReport&, size_t, bool final_attempt) noexcept`，public flush 固定 false；final=true 不自动打开恢复文件，永久错误的关闭统一留给最终 close，避免双关句柄。

disk_full/retry_same_fd 可被显式 flush 立即尝试，不必等 deadline。成功恢复后不将之前已恢复的错误重新塞进新请求；新失败写入当前 report。普通周期到期时才重试真实故障/零进展，不在一个 worker 外层循环中无限立即重复失败 flush。

BQLog 的两层方法应分别理解：flush_write_cache 把用户态缓存交给 write；flush_write_io 在 Linux 调 fdatasync。QLog buffered/durable 对应这两层组合；不把普通 force flush 自动解释为 durable。

### 2.3 错误分类与最终关闭

| 事件 | pending 后缀 | 下一步 |
|---|---|---|
| write EINTR | 保留 | 本次 write_all 内重试，不设次数上限 |
| 正常短写 | 仅保留剩余 | 本次 write_all 内继续 |
| 非空 write 返回0 | 保留 | 本次 incomplete；同fd按deadline再试，不伪造errno |
| ENOSPC/EDQUOT | 保留 | disk_full，拒绝新行，到 retry_due 重试原fd |
| EAGAIN/EWOULDBLOCK | 保留 | retry_same_fd，按deadline重试原fd |
| 其他永久 write 错误 | 记录后实际discard；前缀不重放 | TextFile关闭旧fd，reopen_pending；Console延时恢复接收 |
| fdatasync 失败（含 EINTR） | 不因此discard | sync_failed，当前请求uncertain；下次durable重试 |

ENOSPC 保留与永久错误恢复参考 BQLog；EDQUOT/EAGAIN 分类、显式报告、恢复命名保留已接受的 QLog 适配，不因为 flush 对齐而偷偷更换全部文件故障合同。

Console 永久 write 错误记录实际discard，不尝试恢复文件，不close(1/2)；retry_same_fd 到期且空批次时恢复 active。TextFile reopen_pending 即使空批次也不能被普通flush清除。

`final_close` 用于 retire/shutdown，每对象只执行一次：

1. 停止接收新行/重开任务，保留现有 pending 偏移。
2. 调一次 `flush_impl(mode,report,index,true)`。这“一次”内部依照 BQLog 持续推进短写、write EINTR重试，**不是8次调用或256KiB预算**。
3. 完成则正常关闭；真实错误/零进展仍有后缀则记录剩余，实际discard一次并累加lost_bytes，不等待磁盘恢复，不打开新恢复文件。
4. TextFile取出fd、成员先置-1，再close一次；Linux close EINTR不重试。close错误报告stage=close；Console close_output不关闭借用fd。
5. state=closed；Session 随即 capture_first_error 再 merge_history_into；重复final_close直接返回。

shutdown不循环等待ENOSPC修复，但持续write EINTR或阻塞系统调用仍可延迟完成；撤销暂存草案“最终字节预算耗尽也丢弃健康缓存”的规则。后台退出/Session解绑前不释放资源，不用detach伪装超时成功。

周期触发保留配置默认100ms，容量不足先flush，显式请求/最终关闭触发立即flush。BQLog process(force_flush)在消费后刷cache而非每次fdatasync；QLog仍通过steady_clock计算deadline，保留既有处理配额，不复制其消费直到empty造成的跨Session饥饿。

### 2.4 TextFileAppender：真实 fd 与恢复文件

已实现 `detail/text_file_appender.hpp`、`src/text_file_appender.cpp`。派生字段：`int fd_{-1}`、原始规范化路径 `base_path_`、当前输出路径 `current_path_`、`uint64_t recovery_index_`、预分配恢复路径 buffer、`bool index_exhausted_`。原路径匹配兼容性用 config.file.path，不能拿运行中的 recovery 路径比较。

冷工厂打开原路径 `O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC | O_NONBLOCK`（O_NONBLOCK 防止误配 FIFO 在 fstat 前阻塞，普通文件语义不变），mode 0644 受 umask 限制；fstat 验证普通文件，失败关闭并返回 open_failed。父目录必须存在，不在日志库隐式 mkdir。允许重复路径，禁止重复 Appender name。

恢复文件使用 `base + ".recovery." + index`，整数用 `to_chars` 写入预留 buffer，open 再加 O_EXCL。每次最多 16 次 EEXIST 名称冲突尝试；此处限制恢复名称搜索，不限制 flush 的 write 重试。已打开但未验证的 fd 必须同次验证，失败关闭，不能泄漏。

恢复实现采用独立的名称尝试上限：最多 16 次 open 尝试，每次至多一次 fstat 和一次失败 close，不在这三个 syscall 内部重试 EINTR；EINTR 延至下一恢复周期。此名称搜索上限只供 reopen_pending 使用，该轮不再立即对新fd执行普通写入。打开成功后下一轮才能接收新行。文件名在 EEXIST 时递增，其他失败不必每个 tick 跳号；uint64 最大值碰撞后标记耗尽，不回绕覆盖文件。具体 errno/path 通过 note_io_failure 写当前恢复 report 与 history；服务层不重复计数。索引耗尽后只保持最后故障，不重复制造 syscall 事件。

steady_clock 控制 retry_interval_us（默认 100000）；系统日历回退不改变恢复时机。TextFile buffered 只 write；durable 用 fdatasync，无 fflush、无每条 fsync。不加入滚动删除、目录 fsync、mmap 或重启恢复协议。

### 2.5 ConsoleAppender：锁的范围与存活期

已实现 `detail/console_appender.hpp`、`src/console_appender.cpp`。持有选定 fd 1 或 2、公共 ConsoleOutputGate 的稳定引用；不拥有 descriptor。Console sync_output 直接成功，不能 fdatasync(stdout)。Console 输出按明确字节长度，NUL/换行均原样。

`ConsoleOutputGate` 仅一个 `std::mutex mutex`。由进程级 BackendRuntime 的共享资源持有，Runtime 在任何 worker 创建前构造，所有共享/独立 worker 退出后才销毁。独立模式也引用这一资源，不能每个独立 worker 各造一把“全局锁”。默认 Runtime 静态生命周期要求所有 Logger 先结束；不靠 detach 或泄漏 Session 规避寿命问题。

为便于用同一基类控制循环，初版在 Console `write` 内用 lock_guard 覆盖**一次 write syscall**，短写后释放锁再进入下一次调用。用户批准的是单次输出串行化，不是整个批次/整行原子性。不要持锁格式化、扫描 Ring 或服务其他目标；不依赖 flush 调用/字节预算限定持锁时间，阻塞write仍可持锁等待。

worker 启动时仅屏蔽 SIGPIPE，使 EPIPE 进入故障路径；检查 pthread_sigmask 的返回错误码而非 errno。冷工厂只 open 不 write；析构兜底也不从未屏蔽 SIGPIPE 的管理线程写 Console。worker 失效后的接管最终清理如需 Console write，必须在调用线程用 RAII 临时屏蔽 SIGPIPE，并正确处理本次新产生的 pending SIGPIPE 后恢复原 mask；更简洁的 V1 接管策略是**不再输出，报告剩余并关闭**，统一在 §6.5 采用后者。

检查点：注入 `[short, EINTR, short, success]` 精确拼接；连续有限次 EINTR 后成功且不丢失；write返回0保留后缀不伪造errno；ENOSPC 周期不忙转；EPIPE 不杀进程；Console NUL 不截断；fdatasync 失败不换文件；重名 16 次后让步；双 close 防护；一次flush返回后其他目标得到服务，不能声称在flush内部可抢占。

## 3. compose_line：新增行层，不重写已验收正文

### 3.1 头文件与固定格式

在 `detail/text_formatter.hpp` 加 `<array>`、`<cstdint>` 及 ChannelCold/RecordHeader/TimeZoneConfig 所需声明。新增：

```cpp
struct CalendarCache final {
  bool valid{false};
  std::int64_t local_second{};
  std::int16_t offset_minutes{};
  std::array<char, 19> calendar{}; // YYYY-MM-DDTHH:MM:SS，无终止零
};
[[nodiscard]] FormatResult compose_line(
    const ChannelCold& channel, const RecordHeader& header,
    const TimeZoneConfig& time_zone, CalendarCache& cache,
    const std::byte* message, std::size_t message_size,
    std::byte* output, std::size_t capacity) noexcept;
```

`format_spec.hpp::FormatError` **末尾追加** `time_conversion_failed`，不重排旧值。完整行固定：

```text
[2026-09-19T12:34:56.123456789+08:00] [INFO] [logger/category] [tid=12345] message\n
[time=unavailable] [INFO] [logger/category] [tid=12345] message\n
```

上面 `\n` 表示实际一个 LF，不是反斜杠字母 n。LEVEL 顺序 TRACE/DEBUG/INFO/WARNING/ERROR/FATAL。UTC 显示 `+00:00`。名字、类别、正文按字节复制，不转义内嵌 NUL/换行；因此“完整行”是布局单元，不保证文本中只有一个 LF。OS tid 在 Context 冷创建时调用 Linux gettid/syscall(SYS_gettid) 保存，不能拿 producer_token 冒充。

### 3.2 匿名命名空间辅助函数与实现顺序

复用现有 CheckedTextWriter；先读它的接口，新增小 helper 调用现有 writer，不再定义同名 writer。建议私有函数签名：

```cpp
bool write_fixed_decimal(CheckedTextWriter&, std::uint32_t value,
                         unsigned digits) noexcept;
std::optional<FormatError> write_timestamp(
    CheckedTextWriter&, const RecordHeader&,
    const TimeZoneConfig&, CalendarCache&) noexcept;
bool write_thread_id(CheckedTextWriter&, std::uint64_t tid) noexcept;
```

write_timestamp 返回空 optional 表示成功，失败返回 text_output_limit_exceeded 或 time_conversion_failed，不能只用 bool 丢失原因。其余两个 bool helper 仅表示容量不够。已核对现有 CheckedTextWriter：用 append_bytes(const void*, size_t)、append_char(char)、append_fill(char, size_t)，最终从 used 取字节数；复用 make_success/make_failure。

按以下顺序写 compose_line：

1. 校验输出/输入指针与显式长度；capacity 上限 65536；category_id 小于 `channel.dependencies.category_names.size()`；level < 6。返回 failure 时 size=0，外部必须丢弃 scratch，禁止交付部分前缀。
2. `header.flags & kTimestampStatusMask` 为 time_unavailable 时写固定占位。合法/备用时间都按 epoch nanoseconds 处理；不把 0 epoch 当 unavailable。
3. `seconds = time_value / 1000000000ULL`，`nanos = time_value % 1000000000ULL`。uint64 ns 的秒数可装入 int64，先转换再加 `int64(offset_minutes) * 60`，避免负时区与无符号混算。验证时区 [-840,840]。
4. 检查 local_second 可表示为 time_t，调用 gmtime_r；失败或年份不在 0000..9999 返回 time_conversion_failed，不截断年份。时区偏移已经手工加过，禁止再调用 localtime_r 重复加时区。
5. CalendarCache 键是 local_second 与 offset_minutes 的**相等比较**，支持时间回退；命中只复用 19 字节年月日时分秒。纳秒每条重写，不能缓存整个时间戳；时区更新置 valid=false。
6. 年月日等用固定宽度十进制，ns 恰好九位；时区使用正负号与 abs(offset)/60、abs(offset)%60。常量除法交给编译器，不写不经验证的手动 reciprocal。
7. 顺序写等级、logger 名、category 名、tid、正文和最终 LF。每一步经 CheckedTextWriter 边界检查，最终返回实际字节数，不写终止 NUL。

每 worker 的 message/line scratch 各 65536 字节，冷分配在堆上，避免 worker 栈放 128KiB；每 Appender 一个 CalendarCache。正文成功后按每目标时区 compose_line。目标 A 行过长只丢 A 的 delivery；正文失败则所有选中目标都不交付。错误不向同 Logger 递归记录。

检查点：epoch0、时间 unavailable/fallback、负时区跨前一天、+14/-14、跨年闰日、回退秒、纳秒0/999999999、容量刚好/差1、空正文、名字含 NUL/换行、非法 category/level、64KiB body 加前缀溢出。使用现有 body 测试回归，不重新改 BQ brace 行为。

## 4. 冷准备与邮箱：先把资源和结果容量准备好

### 4.1 数据结构与状态所有者

新建 `detail/control_mailbox.hpp`：

```cpp
enum class CommandState : std::uint8_t { empty, pending, completed };
enum class CommandKind : std::uint8_t { reset_appenders, flush_batches, drain };
enum class CommandPhase : std::uint8_t {
  begin, drain_records, flush_targets, retire_targets, apply_reset, finish
};
struct PreparedCommand final {
  std::uint64_t request_id{};
  CommandKind kind{};
  CommandPhase phase{CommandPhase::begin};
  FlushMode flush_mode{FlushMode::buffered};
  ResetMode reset_mode{ResetMode::reuse_compatible};
  std::size_t target_index{};
  std::unique_ptr<PreparedReset> reset;
  IoReport report;
  ManagementCompletion completion; // 发布前构造，worker只改标量status
};
struct ControlMailbox final {
  alignas(64) std::atomic<CommandState> state{CommandState::empty};
  std::unique_ptr<PreparedCommand> command;
};
```

PreparedReset 前置声明配合 out-of-line 析构，或在完整定义后放 PreparedCommand；不要在 incomplete type 上实例化 unique_ptr 析构。所有者规则：empty 时管理线程；pending 时 worker；completed 后管理线程。state 是唯一交接同步，不能用 wakeup.waiting 来发布 command 内容。

管理准备失败只销毁未发布资源；目标文件可能已被创建，不能承诺外部文件系统事务回滚。不得删“刚创建的空文件”以模拟回滚，可能与其他使用者并发。

### 4.2 PreparedReset 最终字段与兼容判断

新建 `detail/appender_prepare.hpp`、`src/appender_prepare.cpp`。PreparedReset 包含：

| 字段 | 类型/用途 |
|---|---|
| new_config_snapshot | `vector<AppenderConfig>`，完成领取时更新管理影子 |
| changes | `vector<AppenderConfig>`，给兼容对象 swap 的独立预备值 |
| reuse_old_index | `vector<size_t>`，新目标 → 旧下标，max 表示新建 |
| old_reused | `vector<uint8_t>`，旧目标是否被复用 |
| next_appenders | `vector<unique_ptr<Appender>>`，resize 新数量；新建对象已填，复用槽暂空 |
| retired | `vector<unique_ptr<Appender>>`，resize 旧数量；worker 把不用对象移入，管理最后销毁 |
| next_selection | `vector<uint8_t>`，resize 新数量 |
| next_shutdown_report | `IoReport`，为新活动目标预分配 |
| next_periodic_report | `IoReport`，独立后台周期报告，按新路径容量预分配 |
| prepared_merged_levels | `uint32_t`，冷合并一次 |

冷准备函数签名：

```cpp
std::unique_ptr<PreparedReset> prepare_reset(
    const AppenderConfig* input, std::size_t count,
    const AppenderConfig* acknowledged, std::size_t acknowledged_count,
    std::size_t category_count, ResetMode mode,
    ConsoleOutputGate& console_gate);
```

抛出的 ConfigValidationError 映射 invalid_config/reason/config_index；bad_alloc 映射 resource_exhausted；自定义仅冷路径的 `AppenderOpenError` 携带 IoFailure 映射 open_failed。实际工厂声明 `std::unique_ptr<Appender> make_appender(AppenderConfig prepared, ConsoleOutputGate&);`，只允许冷调用。该工厂当前已实现在 detail/appender_factory.hpp 与 src/appender_factory.cpp，后续直接 include，不再重复定义。

同名 + 相同 type + 相同 batch_bytes + TextFile 相同规范化 file.path / Console 相同 stream 才可复用；recreate_all 全不复用。enabled/filter/time_zone/flush_interval/retry_interval 可兼容更新。非选中专用配置字段不参与判断。不得拿 worker 正在修改的 active config 做冷准备；使用管理线程上次已领取结果对应的 acknowledged snapshot。

空输入按现有 helper 规范化为默认 Console；合法 `(nullptr,0)` 与 `(nullptr,count>0)` 行为沿用 helper。错误容量按照实际旧+新目标及恢复路径上限准备；复用名字相同但新路径变化时使用各自 old/new 槽，不把两个对象错误写进一个错误路径缓冲。

### 4.3 发布、应用、领取的确定顺序

submit：首先检查 stopped/worker_failed 和 `state.load(acquire)`；非 empty 返回 busy，不先打开新文件。准备全部资源后分配非零 request_id，避免回绕；写 command，再 `state.store(pending, release)`，awake。发布之后不得访问 command 内 worker 可变字段。

worker 在无 Frame 借用时 acquire 看到 pending，begin 只执行一次：

- flush_batches：暂停本 Session 消费，固定当前所有 batch；不排空 Ring，不等待后来日志；phase=flush_targets。
- drain：Producer 已停止，phase=drain_records；按配额排空所有 Channel，然后暂停消费转 flush_targets。
- reset：暂停本 Session 消费；逐个旧未复用目标 final_close(buffered)，每轮最多处理一个退休目标；复用对象不强制刷空。完成后进入 apply_reset。

flush_targets 每轮最多处理一个目标，调用一次 flush。completed/incomplete/failed 均前进 target_index；后两者必须已记report，不能把零进展算成功。一次flush内部持续短写/EINTR，不跨轮保存cursor。真实失败不跳过其他目标，也不等待磁盘修好。所有目标结束后按report.has_errors决定completed/completed_with_io_error。

apply_reset 在同一 worker 步骤中执行无抛出 move/swap：旧未复用目标已关闭并在 retired；复用对象移动到对应 next_appenders 槽并 apply_compatible_config(changes[i])；swap active_appenders、selection、shutdown_report、periodic_report；最后通过 FilterConfigAccess 发布 prepared_merged_levels。标记 applied / applied_with_io_error。应用过程中不扫描 Record、不让其他线程读取正在改的容器。

完成时 worker 写 completion.request_id/status，`state.store(completed, release)` 后**不再访问 command**。poll acquire 看到 completed，验证 id，把 report.take_errors 移入 completion；reset applied 时先 swap 管理影子；把结果移出、销毁 command/retired 后，最后 `state.store(empty, release)`。pending 返回 pending，id 不匹配或结果已领取返回 unknown_request。

一个管理线程同时只许一个未领取请求；completed 未领取仍 busy。完成领取可能释放大量旧资源，属于管理冷路径。不要在 worker 发布 completed 后继续写调试字段。

检查点：busy 不调用 open；invalid_config 不动 active；发布后管理不访问工作字段；命令跨目标推进不重新begin；flush 期间 Producer 持续写也不扩大当前 batch 目标；reset 部分退休错误仍应用；失败准备 RAII 无 fd 泄漏；重复 poll/过期 id；id UINT64_MAX 后拒绝。

## 5. BackendSession：无 I/O 的 Record 借用区

新建 `detail/backend_session.hpp`、`src/backend_session.cpp`。Session 独占 Appenders、selection、mailbox 命令执行状态、每 Channel 消费游标、shutdown report、历史汇总；引用 Impl 的稳定 metadata/filter/Context 发布头。Worker 只借用 Session，不能反过来让 Session 销毁 worker。

```cpp
enum class SessionProgress : std::uint8_t { idle, runnable, waiting_io, detached };
struct BackendWorkspace final {
  std::array<DecodedArg, 32> args;
  std::unique_ptr<std::byte[]> message; // 65536，启动前分配
  std::unique_ptr<std::byte[]> line;    // 65536，启动前分配
};
class BackendSession final {
 public:
  SessionProgress service(BackendWorkspace&,
                          std::chrono::steady_clock::time_point) noexcept;
  void request_stop(FlushMode) noexcept; // 控制方release请求，worker读取
  bool detached() const noexcept;       // 控制方acquire
 private:
  // active_appenders、selection、mailbox引用、config配额、轮转cursor、
  // Context发布头引用、最后发现头、stop_mode/stop_requested、
  // shutdown阶段/目标下标、ShutdownResult及预分配报告、RetiredIoSummary。
};
```

在实现头中把上述字段实际展开，不能只存一个 `draining` bool 复用成所有状态。建议 `StopPhase { running, drain_records, close_targets, detach_ready, detached }`；stop_mode 普通字段先写，再 stop_requested.store(release)，worker acquire 后读取。detached 标志单向 release，重复 shutdown 不再写 stop_mode。

### 5.1 Context 发现与公平轮转

`ProducerContext` 增加 worker-only `bool consumer_faulted{false}` 和 `ProducerContext* consumer_next{nullptr}`；published_next 保持发布后不可变。每轮 acquire load 发布头，把新前缀链接入 consumer_next 私有活动链；不要修改 published_next 或在生产线程运行时回收 Context。

持续注册时扫描也要有界：保存 `discover_cursor` 和本次 `discover_boundary`，每轮最多纳入 64 个新节点；完成当前快照后再取新 head。旧边界节点仍存活，指针可比较。不能每轮重置从新 head 扫到末尾，否则注册风暴可能饿死日志消费。

每次 Session visit 服务一个 Channel，最多 records_per_channel 条、bytes_per_channel 字节；至少允许首条合法 Frame 推进一次，即使它超过 bytes 配额，防止配置 bytes=1 永久不消费。计数比较用减法/饱和；之后保存下个 consumer cursor，返回 worker 轮转其他 Session。发现/管理/I/O 工作也需进入每 visit 限额，不能外层公平、内层扫完整 Logger。

### 5.2 消费一条的精确顺序

1. **无 Frame**：按到期/容量服务 Appenders；每目标每轮至多调用一次flush或恢复服务。记录哪些 active 目标 `ready_for_record`。故障目标跳过；不能因一个 ENOSPC 永久堵住所有 Channel。
2. try_read。empty 时 publish_reclaimed 后离开；corrupted/read_pending 标记当前 Channel consumer_faulted 和 shutdown drain_incomplete，不猜长度跳过，不反复读取同一坏帧。
3. 对成功 Handle 立刻建立 RAII FrameLease，负责异常/提前返回时 release 正常已借用 Frame；不得复制 lease。ReadStatus::read_pending 本身不建立新 lease，也不猜旧 Handle。
4. decode_v1(handle.data, handle.size, workspace.args.data(), 32, policy)。解码失败只丢该 payload，随后 release；外层 Frame 仍可信，所以不把整个 Channel 标坏。
5. 校验 category_id，按目标当下配置 selects 且 ready_for_record 生成 selection；无目标直接 release，不渲染正文。
6. 调 render_message_utf8 一次；失败不交付任何目标。成功对 selection 逐个 compose_line 并 accept_line 复制。**此区间只有内存操作，不能 write/open/sync、获取 Console mutex、应用 reset、等待 CV。**
7. lease.release_now；使所有 DecodedRecordView/arg string 指针不再被使用；到配额边界/执行任何 I/O 前 publish_reclaimed。
8. 无 Frame 后才进行 I/O、命令转换与下一轮调度。

ready_for_record 不足时，先在无Frame阶段flush已有批次。成功后再读取Record；真实I/O错误或write返回0时该目标进入故障/重试状态，跳过其delivery并服务其他健康目标。健康目标不因正常短写被人为预算阻塞，因为短写已在这次flush内继续推进。发生故障的目标不永久阻挡所有Channel；其未交付记录由target_unavailable计数，不能伪称accepted一定送达所有目标。

### 5.3 后端计数与完成结果分工

Debug/启用诊断时增加 worker 私有计数：frames_read、decode_failed、body_failed、deliveries_selected、delivered_to_batch、line_failed、target_unavailable。它们不应在普通 Release 引入共享原子；本步骤不新增无界 public stats API。生产必要的 fault/state/history 保留 Release。

drain_incomplete 表示无法排空/解释 Ring 的结构或后台失败，不表示文件剩余字节；文件未完成通过 errors/unwritten_bytes、retired_io.lost_bytes 表达。Record 内容格式失败记录诊断并按既定错误丢弃，不伪装成 I/O errno。验收记录分类损失，不能只看 Producer accepted 就声称全部落盘。

检查点：真实 encoder 写 Ring，Session 人工 service，内存 I/O 注入器收集精确字节；Frame 释放前 I/O hook 断言失败；正文仅调用一次、多时区行不同；坏 payload 可继续下一 Frame、坏 frame 标 Channel 故障；持续新 Context 不饿死已注册 Channel；配额1仍前进。

## 6. Worker 与 Runtime：先保证可停止，再提供服务

### 6.1 WorkerWakeup

新增 `detail/worker_wakeup.hpp`、`src/worker_wakeup.cpp`：

```cpp
class WorkerWakeup final {
 public:
  void awake() noexcept;
  void wait_until(std::chrono::steady_clock::time_point deadline) noexcept;
 private:
  std::atomic<bool> waiting_{false};
  std::atomic<bool> wake_failed_{false};
  std::mutex mutex_;
  std::condition_variable cv_;
};
```

awake：`if (!waiting_.exchange(false, relaxed)) return;`，命中才 lock_guard 同一 mutex 并 notify_one。异常捕获后 wake_failed=true；不能从 try_log noexcept 抛出，不回滚 accepted。worker 在 mutex 内 waiting.store(true, relaxed)，执行 wait_until，返回后 store(false)。无条件 timed wait 必须接受虚假唤醒；有积压时不调用 wait。deadline 为 min(now+66ms, 最近到期 I/O deadline)，到期先服务再算下一次，避免过去 deadline 忙转。

wait 失败置 wake_failed 并退到短暂系统 sleep 的有界轮询，不允许反复抛异常忙转；记录为后台诊断，保持正常 stop 握手。awake 在 waiting 置位前发生可能被 tick 兜底，不能声称绝无延迟窗口。所有日志/命令的可见性仍由 Ring/mailbox 原子保证。

### 6.2 RuntimeNode 与共享 worker

新增 `detail/backend_runtime.hpp`、`src/backend_runtime.cpp`；内部接口固定为：

```cpp
enum class NodeState : std::uint8_t { published, attached, detached, failed };
struct RuntimeNode final {
  RuntimeNode* published_next{nullptr}; // CAS后不可变
  RuntimeNode* active_next{nullptr};    // worker私有
  BackendSession* session{nullptr};     // 发布前写；worker解绑时清
  std::atomic<NodeState> state{NodeState::published};
};
class BackendRuntime final {
 public:
  static BackendRuntime& instance();
  WorkerWakeup& shared_wakeup() noexcept;
  ConsoleOutputGate& console_gate() noexcept;
  RuntimeNode* attach(BackendSession&); // 冷准备/发布/等待确认，可失败
  void shutdown_all(); // 全部Logger已shutdown且不再构造时调用
  ~BackendRuntime() noexcept;
 private:
  // ConsoleOutputGate, shared BackendWorker, CAS只增注册头，停止状态。
};
```

node 冷分配、填 session，release CAS 加入只增链后所有权转 Runtime。构造线程即使遇启动/附着失败也不能直接 delete 已发布 node；必须等待 node failed/detached 或 worker exited 确认不再借用 Session。node 直到 Runtime 全部 worker 停止后统一释放。

worker acquire 发现新 node，纳入 active_next 私有链后 store attached(release)。遍历游标/发现游标同样有界，禁止持续 attach 饿死活动 Session。Session 请求关停后继续轮转排空，完成 final_close，再从 active 链摘除；清当前/下次/发现流程中的所有 Session 借用，node.session=nullptr，最后 store detached(release)。Logger acquire 见 detached 才能释放 Session/Contexts。

公共 worker 不因一个 Logger shutdown 而 join；Runtime shutdown_all 只允许所有 Logger 已脱离、不再新建；不添加 watchdog 自动重启。共享 worker 失败后 Runtime fail-closed，后续 attach 明确失败。

### 6.3 BackendWorker 线程入口

新增 `detail/backend_worker.hpp`、`src/backend_worker.cpp`。一个 worker 持有线程、wakeup、BackendWorkspace、ready/failed/exited 原子；独立模式只借用一个 Session，共享模式遍历 RuntimeNode。工作区在线程启动前分配。

线程入口：屏蔽 SIGPIPE → 发布 ready → 发现注册/服务轮转 → 没有 runnable 工作时 timed wait → 停止条件满足后退出。初始化失败设置 failed，再 release exited，不发布 ready 成功。启动方 acquire 等 ready 或 exited，不能等一个永远不会出现的 ready。

每轮 Session 返回 idle/runnable/waiting_io/detached；runnable 表示仍有Ring工作或待处理命令目标，waiting_io 只等待恢复 deadline；关闭完成从列表移除。不要对失效 Session 继续访问。外层 catch 捕获真正异常，所有 RAII Frame/临时引用先离开作用域，再发布 failed/exited。worker 不拿 Console 锁进行调度，也不拿 wakeup 锁做 I/O。

### 6.4 正常 shutdown 的状态机

控制线程先完成/领取已有 pending 管理请求，再 close_registration，写 stop_mode 并 release stop_requested，awake。worker：drain_records 按正常配额排空所有已发布 Context（Producer 已停止，集合有限）→ close_targets 每 visit 一个 final_close → 汇总错误/历史 → detach_ready → 共享 node detach 或独立线程退出。

独立模式 join 后才释放；共享模式等 node detached 后释放但不 join 公共 worker。结构损坏 Channel 标 drain_incomplete 后跳过，仍处理其他 Channel 和所有目标，不因一处坏帧永远关不掉。

`ShutdownResult.errors` 是最终操作的错误/未完成；`retired_io` 是整个生命周期累计。之前 flush 已报告的已恢复错误只在历史中；仍未解除且导致最终未完成的故障还需进入本次 errors。返回保存结果 const 引用；后续 shutdown 不重新 flush、不重新合并计数，忽略新传入 mode 并返回第一次完成结果。

### 6.5 后台失效的安全接管

worker.failed 不等于它已经退出。控制线程必须 acquire 确认 exited；独立线程 join，共享线程由 Runtime 唯一 join 所有者处理。之后管理线程才可接管未完成 command、活动/退休/新建目标。

本路径不再尝试 write/sync/reopen：标记 backend_failed=true、drain_incomplete=true；记录/丢弃所有仍拥有的 batch 后缀并合并历史，关闭 TextFile fd 一次。Console fd 不关闭。无需在管理线程冒 SIGPIPE 写输出风险。

reset 即使在中途失败也要覆盖 active_appenders、PreparedReset.next_appenders、retired 中全部非空 unique_ptr；每对象只存在于一个容器，通过 move 保证唯一所有权。预分配失败清理报告按 old+new 目标容量，不能等失败后才建 vector。管理 completion 返回 backend_failed；已应用配置如实保留内部阶段标志，不假报 applied 成功。对控制方法的失败接管只由单管理线程执行，不能多个 Logger 同时 join 同一公共线程。

检查点：启动异常、ready前失败、attach时worker退出、Session detach后马上析构、反复创建Logger、独立/共享混合、慢目标公平、唤醒窗口/虚假唤醒、停止时睡眠、outer catch后无借用、reset各phase注入退出。使用 sanitizer 检查 UAF/fd泄漏；普通吞吐压力不能替代这些确定性交错测试。

## 7. 最后一次性接入 AsyncLogger 公共路径

### 7.1 构造字段与析构必须在同一个变更中完成

当前 `src/async_logger.cpp::Impl::~Impl` 直接 delete published_context。接 worker 时替换为：先调用不抛出的完整 shutdown 内部实现，确认 detached/join，才遍历删除 Context。不能只给构造加 start，留析构“下一步补”。

Impl 字段声明按依赖存活顺序：prepared config/logger_id/filter/clock/policy/hash → 稳定 Runtime 资源引用与未启动的独立 worker 对象（若选独立模式） → ChannelDependencies → Context 发布头/registration → mailbox/管理影子/预分配结果 → Session → node/线程启动状态 → shutdown 状态。C++ 按声明次序构造；销毁需要明确执行 shutdown，再让成员析构，不能只依赖逆序碰巧正确。

构造步骤：规范化 config → 准备初始 Appenders/报告/Session/工作区 → 完整初始化 dependencies → 独立 start 或共享 attach → 等 ready/attached → 构造成功。任何发布前异常由 RAII 清理；发布后失败按 §6 握手才允许 Impl 退出。

头文件尽量前置声明，Impl 完整类型仍在 cpp。BackendSession 不引用 `AsyncLogger::Impl` 私有类型；通过构造参数传稳定所需引用，避免双向包含。

### 7.2 FilterConfigAccess、唤醒依赖、OS tid

把 src/async_logger.cpp 内当前局部 `FilterConfigAccess` 类移到 `detail/filter_config_access.hpp`，保持唯一类定义，增加静态 `set_levels(FilterState&, uint32_t) noexcept` 调 private publish_merged_levels；set_category 保留。限制 friend 调用者为 AsyncLogger 和 BackendSession，避免把任意热路径写配置暴露出去。

ChannelDependencies 一次增加 `WorkerWakeup& wakeup`；同步更新 Impl 初始化、ContextRegistryView 传递、全部测试 fixture 聚合初始化。引用指向负责该 Logger 的稳定 worker wakeup，必须活过所有 Producer/Context。Context 冷创建填 os_thread_id；稳态 try_log 不额外 syscall。

SpscRingBuffer 新增 producer-only 方法：

```cpp
bool producer_low_space() const noexcept {
  const auto used = writer_state_.current_write_cursor_ - writer_state_.cached_read_cursor_;
  return used >= cold_state_.config_.capacity_bytes / 2U;
}
```

上述成员名已核对当前 spsc_ring_buffer.hpp；算法使用 WriterState 本地 cursor 与 cached cursor，无新增共享原子读取；在 commit 之后调用。陈旧 read cache 允许保守多唤醒，不影响 Ring 正确性。不要暴露 reader 状态给 Producer。

在实际 `include/qlog/async_logger_impl.hpp`（不是 detail 子目录）和 Channel submit 实现定位现有唯一 reserve/commit 分支：

- reserve full：awake 后仍返回原 full；不得重试 reserve。
- commit 成功：producer_low_space 为 true 才 awake。
- validation/filter/context/measure 拒绝和 encode abort 不通知。

不得改变 validate → 一次 filter → measure → 一次 reserve → admission timestamp → encode → commit/abort 顺序；诊断八条返回路径保持守恒。压力分支允许 waiting.exchange 和短暂等待 mutex，普通低占用不拿锁。

### 7.3 公共管理函数逐个填齐

签名保持 `include/qlog/async_logger.hpp` 现有声明：

```cpp
SubmitResult request_reset_appenders(const AppenderConfig*, std::size_t,
                                     ResetMode = ResetMode::reuse_compatible);
SubmitResult request_drain(FlushMode);
SubmitResult request_flush_batches(FlushMode);
PollResult poll_management(std::uint64_t);
const ShutdownResult& shutdown(FlushMode = FlushMode::buffered);
```

reset 调 §4 prepare_reset；flush/drain 同样冷创建 PreparedCommand/report，不能认为没有新 Appender 就不需要结果容量。提交共享一个内部 `submit_prepared`，busy 检查仍必须在准备前；禁止复制粘贴三套邮箱发布逻辑。poll 执行 §4 的 acquire/move/empty 顺序。

shutdown 内部区分未请求/等待/已完成，保存结果。析构 fallback 走 buffered、不能抛异常；若公开 shutdown 已完成，析构只释放资源。使用者未停止导致并发析构违反现有生命周期前提，不新增逐条 shared_ptr/refcount 来隐藏。

`close_registration` 仍不停止已存在 Context 的 try_log；shutdown 前置条件需在公共头注释和使用示例写清。业务线程结束后可以 drain 并恢复使用；shutdown 完成后不可再 try_log。单管理线程规则同时覆盖 set_category_enabled/reset/poll/shutdown，转交线程需外部同步。

### 7.4 测试迁移与 CMake

新增 cpp 逐模块进入实际 qlog target；测试链接同一个 qlog 编译定义，QLOG_ENABLE_DIAGNOSTICS 不得 consumer TU 自行猜 Debug 宏。不要把 src/async_logger.cpp 既编进库又 include 到同一测试造成重复定义。

当前诊断和 full 探针依赖“没有消费者”的 fixture。接真实 worker 时改成显式受控测试构造/测试访问层阻止启动或暂停消费者；测试能力仅在测试构建可用，不能增加 public LoggerConfig.pause_worker。另加真正异步消费 fixture，检查 full/accepted 竞态合理、shutdown 安全。六配置诊断矩阵继续保留，不能因 live worker 让 full 难复现就删该路径。

检查点：编译/链接所有公开声明、默认Console可输出、TextFile精确落盘、多Appender过滤、两种worker、管理三请求、重复shutdown、构造open失败无线程泄漏、全过程steady Producer无分配。单纯 nm 找到函数不算功能验证。

## 8. 按功能闭环验收 V1

### 8.1 确定性故障与并发验收

| 类别 | 必须验证的结果 |
|---|---|
| 格式/行 | 保留现有 BQ 对齐正文用例；新行字节、时区、NUL、边界精确比较 |
| I/O | 短写/write EINTR续写、write零进展、ENOSPC/EDQUOT/EAGAIN、永久错误恢复、sync/close失败、恢复编号耗尽 |
| 新五项 | Console共享锁但不宣称整行原子；write零进展不伪报errno；混合durable；同路径限制；恢复后新请求无旧错误 |
| 管理 | busy先行、准备失败不应用、reset兼容状态保留、退休失败仍应用、flush目标有限、drain前提、结果单次领取 |
| 生命周期 | ready/attach/detach/join、公共worker不随单Logger退出、失败接管、重复shutdown、Context安全回收 |
| 诊断 | Debug/Release × AUTO/ON/OFF；现有八返回点、故障分支、跨TU、并发守恒；新增后端计数关闭时消除 |
| 边界 | 空配置、非法配置、最小bytes配额、最大batch、无消费者fixture与真实消费者fixture分别测试 |

I/O 注入可用测试派生 Appender 的虚 write/sync/close/reopen 返回脚本；真实 POSIX 层另做临时目录/管道测试。不要在生产每次 write 引入全局 std::function 或加锁 mock 分发。短写脚本记录每次输入切片，直接验证无重放/无遗漏，而不只断言“调用了三次”。

每步仅跑相关新测试和受影响旧测试；最终统一 Debug、Release、ASan+UBSan 全集，诊断六配置矩阵。可用 TSan 针对 mailbox/worker 生命周期复核，若 WSL2/运行时不支持如实列阻塞项，不把编译通过写成线程安全通过。原生 Linux x86-64 发布复核与自动 CI 独立记录，不用 WSL2 冒充。

### 8.2 性能验收与优化顺序

先完成正确性，再测以下代表负载：过滤空路径、低占用 accepted、低空间/full、高并发多 Channel、单/多目标、共享/独立、短/长字符串、数值格式、慢目标和恢复。分别记录 Producer 吞吐及 P50/P99、端到端延迟、worker CPU、RSS/每线程Ring内存、系统调用次数、accepted/实际输出/分类丢失。

计时不混入首次 TLS Context 分配；冷启动单独测。与 BQLog 对照固定提交、编译选项、线程数/亲和、队列容量、记录字节、输出设备、格式工作量、刷新和持久化模式、满队列策略；无法对等的差异明列。Console 和文件设备的瓶颈不能归因为 formatter 本身。没有实测前不能给“保证高性能”或固定吞吐数字。

优化顺序固定：先发现队列/格式/输出谁占主要成本 → 检查正文仅一次、无借用期I/O、稳态分配 → 调整批次容量/消费配额 → 必要时优化整数/日期转换 → 重新跑受影响正确性和代表负载。不要先手写除法魔数、再改语义迁就基准。所有性能改善同时报告 loss、CPU 和 P99，避免用丢更多日志换吞吐。

### 8.3 完成定义与交付物

V1 完成需要：全部公共 API 可用；真实 Console/TextFile 与多目标；BQ 对齐正文和固定行布局；管理 reset/flush/drain；恢复与明确完成结果；共享/独立 worker；生命周期闭环；诊断开关；示例/构建/故障与性能证据。不能用只打印几条日志的 demo 替代。

新增 `examples/v1_async_logging.cpp` 演示构造、多个业务线程、显式长度含NUL、reset/poll、停止/join、shutdown(durable) 和检查结果；演示轮询必须让出 CPU，不写无 sleep 的 busy loop。README 写清 Console durable、同路径限制、关闭前提和write零进展的未完成表示。

每步报告“修改文件/接口、通过用例、未覆盖项、下一步”；最终验收报告记录命令、编译器、平台、退出码、原始输出位置、基准参数及结果。代码指南不能冒充这些执行证据。

## 9. BQLog 参考阅读路线

参考本地固定提交 `60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`：

1. `src/bq_common/platform/posix_misc.cpp::write_file`：先看短写推进，对齐一次flush内写完/实际错误/零进展返回的条件。
2. `src/bq_log/log/appender/appender_file_base.cpp::flush_write_cache`：看 ENOSPC 保留与其他错误重开，注意重开可能清缓存。
3. `src/bq_common/utils/util.cpp::_default_console_output`：看桌面 console_mutex_，保留 QLog pointer+length。
4. `src/bq_log/log/log_imp.cpp::flush_appenders_io`：看文件同步目标范围；不要把 Console 包装为持久化文件。
5. `src/bq_log/log/log_worker.cpp` / `.h`：看共享/独立与66ms等待；不复制全信号屏蔽、watchdog重启。
6. 正文按 ADR-016 已验收实现继续使用；不再建立 FormatPlan/cache，也不迁回 Producer 解析。

以上是机制参考；QLog 的 SPSC、自包含 Record、固定邮箱、消费配额和可检查完成结果仍按本指南实现。


## 10. 首次编码时直接采用的补充类型与函数清单

这一节补齐前文状态机使用的最小接口，避免到 worker 接线时再发明一个不兼容模型。它们仍是待实现声明；构造参数按表传真实引用，不把整个 Impl 传给 backend。

### 10.1 FrameLease 与释放规则

放 `detail/frame_lease.hpp`，包含 spsc_ring_buffer.hpp：

```cpp
class FrameLease final {
 public:
  FrameLease(SpscRingBuffer& ring, ReadHandle handle) noexcept
      : ring_(&ring), handle_(handle) {}
  ~FrameLease() noexcept { release_now(); }
  FrameLease(const FrameLease&) = delete;
  FrameLease& operator=(const FrameLease&) = delete;
  void release_now() noexcept {
    if (ring_ != nullptr) {
      ring_->release(handle_);
      ring_ = nullptr;
    }
  }
 private:
  SpscRingBuffer* ring_;
  ReadHandle handle_;
};
```

只对成功 ReadHandle 构造；异常离开时 release 不会猜帧长度。正常工作还须在后续 I/O 之前 publish_reclaimed；这个显式发布由 Session 执行，不能以 lease 析构替代它。FrameLease 不保存 DecodedRecordView，后者生命周期更短。

### 10.2 Session 的实际状态字段

在 backend_session.hpp 定义并在构造时初始化下列字段（省略构造函数体，不省略状态）：

```cpp
enum class StopPhase : std::uint8_t {
  running, drain_records, close_targets, detach_ready, detached
};
struct SessionBindings final {
  FilterState& filter;
  std::atomic<ProducerContext*>& published_context;
  ControlMailbox& mailbox;
  const RecordValidationPolicy& policy;
};
struct ChannelScan final {
  ProducerContext* active_head{nullptr};
  ProducerContext* next_channel{nullptr};
  ProducerContext* known_head{nullptr};
  ProducerContext* snapshot_head{nullptr};
  ProducerContext* discover_cursor{nullptr};
  ProducerContext* discover_boundary{nullptr};
  ProducerContext* sweep_start{nullptr};
  bool sweep_had_record{false};
};
```

BackendSession 字段顺序：`SessionBindings bindings_`、`BackendConfig backend_`、`vector<unique_ptr<Appender>> appenders_`、`vector<uint8_t> selected_`、`ChannelScan scan_`、`IoReport shutdown_report_`、`IoReport periodic_report_`、`ShutdownResult shutdown_result_`、`atomic<bool> stop_requested_{false}`、`FlushMode stop_mode_{buffered}`、`StopPhase stop_phase_{running}`、`size_t close_index_{0}`、`atomic<bool> detached_{false}`。诊断字段条件编译追加。

periodic_report 与 shutdown_report 必须独立：旧周期错误不能永久污染最终当前操作报告。周期 report 是固定槽临时观察，故障及历史已在 Appender 保存；每轮清 present 即可复用，不清字符串容量；增加 `IoReport::clear() noexcept`。reset 冷准备同时准备 next_periodic_report，应用时与 next_shutdown_report 一起 swap。最终 history 汇总保存在 shutdown_result_.retired_io，避免再引入第二份可不同步的汇总。

drain 判空按完整 Channel sweep：Producer 停止后发现全部发布 Context，记 sweep_start，逐 Channel 到 empty；一圈没有 Record、没有尚未发现 Context、没有未故障 Channel 的借用，才转 flush_targets。上一圈消费过记录就再做一圈确认。consumer_faulted Channel 计入已终止扫描并保留 drain_incomplete；不要直接用一个 Channel 的 empty 宣称整个 Logger 排空。

周期输出 visit 使用 `size_t next_output_` 游标，每 visit 最多服务一个到期/待容量处理目标，避免目标很多时扫到任意一个目标就全部flush。目标选择和一条Record的多目标内存复制仍需 O(Appender数量)，这是 V1 明确的成本；不是常数时间承诺。drain 扫描与输出游标互不覆盖。

### 10.3 Appender 的私有辅助函数

首次写 appender.hpp 就声明：

```cpp
IoWriteResult write_all(const std::byte*, std::size_t) noexcept;
FlushResult flush_impl(FlushMode, IoReport&, std::size_t,
                       bool final_attempt) noexcept;
void record_os_error(IoStage, int, std::uint64_t, bool,
                     IoReport&, std::size_t) noexcept;
void discard_and_account() noexcept;
void handle_write_error(int, IoReport&, std::size_t,
                        bool final_attempt) noexcept;
void mark_retry(std::chrono::steady_clock::time_point) noexcept;
```

再增加公开内部 `void merge_history_into(RetiredIoSummary&) noexcept` 和 `void abandon_after_worker_failure(IoReport&,size_t) noexcept`。前者只在退休/最终结束调用且防重入；后者不write/sync/reopen，只记录剩余、discard、close、closed，供worker.exited后的安全接管。不要让控制方访问基类private batch来复制一套清理算法。

派生类的运行路径上限、report target mapping 在线程启动前固定；新恢复路径更新后 `record_os_error` 从当前路径取数据。由于私有基类不能直接访问派生路径，增加受保护虚 `const std::string& output_path() const noexcept = 0;`；TextFile返回current_path_，Console返回预构造空串。它返回稳定引用，不能每次拼接字符串。

### 10.4 Worker 的声明与 Runtime 资源顺序

BackendWorker 内部公开 `start_shared(BackendRuntime&)`、`start_independent(BackendSession&)`、`wakeup() noexcept`、`exited() const noexcept`、`failed() const noexcept`、`join()`；start方法可抛线程创建异常，两个模式同一对象只可启动一次。成员：BackendWorkspace、WorkerWakeup、std::thread、atomic<bool> ready/failed/exited、模式与稳定Session/Runtime指针。构造只分配，不发布、不启动线程。

Runtime先构造ConsoleOutputGate，再构造共享Worker；独立Worker由Impl提前构造、其wakeup引用进入ChannelDependencies，直到全部Context回收之后才析构。Runtime::shutdown_all 需所有Logger已shutdown且不再创建；独立worker必须全部join后才能释放共用gate。不得让shared worker对象的析构顺序先销毁gate。

### 10.5 源码/声明一致性检查清单

新增头单独编译；每个非模板声明有唯一cpp定义；Appender虚析构out-of-line；所有unique_ptr incomplete-type析构在完整类型可见处定义；static函数只保留一个定义；FilterConfigAccess不同时留在旧cpp与新头；CMake target逐文件接入。

逐步编译允许新模块尚未连接公开Logger，但不允许用`return completed`空实现遮住未完成函数。写测试时显式人工驱动Session；真实线程只在步骤7生命周期全部接好后由公共构造启动。
