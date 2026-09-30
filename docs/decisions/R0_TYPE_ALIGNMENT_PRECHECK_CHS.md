# R0 前置修改通知：枚举、额外类与结构体

> 历史前置检查记录：本文描述清理/模块迁移前的类型位置，不是当前目录迁移指令。当前Ring已在buffer模块；后续Record/Layout目标路径和源码复核以[补全指南第0、2节](./R0_POST_CLEANUP_IMPLEMENTATION_GUIDE_CHS.md)为准。不要按旧detail路径恢复已删除接口或覆盖新草稿。


> **本清单为清理前审计，2026-09-23 已执行对应删除及基础声明修正。** 当前动手入口为 [补全实现指南](./R0_POST_CLEANUP_IMPLEMENTATION_GUIDE_CHS.md)；精确删除项见 [清理记录](./R0_CONFLICT_CLEANUP_20260923_CHS.md)。下文的“先改”说明保留问题来源，不表示这些文件现在仍存在。


日期：2026-09-23。核对的是 `/home/qq344/QLog` 当前源码和 BQLog `60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`。本次只更新指南，下面的代码是待实施方案。用户要求发现冲突先通知，再给方案；不能把清单描述为已经修改。

## 1. 现在先改的声明

### 1.1 include/qlog/log_level.hpp，namespace qlog

现有六个有效值一致，但 uint8_t 与 BQLog `include/bq_common/types/basic_types.h:20` 的 int32_t 不同，并缺少 log_level_max=32。若目标是声明和结构优先对齐，应一起改；记录头的 level 仍单独为 uint8_t。

```cpp
enum class LogLevel : std::int32_t {
    verbose = 0, debug = 1, info = 2,
    warning = 3, error = 4, fatal = 5,
    log_level_max = 32,
};
```

32 不是有效日志等级。现有 valid_level 若仍作为安全 helper 使用，先转 int32_t 再检查 `value >= 0 && value <= 5`；不能保留 uint8_t 截断再验证，否则 256 可错误变为 0。它是局部范围判断，不新增等级或 public 状态协议。

### 1.2 include/qlog/detail/argument_tag.hpp，namespace qlog::detail

整组替换，禁止只往旧枚举末尾追加 wide 字符类型。采用参考枚举项名，保留 QLog 类型名 ArgumentTag 仅作为命名映射：

```cpp
enum class ArgumentTag : std::uint8_t {
    unsupported_type = 0,
    null_type = 1,
    pointer_type = 2,
    bool_type = 3,
    char_type = 4,
    char16_type = 5,
    char32_type = 6,
    int8_type = 7,
    uint8_type = 8,
    int16_type = 9,
    uint16_type = 10,
    int32_type = 11,
    uint32_type = 12,
    int64_type = 13,
    uint64_type = 14,
    float_type = 15,
    double_type = 16,
    string_utf8_type = 17,
    string_utf16_type = 18,
    string_utf32_type = 19,
    string_utf_mixed_type = 20,
};
```

撤销旧 NullUtf8：nullptr 是 null_type；空的 C 字符串指针由对应字符串 helper 形成文本 null，不是另一个 wire tag。UTF-32 普通字符串参数转 UTF-16 存储；不要因为枚举存在 19/20 就声称 Layout 支持该参数编码。

### 1.3 include/qlog/detail/record_header.hpp，namespace qlog::detail

```cpp
struct alignas(8) RecordHeader {
    std::uint64_t timestamp_epoch; // epoch milliseconds
    std::uint32_t ext_info_offset;
    std::uint32_t category_idx;
    std::uint64_t log_thread_id;
    std::uint64_t format_hash;
    std::uint8_t log_format_str_type;
    std::uint8_t level;
    std::uint16_t padding;
    std::uint32_t log_format_data_len;

    static constexpr std::uint32_t get_head_size_without_format_str() {
        return offsetof(RecordHeader, log_format_str_type);
    }
};
static_assert(sizeof(RecordHeader) == 40);
static_assert(alignof(RecordHeader) == 8);
static_assert(offsetof(RecordHeader, timestamp_epoch) == 0);
static_assert(offsetof(RecordHeader, ext_info_offset) == 8);
static_assert(offsetof(RecordHeader, category_idx) == 12);
static_assert(offsetof(RecordHeader, log_thread_id) == 16);
static_assert(offsetof(RecordHeader, format_hash) == 24);
static_assert(offsetof(RecordHeader, log_format_str_type) == 32);
static_assert(offsetof(RecordHeader, level) == 33);
static_assert(offsetof(RecordHeader, padding) == 34);
static_assert(offsetof(RecordHeader, log_format_data_len) == 36);

struct RecordExtHeader {
    std::uint8_t thread_name_len_;
};
static_assert(sizeof(RecordExtHeader) == 1);
```

