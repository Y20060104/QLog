# BQLog 对齐现状与剩余差异（2026-10-15）

本记录以实际 `/home/qq344/QLog` 为准，QLog 起点为 `feat/spsc-ring-opt@ee3ac945397e8a6628192ebef88b4e79e8ec2cd2`，本轮变更尚未提交。生产对齐基准仍是 BQLog `78cbfbef4f87e7558335f0c04bf6dd6894b46451`；`/home/qq344/BqLog` 有 MISO 和构建文件本地修改，比较这些文件时读取固定提交。官方 benchmark 使用独立干净目录 `/home/qq344/BqLog-benchmark-20261015@086f2fd831af86fcfa40e90015f89626d498d3d7`。两个基准各有用途，不能把 benchmark 采用新版本写成生产库已经整体升级。

## 本轮已经完成

### Ring 与公共类型命名

| 原 QLog 名称 | 当前名称 |
| --- | --- |
| `SpscRingBuffer` | `qlog::buffer::siso_ring_buffer` |
| `BatchReadHandle` | `siso_ring_buffer::siso_buffer_batch_read_handle` |
| `BufferResult` | `enum_buffer_result_code` |
| `LogBufferWriteHandle` | `log_buffer_write_handle` |
| `LogBufferReadHandle` | `log_buffer_read_handle` |
| `MemoryMapBufferState` | `memory_map_buffer_state` |
| `LogMemoryPolicy` | `log_memory_policy` |
| `LogBufferConfig` | `log_buffer_config` |

公开文件改为 `include/qlog/buffer/siso_ring_buffer.hpp` 与 `src/buffer/siso_ring_buffer.cpp`，CMake 和当前调用方一起更新。内部结构采用 `chunk_head_def`、`block`、`head`，字节成员为 `padding`，对应局部变量为 `current_block`。没有增加旧名字别名或转发头；外部调用方须同步 include 和类型名。`miso_ring_buffer` 原名已正确，保留。SPSC/MPSC 仍可用来描述单/多生产者和单消费者拓扑，不需要把这些概念词全部替换成类名。

本轮 SISO 改动只涉及名字。把本轮重命名规则作用于改动前的头/cpp，结果与当前文件完全相同；原函数实现与 CR 均保留。这说明此次没有夹带算法变更，不说明现有算法与 BQLog 每个步骤相同。

### 工程宏、线程名与归属

`log_worker.cpp` 的工程条件宏 `BQ_PS` 改为 `QLOG_PS`，Worker 名称改为 `QLogW_Pub` / `QLogW_%...`；`log_utils.hpp` 被注释的备选 VLQ 算法中的 `BQ_MSVC` 改为 `QLOG_MSVC`。后者原本不参与编译，不能把它说成已触发 MSVC 运行错误。QLog 的缓存行常量在决定文档中使用 `qlog::utility::QLOG_CACHE_LINE_SIZE`，当前为 128。

剩余 BQ 名称有明确来源：`BQ_LOG_FAST_INFO` 是上游真实接口的字符串/解析标记；旧 BQ 适配器及 runtime benchmark 的参考后端调用 BQ 的宏和类型；文档中的 `BQ_MOBILE_PLATFORM` / `BQ_UNIT_TEST` 是参考源码说明。它们不是 QLog 宏定义。BQLog 原版权、许可、出处、提交号也保留原名，不做全局 BQ→Q 文本替换。

### RSA 两个零整数分支

用户已明确确认“允许恢复，接受该越界风险”。`trim_be` 与 `fixed_limbs_to_min_be` 恢复 BQLog 的原规则：跳过全部前导零后，若剩余长度为零则令 `n=1`，再执行原范围复制。没有保留先前 `out.assign(1, 0); return;` 的修正。

全零输入会把复制起点推进到输入末尾，再请求复制 1 字节；空输入也可能进入相同的越界范围。因此这两处现在是与参考相同的已知边界，不能再记为 QLog 的安全修复。旧测试中“零密文安全解出零”“零公钥安全拒绝”两条断言已撤回；正常公/私钥解析、非零 RSA 回转及原前导零明文拒绝规则仍保留。CTest 通过不覆盖上述越界路径。

## 对固定生产基准仍存在的具体差异

