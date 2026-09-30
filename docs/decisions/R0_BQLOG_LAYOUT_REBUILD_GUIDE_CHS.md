# R0 重建指南：BQLog Record、记录视图与完整 Layout

> **2026-10-01 最新决定与进度：** 参数命名按 BQLog 实际含义对齐，代码格式沿用 QLog 当前要求；扩容按参考 layout.cpp:1100，不增加 layout_failed_ 或通用防御性错误协议。Layout/UTF cpp 已接入 CMake，但扩容仍为空，完整布局尚未实现；Debug/Release 库与现有 smoke 可执行文件构建通过，保留空扩容函数警告；未运行测试，未验证完整 Layout 调用方链接。详见 [构建记录](../validation/LAYOUT_CMAKE_20261001_CHS.md)。详见 [命名与扩容补充](./LAYOUT_ALIGNMENT_20261001_CHS.md)。本条优先于下方历史进度。

> **2026-09-29接续状态：** Record视图和参数序列化已填写，Record cpp已接入qlog；Debug的QLOG_DEBUG已PUBLIC传播，Debug/Release构建及调用方链接通过。下一模块从本文第6节TimeZone开始，结合补全指南第5节。行为/差分验收仍未完成；详见[TimeZone交接](./TIMEZONE_HANDOFF_20260929_CHS.md)。

> **2026-09-23 Ring补充决定：旧字节Ring/配置及相关测试基准、过期Ring文档已删除。** 当前库仅版本基础；新SISO按8字节block、外部内存、32位游标重建，见 [Ring实现指南](./R0_BQLOG_BLOCK_RING_GUIDE_CHS.md) 与 [清理记录](./R0_RING_CONFIG_CLEANUP_20260923_CHS.md)。此前47项测试只是本次Ring删除前的历史结果。


> **2026-09-23 清理后入口：先按 [补全实现指南](./R0_POST_CLEANUP_IMPLEMENTATION_GUIDE_CHS.md) 查看实际文件状态、LogEntryHandle代码与构建步骤；本文保留完整Layout的源码依据和逐函数算法。旧六参数接口及旧codec已删除，不再是待保留的生产文件。**


创建：2026-09-22；复核更新：2026-09-23。状态：源码核对后的开发指导，尚未实施、编译或完成差分验收。

QLog 权威目录 `/home/qq344/QLog`，起点 `feat/spsc-ring-opt@94fefc0f29b21a1dbdd258fdb5725aadf84d770e`。参考目录 `E:/VisualStudioProject/BqLog`，参考提交 `60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`。本文固定该源码版本，不以最新版 README 或标准格式库代替实际实现。

> 开始修改前先读 [枚举/类/结构体前置修改通知](./R0_TYPE_ALIGNMENT_PRECHECK_CHS.md)：列出准确声明和需要退出的旧包装层。

当前接续顺序、源码复核范围及模块目录以[补全指南第0、2节](./R0_POST_CLEANUP_IMPLEMENTATION_GUIDE_CHS.md)为准。Record归qlog::record，TimeZone/Layout归qlog::layout，共用UTF转换归qlog::utility。本轮只修订文档，不宣称这些目标路径已完成源码迁移。

## 1. 已确认的决定

用户确认整个当前主链优先对齐 BQLog。已经实现、验收或冻结的 QLog 类型也不能阻止对齐。本轮不保留六参数正文函数、调用方固定缓冲、DecodedArg 数组、旧 FormatResult、旧 32 字节 RecordHeader、纳秒时间和旧 tag 编号作为先决条件。

`text_formatter.hpp/.cpp` 重写成完整 Layout 对象：拥有可复用字符缓冲、当前游标、格式说明、线程文本缓存；读取原始记录视图，输出时间/线程/等级/类别/正文；调用后通过指针和长度借用内部结果。每个目标完整调用一次。不增加正文共享或跨目标格式化缓存。

本指南替代旧 R0 的正文抽取方案。模块文件使用include/qlog/layout/layout.hpp及src/layout/layout.cpp，类名使用 `qlog::layout::Layout`；名称变化不意味着保留旧行为。以下 QLog 名称是落地映射，BQLog 源函数是行为依据。

## 2. 源码地图与阅读顺序

以下路径相对 BQLog 根目录，行号供本提交定位，函数名为稳定检索锚点。

