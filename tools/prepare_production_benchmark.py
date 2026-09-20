from pathlib import Path
import subprocess,json,hashlib,tarfile,io,shutil
r=Path(__file__).resolve().parents[1];base=r/'build/native-production';base.mkdir(exist_ok=True)
e=r/'docs/validation/native-production-20260920';e.mkdir(parents=True,exist_ok=True)
p=r/'benchmarks/production/native_benchmark.cpp'
if (e/'preregistration.json').exists():
 raise RuntimeError('Results already registered; prepare in a fresh checkout/evidence directory to preserve provenance')
head=subprocess.check_output(['git','-C',str(r.parent/'BqLog'),'rev-parse','HEAD'],text=True).strip()
snapshot=base/'bqlog-source'
if snapshot.exists() or (base/'qlog-source').exists():
 raise RuntimeError('Source snapshots already exist; use a fresh checkout/build directory; existing evidence is never overwritten')
if not snapshot.exists():
 snapshot.mkdir();data=subprocess.check_output(['git','-C',str(r.parent/'BqLog'),'archive',head])
 with tarfile.open(fileobj=io.BytesIO(data)) as t:t.extractall(snapshot,filter='data')
qsource=base/'qlog-source';qsource.mkdir(exist_ok=True)
for name in ['src','include','cmake']:
 shutil.copytree(r/name,qsource/name,dirs_exist_ok=True)
shutil.copy2(r/'CMakeLists.txt',qsource/'CMakeLists.txt')
shutil.copy2(p,base/'native_benchmark.cpp')
commands=[
 ('qlog-configure',['cmake','-S',str(qsource),'-B',str(base/'qlog-build'),'-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=OFF','-DQLOG_ENABLE_DIAGNOSTICS=AUTO','-DQLOG_BUILD_BENCHMARKS=OFF','-DQLOG_BUILD_EXAMPLES=OFF']),
 ('qlog-build',['cmake','--build',str(base/'qlog-build'),'-j','4']),
 ('bqlog-configure',['cmake','-S',str(snapshot/'src'),'-B',str(base/'bqlog-build'),'-DCMAKE_BUILD_TYPE=Release','-DTARGET_PLATFORM=linux','-DBUILD_LIB_TYPE=static_lib']),
 ('bqlog-build',['cmake','--build',str(base/'bqlog-build'),'--target','BqLog','-j','4'])]
for name,cmd in commands:
 proc=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(e/f'{name}.txt').write_text(proc.stdout)
 if proc.returncode:raise RuntimeError(proc.stdout[-4000:])
lib=next((snapshot/'artifacts').rglob('libBqLog.a'))
for library in ['qlog','bqlog']:
 cmd=['g++','-std=c++20','-O3','-DNDEBUG','-Wall','-Wextra','-Wpedantic',str(base/'native_benchmark.cpp'),'-pthread','-o',str(base/(library+'-native'))]
 if library=='qlog':cmd+=['-DQLOG_ENABLE_DIAGNOSTICS=0','-I'+str(qsource/'include'),str(base/'qlog-build/libqlog.a')]
 else:cmd+=['-DNATIVE_BQLOG=1','-DBQ_LOG_STATIC_LIB=1','-I'+str(snapshot/'include'),str(lib),'-ldl']
 commands.append((library+'-harness',cmd))
 proc=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(e/f'{library}-harness.txt').write_text(proc.stdout)
 if proc.returncode:raise RuntimeError(proc.stdout[-5000:])
(e/'build_commands.json').write_text(json.dumps(commands,indent=2)+'\n')
manifest={}
for name,folder in [('qlog',qsource),('bqlog',snapshot)]:
 members=[]
 for sub in (['src','include','cmake'] if name=='qlog' else ['src','include']):
  for path in sorted((folder/sub).rglob('*')):
   if path.is_file():members.append({'path':str(path.relative_to(folder)),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
 if name=='qlog':members.append({'path':'CMakeLists.txt','sha256':hashlib.sha256((folder/'CMakeLists.txt').read_bytes()).hexdigest()})
 digest=hashlib.sha256(json.dumps(members,sort_keys=True).encode()).hexdigest()
 manifest[name]={'content_id':digest,'files':members}
manifest['bqlog']['git_commit']=head
manifest['qlog']['base_commit']=subprocess.check_output(['git','-C',str(r),'rev-parse','HEAD'],text=True).strip()
manifest['harness_sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
manifest['binaries']={n:hashlib.sha256((base/(n+'-native')).read_bytes()).hexdigest() for n in ['qlog','bqlog']}
(e/'source_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Frozen native source builds ready; production implementations unchanged')
