# 原 HashMap 行为、源码对照与 MISO 写侧候选验证

日期：2026-10-13。QLog feat/spsc-ring-opt@a1ba6b4d45a25d60242327b68894c23d56c027e0，加本轮未提交修改；参考78cbfbef4f87e7558335f0c04bf6dd6894b46451。BQ整树有本地修改；HashMap相关采用头文件无HEAD差异，MISO用提交快照。

## 实际源码

补全type_traits、HashMap完整声明/定义，修正已写type_tools的bool条件别名、integer拼写和漏掉的tagged new[]，删除impl的pragma once以允许第二遍生成；正确allocator、入口、平台宏与MISO草稿保持。原双版本、字段、默认参数/断言、原分配/扩容/对象生命周期规则保留。模板不建空cpp，根CMake源列表31/32不变。

新增tests/hash_map_test.cpp和3个CTest分组：basic 424检查；collision 47868检查；lifetime 32检查；每配置合计48324。两容器版本、常量散列碰撞和扩桶后分组拆分、独立std::map内容模型、删除/空闲节点复用、clear/reset、指针/浮点/成员hash_code键、const/普通iterator、复制/移动、源对象复用及非平凡value/allocator析构覆盖。没有更改生产断言或增加防御协议。

## 四配置结果

Linux/GCC及原生MSVC x64 Debug/Release实际库严格构建、两翻译单元HashMap公开调用方和MISO初始化调用方链接通过。项目常驻15/15，外部固定提交BQ头对照3/3，组合CTest各18/18。48324检查的结果与公开状态摘要四配置均与独立BQ参考完全一致。PUBLIC QLOG_DEBUG仅Debug，库与公开调用方核对。

MSVC外部审查沿用C4324/C4146排除，根警告不改；原BQ完整type_tools解析需系统intrinsics声明，外部参考target使用/FIintrin.h，BQ源码和QLog生产头未为此改写。第一次外部构建缺build_type等参考头，已从同一提交补齐递归闭包；属于参考工程依赖遗漏，不是QLog缺函数。

Linux standalone实际HashMap三个分组ASan+UBSan通过，检查数与行为结果相同。这里只说明该语料的运行结果，不证明全部模板类型、对象生命周期可移植性、OOM、异常、自赋值或失效iterator合同。

## MISO后续指导候选

原TLS宏与三个缓存辅助类型、alloc_write_chunk和commit_write_chunk仅在仓库外完成。实际库替换一份MISO cpp并覆盖宏头，GCC/MSVC x64 Debug/Release严格编译和双翻译单元调用方链接四配置通过，源数31/32；PUBLIC Debug宏核对。未执行新写侧程序或CTest，不计TLS退出/并发/恢复/性能验收。实际生产MISO cpp/头及平台宏本轮未改，函数由用户填写。

证据：D:/qq344/Documents/qlog-hash-implementation-miso-write-guidance-20261013-164640；validation-matrix.json、hash-differential-linux/native.json、hash-sanitizer三组日志及guide四配置日志。
