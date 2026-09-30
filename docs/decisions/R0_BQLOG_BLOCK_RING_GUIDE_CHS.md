# Ring 重建指南：按 BQLog 的 block 单位实现 SISO

> **2026-09-26最新进度（优先于下方2026-09-23快照）：** 第三、四批已填写；本轮补全try_recover_from_exist_memory_map并修正batch结果、断言/拼写、线程身份判断和Release开关方法定义。恢复以外部存储重算有效块数，memcpy读取普通游标快照，用remaining有界验证u32回绕与chunk几何，验证后建立Head/原子并恢复游标；坏快照返回false，非法外部存储仍是assert前置条件。QLOG_DEBUG和NDEBUG/O2两种配置以-Werror编译，并通过独立共享检查库的--no-undefined链接；未接入项目CMake，未运行行为/并发/恢复差分或性能测试。性能CR保留。
>
> 下一轮先做Ring的CMake接入与宏PUBLIC传播，再以Record为一个模块：补全指南第2至4节，配合完整Layout指南第4、5节。TimeZone/Layout排在之后；不要把本次链接检查记为完整Ring验收。


日期：2026-09-23。QLog 开发根目录 `/home/qq344/QLog`；BQLog 参考提交 `60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`。本指南覆盖旧单生产者 Ring 的替代实现，不把 MISO 的并发协议混入 SISO。

## 0. 当前实现进度与指导边界（2026-09-23刷新）

已重新读取实际源码。目录迁移完成；QLog现在使用SpscRingBuffer及spsc_ring_buffer.hpp/.cpp命名，实现目标仍是BQLog SISO block协议，不是恢复已删除的旧字节Ring。

| 当前实际文件 | 进度 |
|---|---|
| include/qlog/utility/utility.hpp | inline round_pow_of_two已有定义，原位保留 |
| include/qlog/buffer/log_buffer_defs.hpp | bool low_space_flag、recover_from_memory_map已修正 |
| include/qlog/buffer/spsc_ring_buffer.hpp | 类、Head字段对齐与偏移断言已建立；第三、四批所需批句柄Debug补充尚待完成 |
| src/buffer/spsc_ring_buffer.cpp | 第一、二批初始化与单条读写已有实现；本轮补齐重复变量、块数断言、读借用配对及转换遗漏 |
| CMakeLists.txt | 库仍只编译version.cpp；Ring尚未链接验收 |

第三批=batch/next/return_batch、traverse；第四批=恢复、线程检查与开关收尾，用户尚未完成。不得把第一、二批语法检查或历史smoke当作完整Ring验收。

本次用户决定：
- 调试成员与代码统一用QLOG_DEBUG；标准assert仍由NDEBUG控制。库和调用者必须采用一致宏定义，后续由CMake PUBLIC传播；不要直接在头中按NDEBUG重新定义QLOG_DEBUG。
- 初始化非法输入使用assert前置条件；Release由调用者保证合法。无abort、无返回0错误协议。alloc的full/invalid正常结果判断仍保留。
- helper输入上界按当前容量约束断言expected<=2^30；尺寸加法用宽整数，游标仍为u32模运算。
- 性能CR保留；固定4字节memcpy与参考字段访问的性能比较延后到完整日志实现后，不提前声称快慢。
- 每轮按模块推进；当前先静态审查，运行测试暂缓，最终验收不取消。

参考固定提交60ef4d3。Linux目录/home/qq344/BqLog的MISO和构建文件有本地改动；本轮后续主链核对使用Windows参考中与HEAD无差异的选定文件，不称整个Linux树干净。

## 0.1 模块划分：目录和命名空间一起明确

当前Ring采用以下目标，不再统一使用qlog::detail。模块化只调整代码归属，不改变BQLog的算法与所有权。

```text
include/qlog/
├─ buffer/
│  ├─ log_buffer_defs.hpp      # 共用Buffer结果与句柄
│  └─ spsc_ring_buffer.hpp     # 单生产者Ring声明
└─ utility/
   └─ utility.hpp             # 已有round_pow_of_two，保留

src/
└─ buffer/
   └─ spsc_ring_buffer.cpp    # 非内联方法定义

tests/
└─ buffer/
   ├─ spsc_ring_buffer_test.cpp
   └─ spsc_ring_buffer_concurrency_test.cpp
```

命名空间分别为qlog::buffer、qlog::utility。后续record、layout、appender、runtime各自成模块，目录可采用include/qlog/record、layout、appender、runtime及对应src目录；这些后续目录目前只是归属规划，不要求现在批量搬迁现有Record文件。detail只用于某模块真正需要隐藏的实现细节，例如qlog::buffer::detail，不能成为所有类型的默认归宿。

