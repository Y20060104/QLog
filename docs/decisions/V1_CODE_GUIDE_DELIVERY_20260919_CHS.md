# V1 代码级指南交付检查（2026-09-19）

本次交付：[剩余代码级指南](./V1_REMAINING_CODE_IMPLEMENTATION_GUIDE_CHS.md)、[ADR-017](./ADR-017-v1-output-and-completion.md)。

用户先确认五项推荐，随后要求flush对齐BQLog。最终文档采用单次write循环、短写推进、write EINTR重试、write返回0保留后缀；撤销暂存预算/跨轮flush方案。既有QLog管理接口职责保留，BQLog cache flush与Linux fdatasync分层说明。

检查结果：

- 新指南与ADR的相对文件链接存在，代码围栏配对。
- 新活动接口没有IoBudget参数或resume_flush旧签名。
- 核对当前Ring成员名、formatter writer接口、公共管理声明、配置helper实际目录。
- 从指南直接提取FlushResult类型、FrameLease及write_all循环，使用g++ C++20、-Wall -Wextra -Werror独立编译成功。
- 独立示例验证：EINTR后短写续写、write返回0、已写前缀后ENOSPC、nullptr+0不调用write，均通过。
- 本轮涉及的已跟踪文档git diff --check通过。
- 安装前后对include/src/tests/tools/benchmarks下251个现存文件计算SHA-256，一致；生产源码与测试文件未改。

验证脚本暂存于BQLog工作区project_design/qlog_v1_code_guide_20260919/verify.py；旧文档副本在同目录backups。它们是文档交付工具，不是QLog生产组件。

限制：仅验证文档与独立算法示例，没有编译完整待实现Backend，没有线程/I/O生产集成测试，没有新增V1性能数据。历史formatter/诊断验收保持原范围，不重写为后端完成证据。
