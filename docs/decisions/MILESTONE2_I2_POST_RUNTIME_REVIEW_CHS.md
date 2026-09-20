# I2 runtime 实现后的静态交接与下一步商榷

> 最新交接：[V1 一轮收尾指南](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md) 已覆盖剩余生产实现。本文是历史静态检查，不再将这里的旧问题和设计候选当作当前待办。

> 后续状态说明：本文保留当时的静态交接记录，其中语法与公共头包含顺序问题已修正。当前已接受的 literal 方案以 ADR-014 为准，具体实施见[数组格式入口实现指南](./MILESTONE2_I2_LITERAL_HANDS_ON_GUIDE_CHS.md)；本文早期 literal 讨论不再作为当前实施要求。

日期：2026-09-16。用户要求先商榷/给出指南，再测试。本轮仅静态阅读与文档更新，没有编译、运行测试或修改生产实现。
当前权威目录 /home/qq344/QLog。沿用 ADR-013，不新增身份别名、锁、强制绑定或 V2 路由策略。

## 1. 本轮完成情况

从源码确认已补齐：公共 LogResult；TLS 指针、编号重试、vector.max_size、槽准备/回滚；make_context、缓存安装、CAS 发布；Impl 依赖初始化与链回收；runtime try_log 主流程。
这些是“实现已写入”的静态结论，不是编译或运行通过。此前指南关于这些项目的待办已过时，以本节及下面剩余清单为准。
实际模板文件目前位于 include/qlog/async_logger_impl.hpp，不在 detail 子目录。可以保留当前路径，重要的是公共入口能引入定义，且不要保留两份模板。

## 2. 测试前必须修正的具体位置

### 2.1 async_logger_impl.hpp：invalid_category 分支缺冒号和 return

在 switch(classify_call(...)) 内，将当前连在一起的 case 与工厂调用改为：

```cpp
case detail::CallGate::invalid_category:
    return detail::LogResultAccess::make_failed(
        LogStatus::invalid_category,
        LogFailure{FailureStage::validation,
                   FailureReason::invalid_category, 0xFFU, 0});
```

此分支直接结束函数，不落入 filtered 或 proceed；其余 measure→reserve→timestamp→encode→commit/abort 顺序保留。

### 2.2 producer_result_map.hpp：四个 switch 的兜底位置

当前 assert(false)/terminate 放在 switch 内、最后一个 case 的 return 后。它们既不可达，也不能处理没有匹配 case 的枚举值。
四个函数都改成下面的控制结构，注意 switch 的右括号位置：

```cpp
switch (error) {
    // 每个合法 case 都返回完整 LogResult。
}
assert(false);
std::terminate();
```

这里是结构示意；map_measure_failure 的输入是 failure.error，map_reserve_error 的输入是 status。
map_reserve_error 另加 `case ReserveStatus::ok: break;`，让错误地传入成功状态时落到 switch 外的断言/终止。
不要写一个任意 internal_error 返回掩盖无对应 FailureReason 的不变量错误，也不要只依赖 Release 会被移除的 assert。

### 2.3 map_encode_error：一个错配、一个遗漏

当前 invalid_time_value 被映射成 fallback_timestamp_not_configured，且缺少真正的 fallback 分支。
将对应位置拆成两个完整 case，随后再关闭 switch、放公共兜底：

```cpp
case EncodeError::invalid_time_value:
    return LogResultAccess::make_failed(
        LogStatus::internal_error,
        LogFailure{FailureStage::encode, FailureReason::invalid_time_value,
                   0xFFU, target_size});
case EncodeError::fallback_timestamp_not_configured:
    return LogResultAccess::make_failed(
        LogStatus::internal_error,
        LogFailure{FailureStage::encode,
                   FailureReason::fallback_timestamp_not_configured,
                   0xFFU, target_size});
```

最后应有七种 EncodeError，各对应同名 FailureReason。其余三个 mapper 的正常映射，从本次阅读未发现与上一指南表冲突，仍须后续验证。

### 2.4 async_logger.hpp：业务只 include 一个公共头就能调用模板

当前公共头没有引入模板定义；用户只 include async_logger.hpp 时仅能看到声明。
沿用当前路径，在 async_logger.hpp 文件末尾、class 和 namespace qlog 都关闭后增加：

```cpp
#include "qlog/async_logger_impl.hpp"
```

同时删除实现头中的 `#include "qlog/async_logger.hpp"`，形成单向关系：公共类声明→实现头→完整 Context/codec。
实现头是公共入口的内部实现文件，不要求业务直接 include 它。不要把模板挪进 cpp，也不要同时新建另一份 detail/async_logger_impl.hpp。
将 classify_call/acquire_context_for_thread 两个声明移到 private，try_log 保留 public。测试访问需求不能成为长期暴露内部 Context 的理由。

