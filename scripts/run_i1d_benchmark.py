from pathlib import Path
import subprocess,json,os,platform,shutil
r=Path(__file__).resolve().parents[1];out=r/'build/validation/i1d-acceptance/benchmark';out.mkdir(parents=True,exist_ok=True)
core=min(os.sched_getaffinity(0));results=[]
pmu=False
if shutil.which('perf'):
    probe=subprocess.run(['perf','stat','-e','cycles:u,instructions:u,branches:u,branch-misses:u','--','true'],capture_output=True,text=True)
    (out/'pmu-probe.log').write_text(probe.stdout+probe.stderr)
    pmu=probe.returncode==0
for compiler in ['g++','clang++-18']:
    b=r/'build/bench'/('i1d-'+compiler+'-release')
    commands=[['cmake','-S',str(r),'-B',str(b),'-G','Ninja','-DCMAKE_CXX_COMPILER='+compiler,'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_FLAGS=-Werror','-DBUILD_TESTING=OFF','-DQLOG_BUILD_RECORD_BENCHMARK=ON'],['cmake','--build',str(b),'--parallel','3']]
    with (out/(compiler+'-build.log')).open('w') as log:
        for cmd in commands:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
    for mode in ['throughput','latency']:
        cmd=['taskset','-c',str(core),str(b/'qlog_record_core_benchmark')]+(['--latency'] if mode=='latency' else [])
        if pmu:cmd=['perf','stat','-x',',','-e','cycles:u,instructions:u,branches:u,branch-misses:u','-o',str(out/(compiler+'-'+mode+'-pmu.csv')),'--']+cmd
        with (out/(compiler+'-'+mode+'.csv')).open('w') as f:subprocess.run(cmd,stdout=f,check=True)
    results.append({'compiler':subprocess.check_output([compiler,'--version'],text=True).splitlines()[0],'cpu_affinity':core,'profile':'Release -Werror; no LTO; hot and rotating input; actual 32-byte alignment plus offset 1','latency':'101 batches of 128; amortized batch ns/op, not single-event tail latency'})
(out/'environment.json').write_text(json.dumps({'platform':platform.platform(),'cpuinfo':Path('/proc/cpuinfo').read_text().split('\n\n')[0],'pmu_available':pmu,'pmu_scope':'Whole benchmark process user-space totals, including setup and timing overhead. Not per-operation counters; do not divide these by an individual case iteration count.','cache_note':'Rotating buffers span about 65 MiB each; small inputs touch only part of that span. This is not a cold-cache guarantee.','timing_note':'WSL2 measurements; setup excluded from timed batches; throughput median/MAD uses 9 batches; latency P99 is nearest-rank over 101 amortized batches.','runs':results},indent=2))
print('GCC and Clang throughput/latency baselines saved',flush=True)
