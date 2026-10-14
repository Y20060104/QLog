# LogBuffer TLS实际与完整写侧候选编译记录（2026-10-14）

| 配置 | 实际TLS/工具库＋两TU | 完整构造/恢复/写侧候选库＋两TU | PUBLIC宏 |
| --- | --- | --- | --- |
| GNU/Linux Debug | 37cpp；编译链接通过 | 37cpp；编译链接通过 | QLOG_DEBUG定义，NDEBUG未定义 |
| GNU/Linux Release | 37cpp；编译链接通过 | 37cpp；编译链接通过 | QLOG_DEBUG未定义，NDEBUG定义 |
| 原生MSVC x64 Debug | 38cpp；编译链接通过 | 38cpp；编译链接通过 | 同Debug |
| 原生MSVC x64 Release | 38cpp；编译链接通过 | 38cpp；编译链接通过 | 同Release |

BUILD_TESTING=OFF，没有执行调用方或CTest。GNU继承库严格警告并-Werror；原生MSVC /WX、/utf-8、/EHsc，只沿既有外部/wd4324 /wd4146，不修改生产警告。八组compile_commands与cpp数/已链接产物/PUBLIC宏逐TU检查；只有Debug定义QLOG_DEBUG，未设为0。所有最终修正后的四配置增量复核通过。

实际使用仓库头/cpp，无外部替换。声明文件从include内.cpp按原字节移动.hpp；实际src/buffer/log_buffer.cpp仅前组四helper/TLS析构及宏，并已接根库。实际调用方覆盖shared完整同T API/两工厂、launder、scoped Oversize read/write、context布局/TLS缓存及析构；未构造实际主LogBuffer或调用未实现运输。

完整写侧候选只在D:/qq344/Documents/qlog-log-buffer-producer-guidance-20261014-01/candidate/src/buffer/log_buffer.cpp，替换库源列表中原TLS cpp一次，头及其他库源全部真实仓库，没有混合布局或重复定义。候选两TU创建真实LogBuffer并引用普通、超大以及need_recovery构造和alloc/commit，真实依赖均链接，未运行；消费者尚未提供，未实例化scoped_log_buffer_handle<log_buffer>以免其析构引用缺失read/return。

额外GNU QLOG_UNIT_TEST Debug/Release仅-fsyntax-only。首个Release检查在BlockList三个只被assert引用的局部索引上-Werror失败；助手仅[[maybe_unused]]三属性，最终两配置通过，旧失败与最终日志分开。未检查UNIT的完整链接/行为或Native UNIT。不将语法编译称测试运行。

209基线一声明移动；助手改CMake及BlockList三属性；用户同期头/cpp新增CR保留，去注释代码与原一致；其余205生产/测试/CMake文件SHA-256不变。before/final清单、protection-summary与concurrent-user-cr.diff保存证据。参考六采用文件相对固定提交无差异，整树仍有本地修改，未写参考树。

QLog a1ba6b4d45a25d60242327b68894c23d56c027e0；BQLog78cbfbef4f87e7558335f0c04bf6dd6894b46451。产物D:/qq344/Documents/qlog-log-buffer-producer-guidance-20261014-01含备份/固定参考、候选/调用方/构建项目、首次及最终构建/UNIT语法日志、命令/矩阵、保护与最终Git状态。所有运行测试仍后置；不计TLS退出、并发/跨路顺序、版本恢复、IO/OOM/异常、移动端/32位/其他平台或性能。参考恢复损坏size循环与misc无is_valid等边界仅静态确认，未称安全。见[决定](../decisions/LOG_BUFFER_PRODUCER_ALIGNMENT_20261014_CHS.md)。
