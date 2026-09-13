from pathlib import Path
import subprocess,json,hashlib
r=Path(__file__).resolve().parents[1];o=r/'build/validation/i1d-acceptance/codegen';o.mkdir(parents=True,exist_ok=True)
baseline='c108301bacb0fa943299b14adbaa8c513bb2fa0e'
before=subprocess.check_output(['git','show',baseline+':src/format_hash_software.cpp'],cwd=r)
(o/'before.cpp').write_bytes(before)
results=[]
for compiler in ['g++','clang++-18']:
    outputs=[]
    for name,source in [('before',o/'before.cpp'),('after',r/'src/format_hash_software.cpp')]:
        obj=o/(compiler+'-'+name+'.o')
        cmd=[compiler,'-std=c++20','-O3','-DNDEBUG','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow','-Werror','-I'+str(r/'include'),'-I'+str(r/'src'),'-c',str(source),'-o',str(obj)]
        subprocess.run(cmd,check=True)
        disassembly=subprocess.check_output(['objdump','-dr',str(obj)],text=True)
        disassembly='\n'.join(line for line in disassembly.splitlines() if 'file format' not in line)
        constants=subprocess.check_output(['readelf','-x','.rodata',str(obj)],text=True)
        (o/(compiler+'-'+name+'.asm')).write_text(disassembly)
        (o/(compiler+'-'+name+'-constants.txt')).write_text(constants)
        outputs.append((disassembly,constants))
    result={'compiler':compiler,'instruction_and_relocation_identical':outputs[0][0]==outputs[1][0],'rodata_identical':outputs[0][1]==outputs[1][1]};results.append(result)
    for source in ['benchmarks/record_core_codegen_probe.cpp','src/record_decoder.cpp']:
        subprocess.run([compiler,'-std=c++20','-O3','-DNDEBUG','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow','-Werror','-I'+str(r/'include'),'-S',str(r/source),'-o',str(o/(compiler+'-'+Path(source).stem+'.s'))],check=True)
(o/'results.json').write_text(json.dumps({'baseline':baseline,'results':results},indent=2))
print(json.dumps(results,indent=2));raise SystemExit(0 if all(v['instruction_and_relocation_identical'] and v['rodata_identical'] for v in results) else 1)