依赖方向：buffer可以用utility；utility不能include buffer/Record/Logger；Block/Head留在Ring类内；共用句柄不依赖具体SISO/MISO类。不要因模块化再引入BufferManager/UtilityManager或新的拥有型包装。

## 0.2 迁移完成，保护当前草稿

权威文件为include/qlog/buffer/log_buffer_defs.hpp、include/qlog/buffer/spsc_ring_buffer.hpp、src/buffer/spsc_ring_buffer.cpp。旧detail路径只代表历史位置，不应重新创建兼容副本，也不能按旧清理列表删除新草稿。后续Record/Layout的目标目录见补全指南；目标位置不代表已有文件被迁移。

## 0.3 每一类声明/定义具体放哪里

| 项目 | 头文件位置 | cpp位置/规则 |
|---|---|---|
| BufferResult、MemoryMapBufferState、LogBufferWriteHandle、LogBufferReadHandle | buffer/log_buffer_defs.hpp，namespace qlog::buffer；完整类型定义 | 无cpp，不建空log_buffer_defs.cpp |
| BLOCK_SIZE/BLOCK_SIZE_LOG2、ChunkHead、Block、Head、布局断言 | buffer/spsc_ring_buffer.hpp，SpscRingBuffer的private；类型必须在使用它的成员声明前完整定义 | 不在cpp再定义第二套结构 |
| BatchReadHandle类型与字段 | 同头，SpscRingBuffer的public嵌套类型；自己的游标字段private | has_next可在类内一行定义；next和Debug verify_chunk在cpp定义 |
| Ring构造、析构、renew | 类的public只写声明 | src/buffer/spsc_ring_buffer.cpp，namespace qlog::buffer写带SpscRingBuffer::的定义 |
| alloc/commit/read/read_an_empty/discard/return | public声明 | 全部在上述cpp实现 |
| batch_read/return_batch/data_traverse | public声明 | 全部在上述cpp实现 |
| calculate_min_size_of_memory | public static声明 | cpp实现，调用qlog::utility::round_pow_of_two；static不要求定义在头里 |
| set_thread_check_enable/is_thread_check_enable | public声明 | cpp实现；条件编译和全局Debug开关接入逻辑不散入共用defs |
| get_block_size/get_total_blocks_count/get_buffer_addr/get_is_memory_recovery/get_memory_map_buffer_state | 类内简单inline getter可直接定义，保持参考热路径组织 | 头里定义后cpp不再重复定义 |
| get_max_alloc_size | public声明 | cpp实现N*8-8 |
| cursor_to_block | private，简单掩码函数可在类内定义 | 不重复定义 |
| get_block_data_addr | private声明 | cpp实现整数边界比较与payload位置判断 |
| try_recover_from_exist_memory_map、init_with_memory_map、init_with_memory、init_cursors | private声明 | 全部cpp实现，不能留成namespace级自由函数 |
| 只供本cpp使用的chunk字段memcpy辅助函数 | 不放public头 | cpp匿名namespace定义 |
| round_pow_of_two(uint32_t) | utility/utility.hpp，保留现有inline完整定义 | 不需要utility.cpp；若改为constexpr也必须保留定义在头中 |

需要constexpr调用或模板实例化的定义通常放头文件；普通非内联成员放cpp。不要用“越多inline越好”决定归属，也不要把同一inline函数在头和cpp各定义一次。

类内部排列建议：private常量和存储类型 → public嵌套BatchReadHandle与API → private方法声明与成员。这样附录代码中的Block/Head在被引用前已有定义，避免把片段直接拼接导致未知类型错误。

## 0.4 round_pow_of_two：沿用utility，补清契约

现有函数是uint32输入/输出的位扩散写法。语义是向上取不小于value的2次幂，并非数学“四舍五入”：0返回0，1返回1，已有2次幂不变，3返回4；大于0x80000000的值会发生无符号回绕并返回0。参考BQLog的uint32实例也需要区分这些边界，不能静默把0改成1或将结果饱和为最大值。

函数本身是纯整数工具，不处理Ring的Head/容量。Ring中的calculate_min_size_of_memory仍定义在buffer cpp，负责调用utility以及检查roundup与加Head/缓存行后是否可表示；不能把整个Ring分配逻辑放进utility.hpp。

cpp开头应为：

```cpp
#include "qlog/buffer/spsc_ring_buffer.hpp"
#include "qlog/utility/utility.hpp"

namespace qlog::buffer {
// uint32_t SpscRingBuffer::calculate_min_size_of_memory(...) 等定义。
// 调用：qlog::utility::round_pow_of_two(expected_buffer_size)
}
```