| 文件 | 入口 | 要解决的问题 |
|---|---|---|
| include/bq_log/misc/bq_log_def.h | `_log_entry_head_def`、`log_arg_type_enum` | 头布局、tag 编号 |
| include/bq_log/misc/bq_log_wrapper_tools.h | `_get_log_param_type_enum`、`size_seq`、`_type_copy` | 每个参数如何测量与编码 |
| include/bq_log/misc/bq_log_impl.h | `log::do_log`、`_do_log_args_fill` | 过滤、测量、预留、填充、提交 |
| src/bq_log/api/bq_log_api.cpp:199 | `__api_log_write_begin/finish` | 取时、头、格式串、线程扩展 |
| src/bq_log/log/log_types.h/.cpp | `log_entry_handle`、`validate` | 记录借用和偏移 |
| src/bq_log/utils/time_zone.h/.cpp | `time_zone` | 配置解析、日历转换、时间文本缓存 |
| src/bq_log/log/layout.h | `layout` | 对象、成员、全部接口 |
| src/bq_log/log/layout.cpp:326 | `do_layout` | 完整布局入口 |
| 同文件:340、377、395 | `layout_prefix/insert_time/insert_thread_info` | 前缀 |
| 同文件:423、540、615 | `c20_format/fill_and_alignment/fill_e_style` | 格式说明、填充、指数 |
| 同文件:651、663、863 | `python_style_format_content*` | 正文分派、UTF-8/UTF-16 扫描 |
| 同文件:1100–1496 | `expand_*`、`insert_*`、`reverse` | 扩容、字符/数值转换 |
| src/bq_log/global/log_vars.h | `log_level_str_`、`digit3_array` | `[V]…[F]`、000–999 三位表 |
| src/bq_log/log/appender/appender_console.cpp | `log_impl` | Logger 名、控制台消费结果 |
| src/bq_log/log/appender/appender_file_text.cpp | `log_impl` | 复制布局结果并追加换行 |

当前执行路径用普通 `python_style_format_content`；`*_legacy` 是参考中的旧实现/性能对照，不能误把它当作当前基线。

## 3. QLog 文件处理表

| QLog 文件/范围 | 本轮动作及原因 |
|---|---|
| include/qlog/layout/layout.hpp | 整体替换声明：Layout、其内部 FormatInfo 与 enum_layout_result；不保留六参数为核心 |
| src/layout/layout.cpp | 用户已删除；按本文重新创建，不恢复旧 compose_line |
| RecordHeader/tag 所在头、encoder/decoder | 重建为下面记录布局和读取方式；旧 32 字节/旧 tag 断言与测试同时迁移 |
| record_decoder 中 DecodedArg 路径 | 不再作为 Layout 入口；新主链通过 LogEntryHandle 读取参数字节 |
| format_spec.hpp | 旧格式状态和结果若仅服务旧格式器，退出新链；先查调用者，再迁移/移除 |
| admission_clock/时间字段 | epoch 毫秒，调用时点对应写入分配前；不得只换字段名称 |
| 新增 record/log_entry_handle.hpp、src/record/log_entry_handle.cpp | 记录视图与 validate；可合并到项目已有 record 文件，但只保留一个权威布局定义 |
| 新增 layout/time_zone.hpp、src/layout/time_zone.cpp | 对齐时区对象，替代旧固定偏移/CalendarCache 集成 |
| Hash helper | 核对 BQLog 拷贝与 hash 的实际路径；不能沿用旧“所有格式串必有同种 hash”的保证 |
| Producer/Channel | 旧记录写入接口退出；先实现独立记录样本编码器，R2 再接 LP/HP 预留/提交 |
| Appender/Worker | R0 定义借用和消费方式；R3/R4 接真实所有权和逐目标调用 |
| CMake 与测试 | 纳入新编译单元；同一新基线不得混用旧 encoder 与新 Layout |

不要为保证旧测试全绿而保留冲突 ABI。旧测试中仍成立的边界检查可移植；与旧协议绑定的期望应标明迁移原因。生产代码和指南必须分开标识完成状态。

## 4. 先重建记录，避免 Layout 再包一层旧 codec

### 4.1 40 字节头

| 偏移 | 类型 | BQLog 字段 | 含义 |
|---:|---|---|---|
| 0 | uint64_t | timestamp_epoch | epoch 毫秒 |
| 8 | uint32_t | ext_info_offset | 从记录起点到线程扩展区 |
| 12 | uint32_t | category_idx | Logger 类别表下标 |
| 16 | uint64_t | log_thread_id | 线程 ID |
| 24 | uint64_t | format_hash | 格式串 hash，部分路径为 0 |
| 32 | uint8_t | log_format_str_type | 格式串编码 tag |
| 33 | uint8_t | level | verbose…fatal |
| 34 | uint16_t | padding | 填充，不应假定生产路径主动清零 |
| 36 | uint32_t | log_format_data_len | 格式串存储字节数 |

