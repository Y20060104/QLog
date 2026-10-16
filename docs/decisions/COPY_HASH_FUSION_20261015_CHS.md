# 格式复制与 hash 融合对齐（2026-10-15）

用户明确授权修复 QLog 先复制格式字符串、再单独计算 hash 的差异。权威源码为 `/home/qq344/QLog`，起点 `ee3ac945` 加已有未提交命名/RSA变更。生产参考固定 BQLog `78cbfbef4f87e7558335f0c04bf6dd6894b46451`，不采用参考脏树的 MISO 修改，也不在本组整体升级到 benchmark 的 086f2fd。

## 实际来源与变更

固定参考 `util.h:26` 声明 `bq_memcpy_with_hash(dst, src, len)`；`util.cpp:156-281` 为共享四路 CRC 内核，284-297 为复制/后端分派，325-327 为公开薄入口；`bq_log_api.cpp:264` 在复制原始格式字节时直接存入返回的 hash。

QLog 已有的 `software_copy_raw` 和 `x86_copy_raw` 都实例化同一 `hash_core<true>`，此前未接到 Record。现在新增 `qlog::utility::qlog_memcpy_with_hash(void* QLOG_RESTRICT dst, const void* QLOG_RESTRICT src, size_t len)`：普通声明放 `hash.hpp`，非 inline 定义放 `hash.cpp`；复用 `get_hash_64` 的同一个 CPU 能力快照和软件/x86分派。`QLOG_RESTRICT` 的 GCC/Clang、MSVC、其他平台三分支逐项采用参考 `macros.h:300-306`，仅改工程前缀。

`record_writer.cpp::log_write_begin` 的非空 `format_str_data` 分支用一次融合调用完成复制并填写 `head->format_hash`。公开普通 UTF-8/UTF-16、无堆栈拼接的格式路径已经进入该分支。现有三个内核文件逐字节不变，CRC 原表（含206/207项）、哈希数值、Record字段及压缩协议均不变，没有新建另一套 CRC 状态或空 cpp。

## 合同与边界

- 参数与参考保持 `dst, src, len`，长度是字节数；源和目标在非空范围内不得重叠，不增加重叠搬移或错误返回协议。
- 复制恰好 len 字节，不额外复制字符串终止符或解析格式；空长度返回零，覆盖空指针/零长度调用。
- 四路累计在读取字块的同时完成复制，32字节主循环及重叠尾窗口沿用现有内核。融合不代表每个字节严格只加载一次，尾部重叠是原算法的一部分。
- `format_str_data == nullptr` 仍把 hash 留为零；自定义格式、UTF-32转换及堆栈拼接等由现有模板填充，压缩器按原规则在需要时补算，未把源编码 hash 误当成最终编码 hash。
- 软件与硬件分别对齐参考；原软件表导致的后端差异继续保留，不强行统一指纹。本轮没有新增 ARM 硬件实现，现有架构分派不扩大。
- 保留现有 hash-only 的 Copy=false 空目标指针处理及此前其他明确适配；本轮不改变 RSA、SISO、Layout 或生命周期合同。

## 验证与性能记录

固定参考原 `DO_COPY=true` 软件/硬件函数独立生成56组期望值（19组两后端不同），作为常驻夹具。新增复制内容/边界及实际 Record 三线程模式回归，两平台 Debug/Release 各42/42；核心 ASan/UBSan 定向通过。详情见[验证](../validation/COPY_HASH_FUSION_20261015_CHS.md)。

README 既有官方 benchmark 表保留原数值并标识“融合接入前”；本轮未重跑整套官方性能测量，不把已有性能表作为融合后的收益证据。
