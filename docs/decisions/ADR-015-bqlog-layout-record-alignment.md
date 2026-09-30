# ADR-015：完整 Layout 与记录主链按 BQLog 重建

> **2026-10-01 最新决定与进度：** 参数命名按 BQLog 实际含义对齐，代码格式沿用 QLog 当前要求；扩容按参考 layout.cpp:1100，不增加 layout_failed_ 或通用防御性错误协议。Layout/UTF cpp 已接入 CMake，但扩容仍为空，完整布局尚未实现；Debug/Release 库与现有 smoke 可执行文件构建通过，保留空扩容函数警告；未运行测试，未验证完整 Layout 调用方链接。详见 [构建记录](../validation/LAYOUT_CMAKE_20261001_CHS.md)。详见 [命名与扩容补充](./LAYOUT_ALIGNMENT_20261001_CHS.md)。本条优先于下方历史进度。

- 日期：2026-09-22；状态：用户已确认方向，当前交付开发指南，未替换生产源码。
- 优先级：本决定补充 ADR-014，并取代其仍可被解释为保留旧 Record/formatter 接口的限制。
- 参考提交：BQLog 60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9。

用户明确：核心采用 layout 对象，拥有可复用字符缓冲、输出游标、当前格式说明、格式扫描与参数转换；调用后读取内部结果。非本部分以及已经实现冻结部分，只要与 BQLog 冲突也允许修改，优先保证对齐。

因此完整前缀与正文一并进入 R0 基础重建，记录头、参数编码、时间单位、记录视图和时区对象同步对齐。不得保留旧 32 字节头/纳秒/旧 tag/DecodedArg/六参数固定输出缓冲作为兼容性前提。替换前后记录协议不得混用，不声称兼容旧持久化格式。

当前唯一 R0 实施指南：[R0_BQLOG_LAYOUT_REBUILD_GUIDE_CHS.md](./R0_BQLOG_LAYOUT_REBUILD_GUIDE_CHS.md)。旧正文抽取指南、接口对照和临时预演退出当前验收入口；旧事实保留。

逐目标完整布局；Layout 结果借用且非持久化结果；Logger 名和换行按 Appender 分层。Manager/Worker/Buffer/reset 等其他主链仍以现行路线的 BQLog 优先原则推进，不恢复旧 Session 和固定 SPSC 冻结限制。

对齐依据是固定版本源码；不将 std::format 行为或旧 QLog 增强保证替代参考。不复制未定义行为；疑似 UTF-32 格式 tag、时间缓存、浮点和容量算术等边界单列复现与修复记录，不能宣称已验证字节一致。

本决定不自动移植 raw/compressed、跨语言等全部产品能力。本轮只修改文档，没有新实现编译/测试通过结论。
