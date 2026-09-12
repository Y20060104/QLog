from pathlib import Path
import subprocess,json,os,platform
r=Path(__file__).resolve().parents[1];out=r/'build/validation/i1d/benchmark';out.mkdir(parents=True,exist_ok=True)
core=min(os.sched_getaffinity(0));results=[]
for compiler in ['g++','clang++-18']:
    b=r/'build/bench'/('i1d-'+compiler+'-release')
    commands=[['cmake','-S',str(r),'-B',str(b),'-G','Ninja','-DCMAKE_CXX_COMPILER='+compiler,'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_FLAGS=-Werror','-DBUILD_TESTING=OFF','-DQLOG_BUILD_RECORD_BENCHMARK=ON'],['cmake','--build',str(b),'--parallel','3']]
    with (out/(compiler+'-build.log')).open('w') as log:
        for cmd in commands:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
    for mode in ['throughput','latency']:
        cmd=['taskset','-c',str(core),str(b/'qlog_record_core_benchmark')]+(['--latency'] if mode=='latency' else [])
        with (out/(compiler+'-'+mode+'.csv')).open('w') as f:subprocess.run(cmd,stdout=f,check=True)
    results.append({'compiler':subprocess.check_output([compiler,'--version'],text=True).splitlines()[0],'cpu_affinity':core,'profile':'Release -Werror; no LTO; hot reused input','latency':'101 batches of 128; amortized batch ns/op, not single-event tail latency'})
(out/'environment.json').write_text(json.dumps({'platform':platform.platform(),'cpuinfo':Path('/proc/cpuinfo').read_text().split('\n\n')[0],'runs':results},indent=2))
print('GCC and Clang throughput/latency baselines saved',flush=True)
