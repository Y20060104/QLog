# NormalBuffer 对齐决定与 MemoryMap 实际收口

> **2026-10-13 实际收口（本段最新）：** 用户已经填写本模块及必要枚举/常量。助手修正已写cpp四处抄写/名称问题并注册实际源；183生产/测试/CMake仅cpp与CMake改变，其余181哈希不变，原CR均保留。实际GNU/Linux和原生MSVC x64 Debug/Release严格构建及双翻译单元公开调用方链接通过，现有CTest各12/12，源数30/31、PUBLIC宏核对；新调用方未运行，不计存储/resize/OOM/恢复/并发/性能。下一组以用户确认的BQ提交版本78cbfbef进入MISO，详见[MISO决定](MISO_ALIGNMENT_20261013_CHS.md)。下方“生产尚未填写”为前轮快照。

日期：2026-10-12。源码和用户最新要求优先；本文件只记录决定和验证边界，完整指导留对话。

## 来源与范围

QLog feat/spsc-ring-opt@a1ba6b4d45a25d60242327b68894c23d56c027e0；BQLog main@78cbfbef4f87e7558335f0c04bf6dd6894b46451。BQ normal_buffer.h/.cpp、log_buffer_defs.h、utility_types.h及MemoryMap五文件相对HEAD无差异；整树仍有MISO/构建等本地修改。正式采用MISO时重新核HEAD与该文件差异。

MemoryMap实际修正和四配置严格构建/链接、现有CTest各12/12见[收口记录](MEMORY_MAP_ALIGNMENT_20261012_CHS.md)。该结果不计存储行为/恢复/并发验收。

## NormalBuffer本组决定

- 保持模块边界：include/qlog/buffer/normal_buffer.hpp与src/buffer/normal_buffer.cpp，namespace qlog::buffer，class normal_buffer；原6字段及其顺序、private/public、原参数/default、virtual析构及所有getter/resize/private create_memory_map均保留。普通定义在cpp，简单getter类内。
- 新枚举create_memory_map_result放现有log_buffer_defs.hpp，failed/use_existed/new_created按原顺序，不复用另有用途的MemoryMapBufferState。
- 原公共BQ_CACHE_LINE_SIZE=128、LOG2=7在include/qlog/utility/utility_types.hpp适配为qlog::utility::QLOG_CACHE_LINE_SIZE及QLOG_CACHE_LINE_SIZE_LOG2。这是必要共享常量，不新建Manager/包装类或辅助函数；不使用现有SISO类内64字节常量替代。
- std::string/empty、std::memcpy、std::min和现有qlog平台/诊断为模块适配；保持原normal_buffer、create_opt、file_size/current_file_size、back_updata等名字与流程。不额外增加assert、默认实参、atomic、复制/移动限制、锁或flush接口。
- auto_create=false仅禁止缺失文件创建；现有文件仍可截短/扩展。new_created/use_existed按对齐目标长度是否相同分类，不等于校验了恢复数据。失败按原关闭/删除后堆回退；析构删除标志只管正常映射析构，不取消失败清理。
- resize按原备份→释放旧映射→对齐/truncate/remap→失败堆回退→复制min前缀→释放备份；堆来源继续用堆，映射来源失败回退后的堆长度也是对齐后的new_size。原成功resize不重算mmap_result_；没有强回滚或地址稳定保证。
- 明示原边界：映射备份分配失败仍memcpy到空指针；resize目标堆分配失败仍设置非零size并可能memcpy到空指针。本轮按用户既有严格BQ决定保留，不称安全OOM或失败保留旧对象。resize不会零初始化新增区；无内部同步。原默认复制会共享裸地址，不能当作独立资源副本使用；不擅加deleted copy/move。
- BQ_UNIT_TEST的test_inject::get_normal_buffer_alloc_fail依赖完整故障注入设施；与此前native open的同一测试设施范围一致，本组未迁移，不引用不存在的占位helper。指导和候选仅对齐原普通构建路径，不声称该测试宏分支已对齐。

## 本轮验证边界与下一步

完整NormalBuffer四文件候选（包含新增枚举/必要常量）只在仓库外准备，未创建新生产定义或提前注册不存在的cpp。实际QLog＋完整候选在Linux/GCC与原生MSVC x64 Debug/Release严格编译及两翻译单元公开调用方链接四配置通过，源数Linux30/Windows31、PUBLIC Debug宏核对。候选没有运行或执行CTest，MSVC外部仅沿用既有C4324/C4146排除，无别名绕过选项；不计NormalBuffer行为、resize、故障、恢复、并发/持久化或性能验收。

证据：D:/qq344/Documents/qlog-memory-map-review-normal-buffer-guidance-20261012-231506。180个原生产/测试/CMake仅MemoryMap4文件变化，其余176个SHA-256不变，全部原注释/CR保留。

用户填写NormalBuffer后同轮审查修正/接CMake，再直接MISO；随后memory_pool/miso_linked_list/block_list/group_list/oversize等真实Buffer依赖，再进入LP/HP/TLS/LogBuffer主链。后置Logger运行态、Appender、Worker；不恢复旧Logger/codec/PreparedRecord。整数越界两方案继续仅记录、暂不选定。
