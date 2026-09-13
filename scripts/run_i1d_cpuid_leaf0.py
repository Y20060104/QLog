from pathlib import Path
import subprocess,os,json,shutil,hashlib
r=Path(__file__).resolve().parents[1];o=r/'build/validation/i1d-acceptance/cpuid-leaf0';o.mkdir(parents=True,exist_ok=True)
src=r/'tests/probes/cpuid_leaf0_probe.cpp';shutil.copy2(src,o/src.name)
flags=['-std=c++20','-O2','-fno-builtin','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-pie','-I'+str(r/'include'),'-I'+str(r/'src'),'-DQLOG_HAS_X86_CRC32C=1']
sources=[o/src.name,r/'src/format_hash.cpp',r/'src/format_hash_software.cpp',r/'src/format_hash_x86_crc32c.cpp'];objects=[];commands=[]
with (o/'build.log').open('w') as log:
    for source in sources:
        obj=o/(source.stem+'.o');objects.append(str(obj));cmd=['g++']+flags+(['-msse4.2'] if 'x86_crc32c' in source.name else [])+['-c',str(source),'-o',str(obj)];commands.append(cmd);subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
    cmd=['g++','-nostdlib','-static','-no-pie']+objects+['-o',str(o/'probe')];commands.append(cmd);subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
env=os.environ.copy();env['LD_LIBRARY_PATH']=str(r/'build/tools/qemu/root/usr/lib/x86_64-linux-gnu')
cmd=[str(r/'build/tools/qemu/root/usr/bin/qemu-x86_64'),'-cpu','max,level=0',str(o/'probe')];commands.append(cmd)
p=subprocess.run(cmd,env=env,capture_output=True,text=True);(o/'run.log').write_text(p.stdout+p.stderr)
(o/'results.json').write_text(json.dumps({'commands':commands,'returncode':p.returncode,'source_sha256':{str(source.relative_to(r)):hashlib.sha256(source.read_bytes()).hexdigest() for source in sources},'scope':'production CPUID guard and dispatch, freestanding probe; no libc, no coverage instrumentation'},indent=2))
print('CPUID leaf 0 probe exit:',p.returncode);raise SystemExit(p.returncode)
