# 旧Ring配置与依赖清理记录

日期：2026-09-23。用户要求Ring按BQLog block单位重建并删除旧配置，结合已授权的冲突文件/测试/基准/过期文档清理，已删除下列30个文件。

不是把旧构造函数改个名字：旧SpscRingBufferConfig与其自有内存、字节游标实现、验证开关和相关测试/基准一起退出。新SISO生产实现尚未创建，当前库仅version.cpp。

新入口：[block Ring指南](./R0_BQLOG_BLOCK_RING_GUIDE_CHS.md)。

备份：`/tmp/qlog-old-ring-20260923-zdd75bl_/before.tar.gz`；逐文件SHA-256：`/tmp/qlog-old-ring-20260923-zdd75bl_/deleted.json`。

## 删除清单

- `benchmarks/CMakeLists.txt`
- `benchmarks/README_CHS.md`
- `benchmarks/benchmark_types.hpp`
- `benchmarks/bqlog_benchmark.cpp`
- `benchmarks/linux_support.hpp`
- `benchmarks/qlog_benchmark.cpp`
- `benchmarks/spsc_ring_benchmark.cpp`
- `benchmarks/spsc_runner.hpp`
- `docs/decisions/ADR-001-spsc-frame-ring.md`
- `docs/decisions/ADR-002-tail-header-contiguous-payload.md`
- `docs/decisions/ADR-003-long-lived-spsc-handles.md`
- `docs/decisions/ADR-004-short-lived-spsc-handles.md`
- `docs/decisions/ADR-005-passive-spsc-handles.md`
- `docs/decisions/ADR-006-spdlog-benchmark-layering.md`
- `docs/decisions/M2_SPSC_RING_BUFFER_GUIDE_CHS.md`
- `docs/decisions/MILESTONE1_COMPLETION_REPORT_CHS.md`
- `docs/decisions/SPSC_ARCHITECTURE_REVIEW.md`
- `include/qlog/detail/ring_geometry.hpp`
- `include/qlog/detail/spsc_ring_buffer.hpp`
- `src/build_config.hpp`
- `src/spsc_ring_buffer.cpp`
- `tests/geometry_test.cpp`
- `tests/spsc_ring_buffer_header_self_contained.cpp`
- `tests/spsc_ring_buffer_layout_test.cpp`
- `tests/spsc_ring_buffer_operation_test.cpp`
- `tests/spsc_ring_buffer_payload_test.cpp`
- `tests/spsc_ring_buffer_storage_test.cpp`
- `tests/spsc_ring_buffer_test.cpp`
- `tests/spsc_ring_buffer_test_access.hpp`
- `tests/spsc_ring_buffer_wrap_concurrency_test.cpp`

## 指南局部验证

在 `/tmp/qlog-ring-guide-layout-h4e_78lf` 使用GCC C++20编译指南的Head/ChunkHead/Block静态断言通过：256字节Head、12字节定位结构、payload偏移8、8字节Block。另对N=2..128各2次幂、全部物理起点与0..N*8请求做175006组布局算术检查，核对容量判定和连续payload不覆盖尾部chunk头。此为声明/数学验证，不是参考库差分、原子并发或恢复验收。

## Build verification

GCC 13.3 Debug: fresh build/r0-block-ring-baseline-20260923 configured and built successfully; CTest 1/1 smoke passed. Active include/src/tests/CMake no longer reference SpscRingBufferConfig, QLOG_ENABLE_RING_VALIDATION, max_payload_bytes or publish_reclaimed. git diff --check passed. This verifies only the retained version foundation, not the new SISO implementation.
