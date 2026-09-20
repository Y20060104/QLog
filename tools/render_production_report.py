#!/usr/bin/env python3
"""Render the Chinese native production-workload report from audited evidence."""
from pathlib import Path
import json
R=Path(__file__).resolve().parents[1]
E=R/'docs/validation/native-production-20260920'
s=json.loads((E/'summary.json').read_text());m=json.loads((E/'source_manifest.json').read_text())
env=json.loads((E/'environment.json').read_text());end=json.loads((E/'run_complete.json').read_text())
assert json.loads((E/'audit.json').read_text())['passed']
g=s['overall'];out=[]
def line(text=''):out.append(text)
def table(headers,rows):
    line('| '+' | '.join(headers)+' |');line('| '+' | '.join(['---']*len(headers))+' |')
    for row in rows:line('| '+' | '.join(map(str,row))+' |')
    line()
line('# QLog V1 与 BQLog 原生默认配置性能报告（WSL2）')
line()
line('报告日期：2026-09-20。状态：102 次正式试验及独立证据审计通过；评测范围为本机缓冲文本日志业务。')
line()
line(f"在预先固定的 8 个饱和负载场景、每场景 5 轮配对测试中，QLog/BQLog 成功输出吞吐指数为 **{g['ratio']:.3f}×**，95% 经验 bootstrap 区间 **[{g['ci95'][0]:.3f}, {g['ci95'][1]:.3f}]**。反向 BQLog/QLog 指数为 **{1/g['ratio']:.3f}×**。指数只衡量本套件的成功输出吞吐，不等于完整生产服务质量或所有负载的性能倍数。")
line()
line('**关键解释：QLog 保留默认满队列拒绝，BQLog 保留默认阻塞；报告同时公开拒绝比例、CPU 成本与延迟。不能用低 API 返回延迟掩盖拒绝，也不能把阻塞实现较少的尝试次数解释为日志丢失。** 当前宿主为 Power saver 电源计划，结果适用于本次明确记录的 WSL2 环境。')
line()
line('## 1. 版本与环境')
line()
table(['项目','冻结值'],[
    ['BQLog 上游提交',f"`{m['bqlog']['git_commit']}`；使用该提交的干净 git archive，未混入本地修改"],
    ['QLog 基础提交',f"`{m['qlog']['base_commit']}`；不能单独代表本次未提交的 V1 工作树"],
    ['QLog 实际源码内容 ID',f"`{m['qlog']['content_id']}`"],
    ['BQLog 源码内容 ID',f"`{m['bqlog']['content_id']}`"],
    ['业务驱动 SHA-256',f"`{m['harness_sha256']}`"],
    ['宿主','Windows 11 家庭版，10.0.26200；i7-9750H，6 核/12 逻辑处理器；约 16 GiB 物理内存'],
    ['来宾','Ubuntu / Linux 6.6.87.2-microsoft-standard-WSL2，约 7.68 GiB 可见内存'],
    ['编译器','GCC 13.3.0，CMake Release；业务驱动 -O3 -DNDEBUG；QLog 诊断 AUTO 在 Release 下关闭'],
    ['输出介质','WSL2 ext4 虚拟磁盘；普通缓冲文本文件，非 /dev/null、非 tmpfs'],
    ['CPU affinity',str(env['affinity'])+'；进程及其线程共用该集合，不是独占物理核心'],
    ['电源及噪声','运行中观测宿主 Power saver；未修改电源计划，未控制频率/温度/宿主其他任务'],
    ['执行时间 UTC',env['start_state']['utc']+' 至 '+end['end_state']['utc']]])
