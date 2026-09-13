from pathlib import Path
import subprocess,concurrent.futures,json,os
r=Path(__file__).resolve().parents[1];out=r/'build/validation/i1d-acceptance/matrix';out.mkdir(parents=True,exist_ok=True)
variants=[('gcc-debug','g++','Debug',False,True),('gcc-release','g++','Release',False,True),('clang-debug','clang++-18','Debug',False,True),('clang-release','clang++-18','Release',False,True),('clang-asan-ubsan','clang++-18','Debug',True,True),('gcc-software-release','g++','Release',False,False)]
def run(v):
    name,compiler,mode,san,hw=v;b=r/'build/test'/('i1d-'+name)
    flags='-Werror'+(' -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' if san else '')
    cmds=[['cmake','-S',str(r),'-B',str(b),'-G','Ninja','-DCMAKE_BUILD_TYPE='+mode,'-DCMAKE_CXX_COMPILER='+compiler,'-DCMAKE_CXX_FLAGS='+flags,'-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DQLOG_ENABLE_X86_CRC32C='+('ON' if hw else 'OFF'),'-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST='+str(r/'build/test/debug/_deps/googletest-src')],['cmake','--build',str(b),'--parallel','3'],['ctest','--test-dir',str(b),'--output-on-failure','--no-tests=error','--parallel','3','--output-junit',str(out/(name+'.xml'))]]
    cmds[0]=[x for x in cmds[0] if not x.startswith('-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=') or Path(x.split('=',1)[1]).exists()]
    env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
    with (out/(name+'.log')).open('w') as log:
        for cmd in cmds:
            log.write(repr(cmd)+'\n');log.flush();p=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
            if p.returncode:
                print(name,'FAIL',flush=True);return {'name':name,'code':p.returncode,'command':cmd}
    print(name,'PASS',flush=True);return {'name':name,'code':0}
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:results=list(pool.map(run,variants))
(out/'results.json').write_text(json.dumps(results,indent=2));raise SystemExit(any(x['code'] for x in results))