头自身 8 字节对齐，总长 40。`get_head_size_without_format_str()` 返回 32，表示不含格式描述字段的部分，不是完整头大小。实现时为 sizeof 和每个 offsetof 建静态断言；这是协议检查，不以编译器默认布局猜测。

```text
[40 字节头][format bytes][补齐到 4 字节][args bytes][name_len:u8][name bytes]

args_offset = 40 + align4(format_bytes)
ext_info_offset = args_offset + args_bytes
record_bytes = ext_info_offset + 1 + name_len
```

线程名来自 producer 的线程信息，参考限制最多 16 字节；按字节截断，不声称 Unicode 字符边界。字符串和结果都不依靠尾部 NUL 表示长度。参数区没有旧 arg_count 字段，遍历截止点由 ext_info_offset 决定。

### 4.2 参数 tag 与步长

| tag | 类型 | 值相对参数起点的位置 | 整个参数步长 |
|---:|---|---|---:|
| 0 | unsupported | 不支持 | 不作为正常参数编码 |
| 1 | null | 无值 | 4 |
| 2 | pointer | +4，固定 uint64_t | 12 |
| 3 | bool | +2 | 4 |
| 4 | char | +2 | 4 |
| 5 | char16 | +2 | 4 |
| 6 | char32 | +4 | 8 |
| 7/8 | int8/uint8 | +2 | 4 |
| 9/10 | int16/uint16 | +2 | 4 |
| 11/12 | int32/uint32 | +4 | 8 |
| 13/14 | int64/uint64 | +4 | 12 |
| 15 | float | +4 | 8 |
| 16 | double | +4 | 12 |
| 17/18 | UTF-8/UTF-16 字符串 | +4 为 u32 字节数 n，+8 为字节 | 8+align4(n) |
| 19/20 | UTF-32/mixed | 不是此 Layout 的正常参数存储类型 | 不支持 |

tag 只写首字节，其余槽内填充不是序列化数据，不要求与参考未初始化内存逐字节相同。普通 UTF-32 字符串参数先转 UTF-16 存储，tag 为 18；单个 char32 参数仍为 tag 6。

保留字节偏移不意味着照搬非对齐 typed load：+4 的 uint64_t/double 未必 8 字节对齐。QLog 可用 memcpy 读入局部标量，保持字节格式和可观察结果。它不是另设 DecodedArg 数组，也不是新的格式语义。

### 4.3 Producer 测量与填充顺序

对应参考 `do_log`：先过滤；测量格式串存储字节；`make_size_seq<true>(args...)` 取得参数原始存储大小及 4 字节对齐总长；begin 预留记录；填参数；finish 写线程扩展并提交。固定宽类型大小编译期决定，动态字符串在测量阶段取得大小，递归填充时复用测量结果。

`_type_copy(value, data_addr, data_size)` 的 data_size 是该值编码所需大小，步进使用对齐后的大小，不应把补齐后的长度写成字符串内容长度。字符串 n 不含终止符；C 字符串、数组、带长度对象、自定义类型分别跟随参考 helper，不能统一用 strlen。

begin 在 Buffer 分配前取得 epoch_ms；block 重试沿用该时间，不在成功时另取旧 admission timestamp。begin 写头和格式串；UTF-8/16 直接拷贝路径用 bq_memcpy_with_hash，初始 hash 为 0。没有直接数据地址等路径由上层 `_type_copy<false>` 写入格式区域，不要补一个“总会计算 hash”的假设。

UTF-32 格式串必须单列核查：参考 begin 有转换至 UTF-16 的分支，但这里仍记录输入格式 tag；layout 分派只接受 UTF-8/16。不能把“能传入 UTF-32”直接宣传为该提交完整支持 UTF-32 格式串。普通 UTF-32 参数转换与此不是同一件事。

## 5. LogEntryHandle：函数、参数、返回值

对象只持有 `const uint8_t* data_` 和 `uint32_t size_`，不拥有内存。来源必须覆盖完整记录生命周期。

| 建议对应签名 | 参数/返回 | 内部实现 |
|---|---|---|
| LogEntryHandle(const uint8_t* data, uint32_t size) | 记录起点和总长；构造无返回 | 保存二者，不复制记录 |
| const uint8_t* data() const | 原指针 | 直接返回 |
| uint32_t data_size() const | 总字节数 | 直接返回 |
| const RecordHeader& get_log_head() const | 借用头 | 起点满足头对齐、对象/存储契约后读取 |
| RecordHeader& get_log_head() | 可修改头引用 | 仅实际可写记录使用；参考通过 const_cast 暴露，不得借此修改只读 fixture |
| const char* get_format_string_data() const | 格式字节起点 | data+40 |
| size_t get_log_args_offset() const | 参数偏移 | 40+align4(format_len)，先提升宽度 |
| const uint8_t* get_log_args_data() const | 参数起点 | data+args_offset |
| uint32_t get_log_args_data_size() const | 参数总长 | ext_offset-args_offset |
| const RecordExtHeader& get_ext_head() const | 扩展头 | data+ext_offset |
| LogLevel get_level() const | 等级枚举 | 头字段转换，不自动校验范围 |
| uint32_t get_category_idx() const | 类别索引 | 读头 |
| bool validate() const | true 表示该结构检查通过 | 按下述步骤，不等于 UTF 合法性或业务有效性 |

