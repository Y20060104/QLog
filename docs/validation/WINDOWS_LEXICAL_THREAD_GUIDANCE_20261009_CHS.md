# Windows词法路径修正与线程基础指导验证

日期：2026-10-09。本次任务是提供后续完整实现指导。

## 实际源码与唯一生产修改

QLog feat/spsc-ring-opt@f24f3afcd8e9418ea48ad5a6b05680c622008145；BQLog main@78cbfbef4f87e7558335f0c04bf6dd6894b46451。两树均有既存草稿/本地修改，BQLog相关参考文件相对HEAD无差异，不能称整树干净。

用户新增公共get_lexically_path声明和完整Windows定义；POSIX只有is_absolute，尚无词法定义。Windows cpp:47误以!result.empty()判断片段栈。reserve后result的size仍0，第一循环期间一直空，故每个..都压入栈而不抵消。当前短路使back不执行，不把它误报成已发生的空栈访问。

修改前真实函数复现a/../../b输出a/../../b；对齐参考预期../b。助手仅纠正这一条件为!result_split.empty()，保留函数其余步骤与CR，没有静默改变已确认的UNC/首反斜杠/重复斜杠等参考边界。

149个include/src/tests与根CMake文件快照中，仅src/platform/windows/file_path.cpp一行条件改变；148个无关原文件SHA-256相同，所有该文件CR行保持相同。根库仍每平台16cpp，没有补POSIX或线程生产定义，没有新常驻测试。

## 实际路径验证

- 修改前严格编译通过但定向行为失败；编译并不能发现错误容器条件。
- 修正后的实际Windows cpp、string_utils、UTF核心在Linux GCC Debug/Release严格编译链接，各1071路径场景通过。
- 原生Windows根qlog静态库及独立调用方Debug/Release各1071同组通过；14独立预期、33边界、1024生成，逐项与当前BQLog函数摘录和真实string/array类型比较。
- 实际qlog两配置源列表均16cpp；QLOG_DEBUG在Debug库与调用方均定义、Release均不定义，PUBLIC传播核对通过。

Windows严格构建使用/W4 /WX /permissive-，审查命令增加/utf-8 /EHsc并排除已有C4324对齐提示；参考头以SYSTEM include处理，根生产警告配置未改。不称无例外/WX通过，不计完整文件系统/安全规范化验收；本轮未跑常驻CTest、sanitizer或完整BQLog双库。

## 后续指导候选

独立四项当前线程操作get_current_thread_id/yield/cpu_relax/sleep对应BQLog thread静态方法；公共头和POSIX/Windows cpp全部代码写入指导和临时目录，未写生产。

Linux GCC与原生Windows MSVC各Debug/Release编译链接及12项身份检查/四个操作调用通过。POSIX原生ID来自pthread_self，不替换gettid；TLS缓存与系统调用对应参考。WindowsSleepEx保持缩窄和alertable规则，POSIX保持EINTR剩余请求规则，不加新回退/截断/异常协议。未验ARM/Apple/BSD、EINTR注入/APC、超大睡眠、锁、完整并发及性能。

## CR回答

split使用等号是初始化返回的vector，保存切分后的原始非空段；NRVO或移动避免必须逐项深拷贝。result_split是处理后的栈，result是最终拼接文本。pop_back的条件不是字符串不同就弹，而是..抵消一个已入栈且不为..的末段；此前也是..则继续保留两个父目录移动。pop_back结束最后string元素生命期，但通常保留vector容量。

证据目录D:\qq344\Documents\qlog-thread-foundation-guidance-20261009-6c0e2f，包含production-before/after、scope-verification、reference、修改前复现、实际路径编译运行日志、两系统线程候选及原生compile_commands。

下一组完整指导：[线程平台基础](../decisions/PLATFORM_THREAD_OPS_IMPLEMENTATION_GUIDE_20261009_CHS.md)。整数越界double/原字符串继续仅记录暂不选定；用户严格对齐BQLog的路径决定继续有效。