当前inline定义可保留；是否改constexpr/noexcept属于后续局部实现调整，本轮没有修改。未来utility函数增多时可再拆math/bit/utf等职责文件，当前不为一个函数强制重建目录。

## 0.5 从现在开始的动手顺序与构建连接

1. 路径/namespace及defs修正已完成；保留utility原位，不重复迁移。
2. 完成class、Head布局断言和所有声明；暂不声称算法已实现。
3. 在cpp写calculate_min_size、init_cursors、init_with_memory、构造/析构与查询；覆盖容量/对齐前置条件。
4. 按正文顺序补alloc/commit/read/return，再补batch/traverse/recovery与Debug。
5. cpp产生实际定义后，CMake显式加入`src/buffer/spsc_ring_buffer.cpp`；定义不完整时可做对象编译检查，但必须明确未完成外部链接。
6. 在tests/buffer中建立新Ring测试；单独编译头、链接真实库，之后再做行为差分/并发验证。

```cmake
target_sources(qlog PRIVATE
    src/buffer/spsc_ring_buffer.cpp
)
```

不把utility.hpp列为需要编译的cpp，不新增空utility.cpp。运行已有version smoke不验证新Ring草稿；新Ring接入构建前应检查include路径、namespace和方法符号是否统一。


## 1. 先确定 block 到底是什么

| 层 | BQLog 单位/职责 | 本轮处理 |
|---|---|---|
| siso_ring_buffer | 一个 block=8字节；单生产者、单消费者；32位block游标 | 本篇完整实现指导 |
| miso_ring_buffer | 一个 block=BQ_CACHE_LINE_SIZE；当前常见目标64字节；多生产者，有块状态和并发分配 | 后续独立指南，不把SISO改成64字节就认为支持MPSC |
| log_buffer | TLS、LP/HP路由、队列块/节点生命周期、满策略、等待重试、oversize | 管理SISO/MISO，不能由SISO自行实现所有策略 |

当前旧QLog虽然把帧补齐到8字节，游标和frame_bytes仍以字节计数。对齐BQLog需要同时改单位、字段宽度、外部内存归属、句柄、返回值和回收时机，不是将原算法中的8换个名字。

本篇“block”也不是log_memory_policy::block_when_full中的阻塞。SISO空间不足返回结果码；默认阻塞策略属于上层重试路径。

## 2. 撤销旧配置与旧接口

以下退出新主链：SpscRingBufferConfig、ValidatedConfig、capacity_bytes/max_payload_bytes双配置、StorageOwner/ColdState中的自有内存、ReserveStatus/ReadStatus、frame_and_status位复用、reservation_pending/read_pending作为public错误、publish_reclaimed及记录数/字节数阈值发布。

旧SpscRingBuffer/ring_geometry实现及绑定这些合同的测试、基准、旧Ring配置文档随清理退出。当前版本的历史47项测试通过事实保留在清理记录，但不作为新block Ring验收。

新SISO构造接口直接对应参考：

```cpp
SpscRingBuffer(void* buffer, std::size_t buffer_size, bool is_memory_recovery);
```

buffer是外部分配的整块内存，buffer_size是整块字节数（包含head）；Ring借用它，不在析构时释放它。不得再包一个SisoRingConfig恢复旧配置层。

上层LogBufferConfig才保存default_buffer_size、need_recovery、policy、high_frequency_threshold_per_second及Logger身份信息。参考桌面默认64KiB、移动端32KiB、need_recovery=false、policy=block_when_full、频率阈值1000。default_buffer_size不是SISO最大单条payload；不要把旧max_payload_bytes搬过去。

## 3. 文件与源码入口

当前实际路径与目标路径的区别见第0节。Ring声明/共用类型归`include/qlog/buffer/`，实现归`src/buffer/`，命名空间`qlog::buffer`；通用整数工具沿用`include/qlog/utility/utility.hpp`、`qlog::utility`。不再按旧版全放detail。

BQLog对照：`src/bq_log/types/buffer/siso_ring_buffer.h/.cpp`；按alloc_write_chunk、commit_write_chunk、read_chunk、return_read_chunk、batch_read、return_batch_read_chunks、data_traverse、calculate_min_size_of_memory、try_recover_from_exist_memory_map、init_with_memory、init_cursors函数名检索。共用类型见log_buffer_defs.h；结果码见include/bq_log/misc/bq_log_def.h。

roundup参考在`include/bq_common/types/type_tools.h`的roundup_pow_of_two；QLog当前映射名称是round_pow_of_two，无需只为拼写一致另建函数。

## 4. 共用枚举和句柄先写

