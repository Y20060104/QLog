# 格式复制与 hash 融合验证（2026-10-15）

本轮修改4个生产文件：`platform/macros.hpp`、`utility/hash.hpp`、`utility/hash.cpp`、`runtime/record_writer.cpp`；测试增加一个cpp和原BQ期望值夹具，接测试CMake。既有 SISO/MISO、RSA、CRC核心和其他未提交草稿/CR均保留。参考固定78cbfbef，决定见[说明](../decisions/COPY_HASH_FUSION_20261015_CHS.md)。

## 实际构建与常驻回归

| 平台 | 库 cpp | 全部 TU | Debug | Release |
| --- | ---: | ---: | --- | --- |
| GNU/Linux x64 | 57 | 70 | 严格构建，42/42 CTest | 严格构建，42/42 CTest |
| 原生 MSVC x64 | 58 | 71 | 严格构建，42/42 CTest | 严格构建，42/42 CTest |

同时构建实际库、全部常驻测试与两个既有 benchmark 目标。GNU 使用项目警告和 -Werror；MSVC 使用 /W4 /WX /permissive- /utf-8 /EHsc，仅沿用已有 C4324/C4146 排除。逐 TU 检查 QLOG_DEBUG 仅 Debug、NDEBUG 仅 Release，未改变调用方宏。

新增 `qlog.utility.copy_hash`：56组固定参考值 × 8种源偏移 × 8种目标偏移，共3584组输入/位置组合；每组分别验证软件、支持时的硬件、公开分派路径。覆盖0–3、4/8/16/32字节分支与跨窗口尾部，最大4097字节；全零和混合字节输入、内嵌NUL、目标前后保护区、源不被修改及零长度空指针。公开 get_hash_64 也对照同组固定期望值。

新增 `qlog.runtime.format_copy_hash`：真实创建 sync/async/independent，每种模式核对 UTF-8/UTF-16 格式的 hash、字节长度和原数据副本；begin后修改源字符串，确认Record已经拥有复制内容。另检查空格式指针仍为零hash的延后填充路径，随后真实commit、flush及Console输出，共9条记录。既有40项回归同时通过。

## 独立参考与 sanitizer

期望值通过固定 BQLog 原表、软件/硬件helper及 `BQ_GEN_HASH_CORE(true, ...)` 生成，没有用QLog自身计算期望值。原片段 SHA-256：`20030bd54d6ac4dc6b1bf57ec3c8539af437db645f0e95dc32dd70731967dc1f`；向量夹具 SHA-256：`780e68e68433fec3896ea6228401dd1867a6d584d3c10847ff2c34c9f452ae2f`。56组中19组SW/HW值不同，两个期望值分别保存。

GNU 对实际 `hash_software.cpp` / `hash_x86_crc32c.cpp` 及共享模板以 ASan+UBSan重新编译，3584组相同边界组合和零长度调用通过，hardware=1。该检查针对融合内核，不称完整运行态或其他平台 sanitizer 验收。

## 证据与范围

证据根：`/mnt/d/qq344/Documents/qlog-fused-copy-hash-20261015-01`。

- `before/`、`before.json` 保存开始时411个实际文件及已有未提交状态；没有复用更早清理前快照覆盖源码。
- `reference/`、`reference.json`、`reference-include/` 是固定参考原文件/头及出处；参考原工作树未写。
- `reference-copy.cpp`、`generate-vectors.cpp`、`oracle-vectors.txt`、`oracle-provenance.json` 是生成过程与数值。
- `linux-{Debug,Release}-*`、`native-{Debug,Release}-*` 为实际构建/CTest，编译命令位于对应新构建目录。
- `bounds-{build,run}.log` 为核心 ASan/UBSan 证据，`final-verification.json` 为最终文件保护和宏核查。

本轮没有重新运行官方12进程 benchmark，原机器结果JSON保持原始哈希；README表中的版本已明确标为融合接入前。这里证明字节和hash合同、实际集成及定向边界，不给出未经新测量的吞吐提升比例。
