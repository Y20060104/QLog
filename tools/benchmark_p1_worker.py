#!/usr/bin/env python3
"""Paired V1 worker comparison using the existing async/visibility/idle workloads.
Requires prebuilt Release libraries in --before and --after. Does not edit production.
Keeps per-trial output, commands, source/binary hashes and actual log files.
"""
import argparse, hashlib, json, os, platform, statistics, subprocess, time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--before', required=True)
parser.add_argument('--after', required=True)
parser.add_argument('--evidence', required=True)
parser.add_argument('--repetitions', type=int, default=3)
parser.add_argument('--prepare-only', action='store_true')
a = parser.parse_args()
r = Path(__file__).resolve().parents[1]
out = Path(a.evidence).resolve(); out.mkdir(parents=True, exist_ok=True)
builds = {'before': Path(a.before).resolve(), 'after': Path(a.after).resolve()}
affinity = sorted(os.sched_getaffinity(0))[:8]
os.sched_setaffinity(0, affinity)
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def command(cmd, label, timeout=180):
    start = time.monotonic()
    p = subprocess.run(cmd, text=True, capture_output=True, timeout=timeout)
    (out/(label+'.stdout')).write_text(p.stdout)
    (out/(label+'.stderr')).write_text(p.stderr)
    meta = {'command': cmd, 'returncode': p.returncode, 'wall_seconds': time.monotonic()-start}
    (out/(label+'.command.json')).write_text(json.dumps(meta, indent=2))
    if p.returncode: raise RuntimeError((label, p.returncode, p.stderr[-3000:], p.stdout[-3000:]))
    return p.stdout

# Startup-only interposition obtains the actual worker pthread CPU clock.
# Shared worker survives logger shutdown, so both clock reads are valid.
helper = r"""
#include <atomic>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/resource.h>
static std::atomic<clockid_t> p1_worker_clock{0};
extern "C" int __real_pthread_sigmask(int, const sigset_t*, sigset_t*);
extern "C" int __wrap_pthread_sigmask(int how, const sigset_t* set, sigset_t* old) {
    const int result = __real_pthread_sigmask(how, set, old);
    if (!result && set && how == SIG_BLOCK) {
        clockid_t clock{};
        if (!pthread_getcpuclockid(pthread_self(), &clock))
            p1_worker_clock.store(clock, std::memory_order_release);
    }
    return result;
}
static double p1_worker_cpu() {
    const auto clock = p1_worker_clock.load(std::memory_order_acquire);
    timespec t{};
    if (!clock || clock_gettime(clock, &t)) return -1.0;
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec)*1e-9;
}
"""
source = (r/'benchmarks/async_logger_benchmark.cpp').read_text()
source = helper + source
source = source.replace('::getrusage(RUSAGE_SELF, &before);', '::getrusage(RUSAGE_SELF, &before);\n    const auto worker_before = p1_worker_cpu();')
source = source.replace('::getrusage(RUSAGE_SELF, &after);', '::getrusage(RUSAGE_SELF, &after);\n    const auto worker_after = p1_worker_cpu();')
anchor = '<< ",\\\"max_rss_kib\\\":"'
# Insert adjacent to the existing CPU output without changing measurement/verification.
needle = '               << ",\\\"max_rss_kib\\\":" << after.ru_maxrss'
# Actual C++ text uses a single backslash before each embedded quotation mark.
needle = '               << ",\\"max_rss_kib\\":" << after.ru_maxrss'.replace('\\\\','\\')
# Stable line-based insertion is clearer than shell/string escape transformations.
lines=source.splitlines(); cooked=[]
for line in lines:
    if 'max_rss_kib' in line:
        cooked += [r'               << ",\"worker_cpu_seconds\":" << ((worker_before >= 0 && worker_after >= 0) ? std::to_string(worker_after-worker_before) : "null")',
                   r'               << ",\"voluntary_context_switches\":" << after.ru_nvcsw-before.ru_nvcsw',
                   r'               << ",\"involuntary_context_switches\":" << after.ru_nivcsw-before.ru_nivcsw']
    cooked.append(line)