validate 顺序：总长至少 41；格式 tag 合法集合；宽整数计算 args_offset 且不越界；ext_offset 不在参数起点之前；扩展头和线程名不越界；遍历参数区，至少能读 tag 槽，按表计算步长，字符串先保证 8 字节再读取 n，宽整数计算对齐长度，步长不超过剩余。直到参数区末尾。

参考 validate 接受格式 UTF-32 tag，但 layout 不处理它；不额外保证 bool 为 0/1、UTF-16 长度为偶数、UTF 编码有效、level/category 有效。主链何处调用 validate 应明确设计并验证，不声称 BQLog do_layout 自动执行完整 validate。

## 6. TimeZone 对象

不能重新补回旧 TimeZoneConfig 来迁就已撤销的 compose_line。对应参考 time_zone，持有 local 标志、小时/分钟偏移、与 GMT 的毫秒差、名称字符串、129 字节时间缓存、有效长度、上次 epoch_ms。

| 对应签名 | 返回值及实现 |
|---|---|
| TimeZone(const String& text="localtime") | 构造并解析；String 是项目字符串类型映射 |
| void reset() | 恢复 local、偏移 0、空名称、缓存长度 0、last_epoch 0 |
| void parse_by_string(const String& text) | trim+大写；识别 LOCAL/LOCALTIME/LOCAL_TIME、GMT/Z/UTC、UTC±H[:MM]；失败诊断并保留默认 local 路径，非 bool 事务 API |
| void restore_by_config(bool local,int32_t hours,int32_t minutes,int32_t diff_ms,const String& name) | 恢复配置字段；另核对调用前缓存初始化，不假定重置一切 |
| bool get_tm_by_epoch(uint64_t epoch_ms, tm& out) const | 输出日历转换结果，失败 false；local 用系统 localtime，固定偏移加偏移后 gmtime |
| String get_time_str_by_epoch(uint64_t epoch_ms) const | 构造时间文本；与 layout 的带时区缓存文本不是同一个接口 |
| static String get_local_timezone_name() | 系统本地时区名称 |
| void refresh_time_string_cache(uint64_t epoch_ms) | 相同完整毫秒值不刷新，否则 inner_refresh；不是自行优化为秒级 cache |
| const char* get_time_string_cache() const | 借用缓存 |
| size_t get_time_string_cache_len() const | 有效长度 |
| 各配置 getter | 返回字段；get_offset_to_epoch_ms=(hours*3600+minutes*60)*1000 |

解析范围：小时 [-12,14]，分钟 [0,59]，负小时使分钟为负。local 跟随系统 DST，固定时区无 DST。固定零偏移名称为 UTC0。缓存用于输出 `时区 年-月-日 时:分:秒.`，毫秒由 Layout 追加。

疑点分别记录：`-00:30` 因小时值为 0 丢失负号；新对象 epoch=0 时 last_epoch 同为 0，可跳过初次缓存生成。这些需最小复现后决定修复，不能默认为正常时区语义，也不能为了对齐照抄错误而不说明。

## 7. Layout 头文件应包含什么

以下是接口骨架，依赖类型需随上述步骤落地；不是声称复制此段即可构建的完整实现。保留参考拼写 `get_formated_str` 方便逐函数对应。