| 范围 | 当前实际差异 | 来源与处理状态 |
| --- | --- | --- |
| SISO 原子存放 | 当前 `head` 直接持有 `std::atomic<uint32_t>`，用 `alignas` 隔开缓存行；BQ 使用字节占位区、显式 padding 和自有 `atomic_trivially_constructible` 访问器 | 早期 SISO 实现遗留；128 字节对齐已对齐，存放形式尚未逐项迁回 |
| SISO 元数据访问 | 当前用 cpp 内 `memcpy` helper 读写两个 u32，并保留 `data_bytes_`；BQ 用 `block::to_chunk_head()` 和原数据偏移成员 | 结构命名已改，访问方式仍不同；CR 保留 |
| SISO 容量、提交、恢复 | 当前容量计算、部分前置断言、根据游标取得提交块、按 `remaining` 验证回绕与 chunk 几何，与固定参考的计算/地址定位/恢复循环不同 | 需要以固定提交完整复核，而不能靠改名宣布严格实现对齐 |
| SISO Debug | 当前用 `std::thread::id` 和两个检查 helper；缺少原 `padding_`、`empty_thread_id_`、结果码统计、读写字节计数及部分原检查 | 应列为 SISO 完整迁回时的工作；MISO 保留原 Debug 的决定没有自动补齐 SISO |
| 字符串、数组与 tuple | 多处仍用 `std::string/vector/tuple`，HashMap 双版本则已经迁入 | 此前明确的标准库适配；对象布局、容量增长、初始化与异常行为不等同于 BQ 自有容器 |
| 压缩模块分配器 | `aligned_allocator` 增加 STL 所需 `rebind`，以元素数计算字节数，失败抛 `bad_alloc` | 为标准容器服务的已明示适配；不是 BQ 原容器失败合同 |
| UTF / Layout / UTF-Mixed 的 SIMD | 当前主要用已实现的软件路径；尚未迁入 BQ 的专用扫描/转换 SIMD 分派 | 此前允许暂缓。Vernam 的 AVX2/SSE/NEON/标量分派已经实现，不能笼统说全库没有 SIMD |
| Windows 本地时区名称 | 当前 `get_local_timezone_name` 在 Windows 上返回 `localtime`；固定参考通过 `GetDynamicTimeZoneInformation` 和 UTF-8 转换取得 `TimeZoneKeyName` | 名称获取分支尚未迁齐；本地日历转换本身已经使用 `localtime_s`，DST 路径与参考一致 |
| TimeZone 输入与失败处理 | 当前保留 `strtol/errno`、`time_t` 范围及日历转换结果检查，扩大年份计算并限制缓存长度；诊断直接写 stderr，参考使用共享 utility 诊断且部分失败检查/长度处理不同 | 早期实现中的显式处理仍存在，本轮未因 RSA 恢复而一并撤销；固定偏移都按有符号秒计算，不能误报为参考仍用无符号毫秒相加 |
| 格式复制与哈希 | `record_writer.cpp::log_write_begin` 先 `memcpy` 格式，再 `get_hash_64`；BQ 有融合的 `bq_memcpy_with_hash` | 哈希核心与软件原表已对齐；复制/哈希两遍处理仍是成本差异 |
| Hash-only 内核 | 当前核心通过 `if constexpr (Copy)` 排除空目标地址的复制/指针运算；原 `BQ_GEN_HASH_CORE` 的大块 hash-only 分支仍计算 dummy 空目标的结束地址 | 哈希复用时已明确保留的差异；恢复 RSA 的决定没有同时撤销这一项 |
| 短写与重叠搬移 | Appender 的三处对应重叠搬移采用 `memmove`，参考用 `memcpy` | 已明示保留的修正，避免依赖重叠 `memcpy` 行为 |
| 文件清理排序 | `std::sort` 替代原 `qsort` | 当前元素含 `std::tuple/std::string`，不能用字节搬移复制非平凡对象；保留时间排序键，等键次序不承诺 |
| API 失败地址及空堆栈 | 先检查分配结果再形成格式地址，空堆栈不做空地址 `memcpy` | 既有显式修正；本轮没有随 RSA 回退一起撤销 |
| 平台辅助 | 部分 POSIX `flock` 清零；诊断输出保持每轮 `va_start` / `va_end` 配对 | 已明示的适配/修正；不是全项目所有参考缺陷均已原样恢复 |
| 公开协议与命名 | 保留 `qlog::record`、`TimeZone`、`Layout` 与 `log_format_str_size/log_format_str_chars/log_custom_format`；普通 UTF-32 转 UTF-16，单个 `char32_t` 保留独立 tag | 用户先前明确要求的模块及协议；custom 四字节字符仍由 `static_assert` 拦截，尚未选定支持方案 |
| 配置整数越界 | 当前 `try_convert_integral_double` 有 int64 范围检查，构树分支转换失败后实际保留 `double` | 用户此前只要求记录 double/原字符串两方案，未选定最终合同；当前代码事实与未决记录均须保留，本轮没有代选或改分支 |
| properties 末尾续行 | 遇到末尾未完成键值项时跳过该项，保留之前的有效项；固定参考的相应续行循环缺少终止边界 | 用户明确选择的解析规则，继续保留 |
| 平台覆盖及测试钩子 | 当前主构建与实际验证覆盖 GNU/Linux、原生 Windows x64；没有完整迁齐移动端默认值和参考 UNIT 测试注入机制 | 定向宏/编译分支存在不等于整个平台已经支持，须单独补构建与验证 |
| 旧 benchmark 适配器 | `benchmarks/adapters/qlog_spsc_adapter.hpp` 仍引用已删除的旧 detail Ring；两个旧适配器均依赖缺失的 `benchmark_types.hpp`，未被当前目标编译 | 历史残留，字节保持原样；当前官方 benchmark 和 runtime benchmark 不使用它们，不恢复旧 Ring 来迁就它们 |