line('源码清单、二进制哈希、完整 CMakeCache、编译输出和 Windows/WSL 环境记录均包含在证据目录。未将 QLog 的旧 HEAD 冒充本次完整源码版本。')
line()
line('## 2. 设计与完成语义')
line()
line('两库均通过各自字面量公共 API 写入 info 文本日志，保留自身队列、满策略、缓存、formatter、完整行前缀、时间戳和 worker 实现。QLog 默认每 Producer 64 KiB SPSC、256 KiB 文件批次缓存；BQLog 保留 desktop 默认 64 KiB 队列配置、原生 MISO/SISO 自适应及 64 KiB 文件缓存。没有强制两库采用相同资源配置或底层算法。')
line()
line('每次构造一个 Logger，3 秒饱和预热并排空后，同一批 Producer 测量 10 秒，随后完成原生 buffered drain/force_flush。成功输出吞吐分母包含 Producer 停止及尾部排空；双文件每个业务记录只计一次。BQLog force_flush 允许调用线程参与尾部处理，QLog 由 worker 完成管理请求，这一产品差异保留。fdatasync、全文件校验和哈希发生在计时之外，不宣称计时覆盖掉电持久化。')
line()
line('8 个主场景各 5 次，2 个到达率附加场景各 5 次，另有两库各 1 次 60 秒持续负载；合计 102 次。场景顺序按固定种子随机化，同一场景库顺序交替。固定到达率按每 Producer 32 条同步批次释放，属于突发型业务输入；截止时未发起调用的请求另计。')
line()
line('总体指数对 40 个配对吞吐比值取等权几何平均。置信区间以完整重复轮次为块，精确枚举 3125 个 bootstrap 重采样；仅有 5 个重复轮次，重采样假设这些块可交换，未证明轮次之间独立；不做异常值剔除或多重比较修正。表中的单库数值为 5 次中位数，“倍数”使用配对几何平均，因此不一定等于表中两个中位数相除。')
line()
line('## 3. 饱和负载成功输出吞吐')
line()
rows=[]
for name,c in s['scenarios'].items():
    if not c['primary']:continue
    q,b,p=c['qlog'],c['bqlog'],c['paired_goodput']
    rows.append([name,f"{q['goodput']/1e6:.3f}",f"{b['goodput']/1e6:.3f}",f"{p['ratio']:.3f} [{p['ci95'][0]:.3f}, {p['ci95'][1]:.3f}]",f"{q['unaccepted_percent']:.2f}%",f"{b['unaccepted_percent']:.2f}%"])
table(['场景','QLog 百万条/秒','BQLog 百万条/秒','Q/BQ 倍数 [95% CI]','Q 拒绝比例','BQ 拒绝比例'],rows)
line('small 为整数短消息；mixed 为整数、double {:.3f} 和 16 字符串混合；large 为 512 字节字符串。后缀 1/4/8 为 Producer 数量；independent-4 为一个 Logger 的独立 worker，fanout-4 为两个文本文件。其他场景使用共享 worker。')
line()
line('各主场景 5 次吞吐范围（百万条/秒）：')
line()
table(['场景','QLog 最小–最大','BQLog 最小–最大'], [[name, '–'.join(f'{v/1e6:.3f}' for v in c['qlog']['goodput_min_max']), '–'.join(f'{v/1e6:.3f}' for v in c['bqlog']['goodput_min_max'])] for name,c in s['scenarios'].items() if c['primary']])
line('这里的拒绝比例以各自饱和循环的尝试次数为分母，不是对同一外部请求集合的丢失率。BQLog 默认阻塞使驱动自然背压，QLog 默认拒绝使驱动继续消耗 CPU 发起更多尝试。该结果反映产品默认过载行为，不能据此声称 QLog 在普通业务中会丢失同样比例的日志。固定外部到达率结果见下一节。')
line()
line('## 4. 固定到达率的交付与响应')
line()
rows=[]
for name in ['offered-200k','offered-1m']:
    for lib in ['qlog','bqlog']:
        x=s['scenarios'][name][lib]
        rows.append([name,lib,f"{x['goodput']/1e6:.3f}",f"{x['unaccepted_percent']:.3f}%",f"{x['not_attempted']:.0f}",f"{x['accepted_p99_ns_upper']/1000:.3f}",f"{x['scheduled_p99_ns_upper']/1e6:.3f}"])