```cpp
enum class BufferResult {
    success = 0,
    err_empty_log_buffer,
    err_not_enough_space,
    err_wait_and_retry,
    err_data_not_contiguous,
    err_alloc_size_invalid,
    err_buffer_not_inited,
    err_io_failure_drop,
    result_code_count,
};
struct LogBufferWriteHandle {
    std::uint8_t* data_addr;
    BufferResult result = BufferResult::err_empty_log_buffer;
    bool low_space_flag = false;
};
struct LogBufferReadHandle {
    std::uint8_t* data_addr;
    BufferResult result = BufferResult::err_empty_log_buffer;
    std::uint32_t data_size;
};
enum class MemoryMapBufferState {
    init_with_memory,
    recover_from_memory_map,
    init_with_memmap,
};
```

不要给BufferResult改成uint8底层并继续声称结构声明完全对应参考；参考此枚举未显式指定底层类型。无效句柄只允许先检查result，不能读取未定义data_addr/data_size。写句柄没有payload长度和占用块数；commit从chunk头读取block_num。读句柄带实际data_size。

共用结果码不等于每个SISO函数都能产生每一项：正常alloc主要返回success/not_enough_space/alloc_size_invalid，read返回success/empty。其他码供上层与其他Buffer使用，尤其io_failure_drop不能被上层变成永久等待。

## 5. 存储布局：12字节结构不等于12字节头开销

参考声明：

```cpp
static constexpr std::size_t BLOCK_SIZE = 8;
static constexpr std::size_t BLOCK_SIZE_LOG2 = 3;

struct alignas(4) ChunkHead {
    std::uint32_t block_num;
    std::uint32_t data_size;
    std::uint8_t data[1];
    std::uint8_t padding[3];
};
static_assert(sizeof(ChunkHead) == 12);
static_assert(offsetof(ChunkHead, data) == 8);

struct Block { std::uint8_t bytes[8]; };
static_assert(sizeof(Block) == 8);
```

固定字段只有前8字节，payload起点是chunk起点+8。不得使用sizeof(ChunkHead)==12来计算块数。data[1]是参考定位payload的技巧，不表示整个payload只有1字节，也不要求复制12字节ChunkHead覆盖尾端。安全移植时将两项u32用memcpy读写，payload地址用底层字节存储+8计算，避免依赖越界数组索引/未建立对象的类型重解释。不要为此另建拥有型FrameLayout。

整块外部内存，在64字节缓存行目标上：

```text
offset 0    Head: 256字节、4条缓存行
offset 256  Block[N]: N*8字节，N为2的幂
剩余字节    不纳入ring有效容量
```

Head逐行归属：

| Head偏移 | 字段 | 所有者/用途 |
|---:|---|---|
| 0 | aligned_blocks_count_cache_:u32 | 初始化/恢复的容量快照 |
| 4 | rt_reading_cursor_cache_:u32 | 消费者本地已推进位置 |
| 8 | rt_writing_cursor_cache_:u32 | 消费者缓存的生产者位置 |
| 64 | wt_reading_cursor_cache_:u32 | 生产者缓存的消费者位置 |
| 68 | wt_writing_cursor_cache_:u32 | 生产者本地已提交位置 |
| 128 | reading_cursor 原子u32 | 消费者release发布、生产者acquire读取 |
| 192 | writing_cursor 原子u32 | 生产者release发布、消费者acquire读取 |

每行补齐64字节；Head总长256且alignof=64。参考原子放在占位存储中并通过atomic_trivially_constructible访问，目的是映射恢复布局。QLog若采用std::atomic<uint32_t>映射，必须静态核对大小、对齐和每个offsetof，并正确建立原子对象生命周期。不能将memset后的任意字节直接当作已构造std::atomic使用；恢复时不能用整Head默认构造覆盖已有普通游标快照。

这里不采用旧“writer→两个atomic→reader”的字段顺序；对齐参考是reader缓存、writer缓存、read atomic、write atomic。缓存行尺寸与block尺寸必须是两个独立常量。

## 6. 初始化与内存大小

### calculate_min_size_of_memory(uint32_t expected_buffer_size) -> uint32_t

参考算法：roundup_pow_of_two(expected_buffer_size)+sizeof(Head)+CACHE_LINE_SIZE。64字节缓存行时就是roundup_pow2(expected)+320。expected是希望保证的数据区字节，不是最大payload。

例如expected=65536，总申请65856字节；构造时可用数据区剩余65600，得到8192个8字节block，即有效数据区65536；多出的64字节不计入有效容量。max_alloc=65536-8=65528。

调用者需分配缓存行对齐的内存；helper多加64字节不表示构造器会自动调整一个未对齐的指针。不要用new uint8_t[]通常对齐的偶然性替代明确的对齐分配。

