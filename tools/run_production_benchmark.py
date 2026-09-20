#!/usr/bin/env python3
"""Run the predeclared native-default suite. No production source mutation."""
from pathlib import Path
import argparse,hashlib,json,os,platform,random,subprocess,tempfile,time
parser=argparse.ArgumentParser()
parser.add_argument('--preflight',action='store_true')
parser.add_argument('--keep-output',action='store_true')
args=parser.parse_args()
r=Path(__file__).resolve().parents[1];base=r/'build/native-production'
e=r/'docs/validation/native-production-20260920';e.mkdir(parents=True,exist_ok=True)
plan_path=r/'benchmarks/production/plan.json';plan=json.loads(plan_path.read_text());plan_hash=hashlib.sha256(plan_path.read_bytes()).hexdigest()
manifest=json.loads((e/'source_manifest.json').read_text())
for lib in ['qlog','bqlog']:
    assert hashlib.sha256((base/(lib+'-native')).read_bytes()).hexdigest()==manifest['binaries'][lib]
def read(cmd):
    p=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);return p.stdout
def system_state():
    data={'utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'loadavg':os.getloadavg()}
    for name in ['meminfo','pressure/cpu','pressure/io','pressure/memory']:
        try:data[name]=Path('/proc',name).read_text()
        except OSError:data[name]=None
    return data
affinity=sorted(os.sched_getaffinity(0))[:8];os.sched_setaffinity(0,affinity)
evidence=e/('preflight' if args.preflight else 'trials');evidence.mkdir(exist_ok=True)
if not args.preflight:
    metadata=e/'preregistration.json'
    frozen={'plan_sha256':plan_hash,'plan':plan,'source_ids':{n:manifest[n]['content_id'] for n in ['qlog','bqlog']},'binaries':manifest['binaries']}
    if metadata.exists():assert json.loads(metadata.read_text())==frozen,'Plan/source changed; use a new evidence directory'
    else:metadata.write_text(json.dumps(frozen,indent=2)+'\n')
    if not (e/'environment.json').exists():
        environment={'platform':platform.platform(),'uname':read(['uname','-a']),'cpu':read(['lscpu']),'compiler':read(['g++','--version']),
          'affinity':affinity,'filesystem':read(['df','-T',str(base)]),'start_state':system_state(),
          'notes':['WSL2 shared host; no frequency/thermal control','no global drop_caches','files from each trial synced and read/verified outside timing; generated text removed unless --keep-output','no syscall wrappers in timed executable'],
          'build_caches':{n:(base/(n+'-build')/'CMakeCache.txt').read_text() for n in ['qlog','bqlog']}}
        (e/'environment.json').write_text(json.dumps(environment,indent=2)+'\n')
data=Path(tempfile.mkdtemp(prefix='native-production-data-',dir=r/'build')).resolve()
rng=random.Random(plan['seed']);jobs=[]
if args.preflight:
    cases=[plan['scenarios'][i] for i in [0,3,5,6,7,8,9]]
    for c in cases:
        for lib in ['qlog','bqlog']:jobs.append((0,c,lib,1,1))
else:
    for rep in range(plan['repetitions']):
        cases=plan['scenarios'].copy();rng.shuffle(cases)
        for ci,c in enumerate(cases):
            order=['qlog','bqlog'] if (rep+ci)%2==0 else ['bqlog','qlog']
            for lib in order:jobs.append((rep,c,lib,plan['warmup_seconds'],plan['measure_seconds']))
    for lib in ['bqlog','qlog']:jobs.append((0,plan['soak'],lib,plan['warmup_seconds'],plan['soak']['measure_seconds']))
schedule=[{'rep':rep,'scenario':c['id'],'library':lib,'warmup':warm,'seconds':seconds} for rep,c,lib,warm,seconds in jobs]
if not args.preflight:(e/'schedule.json').write_text(json.dumps(schedule,indent=2)+'\n')
for index,(rep,c,lib,warm,seconds) in enumerate(jobs):
    name=f"{c['id']}-{rep}-{lib}";record=evidence/(name+'.json')
    if record.exists():
        previous=json.loads(record.read_text());assert previous['plan_sha256']==plan_hash and previous['result']['verified'];continue
    output=data/name
    command=[str(base/(lib+'-native')),str(output),c['mode'],str(c['producers']),str(c['targets']),c['payload'],str(warm),str(seconds),str(c['rate'])]
    state=system_state();started=time.monotonic()
    try:p=subprocess.run(command,text=True,capture_output=True,timeout=max(300,seconds*8))
    except subprocess.TimeoutExpired as error:
        (evidence/(name+'.failure.txt')).write_text(repr(error));raise
    (evidence/(name+'.stdout')).write_text(p.stdout);(evidence/(name+'.stderr')).write_text(p.stderr)
    if p.returncode:
        (evidence/(name+'.failure.json')).write_text(json.dumps({'command':command,'exit':p.returncode,'state':state},indent=2));raise RuntimeError((name,p.returncode,p.stderr[-2000:]))
    result=json.loads([x for x in p.stdout.splitlines() if x.startswith('{')][-1]);assert result['verified']
    file_records=[]
    for file in sorted(output.rglob('*')):
        if not file.is_file():continue
        digest=hashlib.sha256();first=b'';last=b''
        with file.open('rb') as f:
            os.fdatasync(f.fileno()) # only this trial's generated file; outside timed region
            while True:
                chunk=f.read(1024*1024)
                if not chunk:break
                if not first:first=chunk[:2048]
                last=chunk[-2048:];digest.update(chunk)
        file_records.append({'path':str(file.relative_to(output)),'bytes':file.stat().st_size,'sha256':digest.hexdigest(),'first_2048_utf8':first.decode('utf8','replace'),'last_2048_utf8':last.decode('utf8','replace')})
    value={'plan_sha256':plan_hash,'scenario':c,'rep':rep,'library':lib,'command':command,'state_before':state,'state_after':system_state(),
      'whole_process_and_validation_seconds':time.monotonic()-started,'result':result,'output_files':file_records,'raw_text_retained':args.keep_output,'preflight':args.preflight}
    record.write_text(json.dumps(value,indent=2)+'\n')
    if not args.keep_output:
        assert output.resolve().is_relative_to(data)
        for file in sorted(output.rglob('*'),key=lambda p:len(p.parts),reverse=True):
            if file.is_file():file.unlink()
            elif file.is_dir():file.rmdir()
        output.rmdir()
    not_delivered=(result['offered']-result['accepted'])/result['offered'] if result['offered'] else 0
    print(f"{index+1}/{len(jobs)} {name}: goodput={result['goodput']:.0f}/s, business_unaccepted={not_delivered:.2%}, P99={result['accepted_latency']['p99_ns_upper']}ns, verified",flush=True)
    if not args.preflight:time.sleep(1)
if not args.preflight:(e/'run_complete.json').write_text(json.dumps({'trials':len(jobs),'plan_sha256':plan_hash,'end_state':system_state()},indent=2)+'\n')
print('Completed',len(jobs),'trials',flush=True)