table(['场景','库','百万成功条/秒','计划请求未接受','未能发起调用数','接受调用 P99 µs','计划到 API 返回 P99 ms'],rows)
line('“计划请求未接受”同时包含返回拒绝和截止时未发起调用；不等于被接受后的文件丢失。“计划到 API 返回”统计已发起调用的全部采样，包括接受与拒绝，包含批次释放及线程调度延迟；不覆盖从 API 接受到文件可见的逐条延迟，也不为未发起的调用生成假样本。')
line()
line('交付质量不能只看中位数。以下列出五轮合计和最差轮次，避免零中位数掩盖积压：')
line()
table(['场景','库','五轮合计未接受','单轮未接受 最小–最大','五轮返回拒绝数','五轮截止未发起数'], [[name,lib,f"{x['aggregate_unaccepted_percent']:.3f}%",'–'.join(f'{v:.3f}%' for v in x['unaccepted_percent_min_max']),f"{x['total_rejected']:,}",f"{x['total_not_attempted']:,}"] for name in ['offered-200k','offered-1m'] for lib in ['qlog','bqlog'] for x in [s['scenarios'][name][lib]]])
bq=s['scenarios']['offered-1m']['bqlog']
line(f"BQLog 在 offered-1m 的一轮出现明显积压，单轮最多 {bq['unaccepted_percent_min_max'][1]:.3f}% 的计划请求未能在截止前发起；全部五轮累计 {bq['total_not_attempted']:,} 个请求未发起。其 API 返回拒绝为零，并不表示计划业务请求在时间窗口内全部完成。该次慢试验完整保留；现有证据不能将它单独归因于某个实现细节或宿主因素。")
line()
line('固定到达率尾部观测（P99.9 为每次采样分位数的中位数；最大值为全部五次采样中的最大观测值）：')
line()
table(['场景','库','接受 API P99.9 µs','接受 API 最大 ms','计划响应 P99.9 ms','计划响应最大 ms'], [[name,lib,f"{x['accepted_p999_ns_upper']/1000:.3f}",f"{x['accepted_max_observed_ns']/1e6:.3f}",f"{x['scheduled_p999_ns_upper']/1e6:.3f}",f"{x['scheduled_max_observed_ns']/1e6:.3f}"] for name in ['offered-200k','offered-1m'] for lib in ['qlog','bqlog'] for x in [s['scenarios'][name][lib]]])
line('这些仍然是抽样统计，最大观测值不是所有调用的真实最大值，也不是延迟上界保证。')
line()
line('## 5. CPU、尾延迟与内存')
line()
rows=[]
for name,c in s['scenarios'].items():
    if not c['primary']:continue
    q,b=c['qlog'],c['bqlog']
    rows.append([name,f"{q['cpu_us_per_accepted']:.3f} / {b['cpu_us_per_accepted']:.3f}",f"{q['accepted_p99_ns_upper']/1000:.3f} / {b['accepted_p99_ns_upper']/1000:.3f}",f"{q['rss_mib']:.2f} / {b['rss_mib']:.2f}",f"{q['drain_ms']:.3f} / {b['drain_ms']:.3f}"])
