> 历史交接已完成：用户随后授权 Codex 补全整个 V1。真实 Runtime 已实现，默认自动启动；当前见 [实现说明](./V1_RUNTIME_IMPLEMENTATION_20260920_CHS.md)。下文“待用户实现/未安装 provider 抛错”仅记录当时交接状态，不再是当前使用方式。

# Worker 线程交接：对接现有 Session

此文只说明用户接下来实现的 worker/runtime。非 worker 代码已实现；不要重写 formatter、Appender、管理邮箱或 shutdown 公共入口。

## 1. 先看准确接口

源文件：`include/qlog/detail/worker_provider.hpp`、`backend_session.hpp`。

```cpp
class WorkerAttachment {
 public:
  virtual ~WorkerAttachment() noexcept = default;
  virtual void notify() noexcept = 0;
  virtual bool failed() const noexcept = 0;
  virtual bool failed_and_released() const noexcept = 0;
  virtual bool wait_for_command(ControlMailbox&) noexcept = 0;
  virtual bool finish_stop() noexcept = 0;
};
class WorkerProvider {
 public:
  virtual ~WorkerProvider() noexcept = default;
  virtual ConsoleOutputGate& console_gate() noexcept = 0;
  virtual std::unique_ptr<WorkerAttachment> attach(BackendSession&, ThreadMode) = 0;
};
void install_worker_provider(WorkerProvider&);
```

建议新增 `worker_wakeup.hpp/.cpp`、`backend_worker.hpp/.cpp`、`backend_runtime.hpp/.cpp`。具体类名可以自行调整，上述桥接接口不需要再改。每个实际 worker 在启动前创建一个 BackendWorkspace，顺序访问其 Session 时复用。禁止多个 worker 同时 service 同一个 Session。

## 2. Provider 与所有权

Runtime 实现 WorkerProvider，持有共用 ConsoleOutputGate、共享 worker、节点登记和等待状态。Runtime 的生命周期必须长于所有 logger 和 attachment。独立模式也返回同一个 Console gate，以免多个线程输出 Console 时互相穿插。

在应用冷启动或 Runtime 的统一初始化函数中调用 `install_worker_provider(runtime)`，然后才创建 AsyncLogger。相同实例重复安装允许，替换实例禁止；目前没有卸载入口。尚未安装时构造明确抛错，不会自动模拟 worker。

attach 的返回值是每个 logger 的管理/通知句柄。共享模式将 Session 节点发布到 Runtime；独立模式建立专属线程。仅当 ready 且 attached 后返回。若分配、线程创建或启动失败，必须先撤回节点、等待线程释放所有 Session 引用，独立线程 join 完，再抛异常。不能把借用已销毁的 Session 留在共享队列。

## 3. 先写停止与失败确认

finish_stop 被公共 shutdown 在 request_stop 后调用。正常返回 true 的必要条件：Session 已从全部 worker 调度集合移除，acknowledge_detached 已发布；独立线程还必须 join 完成。共享模式只解绑当前 Session，不停止其他 logger 的 worker。重复调用必须安全，包括 Session 已被管理侧失败清理的情况。

失败分两层：failed() 表示观察到失败，可立即拒绝提交；failed_and_released() 必须同时保证真实执行者已退出或永久移除该 Session、没有缓存指针/回调/调度节点还会访问它。用 release 发布终态，读取方 acquire。绝不能将“发出了停止信号”当成释放确认。

wait_for_command(mailbox) 只等待 completed 或确认失败释放，不领取命令、不销毁 command。true 表示 acquire 观察到 completed；false 表示已确认失败且释放。finish_stop 的 false 同样保证释放，独立线程已 join。没有这种证明就继续等待，不能 false 返回让管理线程并发清理。

公共 API 已负责调用 abandon_after_worker_failure；worker 不需要重复执行 Appender 清理。生产 Session::service 是 noexcept；线程外围的可恢复启动/调度故障应发布失败终态。不能承诺捕获进程崩溃、terminate 或任意内存破坏。

## 4. 服务循环的对接顺序

以下是控制流说明，不是另一份待粘贴的 Session 实现：

