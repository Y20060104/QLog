from pathlib import Path
import argparse,subprocess,shutil,concurrent.futures,json,time,os
ROOT=Path(__file__).resolve().parents[1]
if not (ROOT/'include/qlog').exists(): ROOT=Path('/home/qq344/QLog')
ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=int,default=600);ap.add_argument('--output',default='fuzz');args=ap.parse_args()
b=ROOT/'build/test/i1d-clang-fuzz';o=ROOT/'build/validation/i1d'/args.output;o.mkdir(parents=True,exist_ok=True)
flags='-O1 -g -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
config=['cmake','-S',str(ROOT),'-B',str(b),'-G','Ninja','-DCMAKE_CXX_COMPILER=clang++-18','-DCMAKE_BUILD_TYPE=Debug','-DQLOG_BUILD_FUZZERS=ON','-DCMAKE_CXX_FLAGS='+flags,'-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST='+str(ROOT/'build/test/debug/_deps/googletest-src')]
config=[x for x in config if not x.startswith('-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=') or Path(x.split('=',1)[1]).exists()]
names=['record_decode','record_roundtrip','format_hash_equivalence','cstr_length_cache']
with (o/'build.log').open('w') as log:
    subprocess.run(config,stdout=log,stderr=subprocess.STDOUT,check=True)
    subprocess.run(['cmake','--build',str(b),'--parallel','4','--target']+['qlog_fuzz_'+n for n in names],stdout=log,stderr=subprocess.STDOUT,check=True)
def run(name):
    c=o/name;c.mkdir(exist_ok=True)
    for p in (ROOT/'tests/corpus/record_core').iterdir():shutil.copy2(p,c/p.name)
    artifacts=o/(name+'-artifacts');artifacts.mkdir(exist_ok=True)
    cmd=[str(b/'tests'/('qlog_fuzz_'+name)),str(c),'-max_total_time='+str(args.seconds+2),'-max_len=65536','-timeout=10','-rss_limit_mb=2048','-artifact_prefix='+str(artifacts)+'/', '-seed=20260912']
    env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
    start=time.monotonic()
    with (o/(name+'.log')).open('w') as log:
        log.write(repr(cmd)+'\n');log.flush()
        result=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,env=env)
    status={'target':name,'code':result.returncode,'seconds':round(time.monotonic()-start,2)}
    print(status,flush=True);return status
print('Starting four fuzz targets',flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:results=list(pool.map(run,names))
(o/'results.json').write_text(json.dumps(results,indent=2))
raise SystemExit(any(x['code'] for x in results))
