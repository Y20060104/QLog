from pathlib import Path
import os,subprocess,json
r=Path(__file__).resolve().parents[1]
o=r/'build/validation/i1d-acceptance/cpu-fallback';o.mkdir(parents=True,exist_ok=True)
qemu=r/'build/tools/qemu/root/usr/bin/qemu-x86_64'
if not qemu.exists():raise SystemExit('QEMU user tool missing; see acceptance report for local-package setup')
env=os.environ.copy();env['LD_LIBRARY_PATH']=str(r/'build/tools/qemu/root/usr/lib/x86_64-linux-gnu')
probe=o/'probe.cpp';probe.write_text('''#include <cstdio>
#include <qlog/detail/format_hash.hpp>
#include "format_hash_core.hpp"
int main(){
 if(qlog::detail::hash_impl::x86_crc32c_available())return 1;
 if(qlog::detail::FormatHashDispatch::automatic().backend()!=qlog::detail::HashBackend::software)return 2;
 std::puts("compiled hardware backend; CPUID SSE4.2 absent; automatic=software");return 0;
}
''')
subprocess.run(['g++','-std=c++20','-I'+str(r/'include'),'-I'+str(r/'src'),str(probe),str(r/'build/test/i1d-gcc-debug/libqlog.a'),'-o',str(o/'probe')],check=True)
with (o/'probe.log').open('w') as log:subprocess.run([str(qemu),'-cpu','max,-sse4.2',str(o/'probe')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
b=r/'build/test/i1d-acceptance-clang-coverage'
env['LLVM_PROFILE_FILE']=str(r/'build/validation/i1d-acceptance/coverage/raw/qemu-%m-%p.profraw')
cmd=[str(qemu),'-cpu','max,-sse4.2',str(b/'tests/qlog_format_hash_test'),'--gtest_filter=FormatHashDispatch.*:*FrozenVectors*:*EmptyNullAndCheckedFailurePriority*','--gtest_output=xml:'+str(o/'tests.xml')]
with (o/'tests.log').open('w') as log:subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
(o/'commands.json').write_text(json.dumps({'command':cmd,'cpu':'max,-sse4.2','environment':'QEMU user mode on WSL; not native old hardware'},indent=2))
print((o/'probe.log').read_text());print((o/'tests.log').read_text()[-2500:])
