# LogBuffer写侧实际审查与完整消费者决定（2026-10-14）

> **2026-10-14 本轮实际消费者收口（最新）：** 用户已填15个cpp及2个inline消费者定义，与上轮候选逐函数一致，无需修正；实际GNU/原生MSVC Debug/Release各37/38库及两TU严格编译链接，UNIT仅GNU两配置语法通过。209生产/测试/CMake不变，CR保留；未运行调用方/CTest。下一组补Worker实际所需thread/condition_variable，见[决定](THREAD_WORKER_DEPENDENCIES_ALIGNMENT_20261014_CHS.md)与[验证](../validation/LOGBUFFER_CONSUMER_THREAD_BUILD_20261014_CHS.md)。下方为此前实际仅写侧/候选消费者的历史快照。

权威/home/qq344/QLog，feat/spsc-ring-opt@a1ba6b4d45a25d60242327b68894c23d56c027e0；参考/home/qq344/BqLog@78cbfbef4f87e7558335f0c04bf6dd6894b46451。两树有本地修改。采用的log_buffer.h/cpp/defs、TLS宏、array_impl相对固定提交无差异，未使用MISO本地草稿。

用户已填写构造/恢复/HP分配/Oversize及公开写侧九函数。逐函数去注释/格式并合并等价相邻字面量后与上一轮已验证候选一致；没有需要修正的已写算法/签名。本轮生产/include/src/tests/CMake 209文件SHA-256全部不变，原CR保留，CMake已注册不用重复接入。实际主LogBuffer构造/析构与写路径可编译链接，实际读侧仍只有声明。

本组新生产仍由用户填写，完整指导留对话；仅仓库外候选新增13常规cpp成员、Debug线程检查、UNIT查询及头内两个forceinline读归还热路径，复用已有四block helper、全局线程检查counter、时间、文件、Group/Block/MISO/SISO/Oversize依赖，不加Manager/拥有型包装或空返回替身。其他普通成员限定log_buffer::放cpp，原两inline按参考放类后头文件。

验证区分valid/version_pending/version_invalid/seq_pending/seq_invalid。当前代际真实TLS读取expected seq，历史代际只查恢复地图、不解引用旧地址；valid运输完成才deregister。当前TLS finished则aligned_delete；历史finished仅同进程模拟恢复的UNIT分支删除。LP pending用discard保持项，invalid用return消费；结束marker内部消费，不返回正文。Oversize成功归还只推进一次seq，并归还子和父，不能分别deregister两份相同seq。

HP保持原batch缓存：逐条next/verify，最后一条才return_batch发布读游标；active空块不退休，removed且已处理的块才deregister/recycle。先取next，再处理旧块，保留last_block/last_group与used/stage链顺序。组无活跃块时按原前驱/从head加write lock复查，再原delete_and_unlock；空洞计数触发后面块need_reallocate，表示迁移到前面空闲块以整理组，不是改变块内存尺寸。

原遍历末标记与五状态保留；当前位置会被移除时停用末标记，当前代际seq_pending也停用以继续查找。旧版本完整遍历后清其map并推进代际；当前完整遍历才返回底层read_an_empty_chunk，失败/空handle仍配对return。无额外扫描预算、有界重试或公平机制，不保证read调用始终有界。

实际参考消费者Oversize使用cpp线程局部槽：QLOG_TLS_NON_POD(LogBufferReadHandle,rt_oversize_parent_handle_)与QLOG_TLS void* rt_oversize_target_buffer_。头中原有同名current_reading字段此版本未用，保持声明/布局，不擅自切换为实例成员；函数原get()/raw void*及归还后不清target保留。缓存是消费线程共享槽，跨Buffer交错未归还的Oversize读会覆盖槽；不能声称支持此用法。原sticky target会让回收跳过最近目标，不能静默清空。

std::vector既有适配保持BQ array::erase_replace语义：index范围断言来自array_impl.h:535（不是新防御），非末项将back移动赋给index，然后pop_back；保留O(1)及末项替换顺序，不用erase移动全尾部、不建新helper类。读取路径数组read锁；GC保持外层try read/try write和对象try write，先排除TLS当前目标、检查内部项invalid才消费、valid/pending保留；确认empty后销毁对象再删除映射文件。

新引用边界明确：恢复map缺项仅Debug断言，Release原iter解引用不新增fallback（参考cpp517-521）；Oversize版本比较仍直接小于而非另建全回绕合同（547）；GC初始is_empty在数组锁外（1018），last_used普通值且相关访问未建立独占同步（1029），与并发插入/更新存在数据竞争风险，严格对齐不擅改；原no_lock组链、普通need_reallocate、raw/typed访问、原恢复损坏尺寸循环/重开misc和hpMax同步边界继续保留。静态来源/影响记录，不称安全或并发/损坏恢复已通过。

本轮实际写侧及完整消费者候选GNU/Linux与原生MSVC x64 Debug/Release各37/38cpp库与两TU严格编译链接，PUBLIC QLOG_DEBUG/NDEBUG逐TU核对；候选一致覆盖头与库，单次替换log_buffer.cpp，真实构造及read/return、scoped<log_buffer>两分支均引用，无替身。额外UNIT仅GNU两配置语法通过。未运行生成调用方或CTest，不计行为、跨路顺序、TLS/Buffer退出交错、并发、恢复、存储故障或性能。详见[验证](../validation/LOGBUFFER_PRODUCER_CONSUMER_BUILD_20261014_CHS.md)。

填写完整消费者后先实际审查/编译收口，再核对运行态/Worker/Appender依赖；当前不提前实现这些模块，也不恢复旧Logger/codec/PreparedRecord接口。
