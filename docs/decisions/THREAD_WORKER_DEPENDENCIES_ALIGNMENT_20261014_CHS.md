# LogBuffer消费者收口与Worker线程依赖决定（2026-10-14）

权威 /home/qq344/QLog，feat/spsc-ring-opt@a1ba6b4d45a25d60242327b68894c23d56c027e0；参考 /home/qq344/BqLog@78cbfbef4f87e7558335f0c04bf6dd6894b46451。两树有本地修改。LogBuffer、log_imp/log_worker 和本组 thread/condition_variable 文件相对参考提交无差异；MISO仍采用固定提交，不采本地草稿。

用户已填完整LogBuffer消费者。15个cpp定义（含Debug/UNIT）及2个头内读/归还定义与上轮完整候选去注释/格式、合并相邻字面量后全部一致；不需修正。现有CMake已接实际cpp，本轮不重复注册。include/src/tests/CMake 209文件SHA-256全不变，CR及未提交草稿保留。此结论是源码比对及编译链接，不是运输/回收/恢复并发安全验收；上一消费者决定中的边界继续有效。

实际依赖顺序：log_imp.h:122按值持有log_worker；log_worker.h:23继承platform::thread，31持有condition_variable。QLog现有thread_ops只提供静态取时/身份/名称/让出/休眠类服务，未有线程生命周期对象和条件变量；现有mutex已有friend condition_variable及get_platform_handle。先一次补齐这两个直接依赖，再进入新Logger/Manager/Worker/Appender声明与主流程，不恢复旧Logger/Channel/codec/PreparedRecord。

新生产仍由用户填写，完整指导只在对话，验证候选仅仓库外。新增 include/qlog/platform/thread.hpp、condition_variable.hpp；共用 src/platform/thread.cpp 写cancel/is_cancelled及静态服务转发；POSIX/Windows线程对象生命周期和设置名称合入现有各自thread_ops.cpp，复用已有宏探测、prctl探针/Windows API缓存，不另造Manager或线程拥有型包装；两个系统分别新增condition_variable.cpp。原BQ系统薄头仅include公共头，不新增无内容的OS头。

保留原 enum_thread_status 及 pendding_cancel 拼写、thread_attr默认512*1024、thread私有字段顺序/不可复制移动/虚run与on_finished、raw平台指针及malloc+placement构造释放、原默认CAS/显式seq_cst/acquire、原错误/断言/截名>=15规则；std::string/vector及标准类型适配。thread类的五个静态函数调用已验证的现有flat服务，维持一个当前线程ID TLS缓存；WindowsRaiseException固定参数个数只显式转DWORD以满足严格编译，不改值或流程。普通函数放cpp，简单getter与两个CV模板放公共头。

CV保持原谓词循环和epoch毫秒剩余时间算法。POSIX优先MONOTONIC、原REALTIME回退及50ms切片、ETIMEDOUT循环，其他错误返回true；Windows保留普通静态API探测标记、原直接InitializeConditionVariable调用、native或auto-reset event回退、DWORD超时窄化、alertable等待及notify_all旧后端只SetEvent一次。不加默认参数/assert/call_once/谓词完成后重查/饱和算术/强通知保证。

新参考边界明示来源及影响：POSIX thread.cpp:280/339普通thread_id有父子双写，Windows回调和CreateThread输出槽亦不构成普通字段自动同步；状态原子不证明其他字段全安全。start只拒running/detached，原CAS仅接受init/released，不能把中间状态重启称可行；POSIX创建失败保留原状态，Windows转error。析构只释放平台资源，detach只释放系统join资源，均不等待run退出，this须保持活到线程不再访问。condition_variable.h:69-86使用墙上epoch、加减无饱和且底层timeout不重新检查谓词；POSIX ns乘加可回绕，选定MONOTONIC后get_current_ns回退其他时钟会混用；Windows普通静态探测有并发首次调用竞态风险、fallback不能广播全部、DWORD截断。现有Windows名称缓冲释放/版本探测等此前严格对齐边界不静默改动。这里只保留参考并记录，不称其安全或已测。

实际GNU/Linux与原生MSVC Debug/Release各37/38库cpp及两TU消费者严格编译链接；实际UNIT额外GNU两配置仅语法。完整线程/CV候选加真实完整LogBuffer四配置各39/40库cpp与两TU编译链接，逐TU检查PUBLIC QLOG_DEBUG及NDEBUG。未运行任何调用方/CTest，不计线程启动/退出/唤醒/超时/Buffer行为、并发、恢复、退出交错或性能。详见[验证](../validation/LOGBUFFER_CONSUMER_THREAD_BUILD_20261014_CHS.md)。
