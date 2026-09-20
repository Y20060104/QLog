# ADR-012：V1 多 Appender、处理时过滤与独立时区

> 2026-09-16 当前补充：[ADR-015](./ADR-015-v1-backend-control-and-output.md) 已确定单管理线程无锁邮箱、Backend 独占、兼容复用与文件失败状态机。本文“管理锁”和“I4 前待议故障策略”均由新 ADR 覆盖；多目标过滤/Console默认合同保留。

> 2026-09-15 覆盖说明：以 [ADR-013](./ADR-013-v1-automatic-producer-context.md) 为当前 Producer 入口合同。
> 以下涉及显式 bind/public Handle、禁止 TLS、注册 mutex、owner vector 的历史条款不再生效。
> Appender 管理锁也不再作为下一步实施方案；无锁配置交接的线程数/队列/API 尚待 I3 商榷，未随自动入口一并确认。
> 多目标、独立过滤、Console 默认输出和释放 Frame 后 I/O 等未冲突合同仍有效。当前步骤见 [主指南](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md)。


- 状态：用户已确认对齐 BQLog；积压记录按后台处理时配置决定输出。
- 日期：2026-09-13。
- 取代 ADR-011/旧指南中的独立 Logger level setter，以及把一个 Logger 等同于一个输出目标的理解。
- 实施入口：[I2 指南 B](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md#i2-filter)。

## 1. 对齐范围

一个 Logger 拥有多个 Appender。一条日志在所属 Channel 入队一次，Backend 解码一次，
检查当前 Logger category，再对每个 Appender 的 enable/levels/category 独立判定，允许零到多个输出。
不同 Appender 不需要对应不同 Logger。只有需要独立 Logger category、Channel/生命周期时才另建 Logger。
Appender 是路由、过滤和输出配置单位；Sink 是它使用的具体输出机制。
V1 继续实现 Console/TextFile 类型，不因本次对齐增加 raw/compressed、JSON parser 或任意布局语言。

配置 reset 可以新增、删除、替换 Appender；不能以“列表创建时固定”作为 V1 的最终合同。
稳定名字用于匹配目标；同名且类型兼容可复用实例，变更类型需要替换，缺失名字对应移除。
重置必须在后台无借用 Frame 的边界应用；先处理旧目标缓存/退出，再回收旧对象。
输入校验在变更前完成。不要声称 BQLog reset 是全资源事务或能回滚已经发生的文件 I/O。
I4 开始文件重置实现前，必须另行讨论并冻结打开/flush/close 失败后的返回结果、目标保留策略与重试边界；
本次仅冻结可动态变更和安全对象生命周期，不以含糊的 bool 或吞错实现补齐未决故障策略。

## 2. 两层过滤

Producer 使用所有配置中的 Appender level 位图 OR，加 Logger category；OR 不排除禁用目标，
不合并 Appender category。这样保持 BQLog 的粗过滤行为，热路径不随 Appender 数量增长。
运行中更新 Appender levels 或列表后重新计算派生位图；没有独立可覆写的 Logger level 配置。
过滤原子保持独立 relaxed，允许混合新旧值；Backend 列表和 Appender 普通配置采用管理锁保护。
一条记录的分发期间不替换目标列表。Logger category 仍是独立原子，不承诺全配置事务。
已入队记录使用处理时配置；新 Appender 可收到旧积压，删除的目标不再收到尚未处理的记录。
accepted 表示入队成功，不保证任意一个目标写入，更不保证 durable。

## 3. Text 输出和资源边界

各 Appender 可独立设置时区及该输出类型支持的配置。BQLog 的 Text layout 是实现定义的布局，
本次对齐不等于增加任意 pattern 字符串。QLog 既有完整行合同仍适用，但时区不再是 Logger 全局唯一值。
先共享一次 Record decode 和格式解析计划；按目标时区生成完整行，顺序复用 Backend scratch。
只有完整行相关配置等价时才可能复用最终字节；V1 正确性不依赖这种优化。
每个 Text Appender 拥有独立 batch，完整复制输出后才能复用 scratch。
所有目标使用完 Ring 借用数据后只 release 一次；Sink 不得保存 Ring/scratch 指针。
实际慢 I/O 在释放 Frame 后进行；开始读取前为各目标确保一条最大完整行的 batch 空间，必要时先 flush。
单目标格式化/接收失败不提前跳过其余目标；共享 Backend 的慢 I/O 仍会影响其他目标，不承诺隔离吞吐。
增加目标会增加后台判定、格式化/复制和 I/O 成本，不能宣称多目标输出免费。

## 4. 数量统计

沿用 Producer calls/attempted/accepted 公式；排空后 accepted=processed，processed 每 Record 只加一次。
Backend 的记录分类和投递分类分开：

```text
processed = decode_failed + no_destination + dispatched
selected_deliveries = delivery_accepted + delivery_failed
```

dispatched 表示 decode 成功且至少一个目标符合过滤，包括所有投递尝试最终失败的情况。
Logger category 关闭、目标均拒绝归 no_destination；用户空配置先规范化为默认 Console。decode_failed 不产生 selected_deliveries。
delivery_accepted 指 Console 完整行进入自有缓冲或 Text 完整行进入自有 batch；格式化/批接收失败归 delivery_failed。
后续批 I/O 错误按批/字节另报，不能把缓存接收成功直接当作文件成功或 durable。
两个目标接收同一 Record 时 processed=1、selected_deliveries=2。
目标删除前把所需诊断汇入 Logger 累计值，避免退休目标导致统计消失。
Debug 内部诊断和 Release 外部验收规则不变；不新增 public live statistics API。

## 5. 分期与待议内容

I2：完整 FilterState、配置值校验/位图合并、更新权限、Channel 稳定借用和 Producer 写入；
可用独立配置模型验收，不宣称已有真实 Sink reset。
I3：Backend 管理锁边界、多 Console Appender 分发、处理时过滤、添加/移除与排空验收。
I4：多个 TextFile Appender、独立时区/batch、文件重置资源及故障合同、fanout benchmark。
文件资源失败策略在 I4 动手前讨论；本次不冻结未讨论的事务/重试承诺。

## 6. 来源与证据边界

BQLog 本地参考提交 60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9：
`src/bq_log/log/log_imp.cpp` reset_config:218、log:435、refresh_merged_log_level_bitmap:478；
`src/bq_log/log/appender/appender_base.cpp` log:79、set_basic_configs:94；
`src/bq_log/log/appender/appender_file_text.cpp` log_impl:17。
这些函数分别支持动态列表、两层过滤、合并位图和逐目标时区布局的结论。
以上是行为参考；QLog 的 C++ 原子/管理锁、SPSC 和先释放再 I/O 合同按自身设计实现。


## 7. 2026-09-15 追加：配置值与 Appender 抽象基类分离

用户已确认：完整配置类型为 AppenderConfig，其 filter 成员为 FilterConfig（levels/category 开关）；
name/type/enabled 和类型专用配置属于 AppenderConfig。Config 是拥有型数据，不继承 Appender、不持有运行资源。
Appender 采用抽象多态基类，具备虚析构、公共非虚过滤/分发入口及受保护纯虚输出扩展点；
ConsoleAppender/TextFileAppender 提供运行行为，Logger 通过 unique_ptr<Appender> 集合独占持有，Backend 受管理锁保护地借用。
公共配置与当前生效状态须有单一来源，Producer 仅读取派生 FilterState；虚派发不进入 Producer 热路径。
参考 BQLog appender_base 的虚析构、log→log_impl 与 log_imp 的 unique_ptr 列表，详细函数职责见
[I2 指南 D15](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md#i2-appender-runtime)。
QLog 仍要求目标先接收/复制、统一释放 Frame 后慢 I/O；I2 配置模型、I3 抽象基类与 Console、I4 TextFile 分期不变。
文件 init/reset/flush/close 失败语义仍待 I4 前确认，本追加不冻结未讨论的资源事务或失败恢复行为。


### 2026-09-15 最新补充：Console 默认目标与实施分期

用户明确不需要 NullAppender；生产目标采用 ConsoleAppender / TextFileAppender。
本轮将“没有就走 console”解释为创建或整表 reset 的输入 Appender 列表为空时，规范化为一个默认 Console 配置；
显式配置非空时仅使用该列表。disabled、过滤拒绝、文件故障都不自动添加 Console，也不绕过 Logger category。
默认 Console 配置与具体修正顺序见 [主指南 N](./MILESTONE2_I2_HANDS_ON_GUIDE_CHS.md#i2-current-next)。
纯 OR helper 的空序列结果仍为 0；Logger 应先规范化再合并，因此默认 Logger 不是零位图。
需要静默时可通过显式目标 filter.levels=0 或关闭 Logger category 表达，无需 Null 输出类型。

I2 完成 Console/TextFile 配置模型、默认化、Producer/Channel；不启动控制台输出。
I3 完成 Appender 抽象基类、真实 Console、Backend 和排空；Console 所需的基础完整行格式化及自有缓冲必须前移到 I3，
不能继续按原 Null 路径仅计数，也不能保留“全部文本格式化到 I4 才做”的旧依赖。
I4 接入 TextFile、文件生命周期/失败合同、多目标独立配置与格式缓存的后续优化；共用基础格式化不再重复实现。
Console 接收完整行入缓冲与实际终端写入成功分开记录；慢 I/O 均在释放 Frame 后，测试需捕获输出字节而非使用真实终端测速。
I2 test consumer 可以无输出地核对 Record，但它只是测试设施，不能注册成一个生产 NullAppender。


## 2026-09-16 R3（替换修订）：平铺配置 + 枚举选择 + 运行期继承

用户最终确认采用平铺 AppenderConfig：name/type/enabled/filter/text/console/file，替换前次 variant 配置组合方案。
保留 AppenderType::Console/TextFile；不引入 AppenderCommonConfig/AppenderTargetConfig 或动态 property_value 树。配置值不使用继承、不持有运行资源。
公共字段始终校验；type 为 Console 时仅校验并使用 console，忽略 file；type 为 TextFile 时仅校验并使用 file，忽略 console；非法 type 在准备阶段拒绝。
未选中字段允许保留配置值，不产生资源，也不影响创建或兼容判断。类型与专用字段的有效组合由校验和解析规则保证。
Appender 保留抽象虚基类、虚析构、公共非虚控制入口和受保护虚输出扩展点；ConsoleAppender/TextFileAppender 继承 Appender，BackendSession 以 vector<unique_ptr<Appender>> 独占持有。FileAppenderBase 仅在复用文件行为需要时引入。
工厂在冷路径按 type 创建派生运行对象；reset 按 name 匹配，比较 type、text.batch_bytes 和对应 file.path/console.stream 决定兼容复用；未选中字段不参与比较。过滤/时区/周期等兼容更新沿用原有规则。
空列表规范化为 name="console"、type=AppenderType::Console，其余字段使用默认值；等级合并读取 filter.levels，disabled 仍参与。
完整类型、示例及实施步骤见 [V1收尾指南§1.1](./V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md#11-appenderconfig)。生产字段补齐和调用点修正仍由维护者实施。本次仅替换文档，不构建、不测试。