### init_with_memory(void* buffer,size_t size) -> void

1. head指向buffer，blocks指向buffer+sizeof(Head)。
2. max_blocks=(size-sizeof(Head))/8，N为不大于max_blocks的最大2次幂。
3. aligned_blocks_count_=N，保存head容量快照。
4. init_cursors清四个本地游标和两个发布原子；设置state=init_with_memory。

参考init要求max_blocks>1；构造器前面的“至少256字节”消息与实际表达式sizeof(block)*4不一致，不能照抄它作为完整校验。实际必须能装下Head与至少两个block。大小roundup/加法还需要在宽整数中检查溢出及参考允许的容量上界，不把UINT32回绕带入内存分配。

### init_cursors() / renew()

init_cursors将四个缓存设0，两个原子分别store(0,release)。renew调用它并按Debug路径重置线程身份。必须在生产/消费均停止且旧借用无效时调用；它丢弃逻辑内容，不是并发清空API。

析构不释放buffer。拥有者负责在Ring销毁、所有借用结束之后释放外部存储。

## 7. block算术与alloc_write_chunk

定义：N总块数；W生产者本地游标；Rcache生产者缓存的read游标；P请求payload字节。

```text
index = W & (N - 1)
tail = N - index
normal = ceil((P + 8) / 8)

若 normal <= tail:
    count = normal
    payload = blocks + index*8 + 8
否则:
    count = tail + ceil(P / 8)
    payload = blocks起点
```

wrap时chunk头仍在原tail起点，payload整体移到ring起点；tail剩余空间纳入同一个chunk的block_num，不创建单独padding/invalid块。MISO的invalid状态不能移植到SISO这里。

### alloc_write_chunk(uint32_t size) -> LogBufferWriteHandle

按参考顺序：

1. Debug检查生产线程身份。
2. 计算normal、tail。若normal>N、normal==0，或wrap且normal+tail-1>N，返回err_alloc_size_invalid。
3. 确定真实count及payload地址。
4. free=uint32_t(Rcache+N-W)。不足时只刷新一次reading_cursor.load(acquire)，重算free；仍不足返回err_not_enough_space。
5. 在原index处写block_num=count、data_size=size。
6. low_space_flag=((N-free)*2>=N)，它依据本次预留之前的已占空间，不是扣掉本次count之后的剩余量。
7. result=success返回。此时不推进W，不发布原子write游标。

参考核心的可复刻伪代码（正式实现对初始P+8及向上取整用uint64检查，游标差仍用uint32模运算）：

```cpp
auto normal = (std::uint64_t{size} + 8U + 7U) >> 3;
auto index = head_->wt_writing_cursor_cache_ & (N - 1U);
auto tail = N - index;
if (normal > N || normal == 0 ||
    (normal > tail && normal + tail - 1U > N)) {
    handle.result = BufferResult::err_alloc_size_invalid;
    return handle;
}
auto count = normal > tail
    ? std::uint64_t{tail} + ((std::uint64_t{size} + 7U) >> 3)
    : normal;
// 然后按上面第4–7步检查free、写chunk头、填句柄。
```

不要恢复旧reservation_pending返回码。调用协议要求成功alloc后完成对应commit，不能同时保留多个未提交写句柄；参考SISO没有旧QLog那组public pending/abort状态协议。失败句柄传给commit是合法no-op。

特殊边界：该提交的alloc(0)算得normal=1，可以成功，尽管共用结果码注释把0称为非法。此处应按函数实际行为写差分，不额外引入“size必须非零”旧合同。UINT32极值的回绕属于安全问题，不能以复刻为理由制造无效写入。

### 三个具体例子（N=8）

| 初始W物理位置 | P | normal/tail | 实际count | 结果 |
|---:|---:|---|---:|---|
| 0 | 9 | 3/8 | 3 | 头在0，payload从字节8开始 |
| 6 | 17 | 4/2 | 5 | 头在block6，payload从ring起点开始；消耗尾2+头部payload3 |
| 6 | 56 | 8/2 | 9 | err_alloc_size_invalid，即使当前ring为空也不够容纳该布局 |

get_max_alloc_size返回N*8-8，只保证在合适位置（renew后起点）能一次分配，不保证任何空ring的当前游标位置都能分配最大值。不要把位置相关invalid误解释为full，也不要自行renew有并发访问的ring。

## 8. commit/read/return与内存顺序

### commit_write_chunk(const LogBufferWriteHandle&) -> void

