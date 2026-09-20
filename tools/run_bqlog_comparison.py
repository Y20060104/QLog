from pathlib import Path
import subprocess,tarfile,io,json
r=Path(__file__).resolve().parents[1];b=r.parent/'BqLog';e=r/'docs/validation/bqlog-comparison';e.mkdir(parents=True,exist_ok=True)
head=subprocess.check_output(['git','-C',str(b),'rev-parse','HEAD'],text=True).strip()
snapshot=r/'build'/('bqlog-upstream-'+head[:12])
if not snapshot.exists():
 snapshot.mkdir()
 data=subprocess.check_output(['git','-C',str(b),'archive',head])
 with tarfile.open(fileobj=io.BytesIO(data)) as t:
  for member in t.getmembers():
   assert (snapshot/member.name).resolve().is_relative_to(snapshot.resolve())
  t.extractall(snapshot,filter='data')
(e/'bqlog-local-changes.patch').write_text(subprocess.check_output(['git','-C',str(b),'diff'],text=True))
(e/'bqlog-version.json').write_text(json.dumps({'commit':head,'remote':'https://github.com/Tencent/BqLog.git','branch':'main','upstream_verified':'caller must git pull before invoking this benchmark runner','tested_source':'git archive HEAD; local modifications excluded','original_status':subprocess.check_output(['git','-C',str(b),'status','--short'],text=True),'snapshot':str(snapshot)},indent=2)+'\n')
commands=[['cmake','-S',str(snapshot/'src'),'-B',str(r/'build/bqlog-official-release'),'-DTARGET_PLATFORM=linux','-DCMAKE_BUILD_TYPE=Release','-DBUILD_LIB_TYPE=static_lib'],['cmake','--build',str(r/'build/bqlog-official-release'),'--target','BqLog','-j','4']]
for stage,cmd in zip(['configure','build'],commands):
 p=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(e/f'bqlog-{stage}.log').write_text(p.stdout)
 if p.returncode:raise RuntimeError(p.stdout[-5000:])
lib=next((snapshot/'artifacts').rglob('libBqLog.a'))
cmd=['g++','-std=c++20','-O3','-DNDEBUG','-DBQ_LOG_STATIC_LIB=1','-I'+str(snapshot/'include'),str(r/'benchmarks/bqlog_async_benchmark.cpp'),str(lib),'-pthread','-ldl','-Wl,--wrap=write','-Wl,--wrap=fdatasync','-o',str(r/'build/v1-perf/benchmarks/bqlog_async_benchmark')]
p=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(e/'adapter-build.log').write_text(p.stdout);(e/'build-commands.json').write_text(json.dumps(commands+[cmd],indent=2))
if p.returncode:raise RuntimeError(p.stdout[-6000:])
print('Built upstream BQLog',head,flush=True)

import os,hashlib,tempfile,time,platform
binary=r/'build/v1-perf/benchmarks'
qcommands=[['cmake','-S',str(r),'-B',str(r/'build/v1-perf'),'-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=OFF','-DQLOG_ENABLE_DIAGNOSTICS=OFF','-DQLOG_BUILD_BENCHMARKS=ON','-DQLOG_BENCH_WITH_BQLOG=OFF'],['cmake','--build',str(r/'build/v1-perf'),'--target','qlog','-j','4']]
for stage,qcommand in zip(['configure','build'],qcommands):
    built=subprocess.run(qcommand,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (e/f'qlog-{stage}.log').write_text(built.stdout)
    if built.returncode:raise RuntimeError(built.stdout[-5000:])

cmd=['g++','-std=c++20','-O3','-DNDEBUG','-DQLOG_ENABLE_DIAGNOSTICS=0','-I'+str(r/'include'),str(r/'benchmarks/qlog_comparison_benchmark.cpp'),str(r/'build/v1-perf/libqlog.a'),'-pthread','-Wl,--wrap=write','-Wl,--wrap=fdatasync','-o',str(binary/'qlog_comparison_benchmark')]
p=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(e/'qlog-adapter-build.log').write_text(p.stdout)
if p.returncode:raise RuntimeError(p.stdout)
commands=json.loads((e/'build-commands.json').read_text());commands.append(cmd);(e/'build-commands.json').write_text(json.dumps(commands,indent=2))
affinity=sorted(os.sched_getaffinity(0))[:8];os.sched_setaffinity(0,affinity)
def output(cmd):return subprocess.check_output(cmd,text=True)
meta={'utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'platform':platform.platform(),
'affinity':affinity,'cpu':output(['lscpu']),'compiler':output(['g++','--version']),
'qlog_head':output(['git','-C',str(r),'rev-parse','HEAD']).strip(),'qlog_status':output(['git','-C',str(r),'status','--short']),
'bqlog_head':head,'bqlog_tested_source':'clean git archive HEAD','endpoint':'QLog request_drain(buffered) completion; BQLog force_flush return; no shutdown/destruction or fdatasync in timed endpoint',
'qlog_build_cache':(r/'build/v1-perf/CMakeCache.txt').read_text(),
'binary_sha256':{n:hashlib.sha256((binary/n).read_bytes()).hexdigest() for n in ['qlog_comparison_benchmark','bqlog_async_benchmark']}}
(e/'environment.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n')
data=Path(tempfile.mkdtemp(prefix='qlog-bqlog-comparison-',dir=r/'build'))
cases=[('single-saturation','shared',1,10000000,1,0),('four-saturation','shared',4,5000000,1,0),
('independent-four','independent',4,5000000,1,0),('two-target','shared',4,3000000,2,0),
('single-paced','shared',1,1000000,1,500000),('four-paced','shared',4,300000,1,150000)]
results=[]
for repetition in range(3):
 for name,mode,producers,attempts,targets,rate in cases:
  # Alternate order to reduce consistently assigning one library first-run bias.
  for library in (['qlog','bqlog'] if repetition%2==0 else ['bqlog','qlog']):
   executable='qlog_comparison_benchmark' if library=='qlog' else 'bqlog_async_benchmark'
   argv=[str(binary/executable),str(data/f'{name}-{library}-{repetition}'),mode,*map(str,[producers,attempts,targets,0,rate])]
   p=subprocess.run(argv,text=True,capture_output=True,timeout=120)
   (e/f'{name}-{library}-{repetition}.stdout').write_text(p.stdout)
   (e/f'{name}-{library}-{repetition}.stderr').write_text(p.stderr)
   if p.returncode:raise RuntimeError((argv,p.returncode,p.stderr))
   json_lines=[line for line in p.stdout.splitlines() if line.startswith('{')]
   value=json.loads(json_lines[-1]);value.update(library=library,scenario=name,repetition=repetition,command=argv)
   assert value['verified']
   results.append(value);(e/'results.json').write_text(json.dumps(results,indent=2)+'\n')
   rejected=value.get('rejected',value.get('full',0))
   print(f"{name} {library} #{repetition}: {value['accepted_per_second']:.0f} accepted/s, rejected={100*rejected/(rejected+value['accepted']):.2f}%, P99={value['accepted_latency']['p99_ns']} ns, verified",flush=True)
manifest=[]
for root in [snapshot/'src',snapshot/'include']:
 for path in sorted(root.rglob('*')):
  if path.is_file():manifest.append({'path':str(path.relative_to(snapshot)),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
(e/'bqlog-source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(e/'qlog-source-manifest.json').write_text(json.dumps([{'path':str(p.relative_to(r)),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for base in ['src','include','benchmarks'] for p in sorted((r/base).rglob('*')) if p.is_file()],indent=2)+'\n')
print('All 36 trials verified; raw files:',data,flush=True)