本表记录本轮按源码核实的差异；不把少量文本替换或一轮 CTest 当作全库逐函数等价证明。入口证据包括当前 `siso_ring_buffer.{hpp,cpp}`、`record_writer.cpp`、`appender_file_base.cpp`、`argument_serialization.hpp`、`property_value.cpp`，以及此前逐模块决定。

## 与 benchmark 新参考 086f2fd 的差异

新参考增加了 `fast_meta_store`、`log_record_reader` 与 FAST 元数据路径，并调整了 Record/LogBuffer、Layout、Worker、Snapshot、Appender 和压缩写入等模块。当前 QLog 普通接口 benchmark 只是匹配场景，尚未迁入这套生产变更。BQ FAST 结果单列，QLog 当前为未支持。

两项容易误记的事实：新参考的自有 atomic 头从 `src/bq_common/platform/atomic` 移到了 `include/bq_common/platform/atomic`，并没有改成全面使用 `std::atomic`；新参考压缩格式常量仍为 10，不能因为内部路径变了就写成版本 11。另外，新参考已经删除旧 RawAppender/RawDecoder，因此“QLog 缺 RawAppender”只能相对旧 78cbfbef 说明，不能当作对最新参考的必补差异。QLog 仍未提供生产 Decoder 和跨语言入口，此前未自动扩这些模块的范围约定继续有效。

## 当前保留的参考边界

按用户已确认的规则继续保留 GNU release/acquire CAS 映射边界、MemoryMap 私有字节的 typed 访问、MISO 的 `status → block_count → switch` 顺序、Windows 基础服务原失败/释放规则、软件 CRC 表第 206/207 项，以及本轮恢复的 RSA 零整数复制。这些是明确选择的参考行为，不列成尚待助手静默修复的事项。

TimeZone 与参考相同的其他边界也不能误记为本轮修复：缓存仍按完整毫秒值比较，首次 epoch=0 会命中初始缓存键；`-00:30` 仍不能从数值零小时恢复负号，`restore_by_config` 仍不主动使旧缓存失效。local 的日历转换使用系统 DST 规则，固定偏移使用 GMT 日历转换、没有 DST。

## 后续推进顺序

1. 若继续完成固定基准的严格实现对齐，先单独收口 SISO 的原子占位、元数据访问、容量/恢复与完整 Debug，保留用户 CR，再补定向差分。
2. 性能工作以本轮普通接口对照定位候选，再分别测复制与哈希融合、UTF 软件/SIMD路径；不能根据表中某项耗时直接归因。
3. 升级生产基准到 086f2fd 应作为独立迁移，一起处理新的 Record 读取、FAST 元数据、Buffer 和输出链，避免只加入 FAST 宏而缺少相应生命周期。

本轮构建、测试、源码保护及新 benchmark 证据见 [本轮验证记录](../validation/BQLOG_NAMING_RSA_BENCHMARK_20261015_CHS.md)。正常 benchmark 使用方式与结果见 [README](../../README.md)。
