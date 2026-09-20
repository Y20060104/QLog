# V1 Runtime 实现说明

生产实现集中在 `src/backend_runtime.cpp`，内部 Engine、Node、Attachment、Runtime 不暴露为公共 API。`src/worker_provider.cpp` 懒选择内置 provider；测试仍可在首次 logger 构造前安装 manual provider。

## 所有权和发布

Runtime 持有公共 ConsoleOutputGate 与懒初始化共享 Engine。独立模式的 Attachment 持有专属 Engine；两个模式共用 gate。每个 Engine 在启动线程之前分配 BackendWorkspace，避免运行期为每条记录建立 scratch。

Node 在冷路径分配。attach 先通过 registrations 原子取得发布资格，再 CAS 发布到只增 published 链。published_next 发布后不可改；worker 使用单独 next 链维护活动节点，每轮最多发现 64 个新节点。登记不使用等待 mutex；Runtime mutex 只负责冷创建共享 Engine。

Node 由 Engine 持有到线程 join 后才删除；解绑后 Session/Context 可以释放，稳定登记节点不再指向 Session。共享 logger 高频创建会累积这些小节点，这是活动 ADR 的回收策略，不伪称全局节点即刻释放。

## 调度与唤醒

每轮访问各活动 Session 一次；每次 Session 内按 records_per_channel/bytes_per_channel 配额访问一个 Channel。有积压立即进入下一轮，空闲等待取最早输出 deadline 与 66 ms 兜底。过期 deadline 的无进展等待至少 1 ms，避免空转。默认每目标刷新与文件重试 100 ms。

普通低占用 Producer 仅做本地近似占用比较。半满/full 分支调用 waiting.exchange(false)，仅命中已等待标志时取得等待 mutex 并通知 CV。worker 在同一 mutex 下置 waiting 并检查谓词；置标志之前的通知允许由有限 tick 覆盖。管理、附着和 stop 也通知。格式化和文件 I/O 不在等待 mutex 中执行。

一次 flush 仍按 BQLog 语义持续短写/write EINTR 重试，未增加 flush 字节预算。慢 syscall 可阻塞共享 worker，这不是实时调度器。

## 构造、停止与失败

worker 只在自己的线程屏蔽 SIGPIPE；构造等待线程启动成功。线程创建或信号配置失败会撤回构造，未发布 Session；启动失败后线程 join，后续构造仍可成功。

attach 返回前等待 ready/attached。Session 返回 detach_ready 时，worker 先移除活动链接、清空 Node::session，再最后调用 acknowledge_detached，随后发布节点 released。公共 shutdown 等待 released；独立 Engine 还必须 join，共享 Engine 保持运行。

调度异常退出关闭 registrations 的最高位，拒绝新发布，并通过 CV 等待已取得资格的发布者完成。随后清除剩余 Session 借用并标记相关节点 failed/released。管理侧只有在释放得到确认后才调用 Session 的失败清理。该机制不承诺恢复进程崩溃、terminate、任意内存损坏或永久阻塞 syscall；V1 没有 watchdog 重启。

单槽命令仍通过 release/acquire 状态交接。Attachment 等待 CV 只负责完成通知，不领取命令，也不把配置切换变成互斥锁保护。worker 对 Session 的最后访问与管理侧回收界限分别由 detached/released/join 确认。

## BQLog 参考范围

本地 BQLog commit `60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9`，`src/bq_log/log/log_worker.cpp` 与对应头：async/independent、66 ms 定时等待、低空间通知；输出短写和格式化沿用先前已确认合同。QLog 只屏蔽 worker SIGPIPE，不复制 BQLog 的全部信号屏蔽、信号强制刷新和 watchdog；单槽管理、有界 Channel 配额及明确错误结果是 QLog 的适配。