头文件包含 cstdint/cstddef；保留标准布局和 trivially_copyable 检查。BQLog 使用 packed 定义；上述 QLog 映射在当前目标 ABI 上用所有偏移断言约束同一字节布局，不声称适配任意 ABI。padding 不替代旧 flags，arg_count/args_bytes 不再保留。线程名字节紧随 RecordExtHeader，不塞 std::string 进记录。

### 1.4 include/qlog/detail/record_limits.hpp

- kRecordHeaderBytes 改为 sizeof(RecordHeader)，避免第二个 40 常量漂移。
- kTimestampStatusMask、kKnownFlagMask 退出新协议；不能继续写入 padding。
- kMaxArgCount=32、kMaxFormatBytes=8192 不作为新主链的额外限制；相应 requires 和测量失败分支一起替换。仍执行实际字段宽度、算术和 Buffer 可分配长度检查，不能把撤销旧人为限制解释为无限长度。

## 2. 不能原样接入新 Layout 的额外类型

| 当前位置/类型 | 冲突 | 对应方案 |
|---|---|---|
| format_spec.hpp: FormatSpec | 外置旧格式说明，缺 reset 差别 | 移入 Layout::FormatInfo；按参考初始化与 reset 实现 |
| 同文件: FormatError/FormatFailure/FormatResult | 固定工作区/输出上限和细分错误结果 | 退出核心；Layout 内 enum_layout_result 三项，输出指针/长度独立读取 |
| record_types.hpp: DecodedArg/DecodedRecordView | 拷贝头和中间参数数组 | LogEntryHandle 借用原始记录，直接算偏移 |
| 同文件: RecordMetadata/RecordValidationPolicy/MetadataError | 旧 flags、fallback 时间和等级策略协议 | 头由 begin 写入；结构检查映射 log_entry_handle::validate；level/category 在各对应路径检查 |
| 同文件: EncodeError/DecodeError/DecodeFailure/EncodeResult/DecodeResult/RecordCodecAccess | 旧两阶段 codec 结果协议与构造访问层 | 不作为新主链 API；按参考测量/填充/视图职责重建后移除旧调用者 |
| record_measure.hpp: FormatInput/PreparedRecord/MeasureResult/MeasureFailure/MeasureError/RecordMeasureAccess | 预归一化数组、最大 32 参数、预存 hash 的旧协议 | 对应 size_seq 测量和递归参数填充，不再构造 PreparedRecord 数组 |
| argument_traits.hpp: NormalizedArgument/ArgumentKind/ArgumentTraits | 旧 tag、拒绝 char16/32/wchar、强制包装输入等 | 对应参考类型分类、存储类型映射和 _type_copy；不是把第二个 enum 改几个数值即可 |
| arguments.hpp: PointerArgument/CStrArgument | 必须 ptr/cstr 才能走对应输入的旧入口 | 新主链按参考直接识别指针和字符串；这些包装不再是必要层，不保留为默认兼容层 |
| log_format.hpp: FormatView/runtime_format | 只有 UTF-8、额外 stored_hash 输入模型 | 入口对齐模板字符串 helper，按输入字符类型测量与复制；不强制先包装 FormatView |
| admission_clock.hpp: ClockDescriptor/Timestamp 及 ClockSource/Domain/Unit/CalibrationKind/SamplingPoint/TimestampStatus | 纳秒、fallback 标志、旧 admission 时点 | 对应平台 epoch 毫秒取得方式；begin 前采样；Layout 无需这些类型 |
| format_hash.hpp: HashResult/HashError/FormatHashDispatch/HashBackend | 旧失败结果和依赖注入不能自动视为参考 hash 路径 | 实际 hash/copy helper 与参考对应；算法和软件/硬件分派核对后复用，旧包装不成为 LogEntry/Layout 依赖 |

表中的“退出核心”表示新链不依赖，不是现在孤立删除文件。必须先把编码器、测试和调用者迁移为一组可构建修改。可以保留历史测试材料，但不得让新示例走旧接口而声称对齐。

## 3. 后续模块中已经明确冲突的类型

这些现在通知，但无需为了写 Layout 一次性实现全部运行态。

