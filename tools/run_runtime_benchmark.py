#!/usr/bin/env python3
"""Reproducible QLog/BQLog V1 public logging benchmark. No destructive cleanup."""
import argparse,datetime,hashlib,json,os,platform,re,statistics,subprocess
from pathlib import Path

def command(args,cwd=None):
    return subprocess.check_output(args,cwd=cwd,text=True,stderr=subprocess.STDOUT).strip()

def audit_text(directory,expected,threads,records):
    seen=[bytearray(records) for _ in range(threads)]
    previous=[-1]*threads
    count=0;size=0
    pattern=re.compile(rb'record p=(\d+) seq=(\d+) a=123456 text=abcdefghijklmnopqrstuvwxyz012345$')
    for path in sorted(directory.glob('*.log')):
        size+=path.stat().st_size
        with path.open('rb') as f:
            for line in f:
                if b'record p=' not in line:continue
                match=pattern.search(line.rstrip(b'\r\n'))
                if not match:raise RuntimeError(f'invalid body in {path}')
                t,i=map(int,match.groups())
                if not(0<=t<threads and 0<=i<records):raise RuntimeError('body index out of range')
                if seen[t][i] or i<=previous[t]:raise RuntimeError('duplicate or out-of-order producer sequence')
                seen[t][i]=1;previous[t]=i;count+=1
    if count!=expected or any(not all(row) for row in seen):
        raise RuntimeError(f'text audit: expected={expected}, observed={count}')
    return {'verified_text_records':count,'verified_file_bytes':size,'per_producer_order':'passed'}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--qlog-exe',type=Path,required=True)
    parser.add_argument('--bqlog-exe',type=Path,required=True)
    parser.add_argument('--evidence-dir',type=Path,required=True)
    parser.add_argument('--data-root',type=Path,required=True)
    parser.add_argument('--records-one',type=int,default=200000)
    parser.add_argument('--records-many',type=int,default=100000)
    parser.add_argument('--repeats',type=int,default=3)
    args=parser.parse_args()
    args.data_root.mkdir(parents=True,exist_ok=False)
    args.evidence_dir.mkdir(parents=True,exist_ok=True)
    repository=Path(__file__).resolve().parents[1]
    manifest={str(p.relative_to(repository)):hashlib.sha256(p.read_bytes()).hexdigest()
              for directory in ['include','src','benchmarks'] for p in (repository/directory).rglob('*') if p.is_file()}
    manifest['CMakeLists.txt']=hashlib.sha256((repository/'CMakeLists.txt').read_bytes()).hexdigest()
    metadata={'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'platform':platform.platform(),'python':platform.python_version(),'cpu_count':os.cpu_count(),
        'affinity':sorted(os.sched_getaffinity(0)),'lscpu':command(['lscpu','-J']),
        'compiler':command(['g++','--version']),'kernel':command(['uname','-a']),
        'qlog_head':command(['git','rev-parse','HEAD'],repository),
        'qlog_status':command(['git','status','--short'],repository),
        'bqlog_reference':'78cbfbef4f87e7558335f0c04bf6dd6894b46451',
        'source_manifest':manifest,'binary_sha256':{name:hashlib.sha256(path.read_bytes()).hexdigest()
            for name,path in [('qlog',args.qlog_exe),('bqlog',args.bqlog_exe)]},
        'measurement':'block policy; 64 KiB LP/cache; every 256th call latency; HP 1.1s same-thread warm-up; joins include producer TLS retirement; force_flush drains to OS cache, no durability claim'}
    (args.evidence_dir/'environment.json').write_text(json.dumps(metadata,indent=2))
    cases=[('filtered','async','console',1,'filtered'),('async_console_1_lp','async','console',1,'lp'),
      ('async_console_4_lp','async','console',4,'lp'),('independent_console_1_hp','independent','console',1,'hp'),
      ('independent_console_4_hp','independent','console',4,'hp'),('sync_console_1','sync','console',1,'lp'),
      ('async_text_1_lp','async','text',1,'lp'),('async_text_4_lp','async','text',4,'lp'),
      ('independent_text_4_hp','independent','text',4,'hp')]
    results=[]
    for label,mode,sink,threads,profile in cases:
        records=args.records_one if threads==1 else args.records_many
        for repeat in range(args.repeats):
            order=[('qlog',args.qlog_exe),('bqlog',args.bqlog_exe)]
            if repeat%2:order.reverse()
            for backend,exe in order:
                directory=args.data_root/f'{label}-{repeat}-{backend}'
                cmd=[str(exe.resolve()),mode,sink,str(threads),str(records),profile,str(directory)]
                proc=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=120)
                (args.evidence_dir/f'{label}-{repeat}-{backend}.log').write_text(proc.stdout)
                line=next((x for x in proc.stdout.splitlines() if x.startswith('BENCH_JSON ')),None)
                if proc.returncode or not line:raise RuntimeError(f'{label}/{backend} failed: {proc.stdout[-2000:]}')
                obj=json.loads(line.removeprefix('BENCH_JSON '))
                assert obj['attempted']==obj['accepted']+obj['rejected']
                row={'case':label,'mode':mode,'sink':sink,'threads':threads,'profile':profile,
                     'repeat':repeat,'backend':backend,'records_per_thread':records,'command':cmd,**obj}
                if sink=='text':row.update(audit_text(directory,obj['accepted'],threads,records))
                results.append(row)
                (args.evidence_dir/'raw-results.json').write_text(json.dumps(results,indent=2))
                print(label,repeat,backend,'accepted',obj['accepted'],'producer M/s',round(obj['producer_mps'],3),
                      'drained M/s',round(obj['drained_mps'],3),'p99 ns',obj['p99_ns'],flush=True)
    summary=[]
    fields=['producer_mps','drained_mps','p50_ns','p99_ns','p999_ns','max_sample_ns','cpu_cores_used','peak_rss_kib','flush_seconds','file_bytes']
    for label,mode,sink,threads,profile in cases:
        cell={'case':label,'mode':mode,'sink':sink,'threads':threads,'profile':profile}
        for backend in ['qlog','bqlog']:
            rows=[r for r in results if r['case']==label and r['backend']==backend]
            cell[backend]={k:statistics.median(r[k] for r in rows) for k in fields}
            cell[backend]['drained_mps_min']=min(r['drained_mps'] for r in rows)
            cell[backend]['drained_mps_max']=max(r['drained_mps'] for r in rows)
            cell[backend]['attempted']=sum(r['attempted'] for r in rows)
            cell[backend]['accepted']=sum(r['accepted'] for r in rows)
            cell[backend]['rejected']=sum(r['rejected'] for r in rows)
        denominator=cell['bqlog']['drained_mps'] if profile!='filtered' else cell['bqlog']['producer_mps']
        numerator=cell['qlog']['drained_mps'] if profile!='filtered' else cell['qlog']['producer_mps']
        cell['qlog_bqlog_ratio']=numerator/denominator if denominator else None
        summary.append(cell)
    current={str(p.relative_to(repository)):hashlib.sha256(p.read_bytes()).hexdigest()
             for directory in ['include','src','benchmarks'] for p in (repository/directory).rglob('*') if p.is_file()}
    current['CMakeLists.txt']=hashlib.sha256((repository/'CMakeLists.txt').read_bytes()).hexdigest()
    if current!=manifest:raise RuntimeError('production source changed during measurement')
    (args.evidence_dir/'summary.json').write_text(json.dumps(summary,indent=2))
    metadata['completed_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
    metadata['source_drift']=False
    (args.evidence_dir/'environment.json').write_text(json.dumps(metadata,indent=2))
    print('Completed',len(results),'measured trials; source drift absent.',flush=True)
if __name__=='__main__':main()