### 2.5 根 CMakeLists.txt：现有 qlog target 尚未编译 I2

在现有 add_library(qlog STATIC ...) 中保留 I1 源，并补入：

```cmake
src/filter_state.cpp
src/admission_clock.cpp
src/producer_context.cpp
src/async_logger.cpp
```

Channel 当前头内定义，不添加空 channel.cpp；身份分配定义在 producer_context.cpp，不另造 producer_identity.cpp。
这一步完成后，后续实际构建才会覆盖新增 cpp。现有 I1 测试即使通过也不能单独证明 I2 正确。

## 3. 建议推进顺序——待本轮商榷

建议采用：本节修正 → runtime 专项验证 → Debug 诊断/literal 补齐 → I2 完整验收 → I3 Backend/Console。
不建议在尚未验证的 runtime 路径上同时叠加 literal、诊断和 Backend：一旦出错，会难以区分是现有接线还是新增功能造成。
这是推进顺序建议，不更改 accepted 的语义，也不把 runtime 专项通过称为完整 I2 完成。

如果用户希望先补齐诊断/literal 再统一测试，应先细化下面第5节的具体接口；本轮不默认替用户选择该路线。

## 4. 商榷确认后拟进行的第一轮验证（本轮未执行）

1. GCC/Clang 的实际 Debug 构建及真实消费方链接：消费方只 include async_logger.hpp，并实例化 runtime 两整数调用；验证模板与四个 I2 cpp 真正接入。
2. 四 mapper 的合法错误枚举逐项验证 status/stage/reason/index/bytes，尤其修正后的两个 EncodeError。随后检查 invalid level/category、filtered、正常 accepted、full 的公开结果。
3. 实际 Logger 调用后的 Ring→I1 decoder 验证：核对格式字节、参数、category、level、时间 flags；accepted 只代表提交，不要求此时 Console 输出。
4. TLS 复用与注册：同线程重复、A→B→A→A、多线程首用、无并发使用前提下两种退出顺序。分配失败注入作为独立测试工具，不在每条生产路径加入常驻故障开关。
5. 再做 Release、ASan/UBSan 等相应覆盖。诊断尚未实现时，只报告 runtime 覆盖范围，不冒充诊断宏矩阵或无稳态分配已经通过。

建议测试访问方式：测试专用 friend/accessor，定义放测试支持文件、私有 Impl 访问落在可见完整 Impl 的位置；无业务 public getter，无生产热路径分支、不用解封 private 宏。具体实现进入测试阶段再确定，当前不新增该接口。
测试消费者借用节点和 Record view 的寿命不能超过 Logger/读取帧，读取后按 Ring 协议释放。它不是未来 Backend 的 API 承诺。
测试方案若必须修改生产接口或增加常驻状态，将先说明具体需求再商榷；编译修正与既有合同内的测试不反复要求确认。

## 5. runtime 稳定后仍需完成的工作

### 5.1 Debug 诊断

已定：过滤前不建 Context，calls 不能仅存在 Channel；普通 Release 删除统计字段与更新，宏经 qlog target PUBLIC 传播；结束/join 后验证守恒。
待细化：Impl 内计数结构、模板到诊断的桥接方式、读取快照接口与测试访问。应明确每个分支只记一次，以及关闭宏后不留下函数调用开销。
共享计数只在诊断构建存在；不要在此时为了统计引入每条 Release 原子 RMW。

### 5.2 literal 格式

已定：runtime stored_hash=0；literal 可提供受控预计算 hash，与 I1 哈希算法一致；保持 pointer+长度，不允许普通调用者任意填非零 hash。
待细化：选择何种 literal 工厂表达式、如何在编译期取得字符数组并保证静态存储期、char/char8_t 支持边界、临时对象寿命与误用诊断。
先给出明确签名与调用示例再冻结，不用一个普通字符串参数函数冒充编译期预计算。

### 5.3 I3 及 V2

I3 才实现 Backend 扫描、基础文本格式化与 Console 输出；空配置默认 Console 的合同保持。
动态 Appender 命令交接、文件 reset 失败策略、V2 SPSC/MPSC 切换与跨队列 FIFO 仍未确定，届时先商榷。本批不新增 NullAppender，不为测试改变生产默认输出语义。

## 6. 本轮状态

静态检查发现并记录了上述阻塞；未运行编译、单元测试、临时参考测试、sanitizer、性能测试或验收。
未修改 include/src/tests/CMake。已有昨天的参考验证报告只证明当时提取片段，不覆盖今天维护者写入的实现。
待用户确认第3节的推进顺序后，按约定进入下一阶段。
