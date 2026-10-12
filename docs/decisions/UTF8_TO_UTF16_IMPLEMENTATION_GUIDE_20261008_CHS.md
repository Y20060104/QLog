# UTF-8 到 UTF-16 软件转换完整指导

> **2026-10-09 生产实现完成并重新审查：** 用户已填写本函数，审查期间抄写错误已由用户修正；当前实际源码在Linux/原生Windows Debug/Release严格编译运行通过，对全部1,112,064个有效Unicode标量与BQLog软件摘录一致并回转，Linux ASan+UBSan同组通过。根库已包含utility cpp，无需新增编译单元；常驻测试尚只有反向UTF测试。见[最新实际记录](../validation/UTF8_PLATFORM_IO_CODE_REVIEW_20261009_CHS.md)。下一组为[路径基础](PLATFORM_PATH_FOUNDATION_IMPLEMENTATION_GUIDE_20261009_CHS.md)。下方“待填写”及旧81/117结果只属于原阶段。
> **2026-10-08 通用核心进度更新：** UTF-16→UTF-8生产函数已按用户授权收齐BQLog接口和算法，输入长度现在计码元；Layout专用扫描独立保留。本文UTF-8→UTF-16仍是用户待填写的指导，没有代写新函数；旧回转候选的81/117结果只属于当时接口。见[实际核心对齐与验证](../validation/UTF_CORE_ALIGNMENT_20261008_CHS.md)。

日期：2026-10-08。本次按用户最新要求修订；生产新函数由用户填写，下面代码与验证对象均为临时指导材料。

## 源码与模块

QLog 的 include/qlog/utility/utf_conversion.hpp、src/utility/utf_conversion.cpp 已有 utf16_to_utf8_sw，cpp 已接根库。追加另一方向的普通自由函数，namespace qlog::utility；保留原函数与 CR，不新建类、Manager、detail 或空 cpp。

BQLog 参考为 main@78cbfbef4f87e7558335f0c04bf6dd6894b46451。重新核对下列文件相对 HEAD 无差异；参考整树仍有 MISO/构建等本地修改：

- src/bq_common/utils/util.h:114、131：公开转换及软件测试入口声明。
- src/bq_common/utils/util.cpp:798-827：_impl_utf8_to_utf16_sw 软件算法。
- src/bq_common/platform/win64_misc.cpp:32-43：私有路径适配器分配 UTF-16 存储、转换、收缩到实际长度并替换斜杠。

本组仍只补软件转换。SIMD 后置；Windows 路径规则留在后续 platform 模块。

## 最新决定与参考边界

用户最新要求：“修改：完全对齐 不截断应该和已实现的函数一致吧”。这撤销了此前“合法输入对齐 BQLog；遇到截断尾部就停止，保留已转换前缀”的选择。旧规则与包含三处尾部不足 break 的旧指导不再作为实现依据。

按实际 BQLog 软件算法扫描：ASCII、两字节、三字节及 else 分支的读取顺序、位提取和代理项生成保持一致；不添加多字节尾部剩余量检查，不增加容量不足时截断输出、返回部分前缀的规则。dst_character_num 在参考中不参与计算，继续由调用方保证足够容量。

必须区分“不增加尾部停止规则”和“残缺输入也能安全转换”。参考消费头字节后直接读取后续 1/2/3 字节，截断序列可能越界。本指导以完整、有效的 UTF-8 输入和足够目标容量为正常调用前提；没有把残缺输入定义为安全继续或安全返回。完整异常字节序列仍进入参考对应分支，不新增续字节、最短编码、代理区或最大码点验证，也不新增 U+FFFD、异常、错误对象或其他异常编码处理规则。

与已经收齐的 utf16_to_utf8_sw 一致的是调用方提供存储、显式长度以及不按容量截断输出；两个通用软件核心的容量参数均按BQLog保留但不参与计算。反向函数现在直接接收UTF-16码元数量，仍按参考跳过孤立低代理项、丢弃未配对高代理项；它不会据此决定UTF-8残缺尾部规则。两个方向分别对齐各自参考，不要求非法编码处理对称。反向生产函数本次已按用户授权修改，CR保留；本文UTF-8函数仍待用户填写。