(out/'async_probe.cpp').write_text('\n'.join(cooked)+'\n')
source=helper+(r/'docs/analysis/performance_audit_20260920/idle_probe.cpp').read_text()
source=source.replace('const auto cpu_begin = process_cpu_seconds();', 'const auto cpu_begin = process_cpu_seconds();\n    const auto worker_begin = p1_worker_cpu();\n    rusage usage_before{}, usage_after{};\n    getrusage(RUSAGE_SELF, &usage_before);')
source=source.replace('const auto cpu = process_cpu_seconds() - cpu_begin;', 'const auto cpu = process_cpu_seconds() - cpu_begin;\n    const auto worker_cpu = p1_worker_cpu() - worker_begin;\n    getrusage(RUSAGE_SELF, &usage_after);')
source=source.replace('<< ",cpu_cores=" << cpu / wall', '<< ",worker_cpu_s=" << worker_cpu << ",voluntary_cs=" << usage_after.ru_nvcsw-usage_before.ru_nvcsw << ",involuntary_cs=" << usage_after.ru_nivcsw-usage_before.ru_nivcsw << ",cpu_cores=" << cpu / wall')
(out/'idle_probe.cpp').write_text(source)
for version,build in builds.items():
    for name in ['async','idle']:
        cmd=['g++','-std=c++20','-O3','-DNDEBUG','-DQLOG_ENABLE_DIAGNOSTICS=0','-pthread','-I'+str(r/'include'),str(out/(name+'_probe.cpp')),str(build/'libqlog.a'),'-Wl,--wrap=pthread_sigmask']
        if name=='async':cmd+=['-Wl,--wrap=write','-Wl,--wrap=fdatasync']
        cmd+=['-o',str(out/(version+'-'+name))]
        command(cmd,'compile-'+version+'-'+name)
metadata={'utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'platform':platform.platform(),'affinity':affinity,
 'head':subprocess.check_output(['git','-C',str(r),'rev-parse','HEAD'],text=True).strip(),
 'git_diff':subprocess.check_output(['git','-C',str(r),'diff','--','src','include'],text=True),
 'libraries':{k:{'path':str(v/'libqlog.a'),'sha256':sha(v/'libqlog.a'),'cmake_cache':(v/'CMakeCache.txt').read_text()} for k,v in builds.items()},
 'probe_sha256':{p.name:sha(p) for p in out.glob('*_probe.cpp')},
 'method':'Existing async benchmark: 1/67 API sampling, warmup drained, timed producers+shutdown, every accepted ID verified per sink. Extra startup-only pthread CPU-clock hook and 2 boundary reads; rusage context switches cover same interval. Write wrappers retained equally. Idle samples 1 second per phase. Independent worker CPU not measured after its join.'}
(out/'environment.json').write_text(json.dumps(metadata,indent=2))
if a.prepare_only:raise SystemExit(0)
scenarios=[('single-saturation','shared',1,10000000,1,0,0),('four-saturation','shared',4,5000000,1,0,0),('independent-four-saturation','independent',4,5000000,1,0,0),('two-target-saturation','shared',4,3000000,2,0,0),('single-paced','shared',1,1000000,1,0,500000),('four-paced','shared',4,300000,1,0,150000),('single-paced-durable','shared',1,1000000,1,1,500000)]
results=[]; idle=[]; visibility=[]
data=r/'build/p1-worker-20260922/measurement-files';data.mkdir(exist_ok=True)
for repetition in range(a.repetitions):
    versions=['before','after'] if repetition%2==0 else ['after','before']
    for case,mode,producers,attempts,targets,durable,rate in scenarios:
        for version in versions:
            label=f'{version}-{case}-{repetition}'
            cmd=[str(out/(version+'-async')),str(data/label),mode,*map(str,[producers,attempts,targets,durable,rate])]
            result=json.loads(command(cmd,label));result.update(version=version,scenario=case,repetition=repetition)
            result['files']={p.name:{'bytes':p.stat().st_size,'sha256':sha(p)} for p in (data/label).glob('*.log')}
            results.append(result);(out/'results.json').write_text(json.dumps(results,indent=2))
            print(label,round(result['accepted_per_second']),result['accepted_latency'],flush=True)
    for case in ['pair102102','pair101103','single204']:
        for version in versions:
            label=f'{version}-idle-{case}-{repetition}'
            text=command([str(out/(version+'-idle')),case,'1'],label,60)
            for line in text.splitlines():
                if line.startswith('sample,'):
                    parts=line.split(',');record={'version':version,'case':case,'phase':parts[2],'repetition':repetition}
                    record.update({k:float(v) for k,v in (part.split('=') for part in parts[3:])});idle.append(record)
            assert all('ok=1' in line for line in text.splitlines() if line.startswith('shutdown,'))
            (out/'idle.json').write_text(json.dumps(idle,indent=2))
            print(label,[x['cpu_cores'] for x in idle[-2:]],flush=True)
    for version in versions:
        label=f'{version}-visibility-{repetition}'
        cmd=[str(builds[version]/'benchmarks/qlog_visibility_benchmark'),str(data/(label+'.log'))]
        result=json.loads(command(cmd,label,40));result.update(version=version,repetition=repetition);visibility.append(result)
        (out/'visibility.json').write_text(json.dumps(visibility,indent=2));print(label,result,flush=True)
print('All paired measurements complete; raw logs retained in',data,flush=True)
