#!/usr/bin/env python3
"""Serial, reproducible WSL/Linux async benchmark. Build v1-perf first."""
import argparse, hashlib, json, os, pathlib, platform, subprocess, tempfile, time

parser=argparse.ArgumentParser()
parser.add_argument('--build',default='build/v1-perf')
parser.add_argument('--evidence',default='docs/validation/v1-final/performance')
parser.add_argument('--repetitions',type=int,default=3)
args=parser.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]
os.chdir(root)
evidence=root/args.evidence; evidence.mkdir(parents=True,exist_ok=True)
build=root/args.build
affinity=sorted(os.sched_getaffinity(0))[:8]
os.sched_setaffinity(0,affinity)
def command(argv):
    return subprocess.run(argv,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=False).stdout
metadata={
    'utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),
    'platform':platform.platform(),'uname':command(['uname','-a']),
    'cpu':command(['lscpu']),'compiler':command(['c++','--version']),
    'affinity':affinity,'git_head':command(['git','rev-parse','HEAD']).strip(),
    'git_status':command(['git','status','--short']),
    'filesystem':command(['df','-T',str(build)]),
    'meminfo':pathlib.Path('/proc/meminfo').read_text(),
    'build_cache':(build/'CMakeCache.txt').read_text(),
    'executable_sha256':hashlib.sha256((build/'benchmarks/qlog_async_benchmark').read_bytes()).hexdigest(),
    'method':'Release diagnostics OFF; fixed 8-CPU affinity pool; 1/67 sampled (coprime to 32-call pacing batches) call wall latency; 1000 warm messages per producer; serial trials; final shutdown included in effective throughput; page-cache buffered vs final fdatasync; verify every accepted ID per target'
}
(evidence/'environment.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2))
data=pathlib.Path(tempfile.mkdtemp(prefix='v1-performance-',dir=build))
scenarios=[
    ('single-saturation','shared',1,10000000,1,0,0),
    ('four-saturation','shared',4,5000000,1,0,0),
    ('independent-four-saturation','independent',4,5000000,1,0,0),
    ('two-target-saturation','shared',4,3000000,2,0,0),
    ('single-paced','shared',1,1000000,1,0,500000),
    ('four-paced','shared',4,300000,1,0,150000),
    ('single-paced-durable','shared',1,1000000,1,1,500000),
]
results=[]
for repetition in range(args.repetitions):
    for name,mode,producers,attempts,targets,durable,rate in scenarios:
        output=data/f'{name}-{repetition}'
        argv=[str(build/'benchmarks/qlog_async_benchmark'),str(output),mode,*map(str,[producers,attempts,targets,durable,rate])]
        p=subprocess.run(argv,text=True,capture_output=True,timeout=120)
        (evidence/f'{name}-{repetition}.stdout').write_text(p.stdout)
        (evidence/f'{name}-{repetition}.stderr').write_text(p.stderr)
        if p.returncode: raise RuntimeError((argv,p.returncode,p.stderr))
        result=json.loads(p.stdout); result.update(scenario=name,repetition=repetition,command=argv)
        results.append(result)
        (evidence/'results.json').write_text(json.dumps(results,indent=2))
        print(f"{name} #{repetition}: {result['accepted_per_second']:.0f} accepted/s, drop={result['full']/(result['full']+result['accepted']):.1%}, p99={result['accepted_latency']['p99_ns']} ns, verified",flush=True)
visibility=subprocess.run([str(build/'benchmarks/qlog_visibility_benchmark'),str(data/'visibility.log')],text=True,capture_output=True,timeout=30)
if visibility.returncode:raise RuntimeError(('visibility',visibility.returncode,visibility.stderr))
(evidence/'visibility.json').write_text(visibility.stdout)
print('Visibility:',visibility.stdout.strip(),flush=True)
print('Raw files retained under',data,flush=True)

micro=subprocess.run([str(build/'benchmarks/qlog_formatter_benchmark')],text=True,capture_output=True,timeout=30)
if micro.returncode:raise RuntimeError(('micro',micro.returncode,micro.stderr))
(evidence/'micro.json').write_text(micro.stdout)
print('Micro:',micro.stdout.strip(),flush=True)