| 当前位置/类型 | 冲突与替换方向 | 阶段 |
|---|---|---|
| log_result.hpp 全组 LogStatus/FailureStage/FailureReason/LogFailure/LogResult/LogResultAccess | 参考 public do_log 返回 bool，Buffer 有独立 enum_buffer_result_code；不保留一套统一细分 public 失败协议 | R1/R2 |
| async_logger.hpp: LoggerConfig/AsyncLogger::Impl | 固定 SPSC 配置、空目标自动 Console、业务对象独占运行态；参考轻量 log + Manager 持有 log_imp | R1 |
| appender_config.hpp: AppenderConfig/FilterConfig/AppenderType | 扁平配置不是配置树；参考 appender_base::appender_type 为 console/text_file/raw_file/compressed_file/type_count | R1/R4 |
| channel.hpp: ChannelDependencies/ChannelCold/Channel | 冷依赖捆绑旧 policy/clock/hash，固定 SPSC 通道是主链对象 | R2，按 LogBuffer/TLS/LP/HP 结构替换 |
| producer_handle.hpp: ProducerHandle | 旧显式/固定 Channel 关联 | R2，业务端用参考自动入口，不保留兼容注册层 |
| producer_context/context_result/producer_identity | 旧线程上下文注册、身份耗尽和结果包装 | R2，按 buffer ID、TLS 缓存/map、上下文和退休机制重做 |
| call_gate.hpp: CallGate | 新增详细分类结果不对应参考 is_enable_for bool | R1/R2，改为对应过滤接口 |
| filter_state.hpp: FilterState | 现有独立冻结配置/发布模型不能替代参考 merged level/category mask 所属与更新 | R1/R3，与 Logger/Appender 配置一起迁移 |
| producer_policy/producer_result_map | 旧 gate→measure→reserve→encode→result 映射 | R2，随旧结果层一起退出 |
| SpscRingBuffer、ReserveStatus/ReadStatus、读写 Handle、FrameHeader/FrameLayout | 仅已验收为 QLog 原 SPSC，不是已经对齐 BQLog SISO/MISO/LogBuffer 的证明 | R2 前逐字段/状态核查，不能原样冻结，也不能只凭“BQLog 也有 SPSC”认定可直接用 |

Appender 枚举按参考建完整取值不表示 raw/compressed 已实现；未支持的类型不能映射为 text_file。新增 MemoryPolicy 和 ThreadMode 按参考 `discard/block/expand`、`sync/async/independent`；不要复用旧非阻塞失败枚举表达等待重试。

checked_size 等纯算术函数、版本信息和仅供测试访问的辅助结构，不因“参考没有同名类型”就自动有语义冲突；仍需不改变可接受输入、字节协议、所有权和结果行为。容器内部实现可以映射，不凭空增加生产抽象层。

## 4. 修正前面指南中的占位命名

LayoutResult、Layout::Result、enum_layout_result 不应三个同时存在。选唯一 `Layout::enum_layout_result`，成员 finished/to_be_continue/parse_error 与参考相同。FormatInfo 只在 Layout 内定义一次。

前文 CategoryNames、CharBuffer、ThreadNameMap 只是讲解占位符，不是要新增三个类。实际声明直接写现有容器类型（例如 vector<string>、vector<char>、unordered_map<uint64_t,string>），或使用具备参考语义的项目容器；不再加拥有型封装。vector 的 size 必须覆盖可写范围，不能只 reserve 后越界写。输出仍是内部 pointer+length，不额外加拥有结果对象。

## 5. 修改顺序

1. 先修改 LogLevel、ArgumentTag、RecordHeader/RecordExtHeader、record_limits；同时标记/迁移所有旧协议调用者。此步声明代码见上文，不运行混合新旧协议的样例。
2. 重建类型识别、size_seq、填充和 LogEntryHandle；去掉 NormalizedArgument/PreparedRecord/DecodedArg 中间层；用独立 fixture 验证新记录，不等待旧 AsyncLogger 能工作。
3. 以 TimeZone 和 Layout 对象重写 text_formatter 头/cpp；使用唯一内部格式说明/结果枚举；按完整指南逐函数实施。
4. 对新链做真实 BQLog 字节差分及库链接；旧测试按所验证合同迁移，不把“旧测试全过”作为不可改 ABI 的理由。
5. R1–R5 依次替换配置、Logger/Manager、Buffer/Producer、Worker/Appender 和管理接口。每阶段再做当前源码差异通知，禁止另加未经说明的 Session、结果或兼容包装。

现在先处理第 1、2 组声明和直接调用链；第 3 组已有明确替换方向，但不能提前声称其并发/所有权细节已逐项对齐。