```cpp
class Layout {
public:
    enum class enum_layout_result { finished, to_be_continue, parse_error };
    Layout();
    enum_layout_result do_layout(const qlog::record::LogEntryHandle& entry,
                     TimeZone& zone,
                     const CategoryNames* categories);
    const char* get_formated_str();
    uint32_t get_formated_str_len() const;
    void tidy_memory();

private:
    struct FormatInfo {
        bool used = false;
        bool upper = true;
        char fill = ' ';
        char align = '>';
        char sign = '-';
        char prefix = ' ';
        uint32_t offset = 0;
        uint32_t width = 0;
        uint32_t precision = UINT32_MAX;
        char type = ' ';
        void reset();
    };

    enum_layout_result layout_prefix(const qlog::record::LogEntryHandle& entry);
    enum_layout_result insert_time(const qlog::record::LogEntryHandle& entry);
    enum_layout_result insert_thread_info(const qlog::record::LogEntryHandle& entry);
    void python_style_format_content(const qlog::record::LogEntryHandle& entry);
    void python_style_format_content_utf8(const qlog::record::LogEntryHandle& entry);
    void python_style_format_content_utf16(const qlog::record::LogEntryHandle& entry);
    template<class Char> FormatInfo c20_format(const Char* style, int32_t len);
    void fill_and_alignment(uint32_t begin);
    void fill_e_style(uint32_t exponent, uint32_t begin);
    void expand_format_content_buff_size(uint32_t required);
    uint32_t insert_str_utf8(const char* bytes, uint32_t len);
    uint32_t insert_str_utf16(const char* bytes, uint32_t len);
    void insert_pointer(const void* value);
    void insert_bool(bool value);
    void insert_char(char value);
    void insert_char16(char16_t value);
    void insert_char32(char32_t value);
    uint32_t insert_integral_unsigned(uint64_t value, uint32_t base = 10);
    uint32_t insert_integral_signed(int64_t value, uint32_t base = 10);
    void insert_decimal(float value);
    void insert_decimal(double value);
    void reverse(uint32_t begin, uint32_t end_inclusive);

    TimeZone* time_zone_ptr_ = nullptr;
    const CategoryNames* categories_name_array_ptr_ = nullptr;
    CharBuffer format_content;
    uint32_t format_content_cursor = 0;
    ThreadNameMap thread_names_cache_;
    FormatInfo format_info_;
};
```

CategoryNames、CharBuffer、ThreadNameMap 仅为上段讲解占位符，不是新增类：实际声明直接使用字符串数组、可增长字符数组、uint64→字符串 map。使用 QLog 容器/标准容器时保持下面 size/capacity、指针失效、缓存语义；不是必须复制整个 bq_common。模板 c20_format 的定义放 cpp 且仅由该 cpp 实例化即可，不需为此把全部实现暴露到 public 头。

### 7.1 成员不变量与返回值

- format_content 的“已分配可写元素数”覆盖 [0,cursor)，cursor 是有效字节数；不是 capacity 大就可越过 vector.size 写。
- 正文分派成功完成后写入尾部 NUL，cursor 不包含它；前缀失败等未完成路径不保证 NUL 终止。`get_formated_str()` 在缓冲为空时返回 nullptr，否则返回起点；长度 getter 返回 cursor。
- 视图只用于本次结果；下次布局、扩容、tidy 或析构后不可继续使用。time_zone/categories 在调用期间必须有效。
- 同一 Layout 不可并发/重入。参考头注释称 static，但实际缓冲是成员；约束应来自对象共享及 Manager/Logger 调度，不写成全进程所有 Layout 互斥。
- do_layout 返回格式化状态，不表示 Appender 写入成功。`to_be_continue` 在枚举中存在，但本次已检查的入口没有实现可恢复分段输出协议，不编造续传 API。
- 无固定 64KiB 缓冲上限，也不把含分配的路径一概声明 noexcept。

### 7.2 do_layout 与内存管理

do_layout 参数 entry 是借用记录，zone 是该目标时区，categories 是 Logger 类别名称数组。依次绑定指针、cursor=0、请求至少 1024 可写字节、layout_prefix；前缀失败原样返回；正文分派；返回 finished。参考并不在入口全面清零 format_info，跨调用状态测试也应保留。

expand(new_size)：按2026-10-01决定对齐参考，在 format_info.offset 非0时按 uint32_t 运算加 width+precision+2；已有 size 足够直接返回，否则 round_pow_of_two 后以 vector.resize 建立可写范围。保留精度哨兵参与的有定义回绕，不增加 layout_failed_ 或扩展 parse_error 合同。极端容量与后续越界风险仍未验收，不声称任意输入安全；详见命名与扩容补充。

tidy_memory：capacity>1024 时 clear 并设容量为 1024；最后 cursor=0。它不清空线程名 map。因此“可复用缓冲”不等于永远保留峰值内存。

## 8. 完整前缀逐函数实现

`insert_time(entry)`：取 timestamp_epoch；刷新 zone 的缓存；复制缓存长度个字节；取 timestamp%1000，通过三位表复制恰好 3 字节；返回 finished。不得在毫秒后额外插入空格。

`insert_thread_info(entry)`：按 log_thread_id 查缓存。未命中则构造 `[tid-十进制ID 空格线程名]\t`（结尾为 `]` 加 tab），线程名读取扩展长度；存 map。命中直接追加缓存文本。空线程名时 ID 后仍有空格；同 ID 后续改名不会自动刷新这个缓存。