table(['场景','CPU µs/成功条 Q/BQ','接受 API P99 µs Q/BQ','进程峰值 RSS MiB Q/BQ','尾部排空 ms Q/BQ'],rows)
line('CPU 包含驱动循环、成功记录指纹、抽样计时和库线程；QLog 在饱和输入下的拒绝重压也包含其中。RSS 是预热及测量期间的进程高水位，包含测试直方图和运行时，不能视为日志队列净内存；启动/预热高水位可能掩盖测量阶段的驻留内存差异。P99 是每 1021 次尝试抽样所得的直方图桶上界，接受/拒绝分开存储；表中仅展示接受调用，不能孤立解读。')
line()
line('## 6. 60 秒持续负载抽查')
line()
table(['库','百万成功条/秒','拒绝比例','CPU µs/成功条','接受 P99 µs','尾部排空 ms'],[[x['library'],f"{x['goodput']/1e6:.3f}",f"{x['unaccepted_percent']:.2f}%",f"{x['cpu_us_per_accepted']:.3f}",f"{x['accepted_p99_ns_upper']/1000:.3f}",f"{x['drain_ms']:.3f}"] for x in s['soak']])
line('该项每库仅一次，采用 mixed-4，未纳入总体指数；每秒累计尝试数、接受数和文件字节数保存在原始 JSON 中。60 秒抽查不是数小时/数天的生产稳定性认证。')
line()
line('## 7. 正确性、证据及复现')
line()
line(f"全部 **{s['trial_count']} 次**正式试验通过输出完整性校验，累计测量阶段接受 **{s['accepted_measured_total']:,}** 条业务记录；包含预热和双文件副本，实际解析验证 **{s['verified_lines_including_warmup_and_fanout']:,}** 行，生成文件合计约 **{s['generated_output_bytes']/1024**3:.2f} GiB**。没有剔除慢试验、用最快结果替代中位数或以入队次数代替真实输出。")
line()
line('每条日志核验精确正文、Producer/阶段内严格递增序号、接受数量，以及两种 64 位指纹；双目标分别核验。指纹存在有限碰撞概率，不能描述成无碰撞证明。前缀保持各库原样，但没有逐字段比较其语义。所有成功接受记录均通过此验证；满队列拒绝不属于接受后丢失。')
line()
line('交付验证另外完成了计量直方图边界、分位数、指纹错序/缺失和字段解析自检；证据审计接受全部 14 份预检数据，并拒绝 7 类故意损坏的证据。冻结源码包已在新目录完整重建两库，各完成一次双文件输出短测，均通过校验；这两次短测不计入正式性能样本。见 [复现验证](../validation/native-production-20260920/reproduction_check.json)、[计量自检](../validation/native-production-20260920/measurement-selftest.txt) 和 [证据负向验证](../validation/native-production-20260920/audit_negative_controls.json)。')
line()
line('巨大文本文件已在验证、fdatasync 和 SHA-256 留证后删除；每次 JSON 保存逐文件字节数、哈希、首尾样本和 Producer 指纹。公开证据足以重算统计，但若要重新逐行验证原始全文，需要重跑并传入 --keep-output。')
line()
line('- [完整方法学与配置边界](../../benchmarks/production/README_CHS.md)')
line('- [预定测试方案](../../benchmarks/production/plan.json) / [执行前冻结信息](../validation/native-production-20260920/preregistration.json)')
line('- [逐次 CSV](../validation/native-production-20260920/trials.csv) / [统计 JSON](../validation/native-production-20260920/summary.json) / [独立审计](../validation/native-production-20260920/audit.json)')
line('- [原始试验目录](../validation/native-production-20260920/trials) / [构建命令](../validation/native-production-20260920/build_commands.json)')
line('- [冻结源码与哈希](../validation/native-production-20260920/sources/archives.json) / [逐文件校验清单](../validation/native-production-20260920/SHA256SUMS.txt)')
line('- [复现工具](../../tools/reproduce_production_benchmark.py) / [数据分析工具](../../tools/analyze_production_benchmark.py) / [报告生成器](../../tools/render_production_report.py)')
line()
line('在具有 Python 3.12+、CMake 和 GCC 13+ 的 Linux 环境中，从仓库或交付包运行：')
line()
line('```bash\npython3 tools/reproduce_production_benchmark.py /absolute/new/qlog-native-reproduction\ncd /absolute/new/qlog-native-reproduction\npython3 tools/run_production_benchmark.py --preflight\npython3 tools/run_production_benchmark.py\npython3 tools/analyze_production_benchmark.py\n```')
line()
line('复现工具只接受不存在的新目录，校验并解压冻结的两库源码，按留存命令重新构建；无需把本次未提交工作树误当成某个 Git 提交。新环境的二进制哈希及性能可以不同，不能要求重现相同数值；应保留新的环境记录与全部结果。')
line()
line('与此前强制对齐测试的关系：本次保留原生满队列策略和缓存，采用持续计时及不同的负载套件。两份报告的倍率不可直接相减以证明版本优化或退化；本次结果也不能单独归因到某一个实现细节。')
line()
line('## 8. 可引用结论与适用范围')
line()
line(f"> 在 Windows 11 / WSL2、i7-9750H、8 个逻辑 CPU affinity、原生默认缓冲文本输出的固定工作负载套件中，QLog/BQLog 的成功输出吞吐几何平均比为 {g['ratio']:.3f}×（5 轮配对，95% 经验块 bootstrap 区间 {g['ci95'][0]:.3f}–{g['ci95'][1]:.3f}）。两库保留不同的满队列策略；该比值必须结合拒绝比例、阻塞延迟和 CPU 成本解释。")
line()
line('这是一份可以提交为性能基线、按其范围发布和复现的产品工作负载报告。当前不支持“QLog 在所有生产环境快/慢固定倍数”“达到原生 Linux 发布性能验收”“低延迟且无损”“已验证持久化延迟”等扩大结论。部署决策还需要目标 Linux 主机、电源与负载条件、真实消息分布、多个 Logger 竞争和业务丢失/延迟 SLO；不能把这些缺项算作已经完成。')
line()
(R/'docs/decisions/V1_NATIVE_PRODUCTION_PERFORMANCE_20260920_CHS.md').write_text('\n'.join(out)+'\n')
print('Report rendered from audited data')