非success直接return。若data_addr==blocks起点，chunk头取当前W所指块；否则chunk头=data_addr-8。读取block_num，W+=block_num；writing_cursor.store(W,release)。发布前调用方必须已写完payload和chunk头。

不能根据handle地址反向访问起点前8字节：wrap分支必须用当前逻辑W定位尾部chunk头。不能在alloc时提前推进W，commit以后再次使用同一句柄属于调用协议错误。

### read_chunk() -> LogBufferReadHandle

Debug标记本次借用开始（空结果也需要归还）。若uint32_t(cachedW-localR)==0，刷新writing_cursor.load(acquire)；仍为0返回empty。否则读localR处的chunk头，设置data_size；判断`8+data_size > tail*8`则payload在blocks起点，否则在当前块+8；result=success。read本身不推进R。

比较使用整数长度而不是先构造超过对象边界的指针，以保持同样wrap判定并规避越界指针算术。只有acquire观察到生产者提交后才读取payload。

### return_read_chunk(const LogBufferReadHandle&) -> void

Debug清借用标记；非success不推进任何游标。成功时按与commit同样的“起点payload特殊分支”找到头，localR+=block_num，然后reading_cursor.store(localR,release)。这使前面已读完的字节可被生产者acquire后覆盖。

普通单条return每次发布；撤销旧QLog累计条数/字节阈值再publish的设计。若要批量发布，使用参考batch接口，不加另一个策略配置。

### discard_read_chunk(LogBufferReadHandle&) -> void

清Debug借用标记，将handle.result置empty，不推进localR也不发布。下次read仍得到同一条。不要清除/释放payload。read_an_empty_chunk仅返回empty并进入对应Debug借用状态，用于上层特殊调度，也需配对return/discard。

### 成功与失败都配对的使用顺序

```cpp
auto wh = ring.alloc_write_chunk(bytes);
if (wh.result == BufferResult::success) {
    // 写恰好bytes字节，不能访问超过payload的空间。
}
ring.commit_write_chunk(wh);

auto rh = ring.read_chunk();
if (rh.result == BufferResult::success) {
    // 此处可构造LogEntryHandle(rh.data_addr, rh.data_size)。
}
ring.return_read_chunk(rh);
```

SISO不负责等待、不唤醒Worker、不sleep。上层区分not_enough_space和invalid/oversize，再按policy处理，不能把每个失败都转换为block重试。

## 9. batch_read：参考唯一成套的批量回收接口

BatchReadHandle保存parent、start_cursor、current_cursor、end_cursor和result；不拥有payload，不增加vector<ReadHandle>。

batch_read先按read的规则在缓存空时acquire刷新W，然后快照start=localR、end=cachedW、current=start。缓存非空时并不强制刷新最新W，因此这是本次缓存可见范围，不是调用瞬间所有已提交记录的强快照。

has_next返回current!=end。next取current处的chunk，构造普通ReadHandle，current+=block_num，并同步parent.localR=current；不发布read原子。不能对next返回的每条handle再调用普通return_read_chunk。

return_batch_read_chunks：失败句柄no-op；成功时Debug要求current==end、父localR==end，且start匹配已发布read游标；最后一次reading_cursor.store(localR,release)。必须遍历完该批才整体归还，不编造半批return协议或自动析构消费。

参考Debug下无效读也会设置waiting标记；必须将空batch传入return_batch_read_chunks完成配对，否则下一次读可触发断言。

## 10. traverse、查询与Debug边界

data_traverse(callback,user_data)：用局部游标从localR扫描；遇缓存范围末端时刷新write原子；逐条回调(data,size,user_data)。不推进父localR、不发布read，不消费记录。由于不断刷新write，持续生产时不保证有限快照结束。回调不可对同一Ring重入消费/renew。

查询函数：get_block_size=8；get_total_blocks_count=N；get_buffer_addr返回数据block起点（不是整块buffer/head起点）；get_max_alloc_size=N*8-8；get_memory_map_buffer_state返回状态；get_is_memory_recovery返回构造参数。

Debug：单读线程、单写线程检查，借用配对、块数范围和统计。set_thread_check_enable(false)允许上层在已有排他协调时切换调用线程，不意味着允许并发多读/多写。参考有效检查还受全局开关控制，后续Manager集成需要对应；没有全局开关前不得声称该接口全链完全等价。

BQLog参考按!NDEBUG组织；QLog按用户决定使用QLOG_DEBUG控制调试代码，assert是否执行仍由NDEBUG控制。旧QLOG_ENABLE_RING_VALIDATION=AUTO/ON/OFF三态配置删除；新Debug检查不引入运行时正常返回的corrupted/read_pending状态。

## 11. 映射恢复：Ring的外部存储解释，不是Ring去打开文件