`layout_prefix(entry)`：insert_time → insert_thread_info → 校验 level 范围 → 追加三字节 `[V]/[D]/[I]/[W]/[E]/[F]` 和 tab → 校验 category_idx → 非空类别追加 `[类别]\t`。空类别不加括号和 tab；越界返回 parse_error。此前已写出的部分前缀仍在缓冲中，调用者必须检查结果。

示意字节（`\t` 表示一个 tab）：

```text
UTC0 2026-09-22 10:20:30.007[tid-123 worker]\t[I]\t[net]\tconnected 8
```

Layout 本身不添加 Logger 名和换行。Console 和 TextFile 的差别在第 12 节。

## 9. c20_format：不要替换成 std::format 的语法

参数 style 指向 `{` 后第一个字符；len 包含末尾 `}`。返回局部 FormatInfo，不直接写正文。新 fi.used=true；若首字符不是冒号直接返回默认说明，所以 `{0}`、`{1}` 不实现位置索引，按参数出现顺序消费。

主要步骤：从冒号后逐字符扫描，内部最多处理到索引 10；遇 `}` 记录 offset；遇嵌套 `{`、长度边界等执行参考回退；宽度/精度的数字暂存最多两位，不能自行扩成任意整数语法。

1. `< > ^` 首次出现设置 align；fill、符号和对齐按源码顺序更新，不套用标准库解析器。
2. `+/-` 设置 sign，`#` 设置 prefix；前导零设置填充。左/居中对齐的某些分支强制空格，不能一律允许任意 fill。
3. `.` 切换精度阶段，保存此前宽度并重置数字暂存。
4. 支持识别 b/B、e/E、f/F、x/X、o、d；大写 B/E/X 转小写内部 type 并设 upper。F 的分支行为与 f 不要凭直觉统一。
5. 有冒号而没显式 type 的结尾默认 d；无冒号的默认 type 是空格。offset 是解析消耗位置，不是输出位置。

`FormatInfo::reset()` 与成员初始值有一处重要差别：reset 的 align 为 `<`，默认构造为 `>`。必须逐函数映射，不能合并为“统一初始化”而未做差分。

## 10. 正文扫描与参数转换

### 10.1 UTF-8 scanner

从 entry 取得 format_ptr/format_len、args_ptr/args_len，使用两个独立游标。args_len==0 时直接复制整个格式串，连花括号也不做转义；它与“有参数但没有占位符”是不同分支。

循环找 `{` 或 `}` 并复制前面的字节。参考 find_brace_and_copy 含 SIMD 派发；可先复刻参考软件分支作为基线实现步骤，但不得宣称 SIMD 路径也已移植。软件和 SIMD 应做同一字节语料对照。

- `}}` 折叠成一个 `}`；单独 `}` 原样输出。
- `{` 后最多向前看 20 个字符，遇嵌套 `{` 判当前起始括号为普通文本。不要预先按标准 fmt 把 `{{` 折叠。
- 有剩余参数且找到闭括号：调用 c20_format；按其 offset 或完整 spec 长度推进格式游标；保存输出 begin；读取当前 tag；调用对应 insert；按第 4 节推进参数游标；fill_and_alignment(begin)。
- 没有参数/没有找到有效闭括号：输出当前 `{`，后续继续扫描。多余参数不会自动追加到正文。

每个正常 tag 的读取偏移必须和编码器共用同一契约；不要先解码成 vector<DecodedArg> 再返回旧正文函数。

### 10.2 UTF-16 scanner

format_len 仍是字节数，扫描单位是 char16_t；正文输出始终 UTF-8。参考有查括号并转换 ASCII 段的快速 helper，对非 ASCII 段逐码点转换。必须分别复刻两分支：UTF-16 没有 UTF-8 的 args_len==0 整段复制捷径，无参数时仍可能折叠 `}}`；其 spec 前瞻还允许跳过成对 `{{`，而 UTF-8 遇嵌套 `{` 立即终止该次占位符识别。不能直接把 char 替换成 char16_t 后声称相同。

`insert_str_utf16(const char* bytes, uint32_t len)` 中 len 是输入字节数；参考申请 `len*3/2+1` 输出空间，以 len/2 个 code unit 调用 utf16_to_utf8，返回实际追加的 UTF-8 字节数。不要把 UTF-8 字节长度当显示列数，也不要用 strlen 处理含 NUL 的参数。

