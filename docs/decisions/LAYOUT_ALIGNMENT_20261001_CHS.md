# Layout 参数命名与扩容对齐补充（2026-10-01）

> 2026-10-03进度：前缀与入口已填写，本轮修正抄写错误并新增6个Layout行为测试分组。Debug/Release各7/7（含smoke），完整范围见 [验证记录](../validation/LAYOUT_BEHAVIOR_20261003_CHS.md)。下方旧进度为历史记录。

本文件记录用户最新决定，优先于旧指南以及对话中未确认的防御性方案。

## 对齐范围

- 函数参数名按 BQLog 实际源码取名，并解释其业务含义；代码格式继续采用当前 QLog 的缩进、括号、类型书写和头文件/cpp 分工，不照搬 BQLog 排版。
- 本轮参考 Linux BQLog HEAD：78cbfbef4f87e7558335f0c04bf6dd6894b46451。src/bq_log/log/layout.h、layout.cpp、src/bq_common/utils/util.cpp 相对 HEAD 无差异；整树仍有 MISO/构建等本地修改。
- 普通成员在头文件声明，在 cpp 用 Layout:: 定义；保留 CR。本轮只接入 CMake 和更新文档，未代写剩余生产函数，也未批量重命名用户草稿。

## 参数名映射

| 函数 | BQLog 参数名 | 含义 |
|---|---|---|
| do_layout | log_entry, input_time_zone, categories_name_array_ptr | 借用记录、目标时区、类别数组指针 |
| layout_prefix / insert_time / insert_thread_info / python_style_format_content* | log_entry | 当前记录视图 |
| c20_format | style, len | 格式说明起点、可读码元数 |
| expand_format_content_buff_size | new_size | 所需绝对可写大小，不是追加字节数 |
| insert_str_utf8 / insert_str_utf16 | str, len | 原始数据地址、输入字节数 |
| insert_pointer | ptr | 指针值 |
| insert_bool / insert_char* / insert_decimal | value | 待输出值 |
| insert_integral_unsigned / insert_integral_signed | value, base | 数值、默认进制 |
| fill_e_style | eCount, begin_cursor | 指数值、本次数值起始游标 |
| fill_and_alignment | write_begin_pos | 待对齐字段起点；用户明确不沿用参考的 wirte 拼写错误 |
| reverse | begin_cursor, end_cursor | 反转区间的首尾下标，尾端包含在内 |

参数名对齐以含义和单位一致为前提。当前 utf16_to_utf8_sw 是接收原始字节的适配接口，src_byte_len 是字节数；BQLog 的 src_character_num 是 UTF-16 码元数。不能只改名就隐式改变单位。该适配差异保留并解释，后续若改变签名先核对调用方。

## 扩容决定

依据 BQLog layout.cpp:1100，保留 void 返回类型和 uint32_t 容量运算：offset 非零时，new_size 加 width + precision + 2；若已有 size 足够则返回，否则按 round_pow_of_two 扩展可写元素范围。QLog 使用 vector.resize 实现可写 size，不能仅 reserve。

不采用此前提议的 layout_failed_，不增加通用错误传播层，不为该扩容函数新增 parse_error 合同，不自动追加异常捕获、截断或容量上限。

UINT32_MAX 精度哨兵参与无符号运算的回绕本身有定义；在当前 32 位算术下，width + UINT32_MAX + 2 等于 width + 1（模 2^32）。本轮不将它宽化成额外申请约 4 GiB。其他长度加法或向上取整越界仍是已知未验收边界，不能由此声称任意长度安全；本决定不授权静默复制后续越界访问或浮点转换等 UB。已知风险保留记录，不扩大防御性机制。

std::vector<char>::resize 会初始化新增字符，与 BQLog fill_uninitialized 不同；这是当前容器适配成本，不声称分配/初始化性能一致。

## 当前进度与后续

用户已修正解析器冒号字面量、分号、atoi、B 归一化，以及 UTF 代理下界和 0x10000 基值。expand_format_content_buff_size 仍为空；其余 Layout 主链尚未完成。CMake 现纳入 src/layout/layout.cpp 与 src/utility/utf_conversion.cpp，构建证据另见 docs/validation/LAYOUT_CMAKE_20261001_CHS.md。

下一步：按上表同步相关参数名，填写扩容、UTF-8/UTF-16 追加、bool/char/char16/char32；随后整数/指针、填充/指数/浮点、正文扫描器、前缀/do_layout。当前软件转换不是完整 SIMD 对齐。TimeZone 之后仍聚焦 Layout，不提前进入 Worker/Appender。

## 协作分工补充（2026-10-01）

用户明确：已经实现的函数由助手核对 BQLog 并同步参数名（声明、定义和函数体引用一起修改）；尚未实现函数由用户完成定义和实现，不由助手补写。本次仅将已实现 Layout::reverse 的 end_inclusive 改为 end_cursor，闭区间语义不变。c20_format 的 style/len 及 TimeZone 对应函数参数已与参考一致，无需重复修改；UTF 字节适配接口保留上文的单位差异。空的 expand_format_content_buff_size 仍视为未实现，留给用户按 new_size 命名填写。

命名补充：用户在 fill_and_alignment 的 CR 中明确不沿用 wirte 拼写错误，QLog 保留 write_begin_pos；命名学习侧重实际含义，不复制明显拼写错误。已核实 Release_2.5.0 标签的 layout.cpp:540 也使用 wirte_begin_pos。fill_e_style 在参考头文件使用 eCount、cpp 使用 e_count；后续 QLog 声明/定义统一使用 cpp 的 e_count。
