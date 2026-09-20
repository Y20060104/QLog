from pathlib import Path
import subprocess, concurrent.futures, json
r=Path(__file__).resolve().parents[1];out=r/'docs/validation/v1-final';out.mkdir(parents=True,exist_ok=True)
def run_case(case):
 config,mode=case;b=r/f'build/diagnostics-{config}-{mode}'
 commands=[['cmake','-S',str(r),'-B',str(b),f'-DCMAKE_BUILD_TYPE={config}',f'-DQLOG_ENABLE_DIAGNOSTICS={mode}',
 '-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST='+str(r/'build/_deps/googletest-src')],
 ['cmake','--build',str(b),'--target','qlog_producer_diagnostics_test','qlog_producer_public_probe',
  'qlog_backend_session_test','qlog_backend_allocation_test','qlog_worker_boundary_test','qlog_worker_runtime_test','qlog_worker_allocation_test','-j','2'],
 ['ctest','--test-dir',str(b),'-L','backend|diagnostics|worker','--output-on-failure']]
 for stage,cmd in zip(['configure','build','test'],commands):
  p=subprocess.run(cmd,cwd=r,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  (out/f'matrix-{config}-{mode}-{stage}.log').write_text(p.stdout)
  if p.returncode:raise RuntimeError((config,mode,stage,p.stdout[-3000:]))
 expected=mode=='ON' or (config=='Debug' and mode=='AUTO')
 symbols=subprocess.check_output(['nm','-C',str(b/'libqlog.a')],text=True)
 assert ('qlog::AsyncLogger::record_call_result(' in symbols)==expected
 pre=subprocess.check_output(['g++','-E','-P','-std=c++20',f'-DQLOG_ENABLE_DIAGNOSTICS={int(expected)}',
 '-I'+str(r/'include'),'-x','c++','-'],input='#include "qlog/detail/backend_session.hpp"\n',text=True)
 assert ('struct BackendDiagnostics final' in pre)==expected
 print(f'PASS {config}/{mode}: backend + diagnostics + worker, fields and producer symbols',flush=True)
 return {'config':config,'mode':mode,'enabled':expected,'passed':True}
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
 results=list(pool.map(run_case,[(c,m) for c in ['Debug','Release'] for m in ['AUTO','ON','OFF']]))
(out/'diagnostics-matrix.json').write_text(json.dumps(results,indent=2)+'\n')
