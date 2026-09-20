# QLog Producer 诊断专项验证

日期：2026-09-19。权威仓库 `/home/qq344/QLog`。结论：**当前 Producer 诊断 B 专项在 WSL2 开发环境通过，可以进入完整 V1 后端开发。** 本记录不等于 Backend、I/O、TSan 或原生 Linux 发布验收。

## 实现与测试边界

- 新增 `tests/producer_diagnostics_test.cpp` 和 `tests/producer_diagnostics_consumer.cpp`；注册 `qlog.producer_diagnostics`。
- 将已有 `tests/i2_runtime_public_probe.cpp` 接入独立目标 `qlog_producer_public_probe`，实际链接 qlog 静态库；包含23种错误映射、公开路径、并发首次使用及同地址重建检查。
- `include/qlog/async_logger.hpp` 仅增加内部测试友元声明，不增加运行期状态、公开统计接口或热路径操作。
- `src/async_logger.cpp` 补齐直接使用的 `<cassert>` 和 `<exception>` include。没有修改既有计数分类和 try_log 的执行顺序；本轮未发现计数逻辑错误。
- 内部计数测试包含实际 `src/async_logger.cpp`，使测试访问器能看到完整 Impl；不是另写一份记账算法。该测试提供 AsyncLogger 定义，静态链接不再抽取库内同一对象。另一个公开探针独立链接真实库，覆盖实际库/头接线。
- 所有计数读取都发生在对应 Producer 停止或 join 后，不承诺多个 relaxed load 是运行期一致快照。

## 覆盖范围

| 场景 | 检查 |
|---|---|
| invalid_level / invalid_category | calls 与 rejected_pre_admission 各增1，不创建 Context |
| filtered | calls 与 filtered 各增1；过滤先于坏 format 检查，不创建 Context |
| Context 注册关闭 | 公开 try_log 返回 registration_closed，计入 pre-admission rejection |
| measure 无效 format / format 太长 | 不计 attempted，后续正常输入仍能成功 |
| accepted | calls / attempted / accepted 各增1 |
| literal / FormatView / 非NUL结尾数组 | 跨翻译单元调用，数组转发没有重复计数 |
| full | 使用无消费者的有界 Ring 确定性填满，仅计 dropped_full |
| reserve 非 full | 内部持有 reservation、受控 payload quota 不一致，实际经过 try_log 的 reserve 失败返回点 |
| encode 失败 | 将测试拥有的依赖视图临时绑定到拒绝等级的合法 policy，不修改 const 生产对象；实际走 encode 失败与 abort |
| abort 后恢复 | 失败不提交 Record；恢复依赖后下一次 try_log 成功 |
| 极限资源错误 | allocation_failed / TLS容量 / token耗尽等使用映射结果直接送入生产记账函数；不声称公开 API 真实耗尽了这些资源 |
| 8线程、2 Logger | join 后核对第一 Logger 8000 calls、4000 rejected、4000 accepted；另一 Logger 4000 accepted，互不串账 |
| close_registration 边界 | 已有 TLS Context 仍可使用；该操作不能代替 shutdown/join |

每个单线程步骤及并发结束后均核对七项精确值和两个恒等式：

```text
calls = filtered + rejected_pre_admission + attempted
attempted = accepted + dropped_full + failed_after_attempt
```

计数守恒适用于计数未溢出时。本次没有模拟 2^64 次调用，也没有增加字节统计。

## 配置及生成代码验证

| 配置 | AUTO | ON | OFF |
|---|---|---|---|
| Debug | 开启，通过 | 开启，通过 | 关闭，通过 |
| Release | 关闭，通过 | 开启，通过 | 关闭，通过 |

六种配置都实际构建 qlog 库、内部诊断测试和独立公开探针，两项 CTest 全部通过。外部核验按配置名独立计算期望值，不只让两个由同一个 CMake 变量生成的宏相互比较。

关闭时检查：Impl 不含 diagnostics 成员；实际库不含 record_call_result 符号；消费方 object 无对应引用；消费方预处理结果不含该记账声明/调用。因此诊断原子更新路径被移除。此结论不表示 Producer 整体没有原子操作，Ring发布、Context注册及将来的压力唤醒仍按既定协议执行。

显式非法值 `QLOG_ENABLE_DIAGNOSTICS=INVALID` 被 CMake 拒绝。没有把宏定义改成 PRIVATE，没有引入 QLOG_TEST_* 条件改变生产类的字段布局。

## 回归结果与证据

- GCC 13.3 Debug AUTO：142项全部通过。
- GCC 13.3 Release AUTO：141项通过，1项按既有 Ring validation 配置跳过，0失败。
- Clang 18 Debug AUTO + ASan/UBSan：142项全部通过。
- 现有 Release `producer_context.cpp` 中 index 参数未使用告警仍存在，未将它归入本次计数逻辑修复。
- `git diff --check` 通过。

原始日志、六配置独立预期/符号检查和源文件SHA256保存在 `docs/validation/diagnostics_20260919`。TSan、真实 worker 与故障恢复、多目标输出、原生 Linux 验证属于后续完整 V1 验收；本次不能替代。

## 复现

在仓库根执行。下面每个目录只构建这两个专项目标；其他默认测试未构建时不要在该目录运行不带标签的全套 CTest。

```sh
for config in Debug Release; do
  for mode in AUTO ON OFF; do
    build_dir="build/diagnostics-${config}-${mode}"
    cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE="$config" \
      -DQLOG_ENABLE_DIAGNOSTICS="$mode" \
      -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$PWD/build/_deps/googletest-src"
    cmake --build "$build_dir" --target \
      qlog_producer_diagnostics_test qlog_producer_public_probe -j 4
    ctest --test-dir "$build_dir" -L diagnostics --output-on-failure
  done
done
python3 tools/verify_diagnostics_matrix.py
```

## 接入 Backend 时必须保留的测试意义

当前 full/内部故障测试依赖“没有消费者”的受控环境。接入 Backend 后，应将这些场景迁入显式静止的 Producer fixture，不能依赖真实 worker 恰好没有运行，也不能删除测试以掩盖失败。公开探针中的“最终full”和“注册关闭”同样需要适配；新增真实消费、停止、排空、管理和资源回收测试。

下一阶段完整范围见 [完整V1收尾执行计划](./V1_FULL_COMPLETION_EXECUTION_PLAN_20260919_CHS.md)，不再把 B 专项列为未完成。