参考 helper 在 `src/bq_common/utils/util.cpp:653`：ASCII 含 NUL 按长度复制；低于 0x800 写 2 字节；有效代理对合成为码点写 4 字节；孤立低代理忽略；高代理后不是低代理时丢掉高代理但不消费后一个码元；其他 BMP 写 3 字节。软件 helper 忽略 dst_character_num，因此调用方必须先保证容量。公开 utf16_to_utf8 在长度>=8 且支持 SIMD 时先尝试 ASCII，再接 SIMD/软件尾段。主 scanner 的非 ASCII fallback 也应逐分支核对。非法 Unicode 与 SIMD 一致性仍需运行差分，不用标准库默认替换字符代替参考。

### 10.3 各 insert 函数

| 函数 | 参数及返回值 | 实现要点 |
|---|---|---|
| insert_str_utf8(bytes,len) | len 为字节；返回 len | 扩容、memcpy、cursor+=len |
| insert_str_utf16(bytes,len) | 输入字节；返回输出字节 | 上述 UTF 转换后按实际长度推进 |
| insert_bool(value) | void | TRUE/FALSE，大写，后续统一对齐 |
| insert_char(value) | void | 追加一个原始字节 |
| insert_char16(value) | void | 按参考 UTF-16→UTF-8 路径 |
| insert_char32(value) | void | value<=0xFFFF 时取 UTF-16 路径；否则减 0x10000，生成高/低代理两码元，再调用 insert_str_utf16；不是独立通用 UTF-32 converter |
| insert_pointer(value) | void | 空值输出 null；非空追加 0x，再调用无符号整数、base=16；该 helper 仍受当前 format_info 影响 |
| insert_integral_unsigned(value,base) | 返回本次转换实际追加的字符数 | base 可由说明覆盖；见下 |
| insert_integral_signed(value,base) | 返回本次转换实际追加的字符数 | 同上但符号和负数处理独立 |
| insert_decimal(float/double) | void | 参考逐位截取小数算法，非标准库格式化 |
| reverse(begin,end) | void；end 为包含端点 | 两端交换直到 begin>=end |

整数先记录 begin；默认 base10，说明可切换二/八/十六。`#` 仅为二/十六进制加前缀。unsigned 的 `+` 只在 value>0 时输出，signed 的 `+` 在 value>=0 时输出；这不是可随意统一的小细节。

普通路径反复取模写低位、除以 base，再 reverse；signed 负值小余数取正，避免直接在有符号域对 INT64_MIN 取负。十进制大值且非 e 时，参考按 1000 分组查三位表，通过临时数组拼接；移植该路径须与简单数值域逐位差分。e 的计数条件是除后 value>base，signed 负值分支有特殊结果，不能替换成标准科学计数法。

浮点默认 precision：float 7，double 15。先将整数部分转换并输出，再取小数部分的绝对值，循环乘 10、截取整数数字，直接追加；没有标准格式库的正确舍入承诺。width 可限制 precision；float 与 double 对负剩余宽度的处理不同。e 临时切换内部 type，移动小数点、按精度截断，再补指数。

NaN/Inf、浮点超整数转换范围、signed 科学计数和宽度极端值不得写成已经验证支持。也不得悄悄改用 to_chars 然后宣称字节一致。专项样本确认之后区分参考行为、参考缺陷与明确修复。

### 10.4 fill_and_alignment / fill_e_style

fill_and_alignment(begin)：当前片段为 [begin,cursor)。若实际长度>=width 不填充；否则计算 pad。右对齐向后搬移内容，非空格填充时有保留 +/- 的处理，零填充与二/十六进制前缀有跳过前缀处理。左对齐追加填充；居中前面 pad-pad/2、后面 pad/2，奇数多出来的一格在左。cursor 加 pad。参考仅实际填充后用 memset 清空 format_info，不能把这解释成每次转换都会 reset。

fill_e_style(exponent,begin)：估计指数位数、扩容，按当前 width 和指数占位调整 cursor，再追加 e/E、正号、指数数字；小于 10 的指数补零。该 cursor 调整包含有符号运算转无符号风险，应以边界测试和安全算术处理，不能单凭函数名推断为规范科学计数法。

## 11. 开发顺序和每步可检查产物

