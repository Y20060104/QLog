# FileCompressed 对齐决定（2026-10-15）

用户本轮明确要求补充对齐 file compress，覆盖此前“不自动扩 compressed”的范围限制。此次由助手实现、接库并补常驻测试；旧 Logger/codec/PreparedRecord 不恢复，RawAppender、生产 Decoder 和跨语言接口仍不自动扩项。

## 来源与模块

权威 QLog：feat/spsc-ring-opt@c2faba4032c145de534d0a3d70cc05af58b523b4 加当前未提交工作树。参考 BQLog：78cbfbef4f87e7558335f0c04bf6dd6894b46451；整树仍有 MISO/构建本地修改。采用的16个文件逐项检查相对 HEAD 无差异，SHA-256 与上一轮 git archive 固定导出也一致；参考工作树未修改。

- `runtime/appender/appender_file_binary`：原文件头、分段容器、metadata、读缓存定位、拓扑检查、普通/恢复分段及加密链。
- `runtime/appender/appender_file_compressed`：原模板缓存、压缩记录写入、文件复用扫描、配置初始化和重置；工厂识别 `compressed_file`。
- `utility/bounded_hash_cache.hpp`：原线性探测缓存、半满倍增、硬条目上限、1/64准入和时钟淘汰、insert_token、配置缩小及原测试钩子。
- `utility/log_utils.hpp`：原命名的 signed traits、ZigZag 和 VLQ；已迁到配置模块的过滤函数不再复制。
- `utility/utf_mixed`：ASCII 前缀 + 0xFF + 原 UTF-16 后缀、反转 UTF-8、重建原 UTF-16 格式哈希；复用已有软件 UTF 转换及已选择的 CRC 后端。
- `encryption/aes、rsa、vernam`：原 AES-CBC/RSA/XOR 文件加密链；Vernam 保留 AVX2/SSE/NEON/标量及原分派。随机数使用原独立 TLS xorshift32/64 状态。
- `utility/aligned_allocator.hpp`：为标准容器适配原平台对齐分配服务；实际库新接7个 cpp，Linux57、Windows58。

## 格式与缓存合同

扩展名 `.logcompr`、格式版本10、compressed类型值2；8字节文件头、12字节分段头、52字节首段metadata和类别表保持参考。压缩项继续使用格式模板、线程信息模板、时间差 ZigZag/VLQ、模板索引及去除内存对齐填充的参数数据。原 UTF-16 参数转 `string_utf_mixed_type`，长度预估缩短时保留原零占位规则。VLQ 是参考自己的前缀长度编码，不替换成通用 LEB128。

格式键仍为 format_hash XOR category/level 混合值；L1直接映射256/64槽，L2默认100000/2048项；最小8，最大16*1024*1024及1024*1024。配置名保留 `format_template_cache_max_entries`、`thread_info_cache_max_entries`。不增加碰撞比对、原子、断言或额外拥有型包装；缓存满、分配失败、容量重置与模板索引递增按原规则。

QLog 已确认的内存 RecordHeader/tag/序列化协议不变。压缩器经现有 LogEntryHandle 取字段和参数，不把 QLog 内存 Record 原样冒充 BQLog Raw 文件。普通 UTF-32 继续走既有 UTF-16 协议，custom四字节字符仍是未决 static_assert。

## 明示适配与修正

标准 string/vector/tuple 代替原对应容器；追加、clear、原始指针和移动调用逐项适配。`aligned_allocator` 增加 STL 必需的 rebind，并按元素数量计算分配字节；分配失败按标准容器合同抛 bad_alloc，不声称保留 BQ 自有容器的失败形态。UTF-Mixed 的字节格式和重建哈希保持参考，扫描/扩展 ASCII 采用当前已接受的软件路径，没有迁入 UTF 专用 SIMD。

ZigZag 左移先转对应无符号类型，保留所有编码位，避免依赖旧语言版本的负有符号左移规则。原线程模板写入中的断言专用 `real_body_len_size` 标为 `[[maybe_unused]]`，Release执行的VLQ写入不删除。

BQ `rsa.cpp:64-78` 的 `trim_be` 及 `rsa.cpp:428-446` 的 `fixed_limbs_to_min_be` 在全零输入时，把已到末尾的指针再复制1字节。QLog这两处显式返回单字节零，不复制末尾之外的字节。原 encrypt 对首字节零的拒绝仍保留；新回归分别验证零密钥解析被拒、全零密文解出零，不能用“加密零明文成功”作为参考合同。

## 保留边界

沿用此前已明确接受的原子映射、typed字节访问和 MISO读取顺序。本模块继续保留原哈希缓存碰撞行为、线程名仅按线程ID缓存、恢复时格式表重建和线程信息扫描规则。VLQ解码没有输入长度参数；文件复用的常规成功不能推广为任意截断/损坏输入安全。零占位与字符串首零字节的歧义保留，不声称覆盖所有内嵌NUL组合。

加密仍是参考 raw RSA、AES-CBC、周期XOR key blob及xorshift流程，不升格为安全审计结论；文件加密互通使用原固定256字节RSA密文所对应的2048位测试密钥。软件CRC表/硬件潜在差异继续沿用此前决定，不声称所有硬件环境的指纹一致。加密文件不按明文复用；显式 force_flush 仍是调用方责任，不新增析构自动排空/强事务或持久化保证。

本轮四配置各40/40回归，固定参考原解码器对四配置共40文件、24008记录成功互通。未做崩溃/短写/断电故障注入、全部恢复分支、TSAN、ARM运行或新 Compress 性能验收。证据与配置见[验证记录](../validation/FILE_COMPRESSED_ALIGNMENT_20261015_CHS.md)。
