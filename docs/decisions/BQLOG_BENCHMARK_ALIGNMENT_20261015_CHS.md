# 官方 benchmark 对齐决定（2026-10-15）

- 新参考单独置于 `/home/qq344/BqLog-benchmark-20261015`，拉取时固定官方提交 `086f2fd831af86fcfa40e90015f89626d498d3d7`；稀疏检出 src/include/benchmark 及根文件。旧参考的 HEAD、MISO/CMake 本地改动和未跟踪文件保留。
- 只迁移官方 C++ benchmark 的工作负载，复用当前 QLog。用 std 字符串/容器与现有 info/force_flush_all 接口适配；不将生产库整体迁到新提交，不改既有 Buffer/Record/Layout/Worker/Appender。
- 用户明确选择普通接口互比、BQLog FAST 单列，QLog 无此 API，标为未支持。没有用普通入口代替 FAST，也没有为本轮新增生产 FAST 接口。
- 保留每线程每场景 200 万、默认格式池 50000、原消息/参数/字符池/多模板选择、默认配置和 flush 计时。1/4 线程各三轮，用全新输出目录，Q/BQ、BQ/Q、Q/BQ 顺序；库和 benchmark 前后源码清单比对。
- 保留上游 C++ system_clock 毫秒计时；时钟偏移、库 I/O 诊断和 WSL Windows 宿主盘余量检查放在外部运行器。完整原程序仍包含 BQ 额外 FAST 场景，两程序完整运行历史不完全相同。
- 初批 Windows C 盘耗尽使 WSL 出现 I/O 错误，另有墙时钟数据不自洽，整批排除。用户允许暂停 Ubuntu 和备份清理本轮输出；恢复后核对源码、复跑 40/40 CTest，80 个旧输出文件逐文件 SHA-256 备份到 D 盘后才移除原目录。实际仅 guest fstrim，未执行 Windows 离线 VHD 压缩；正式测量使用新的 -02 目录。
- 吞吐只称名义调用吞吐；原返回值未统计，capacity_limit=1 下保留文件不代表全量。写侧分位数是 20 次调用组成批次的平均值分位数。Windows 整套只作运行验证，不混入 WSL 性能表；未跑新参考全量解码/丢失/断电/TSAN 验收。

结果及命令见 [README](../../README.md)；结构化结果见 [BQLOG_BENCHMARK_20261015_RESULTS.json](../validation/BQLOG_BENCHMARK_20261015_RESULTS.json)。本文件只记录决定，原始日志、源码清单及二进制哈希位于 README 指向的独立取证目录。
