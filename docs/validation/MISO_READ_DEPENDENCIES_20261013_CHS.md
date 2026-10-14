# MISO 读侧 Debug 依赖实际收口与指导验证

日期：2026-10-13。QLog 权威目录 /home/qq344/QLog，feat/spsc-ring-opt@a1ba6b4d45a25d60242327b68894c23d56c027e0。BQLog 采用提交78cbfbef4f87e7558335f0c04bf6dd6894b46451；整树仍有本地修改，MISO 使用提交快照，保留128字节布局和完整Debug，不采用本地删检查草稿。

## 授权与实际范围

用户选择助手先对齐依赖，再提供读侧指导。迁入原qlog::log_global_vars的Debug子集及scoped_thread_check_disable：原global_vars_base<log_global_vars, common_global_vars>继承、private atomic<int32_t>初值0、relaxed查询/加/减、friend和嵌套规则保留；Release无该字段，查询false且guard无操作。原类的Manager/Appender、布局表、构造逻辑及退出强制flush留待对应模块，当前构造default及空partial仅服务已有Debug子集，不代表原日志全局态/退出验收。

SISO/MISO查询共同采用check_thread_ && log_global_vars::get().is_thread_check_enabled()。src/global/log_vars.cpp已接根CMake。助手修正已写MISO的TLS赋值缺=、QLLOG_DEBUG拼写和fetch_add_release误用，按原fetch_add_relaxed预留；原CR保留，五个读侧函数生产定义未写。

## 实际构建及回归

| 配置 | 实际库cpp | 常驻CTest | 外部BQ参考 | 依赖调用方 | 读侧候选 |
| --- | --- | --- | --- | --- | --- |
| GNU Debug | 32 | 20/20 | 4/4 | 两TU严格链接 | 三TU严格链接，未运行 |
| GNU Release | 32 | 20/20 | 4/4 | 两TU严格链接 | 三TU严格链接，未运行 |
| MSVC x64 Debug | 33 | 20/20 | 4/4 | 两TU严格链接 | 三TU严格链接，未运行 |
| MSVC x64 Release | 33 | 20/20 | 4/4 | 两TU严格链接 | 三TU严格链接，未运行 |

实际源码构建，不使用上一轮冻结MISO副本。GNU -Werror；MSVC /WX仅沿用既有外部/wd4324 /wd4146，不改变生产警告。库、两TU依赖调用方及三TU读侧候选的PUBLIC QLOG_DEBUG已由compile_commands核对：Debug定义，Release不定义。实际依赖调用方包含MISO初始化/alloc/commit/getter，但只编译链接、未运行。

新增qlog.buffer.thread_check_policy每配置14项检查：原全局初值、嵌套guard、内层退出保留外层禁用、最后退出恢复、异常展开、其他线程观察同一全局对象/禁用状态、SISO查询及实例开关。SISO构造仅作为查询夹具，无生产读写测试。第一次夹具使用普通vector不足128字节对齐，触发现有断言；已改为平台aligned_alloc/aligned_free，并保存首次失败日志。没有放宽Ring要求。四配置合计各24/24CTest。

## 读侧指导候选与明确保留的边界

read_chunk、read_an_empty_chunk、discard_read_chunk、return_read_chunk、data_traverse五个完整定义只在仓库外候选提供，类型/namespace/thread_ops按QLog既有接口适配；对照原提交的语句顺序、断言、内存序和N/4批量释放。附两个公共头调用方，共三TU，四配置严格编译链接通过，未执行。

用户明确两次确认：“保留BQLog原顺序，明确记录边界”“严格对齐BQLog顺序不做改动”。因此read_chunk/data_traverse保持先acquire读取status，再立即读取block_count，之后switch；data_traverse的unused分支仍先置finished，随后执行原尾部游标计算。参考堆初始化只设status=unused，不保证块数字段初始化，未提交头也可能被生产者修改；保留这一未初始化/并发元数据读取边界，不声称消除UB或完成可移植并发证明。延后块数读取到used/invalid分支属于另一方案，用户未采用。

保留原直接status=unused写入、get_block_num的原字节区访问、批量归还、load_raw终止检查、指针断言及损坏块数失败边界；没有增加额外assert、默认参数、generation、Manager或拥有包装。Debug禁用guard只控制线程ID检查，不关闭读句柄配对断言，也不充当互斥锁。

本轮未运行读侧/新MISO调用方、sanitizer、MISO并发/恢复、全局首次初始化竞争/退出flush或性能。20项常驻结果不接受完整Ring/Record行为，旧47项测试不计入。

194个原生产/测试/CMake指纹核对，七个命名修改之外无变化；MISO/SISO原CR保留，读侧五定义仍缺。产物：D:\qq344\Documents\qlog-miso-consumer-guidance-20261013-203539，含before、reference-head、首次实际MISO语法失败/夹具失败日志、实际依赖构建与CTest、读侧完整候选、四配置编译链接日志和validation-matrix.json。详细指导留对话，文件只记决定和证据。未提交或推送。