QLog 现有的零长度提前返回和非零指针 assert 惯例仍保留：零输入可传空指针，不对空指针做参考中的加法或相减。除这项明确的入口处理外，扫描采用参考的 s/end/d 指针结构；不依赖尚未提供的辅助函数。

## 接口、单位与不变量

- src_character_num 按 BQLog 参数命名，实际计 UTF-8 输入字节；dst_character_num 计可用 char16_t 单元；返回值计已写 UTF-16 单元。均使用 std::uint32_t。
- 按显式长度处理，嵌入零也转换，不用 strlen，不追加终止符。
- 非零输入时指针有效、输入完整、源目标不重叠，目标满足 char16_t 对齐且容量足够；调用方保证长度和返回数量可由 uint32_t 表示。
- 对完整、有效输入，分配“输入字节数个 char16_t”是足够的容量上界；它不是必须分配的最小量，也不新增 dst_character_num >= src_character_num 的断言。实际结果较短时，较小但足够的目标同样可用。
- s 指向下一个待读 UTF-8 字节，end 指向输入末尾之后，d 指向下一个待写 UTF-16 单元。每轮结束位于完整字符边界；在正常输入前提下 s 不越过 end，d 不越过目标空间。
- 外层 s < end 只保证头字节可读；后续字节可读依赖完整输入前提。这不是校验器或分块流式解码器，不保留待拼接尾部状态。
- 返回 d - dst 的单元数。零输入提前返回，避免空指针运算。

## 头文件追加声明

在现有 qlog::utility 内追加，保留反向声明。

```cpp
std::uint32_t utf8_to_utf16_sw(const char* src,
                              std::uint32_t src_character_num,
                              char16_t* dst,
                              std::uint32_t dst_character_num);
```

## cpp 追加完整定义

在现有 qlog::utility 内、原 utf16_to_utf8_sw 后追加；复用现有 cassert。不要覆盖已有函数或 CR。

```cpp
std::uint32_t utf8_to_utf16_sw(const char* src, std::uint32_t src_character_num,
                              char16_t* dst, std::uint32_t dst_character_num) {
    static_assert(sizeof(char16_t) == 2);
    (void)dst_character_num;

    if (src_character_num == 0) {
        return 0;
    }
    assert(src != nullptr);
    assert(dst != nullptr);

    const auto* s = reinterpret_cast<const std::uint8_t*>(src);
    const auto* end = s + src_character_num;
    char16_t* d = dst;

    while (s < end) {
        const std::uint8_t c = *s++;

        if (c < 0x80U) {
            *d++ = static_cast<char16_t>(c);
            while (s < end && *s < 0x80U) {
                *d++ = static_cast<char16_t>(*s++);
            }
        } else if ((c & 0xE0U) == 0xC0U) {
            *d++ = static_cast<char16_t>(
                ((c & 0x1FU) << 6) | (*s++ & 0x3FU));
        } else if ((c & 0xF0U) == 0xE0U) {
            *d++ = static_cast<char16_t>(
                ((c & 0x0FU) << 12) |
                ((s[0] & 0x3FU) << 6) |
                (s[1] & 0x3FU));
            s += 2;
        } else {
            std::uint32_t cp =
                ((c & 0x07U) << 18) |
                ((s[0] & 0x3FU) << 12) |
                ((s[1] & 0x3FU) << 6) |
                (s[2] & 0x3FU);
            cp -= 0x10000U;

            *d++ = static_cast<char16_t>((cp >> 10) + 0xD800U);
            *d++ = static_cast<char16_t>((cp & 0x3FFU) + 0xDC00U);
            s += 3;
        }
    }
    return static_cast<std::uint32_t>(d - dst);
}
```

## 逐分支解释

1. 零长度直接返回 0；非零输入检查指针。uint8_t 视图让高位字节参与运算时保持 0..255。
2. ASCII 输出一个同值 UTF-16 单元，紧密内循环连续处理 ASCII，遇到非 ASCII 后回到外层判断。
3. 两字节头保留 5 位，再拼 1 组 6 位；三字节头保留 4 位，再拼两组 6 位。按参考直接读取续字节，不新增尾部 break。
4. else 按参考用于四字节或异常头字节，拼出 3+6+6+6 位。合法补充平面码点减 0x10000 后分成两个 10 位，分别加 0xD800 和 0xDC00 生成代理对。不能将这个 else 描述为已经验证合法四字节序列。
5. 完整有效输入会处理至 s == end，返回 d - dst；没有按目标容量返回前缀的分支，也没有隐式末尾零写入。