```text
线程启动：屏蔽当前 worker 的 SIGPIPE，发布启动成功
每轮：处理冷附着/解绑请求，轮转访问 Session
  progress = session.service(workspace, steady_clock::now())
  idle       -> 当前完整扫描无工作，可参与等待
  runnable   -> 仍可能有工作，下一轮继续，先给其他 Session 机会
  waiting_io -> 保留在调度中，按 deadline/兜底周期再访，避免忙等
  detach_ready -> 先移出所有调度集合、清除后续引用
                  最后调用 session.acknowledge_detached()
                  此后禁止再读 session，包括 next_deadline()
  未解绑 Session 的 next_deadline() 参与下一次等待时间计算
无立即工作时：带谓词等待 CV，超时后重扫
```

不要在唤醒/登记 mutex 内调用 service、flush、sync、恢复打开或等待线程 join。Session 自己保证 Ring 借用期间不做输出 I/O；worker 不应持有 ReadHandle。

服务访问按每 Channel 64 records / 256 KiB 默认配额（BackendConfig 可调整），Context 每次最多发现 64 个。一次 flush 可因短写或 EINTR 持续执行，这是已确认的 BQLog 对齐语义；调度配额不构成单次系统调用的实时性保证。

## 5. 唤醒必须避免丢通知

实现一个 mutex + condition_variable + pending/停止谓词。通知方在锁内设置 pending 后 notify；等待方在同一锁下检查 pending，消费该标志，再等待到下一 deadline。正确性优先，不要仅检查一个无同步 bool 后 wait。线程服务 I/O 时不持有此锁。

Producer 已仅在 reserve full 或 commit 后达到半满时调用 notify；管理提交和 shutdown 会显式通知。低流量不足半满的记录依赖 worker 的兜底扫描，建议保持既定约 66 ms 上限，并与所有活跃 Session 的 next_deadline 取最早值。后台 deadline 已到而 I/O 仍无进展时要安排后续等待，避免零超时连续空转；但不能因此推迟可运行 Session。

BackendWorkspace 每线程复用；共享节点在冷路径准备，运行期避免为每次轮转分配 vector。节点仅解绑后由安全所有者回收。线程登记和等待允许锁，普通低压力 Producer 路径不会调用这个通知锁。

## 6. 公共 shutdown 已实现的调用链

```text
调用者停止并 join Producer/其他管理使用者
AsyncLogger::shutdown:
  关闭注册和新管理提交
  pending 命令 -> notify -> wait_for_command
  completed 命令 -> ManagementController::poll 领取/销毁退休对象
  Session::request_stop(mode) -> notify
  WorkerAttachment::finish_stop
  必要时在确认释放后 abandon_after_worker_failure
  确认 detached -> 返回稳定的 ShutdownResult
析构随后释放 Context 与 Session
```

不要让 worker 领取 completed 邮箱；completed 到 empty 属于管理线程。禁止活线程 detach 后宣称 shutdown 成功。调用者违反“先停并 join 使用者”前提不由 close_registration 自动补救。

## 7. 按顺序验证真实线程

1. 独立线程启动、低流量兜底唤醒、压力唤醒、空 logger shutdown、重复 shutdown、实际 join。
2. 一个共享线程服务多个 logger；一个空闲/持续繁忙/故障输出不能让其他 Session 永久饥饿。
3. 构造失败的部分附着回滚，线程启动失败，通知和进入 wait 竞态，无丢唤醒；SIGPIPE 仅屏蔽 worker。
4. pending/completed reset 各阶段 shutdown，drain 与 flush 不同语义，解绑后不再访问 Session/Context。
5. 故障已报告但线程尚未释放时禁止接管；释放后管理回收 pending、部分 retired、新目标各容器，报告领取一次。
6. 原有 backend、diagnostics、完整回归不回退；可用环境下增加 TSan，记录环境限制。
7. 真正异步压测：Producer p50/p95/p99、吞吐、丢弃率、输出完整性、各 producer 公平性、shutdown 延迟；分离 cold 与 steady，固定负载和落盘条件。现有无分配探针不是吞吐或延迟结论。

`tests/manual_worker_fixture.hpp` 仅是可控测试驱动，可参考调用契约，但没有真实线程同步证明，禁止作为生产 worker 直接使用。
