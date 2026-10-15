# GroupList 实际接库与 OversizeBuffer 接续决定（2026-10-14）

> **2026-10-14 用户填写后实际完成（最新）：** Oversize两文件已填、无需算法修正，仅接cpp，析构CR保留；最终四配置36/37库与调用方链接、未运行/CTest。下一组[LogBuffer声明/TLS基础](LOG_BUFFER_DECLARATIONS_ALIGNMENT_20261014_CHS.md)同组原shared/scoped/launder，新生产由用户写。Atomic入口已同内容移platform/atomic/atomic.hpp。下方未填进度为前次快照。

## 当前源码与修正范围

QLog 权威 /home/qq344/QLog，feat/spsc-ring-opt@a1ba6b4d45a25d60242327b68894c23d56c027e0；BQLog 固定提交78cbfbef4f87e7558335f0c04bf6dd6894b46451。两树已有草稿/修改保留，不能称整树干净。采用OversizeBuffer两文件及核对GroupList、MemoryPool、LogBuffer头/cpp相对参考提交无差异。

用户已填写MemoryPool、GroupList、RW锁及四个scoped形式、对齐对象工具、两平台原毫秒时钟和原警告宏。助手仅将MemoryPool前置声明的`typename T.bool Align`修为`typename T, bool Align`，保留所有CR；其余已写算法无必要修正。根CMake加入实际group_list.cpp及按平台二选一的time_ops.cpp；模板头不建空cpp，PUBLIC Debug宏规则不变。

205原生产/测试/CMake文件保护核对，助手主动只改CMakeLists.txt和memory_pool.hpp。末次检查又观察到group_list.cpp原config CR的注释空格同步保存，已保留，函数代码不变；其余202文件SHA-256不变。最新保存源码再次增量编译链接四配置通过，原完整日志保留before-source-format。所有CR原文保留。本轮不运行程序或CTest，不据编译声称锁并发、组退休、存储/恢复或性能验收。

## OversizeBuffer 对齐决定

下一模块采用原oversize_buffer、normal_buffer继承、HEAD_SIZE=128、alignas(8) siso_buffer_obj_及原get_misc_data/proxy/getter/私有初始化接口。已有QLog siso_ring_buffer、LogBuffer句柄、MemoryMapBufferState及std::string按已确定命名适配；不迁回旧TimeZone/Layout/Logger接口。

普通构造、析构、容量计算、恢复判断及初始化在cpp，原模板/forceinline薄转发留头。构造的is_valid检查和析构的is_valid检查就在所选参考提交中，保留而不误称新增防御。failed且base有效时使用堆SISO(false)；new_created使用SISO(true)并清misc；use_existed先由SISO尝试恢复，成功保留misc，失败按原清前128字节。oversize恢复判断只是查询SISO状态，不自建另一套扫描。

对象字节区保存普通SISO控制对象；NormalBuffer拥有的外部内存先128 misc，再SPSC Head及blocks。SISO逻辑长度采用calculator减128，不使用页扩展后的NormalBuffer::size。当前SPSC Head512，总逻辑长度roundup(required)+768；真实块容量仍以现有SPSC算法/getter为准，不推定任意极小请求也正好等于roundup(required)。

get_misc_data只保留原sizeof断言和直接T*访问，不额外添加类型/对齐断言或包装。它不执行T构造/析构，调用方仍须满足对象访问前提；无效base跳过SISO构造，调用方须按原is_valid判断后使用proxy。原OOM/typed metadata/映射及SISO恢复边界保留，不称任意损坏文件可安全恢复。基类默认不删映射文件，显式删除策略留LogBuffer。Oversize本身仍SISO；version/TLS归属、父运输context、锁和回收在LogBuffer建立。

## 主线依赖更正

固定提交git grep表明：生产src内对miso_linked_list的使用只有其定义及log_buffer.h:25的include，LogBuffer没有成员或函数实例化它；实际实例化在test/cpp/test_log_buffer.h。此前“必须先迁MISO链表再进LogBuffer”的说明不成立。

因此当前顺序是已接库MemoryPool/GroupList → 用户填写OversizeBuffer → 审查并接真实cpp → 直接LogBuffer结构/context/TLS/LP-HP及生产者/消费者/恢复流程。适配LogBuffer时省略原未使用的miso_linked_list include；该容器可在迁原相关测试或独立学习时再补，不作为生产主链阻塞项。原scoped_log_buffer_handle在LogBuffer组按实际defs补入，Manager/Worker/Appender后置。

## 证据

实际GNU/Linux及原生MSVC x64 Debug/Release整库35/36cpp、BlockList/MISO/GroupList各两TU及MemoryPool两种节点模板公开调用方严格编译链接通过。完整Oversize候选仅仓库外增加一个cpp，36/37cpp及两TU调用方四配置严格编译链接通过；新生产未代写。所有TU PUBLIC宏核对，仅Debug定义QLOG_DEBUG，其他不定义。GNU -Werror；MSVC /WX，仅外部已有/wd4324、/wd4146。见[验证](../validation/GROUP_LIST_OVERSIZE_BUILD_20261014_CHS.md)。完整实现教学在对话，不复制进本记录。
