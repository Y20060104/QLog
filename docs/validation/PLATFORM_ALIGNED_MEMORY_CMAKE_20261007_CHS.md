# 平台对齐分配实际审查、CMake 与接续记录

日期：2026-10-07。QLog 权威目录 /home/qq344/QLog，分支 feat/spsc-ring-opt，HEAD f24f3afcd8e9418ea48ad5a6b05680c622008145。BQLog /home/qq344/BqLog，HEAD 78cbfbef4f87e7558335f0c04bf6dd6894b46451。BQLog 整树有 MISO 和构建修改，不能称整树干净；本次采用的平台/文件操作、MemoryMap、NormalBuffer 以及路线列出的其他 Buffer/header 文件相对 HEAD 无差异。

## 实际审查与改动

用户新增 include/qlog/platform/aligned_memory.hpp 与 src/platform/aligned_memory.cpp。函数体的指针大小对齐提升、POSIX posix_memalign/free、Windows _aligned_malloc/_aligned_free 与实际参考一致。

审查起点头文件释放声明为 align_free，cpp 定义为 aligned_free；cpp 在 qlog::platfrom，头文件在 qlog::platform。Debug/Release 严格单文件语法检查通过，但实际公开头文件调用方同时编译该 cpp 后，链接报 qlog::platform::aligned_alloc 与 align_free 的 undefined reference。这是独立符号问题，不能由语法通过推断可用。

编辑前哈希保护检测到用户已自行把头文件释放名改为 aligned_free，助手保留这次修改，没有用旧内容覆盖。助手仅修正 cpp 两处 qlog::platfrom 拼写（定义与末尾注释），并给根 CMake 添加实际 src/platform/aligned_memory.cpp；不改分配函数内部算法、签名、其他模块代码或测试，不提交 Git commit。新平台文件无 CR；其他文件/CR 原样保护。

原有 13 个库 cpp 加本组 1 个，现在根 qlog target 共 14 个 cpp。QLOG_DEBUG 保持 PUBLIC、仅 Debug 定义，其他配置不定义；不设为0。公开消费者继承目标宏。

## 实际验证

临时外部工程 add_subdirectory 权威 QLog，以公开头文件调用平台函数，只链接 QLog::qlog，不加入替代平台 cpp 或头文件。GCC C++20，根库及消费者启用 -Werror 与现有 Wall/Wextra/Wpedantic/Wconversion/Wshadow。

- 实际根库/公开消费者 Debug、Release 严格 configure/build/link 通过。
- 外部 CTest Debug、Release 各 13/13：仓库常驻10项（smoke1、Layout/序列化9）+临时配置2+临时平台分配1。仓库常驻测试仍是10，不把临时检查注册成常驻测试。
- 实际分配各85个 Linux定向检查：0/1/4小对齐提升，8/16/64/4096，对非对齐倍数size的分配、完整请求范围可写、配对释放；POSIX非法alignment12返回nullptr。nullptr释放及size0也调用，但不新增size0返回值保证。
- CMake UBSan实际库/公开消费者的分配同组85个检查通过，定向CTest1/1；不是全部仓库测试的UBSan。
- 实际 Buffer配置48检查（含12个实际BQ头文件checksum调用）、过滤61检查在两种配置再次通过；Appender仍只是摘录匹配条件探针，没有新Appender/Logger主链。
- 编译命令核对：实际Ring、平台cpp及公开分配消费者 Debug都有-DQLOG_DEBUG、无-DNDEBUG；Release无QLOG_DEBUG、有-DNDEBUG。
- 验证开始/结束137文件（include/src/tests加根CMake）SHA-256一致。相对审查起点只有平台hpp（用户修正）、平台cpp（助手修正）及根CMake变化。

证据目录：/tmp/qlog-platform-review-20261007-fpmfnmno
生产构建/测试日志：Debug-configure/build/ctest.log，Release-configure/build/ctest.log，UBSan-configure/build/ctest.log。
接口失败日志：pre-fix-Debug-syntax/link.log，pre-fix-Release-syntax/link.log。
宏与源码证据：macro-propagation.json、before-manifest.json、validation-start-manifest.json、validation-end-manifest.json、adopted-reference-manifest.json、adopted-reference.diff。

未计作 Windows/移动端编译运行、NormalBuffer/MISO/恢复/并发或性能验收；旧47项Ring测试不计当前验收；整数越界两方案继续“仅记录、暂不选定”。

## 下一组与候选边界

下一组是平台文件原生类型与已打开句柄操作：get_file_size(handle)、read_file、write_file、seek_file、truncate_file、flush_file、lock_file、unlock_file。完整指导见 [PLATFORM_FILE_IO_IMPLEMENTATION_GUIDE_20261007_CHS.md](../decisions/PLATFORM_FILE_IO_IMPLEMENTATION_GUIDE_20261007_CHS.md)。

没有提前写生产 file_defs.hpp/file_io.hpp/file_io.cpp，没有声明/调用还未实现的 QLog open/close/路径/排他登记 helper。普通句柄不拥有文件；open/close与登记在下组按同一套参考成对补齐，之后FileHandle/FileManager。

候选只在 /tmp/qlog-platform-review-20261007-fpmfnmno/file-io-candidate。Debug、Release严格编译链接、定向CTest各1/1，UBSan同组1/1，各50个检查通过，涉及二进制NUL、EOF/短读、相对seek、truncate不改变游标、扩展零字节、原生锁、错误输出清零、非阻塞pipe部分write再EAGAIN及真实写出前缀。Windows/macOS分支未编译运行；未跑信号EINTR注入、跨进程锁竞争或BQLog双库差分。候选验证不等于新生产模块通过。

## 后续进入前必须复核的来源边界

- BQLog POSIX write_file遇EINTR重试，read_file直接返回errno；低层短读成功返回，不强行补满。
- get_file_size(handle) POSIX把所有非regular类型返回EISDIR，Windows直接GetFileSizeEx。
- platform truncate只调整长度；FileManager::truncate_file再查询长度并seek到EOF。
- lock_file是原生文件范围锁，不是C++线程互斥，也不能替代open_file的进程内排他登记。
- MISO本地header改head alignas(cache line)到alignas(4)，cpp有70行diff、删除多处Debug/几何检查；正式进入前重新核对HEAD与diff，不当官方采用。
- 路径/打开组将单独讨论现有~user拼接、getpwuid失败解引用、getpwnam共享缓冲与exclusive缓存登记寿命；本组不选择新的路径/错误合同。
- Windows打开文件依赖UTF8→UTF16，当前QLog utility仅有反向转换；进入该组前给出实际软件实现及输入边界，SIMD仍后置。

## 验证后的用户注释编辑

在上述137文件验证结束且哈希一致后，最终检查检测到用户编辑了src/record/log_entry_handle.cpp:25：只删除旧说明注释并留下两个行尾空格，没有改变表达式/控制流。助手保留该用户改动，没有覆盖或格式化。HEAD仍为f24f3afc。

为避免把旧SHA当当前SHA，已分别对HEAD源文本与当前源文本在Debug/Release进行同一公开头配置的g++ -E -P预处理；输出完全一致，证明当前新增改动未改变对应C++编译内容。证据record-comment-only-proof.json。此前构建/运行结果仍对应同一生产C++内容；没有因注释变化重复整套测试，也不再声称最终所有137文件字节哈希等于验证开始。

最终清单为final-source-manifest.json。任务相关CMake/AGENTS/docs diff --check通过。整树git diff --check另报告上述用户Record行尾两个空格，未自动改该无语义草稿，不把整树称为格式检查干净。