## 面试问题与回答

- 为什么续字节只取 6 位？编码形式是 10xxxxxx，前 2 位是标记，低 6 位承载码点，所以掩码 0x3F、拼接时移 6 位。
- 四字节 UTF-8 为什么输出两个 UTF-16 单元？U+10000..U+10FFFF 减去 0x10000 后最多 20 位，代理对的两个单元各承载 10 位。
- character_num 是可见字符数吗？不是；输入计字节，输出计 16 位单元。码点和用户看到的字素簇又是不同计数。
- 外层有 s < end 为什么还可能越界？它仅约束本轮头字节；多字节分支随后直接读 1/2/3 字节，正确性依赖完整输入。
- 既然传了 dst_character_num，为什么不用它限制输出？这是实际 BQLog 软件函数的接口行为；参数保留，容量责任在调用方，不新增部分成功协议。
- Windows 适配器收缩字符串长度算截断吗？不算转换截断；它移除预分配空间中未使用的尾部，保留所有已转换单元。字符转换和最后的字符串长度管理是两件事。
- 为什么不写末尾零？接口输出存储和长度；嵌入零也属于数据，拥有存储的调用方处理字符串长度或终止符。
- 为什么不加 SIMD/系统 API？当前先补通用软件依赖；平台路径规则与转换分开，优化按既定路线后置。

## 指导验证和接续

本次接续重新验证（UTF-16通用核心接口收齐后）：临时UTF-8→UTF-16候选与当前实际utf16_to_utf8_sw联合严格编译链接，Linux Debug/Release各81检查通过。回转调用已同步为output.data()与UTF-16码元数量n，不再使用旧字节接口的reinterpret_cast<const char*>或n * 2。覆盖空/合法输入，逐项对照当前BQLog软件源码摘录；不执行残缺UTF-8路径。审计目录：/tmp/qlog-utf8-next-guide-20261008-t4586an1。本次没有新增生产声明/定义、改CMake或重跑根库/Windows/sanitizer验收。

此前指导候选（UTF-16接口收齐前的历史结果）：Linux Debug/Release 严格编译链接及定向各 81 检查通过；ASan+UBSan 同组 81 检查通过。覆盖空输入和空指针零长度、长 ASCII、嵌入零、2/3/4 字节边界、补充平面、混合序列、无隐式终止符写入。合法输入对照重新从当前 BQLog 文件摘录的软件函数，并与实际 QLog utf16_to_utf8_sw 回转。源使用精确字节分配；未向取消尾部检查的候选或参考投喂残缺输入。

审计目录：/tmp/qlog-utf8-alignment-20261008-4k8h3ig1。本次未重新进行 Windows 原生编译，也不是实际 BQLog 双库、SIMD、路径或生产新函数验收。生产尚未加入新声明/定义。

历史候选记录：旧“尾部不足时保留前缀”候选曾在 Linux/Windows Debug/Release 各通过 117 检查，Linux ASan+UBSan 同组也通过；这些日志只说明旧候选，不适用于最新取消尾部处理的指导。旧 Linux 审计 /tmp/qlog-platform-io-review-20261008-gf5i4gt4；旧 Windows 审计 D:\qq344\Documents\qlog-io-review-20261008-bf1db3a4。保留历史材料，不把其中残缺输入测试记为当前合同验收。

当前UTF-8→UTF-16仍无生产声明/定义，下一步先在现有utility头/cpp追加本函数，再审实际源码。cpp已经接根库，不增加CMake源文件。2026-10-08重新核对Windows I/O：read循环已修为<，truncate仍使用SetFileInfoByHandle，应改为SetFileInformationByHandle；I/O尚未接根库，原两处问题按[原阶段审查](../validation/PLATFORM_IO_CODE_REVIEW_20261008_CHS.md)保留历史。本函数完成和I/O收口后，进入路径/基准目录/Windows私有适配器 → open/close/exclusive → FileHandle/FileManager → MemoryMap → NormalBuffer → MISO。~user/账户和登记寿命另行讨论；整数越界两个方案继续仅记录、暂不选定。