构造is_memory_recovery=false：init_with_memory。true：try_recover_from_exist_memory_map成功则保留快照；失败则init_with_memory_map重置并标记init_with_memmap。后者调用普通init后改状态，不在SISO内open/mmap文件。

recover输入是已有字节，返回bool：

1. 按整块大小重算N，比较head保存的N，不一致失败。
2. 检查uint32_t(wtW-rtR)<=N。
3. 从rtR遍历到wtW：block_num非零；根据data_size与尾剩余算是否wrap；期望块数=wrap?tail+ceil(P/8):ceil((P+8)/8)，必须匹配。
4. 必须准确走到wtW；恢复cachedW=wtW、cachedR=rtR，并发布两个原子；设置recover_from_memory_map。

参考当前恢复循环用current<wtW，不能正确表示所有uint32回绕情形；坏映射数据的边界和对象生命周期也需专项处理。这是已识别的参考疑点，不能写成“恢复任意损坏文件安全”或“崩溃日志保证不丢”。安全移植用剩余已提交块数有界遍历、先验证范围再读取；记录此修复差异，不复制UB。

本轮正常内存Ring可先用false贯通，但true分支不能伪装成false初始化成功后声称恢复实现完成。若当前V1不接mmap文件，明确“未接持久化存储”，仍保留Ring恢复接口的设计位置。

## 12. 与Record/Layout的边界

```text
SISO chunk头：block_num + data_size，固定字段8字节
    ↓ chunk payload
日志Record：40字节头 + 格式串 + 参数 + 线程扩展
    ↓ LogEntryHandle
逐目标完整Layout → Appender消费 → return_read_chunk
```

RecordHeader不保存Ring block_num；chunk.data_size才是Record总字节数。不要用get_max_alloc_size代替Record可表示长度，也不要在Layout中判断wrap。producer会一次性写入连续payload，consumer拿到的payload同样连续。

## 13. 实施顺序与验收

1. 共用结果码/句柄和内存状态；不恢复旧配置类。
2. Block/ChunkHead/Head布局、原子生命周期、外部存储初始化；sizeof/offsetof静态断言。
3. cursor_to_block、payload定位、块数计算、max_size；用N=8的三个例子核验。
4. alloc/commit/read/return；单线程FIFO和full/empty/invalid；失败句柄配对。
5. discard/read_an_empty、renew；说明调用前置条件。
6. batch_read/next/return_batch、traverse；测试没有提前回收。
7. 两线程数据可见性、缓存刷新、uint32_t游标回绕；TSan验证需要与Debug线程切换规则一致。
8. 映射快照恢复/损坏输入/回绕专项；明确参考缺陷修复记录。
9. 与固定提交BQLog对照相同请求序列、结果码、payload位置、block_num、data_size、low_space_flag、N/max_alloc；分别跑Debug/Release。
10. 接入LogBuffer上层分配与满策略；再进入LP/HP与MISO，不让SISO构造承担全部配置。

测试至少包含：P=0/1/7/8/9，恰好尾端，wrap，空ring但当前位置不能分配最大条目，满空间刷新一次read后成功，commit前不可见，return前不可覆盖，discard重复读取，空读配对，批读遍历完后才发布，read/write游标接近UINT32_MAX再跨界，renew归零，traverse不消费，错误恢复快照。不要直接沿用旧47项测试的契约与统计口径。

实现后CMake显式加入src/buffer/spsc_ring_buffer.cpp，测试用全新构建目录。建议测试名qlog_siso_ring_buffer_test；依赖GoogleTest时再恢复对应FetchContent，不为仅version smoke保留旧依赖。新比较基准以真实SISO接口重写，不用旧QLog句柄适配后冒充协议已经一致。

## 14. 第三、四批之后如何接续

完整模块顺序和源码复核见[补全指南第0节](./R0_POST_CLEANUP_IMPLEMENTATION_GUIDE_CHS.md)。

1. 第三、四批完成后先收口Ring：静态审查所有非内联定义、两种宏配置、存储生命周期和回绕；显式接入CMake并由真实调用者链接。静态库归档成功本身不足以证明未定义符号齐全。
2. 接Record模块：40字节头/tag、LogEntryHandle、参数测量/序列化，之后TimeZone与完整Layout。可先用fixture解耦，不依赖Worker。
3. MISO、block_list、oversize_buffer、LogBuffer/TLS/LP-HP仍需独立模块指导；不能用SISO代替整个Buffer。现有路线不是这些模块的完整逐函数指南。

测试暂缓是当前开发节奏，不等于取消Ring行为、并发、恢复差分验收。性能测试及头文件中的性能CR延后到完整日志实现后。本轮未实现第三、四批。

