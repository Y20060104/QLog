# ADR-016：旧Ring配置退出，按BQLog block SISO重建

> 最新约定：类名SpscRingBuffer，文件spsc_ring_buffer.hpp/.cpp；QLOG_DEBUG控制调试代码，assert由NDEBUG控制。初始化非法输入采用断言前置条件，无abort或返回0错误协议；性能CR保留到完整日志后的测量。接续模块见[补全指南第0节](./R0_POST_CLEANUP_IMPLEMENTATION_GUIDE_CHS.md)。


> **模块与进度更新（2026-09-23）：** 用户已写utility函数及部分Ring结构草稿；目标buffer/utility分模块，hpp/cpp归属见 [block指南第0节](./R0_BQLOG_BLOCK_RING_GUIDE_CHS.md)。目录迁移已完成，第一、二批已有实现草稿；第三、四批及完整Ring验收尚未完成，不能把新增草稿当作旧文件再次清理。

日期：2026-09-23；用户已要求按BQLog block单位对齐并删除旧配置。

参考SISO采用8字节block、32位block游标、外部内存、共享Buffer结果码与读写句柄、单条return立即release发布，以及显式batch接口。MISO使用缓存行block及独立多生产者协议，不混同二者。

旧SpscRingBufferConfig、max_payload_bytes、字节游标/FrameHeader、packed状态句柄、阈值publish_reclaimed、旧验证开关与依赖的实现/测试/基准退出。相关旧RingADR和指导已删除，不保留为新实现约束。

新入口：[完整Ring实施指南](./R0_BQLOG_BLOCK_RING_GUIDE_CHS.md)；[清理记录](./R0_RING_CONFIG_CLEANUP_20260923_CHS.md)。当前SpscRingBuffer已位于buffer模块，第三、四批尚未完成；源码尚未接入CMake，当前库仍只含版本基础。

内存恢复、大小溢出、原子对象生命周期等参考疑点明确记录，不复制未定义行为。不得把构建smoke通过宣称为block Ring对齐通过。
