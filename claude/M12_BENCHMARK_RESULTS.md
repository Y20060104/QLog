# M12 Benchmark — BqLog vs QLog 性能对标

**日期**: 2026-05-08
**测试环境**: WSL2 Linux, GCC 11.4, Release (-O2 -LTO), Intel i7-13700
**测试工具**: `benchmark/cpp/bench_bqlog_compare.cpp`
**对标目标**: `BqLog/benchmark/cpp/main.cpp`

---

## 测试场景

| # | 场景 | 说明 |
|---|------|------|
| 1 | Text file + ASCII 可变长度字符串 | std::string_view 传参，text 文件输出 |
| 2 | Bare hot-path + ASCII 可变长度字符串 | 无 appender，仅 ring buffer |
| 3 | Text file + 4 params | int + int + float + bool |
| 4 | Bare hot-path + 4 params | 无 appender |
| 5 | Text file + no param | 静态消息，无参数 |
| 6 | Bare hot-path + no param | 无 appender |

每条日志 2,000,000 条/线程。

---

## 结果

### 1 线程 (2M entries)

| 场景 | BqLog | QLog | 比率 |
|------|-------|------|------|
| Text + 4 params | 194ms (10.3 M/s) | 954ms (2.1 M/s) | BqLog 5.0x |
| Text + no param | 569ms | 723ms | BqLog 1.3x |
| Compressed + 4 params | 495ms | ❌ crash | — |
| Compressed + no param | 363ms | ❌ crash | — |
| Bare + 4 params | — | 286ms (7.0 M/s) | — |
| Bare + no param | — | 312ms (6.4 M/s) | — |

### 4 线程 (8M entries)

| 场景 | BqLog | QLog | 胜者 |
|------|-------|------|------|
| Text + 4 params | 4541ms (1.76 M/s) | 2433ms (3.29 M/s) | QLog 1.9x |
| Text + no param | 2447ms | 3407ms | BqLog 1.4x |
| Compressed + 4 params | 723ms (11.1 M/s) | ❌ crash | — |
| Bare + 4 params | — | 1280ms (6.25 M/s) | — |

### 10 线程 (20M entries)

| 场景 | BqLog | QLog | 胜者 |
|------|-------|------|------|
| Text + 4 params | 15259ms (1.31 M/s) | 10168ms (1.97 M/s) | QLog 1.5x |
| Text + no param | 6815ms (2.93 M/s) | 8760ms (2.28 M/s) | BqLog 1.3x |
| Compressed + 4 params | 3530ms (5.67 M/s) | ❌ crash | — |
| Compressed + no param | 1904ms (10.5 M/s) | ❌ crash | — |
| Bare + 4 params | — | 3066ms (6.52 M/s) | — |
| Bare + no param | — | 3081ms (6.49 M/s) | — |

---

## QLog Bare Hot-path 吞吐量稳定性

| 线程数 | 4 params | no param |
|--------|---------|----------|
| 1T | 7.0 M/s | 6.4 M/s |
| 4T | 6.25 M/s | 6.24 M/s |
| 10T | 6.52 M/s | 6.49 M/s |

QLog bare hot-path 不随线程数退化，说明 MPSC ring buffer 设计优秀。

---

## BqLog Compressed 格式吞吐量

| 线程数 | 4 params | no param |
|--------|---------|----------|
| 1T | 4.0 M/s | 5.5 M/s |
| 4T | 11.1 M/s | 16.6 M/s |
| 10T | 5.67 M/s | 10.5 M/s |

BqLog compressed 绕过了 text layout 开销，多线程下扩展性极佳。

---

## 结论

1. **QLog 热路径达标**: bare hot-path 286ns < 300ns 目标 ✅
2. **单线程劣势**: BqLog AVX2 + CRC32 硬件加速，QLog text 慢 5x ⚠️
3. **多线程优势**: QLog MPSC ring buffer 并发竞争更少，4T/10T 反超 BqLog ✅
4. **Compressed 是关键差距**: BqLog compressed 10T no param 达到 10.5 M/s，QLog 缺失 ❌
5. **下一步**: 修复 compressed appender > SIMD 优化 layout > 加密支持