## 附录A：头文件声明清单，避免边写边新增接口

下面放入include/qlog/buffer/spsc_ring_buffer.hpp，namespace qlog::buffer。共用BufferResult/句柄来自qlog/buffer/log_buffer_defs.hpp；块和头定义在class私有区。具体函数实现按前面各节，不额外添加旧API兼容层。

```cpp
class SpscRingBuffer {
public:
    struct BatchReadHandle {
        BufferResult result = BufferResult::err_empty_log_buffer;
        bool has_next() const;
        LogBufferReadHandle next();
    private:
        friend class SpscRingBuffer;
        SpscRingBuffer* parent_;
        std::uint32_t start_cursor_;
        std::uint32_t current_cursor_;
        std::uint32_t end_cursor_;
        // Debug另有last_cursor_用于verify_chunk。
    };

    SpscRingBuffer() = delete;
    SpscRingBuffer(void* buffer, std::size_t size, bool is_memory_recovery);
    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;
    ~SpscRingBuffer();

    void renew();
    LogBufferWriteHandle alloc_write_chunk(std::uint32_t size);
    void commit_write_chunk(const LogBufferWriteHandle& handle);
    LogBufferReadHandle read_chunk();
    LogBufferReadHandle read_an_empty_chunk();
    void discard_read_chunk(LogBufferReadHandle& handle);
    void return_read_chunk(const LogBufferReadHandle& handle);
    BatchReadHandle batch_read();
    void return_batch_read_chunks(const BatchReadHandle& handle);
    void data_traverse(void (*callback)(std::uint8_t*, std::uint32_t, void*),
                       void* user_data);

    static std::uint32_t calculate_min_size_of_memory(std::uint32_t expected);
    void set_thread_check_enable(bool enabled);
    bool is_thread_check_enable() const;
    std::uint32_t get_max_alloc_size() const;
    MemoryMapBufferState get_memory_map_buffer_state() const;
    const std::uint8_t* get_buffer_addr() const;
    std::uint32_t get_block_size() const;
    std::uint32_t get_total_blocks_count() const;
    bool get_is_memory_recovery() const;

private:
    // Block、Head见正文和下段；8字节chunk字段用安全读写辅助函数访问。
    Block& cursor_to_block(std::uint32_t cursor);
    std::uint8_t* get_block_data_addr(Block& block);
    bool try_recover_from_exist_memory_map(void* buffer, std::size_t size);
    void init_with_memory_map(void* buffer, std::size_t size);
    void init_with_memory(void* buffer, std::size_t size);
    void init_cursors();

    Head* head_;
    Block* aligned_blocks_;
    std::uint32_t aligned_blocks_count_;
    bool is_memory_recovery_;
    MemoryMapBufferState mmap_buffer_state_;
    std::uint8_t* data_bytes_; // 非拥有字节视图，payload地址从整片存储计算
    // Debug线程身份、借用标记和统计另按参考条件编译，不参与Release协议。
};
```

此清单额外显式删除赋值以防借用对象意外复制所有内部指针；它不新增运行时协议。BQLog原头未显式删除赋值，不要声称C++特殊成员声明逐字相同。

Head在当前Linux 64字节缓存行目标上的标准原子布局映射（用于布局检查；恢复对象生命周期仍必须按第11节另做）：

```cpp
struct alignas(64) Head {
    std::uint32_t aligned_blocks_count_cache_;
    std::uint32_t rt_reading_cursor_cache_;
    std::uint32_t rt_writing_cursor_cache_;
    alignas(64) std::uint32_t wt_reading_cursor_cache_;
    std::uint32_t wt_writing_cursor_cache_;
    alignas(64) std::atomic<std::uint32_t> reading_cursor_;
    alignas(64) std::atomic<std::uint32_t> writing_cursor_;
};
static_assert(sizeof(std::atomic<std::uint32_t>) == 4);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(alignof(Head) == 64);
static_assert(sizeof(Head) == 256);
static_assert(offsetof(Head, rt_reading_cursor_cache_) == 4);
static_assert(offsetof(Head, rt_writing_cursor_cache_) == 8);
static_assert(offsetof(Head, wt_reading_cursor_cache_) == 64);
static_assert(offsetof(Head, wt_writing_cursor_cache_) == 68);
static_assert(offsetof(Head, reading_cursor_) == 128);
static_assert(offsetof(Head, writing_cursor_) == 192);
```

普通内存路径先在对齐存储中正确构造Head，再初始化游标；不要把placement-new写在每次alloc/read中。std::atomic映射不构成跨进程ABI兼容承诺，持久化恢复与跨进程共享是不同问题。
