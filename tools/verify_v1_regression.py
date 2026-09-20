#!/usr/bin/env python3
from pathlib import Path
import subprocess, concurrent.futures, json
r=Path(__file__).resolve().parents[1]
out=r/'docs/validation/v1-final';out.mkdir(parents=True,exist_ok=True)
def run(case):
    name,folder=case;b=r/'build'/folder
    for stage,cmd in [
        ('build',['cmake','--build',str(b),'-j','3']),
        ('tests',['ctest','--test-dir',str(b),'--output-on-failure','-j','2'])]:
        p=subprocess.run(cmd,cwd=r,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (out/f'{name}-{stage}.log').write_text(p.stdout)
        if p.returncode:raise RuntimeError((name,stage,p.stdout[-6000:]))
    print('PASS',name,flush=True)
    return {'configuration':name,'passed':True}
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    results=list(pool.map(run,[('debug','formatter-gcc-debug'),('release','formatter-release'),('san','formatter-san')]))
b=r/'build/v1-tsan'
subprocess.run(['cmake','-S',str(r),'-B',str(b)],check=True)
p=subprocess.run(['cmake','--build',str(b),'--target','qlog_worker_runtime_test','qlog_worker_allocation_test','-j','3'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'tsan-build.log').write_text(p.stdout)
if p.returncode:raise RuntimeError(p.stdout[-5000:])
p=subprocess.run(['ctest','--test-dir',str(b),'-L','worker','--output-on-failure','--repeat','until-fail:3'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'tsan-tests.log').write_text(p.stdout)
if p.returncode:raise RuntimeError(p.stdout[-8000:])
results.append({'configuration':'TSan worker repeated 3 times','passed':True})
print('PASS TSan worker repeated 3 times',flush=True)
(out/'regression-summary.json').write_text(json.dumps(results,indent=2)+'\n')
