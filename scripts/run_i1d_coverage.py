from pathlib import Path
import subprocess,os,json
r=Path(__file__).resolve().parents[1];b=r/'build/test/i1d-clang-coverage';o=r/'build/validation/i1d/coverage';o.mkdir(parents=True,exist_ok=True)
with (o/'run.log').open('w') as log:
    def run(cmd,env=None):
        cmd=[x for x in cmd if not x.startswith('-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=') or Path(x.split('=',1)[1]).exists()]
        log.write(repr(cmd)+'\n');log.flush();subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    run(['cmake','-S',str(r),'-B',str(b),'-G','Ninja','-DCMAKE_CXX_COMPILER=clang++-18','-DCMAKE_BUILD_TYPE=Debug','-DCMAKE_CXX_FLAGS=-O0 -g -fprofile-instr-generate -fcoverage-mapping','-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST='+str(r/'build/test/debug/_deps/googletest-src')])
    run(['cmake','--build',str(b),'--parallel','4'])
    raw=o/'raw';raw.mkdir(exist_ok=True)
    for old in raw.glob('*.profraw'):old.unlink()
    env=os.environ.copy();env['LLVM_PROFILE_FILE']=str(raw/'%m-%p.profraw')
    run(['ctest','--test-dir',str(b),'-L','record_core','--output-on-failure','--no-tests=error','--parallel','4'],env)
    profile=o/'merged.profdata';run(['llvm-profdata-18','merge','-sparse']+[str(p) for p in raw.glob('*.profraw')]+['-o',str(profile)])
    objects=[b/'tests'/name for name in ['qlog_argument_model_test','qlog_format_hash_test','qlog_record_codec_test','qlog_record_property_test']]
    sources=[r/'include/qlog/arguments.hpp']+list((r/'include/qlog/detail').glob('*.hpp'))+list((r/'src').glob('format_hash*'))+[r/'src/record_decoder.cpp']
    sources=[p for p in sources if not any(x in p.name for x in ['spsc','ring','cache_line','geometry'])]
    common=[str(objects[0])]+[v for p in objects[1:] for v in ['-object',str(p)]]+['-instr-profile='+str(profile)]+[str(p) for p in sources]
    with (o/'report.txt').open('w') as f:subprocess.run(['llvm-cov-18','report']+common,stdout=f,check=True)
    with (o/'export.json').open('w') as f:subprocess.run(['llvm-cov-18','export']+common,stdout=f,check=True)
    run(['llvm-cov-18','show']+common+['-format=html','-output-dir='+str(o/'html'),'-show-branches=count'])
print((o/'report.txt').read_text(),flush=True)
