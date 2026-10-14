# Oversize接库、Atomic放置与LogBuffer声明/TLS决定（2026-10-14）

QLog权威/home/qq344/QLog，feat/spsc-ring-opt@a1ba6b4d45a25d60242327b68894c23d56c027e0；参考/home/qq344/BqLog@78cbfbef4f87e7558335f0c04bf6dd6894b46451，两树仍有草稿/修改，均保留。

用户已填Oversize，无必要算法修正；助手仅接实际cpp，原析构CR保留。按Atomic头及Spin两处CR，模板入口同内容移动到include/qlog/platform/atomic/atomic.hpp，与BQ目录对应；没有atomic.cpp，不建空源。十个生产/测试include同步（初次中断前已同步两个，恢复后同步八个），207基线一头原字节迁移、11既有文件改（CMake+十引用）、其他195 SHA-256不变。Spin同期扩充CR保留，函数体不改。

本组新生产由用户填写：完整LogBuffer声明、context16/block_misc32/lp misc/destruction mark、Oversize对象、TLS两个结构/缓存、成员/状态/getter；同组原shared_ptr/make_shared、launder、scoped_log_buffer_handle、四block helper、两TLS析构及TLS宏。构造/版本恢复/producer/consumer/Oversize运输本组仅声明，后续补主体，不放空返回替身；read/return原forceinline快路径留消费者组。未实例化主LogBuffer，不称主对象可用。

原成员名称/顺序/default/assert/atomic order、128对齐、指针32/64包装、temprorary拼写、恢复范围生产2/单测5、每组desktop16/mobile2、两个1000ms常量保留。std::string/vector/unique_ptr沿既有适配；TLS原hash_map_inline+外部对齐节点、销毁标记原shared_ptr。未用MISO链表include不恢复，Java/raw/compressed不扩项。

shared追加utility_types.hpp，原计数低32/关闭标记高32；copy fetch_add_seq_cst，use_count/release load_relaxed，两CAS success/fail均seq_cst，原分离分配及显式T析构/aligned_free保留，复用现有atomic/平台aligned/type_tools forward/enum_new_dummy。launder追加type_tools.hpp，原MSVC/builtin/屏障分支及char区转atomic保留。纯C++TLS节点析构原空体，shared成员仍正常析构，非替身。

参考shared对象间==/!=在utility_types.h:438/447引用不存在rhs.ptr，两个独立GNU C++20语法实例化确认失败；指南明确修两处rhs.ptr_，签名和计数算法不改，参考树未写。nullptr不等原模板保留，普通及显式调用已编译；跨特化move赋值未验。线程ID对应现有平铺platform::thread_id；初候选漏映射导致Debug失败后已修两字段，first-candidate-failure保留。格式采用QLog clang-format。

TLS字典key为Buffer id、value为128对齐独立节点；recent id+pointer、expand_rate(4)、先get_info后direct配对保留。TLS退出先复制shared标记再锁；Buffer活时标同TLS/version Oversize结束，HP写finished并release removed，LP原无限重试发布结束context，消费侧以后删节点，TLS只删字典；Buffer已毁则TLS直接aligned_delete节点。局部protector维持锁对象到解锁以后。原持锁无限重试/OOM/无异常回滚/raw及typed访问/全局退出边界保留，不称安全或有界退出。

seq按运输单位（LP记录、HP新block、Oversize父项），历史TLS地址仅恢复key；rt_cache单消费者、WT/RT普通u32+128隔离，不擅改原子或扩张线程安全保证。

Atomic迁移脚本初次误把旧路径CR当引用，rename后中断、实际Debug暂时缺头失败；已限include匹配并同步十处，atomic-relocation-failure留日志。最终实际GNU/原生MSVC四配置36/37库与调用方链接、原Atomic算术仅编译通过；候选37/38库与两TU四配置通过。所有TU PUBLIC宏一致，仅Debug定义；未运行/CTest、未实例化LogBuffer或调用未实现运输，不验并发/顺序/恢复/退出/存储/性能。下一组构造/版本恢复初始化及producer，再consumer/Oversize运输回收，Logger/Manager/Worker/Appender后置。见[验证](../validation/OVERSIZE_LOGBUFFER_DECLARATIONS_BUILD_20261014_CHS.md)。完整教学在对话，文件只记决定。

2026-10-14后续：用户已实际填写本组声明/TLS/工具，误放include内.cpp现原字节移.hpp并接实际cpp；原代码/CR保留，后续追加CR亦保留。实际及完整构造/恢复/写侧候选四配置最新证据见[写侧决定](LOG_BUFFER_PRODUCER_ALIGNMENT_20261014_CHS.md)和[编译记录](../validation/LOGBUFFER_TLS_PRODUCER_BUILD_20261014_CHS.md)。本文件上方“仅候选/待填写”为历史快照，新九函数仍未写生产。