1. 保存当前工作区状态；不恢复用户删除的旧 cpp。列现有 Record/tag/encoder/decoder 调用者，确认新协议的切换是原子的一组修改。
2. 定义新头和 tag；static_assert sizeof/offset；手工构造一个 UTF-8+int32+thread name 记录，逐偏移核验。先不用真实 Logger。
3. 实现参数测量/填充及 LogEntryHandle/validate；测编码/视图往返和截断长度。旧记录不要无标记地交给新视图解释；当前进程内协议替换，不声称兼容历史持久化文件。
4. 实现 TimeZone 与 Layout 缓冲/游标/字符串追加；用 UTC 固定样本验证前缀和结果借用。对齐缓存和 tidy 行为。
5. 实现所有标量 insert、FormatInfo/c20_format、对齐/指数辅助，再接 UTF-8 scanner；UTF-16/UTF-32 参数转换随对应 helper 完成。需要比较真实 BQLog 结果，不把标准 fmt 输出当 oracle。
6. 补 UTF-16 格式串与非 ASCII；软件扫描先完成，再加入参考 SIMD 或明确记录该实现阶段尚无 SIMD，不能标成完整优化路径已对齐。
7. 将 cpp 纳入 QLog::qlog，用独立外部调用测试证明不仅语法可过，还能链接、获得完整结果。
8. R1–R4 接生产记录入口、Manager/Logger 的 Layout 归属、每个目标时区和 Appender 消费；每一步替换冲突旧冻结接口，不增加 Session 兼容层。

前 7 步完成才可报告“R0 完整记录/Layout 基础已实现”；仍不等于三种 Worker 模式、LP/HP、Appender 文件故障、reset、flush、退出已经完成。

## 12. Appender 如何调用、谁拥有换行

参考成功消费主路径：取得借用记录 → Logger/category/目标过滤 → 各目标准备（TextFile先调用文件基类log_impl，失败可在布局前退出）→ 对需要处理的文本目标完整do_layout → 检查结果 → 消费内部字节 → tidy_memory → 下一个目标 → 作用域读句柄结束时归还记录。失败分支并不保证都调用tidy_memory，下面示意不是逐分支复制。

TextFile 把 layout 的字节复制到文件缓存，并在该输出层追加换行。Console 另加 `[logger_name]\t`，再进入其控制台输出/缓存机制；不把该 Logger 名混入通用 Layout。具体 console 平台层换行行为随平台入口核对，不在 Layout 重复添加。

```cpp
auto result = layout.do_layout(entry, target_zone, &category_names);
if (result == Layout::enum_layout_result::finished) {
    // 在下一次布局/tidy 之前，复制到目标缓存或同步完成消费。
    target_consume(layout.get_formated_str(), layout.get_formated_str_len());
}
layout.tidy_memory();
```

这段是消费示意，不是新增公共 target_consume API。IO 结果由 Appender 路径处理；finished 不能当成落盘成功。消费期间可能触发文件 I/O，不保留旧“必须先归还 Frame 才能做 I/O”的合同。

BQLog process_log_chunk 还会对记录 timestamp 做不递减修正；这是运行态消费环节，不应偷偷放进 Layout，让同一 fixture 在独立布局时改变时间。

## 13. 验收矩阵和未解决项

差分应固定参考 commit、平台、时区、记录字节、category 表、调用次序；同时记录 BQLog 和 QLog 的返回值、输出长度、完整字节、是否发生诊断。参考 padding 不参与内容等价比较。

| 组 | 必须有的样本 |
|---|---|
| 记录 | 全 tag，短/长/含 NUL 字符串，UTF-32 参数转换，完整 offset 断言，截断/越界 ext、长度溢出 |
| 前缀 | UTC0/固定偏移/local，毫秒 0/7/999，6 等级，空/非空/越界 category，空名和同 ID 改名 |
| 语法 | 无参数花括号、有参数 `{{`/`}}`/孤立括号、`{0}/{1}`、多余/不足参数、20 字符前瞻和内部 10 位置边界 |
| 数值 | 0、正负边界、INT64_MIN、各 base、符号、#、大小写、宽度/填充、float/double 小数、e |
| 字符 | ASCII/中文/代理对，含 NUL，char16/32，非法 UTF 单独列结果和安全性 |
| 对象 | 连续两次布局、前次格式说明对后次前缀影响、扩容、tidy、缓存命中、前缀失败后的结果使用 |
| 接入 | 同一记录不同目标时区/过滤，console 名称、文件恰好一处换行，复制前后视图生命周期 |

源码已确认的架构对齐，不等于所有边界已经验证一致。目前尚需专项复现：UTF-32 格式 tag 链路；epoch0 初次时间缓存；-00:30；超长线程名输入对固定临时数组；非法 Unicode helper；浮点非有限值和转换越界；扩容/指数无符号回绕；跨调用 FormatInfo 残留。上述问题不再以“保留 QLog 老合同”解决，也不静默自创新语义。

若直接移植 BQLog 源码片段，保留相应 Apache-2.0 版权与许可信息；改名/改容器不消除来源。当前指南没有交付生产实现，因此没有本方案构建、运行和性能通过结论。旧 R0 临时副本预演只证明旧正文抽取方案，已不属于本方案验收。
