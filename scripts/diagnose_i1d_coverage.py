from pathlib import Path
import subprocess,re,json
r=Path(__file__).resolve().parents[1];o=r/'build/validation/i1d-acceptance/coverage';b=r/'build/test/i1d-acceptance-clang-coverage/tests'
names=['qlog_argument_model_test','qlog_format_hash_test','qlog_record_codec_test','qlog_record_property_test','qlog_record_allocation_test']
cmd=['llvm-cov-18','show',str(b/names[0])]+[v for n in names[1:] for v in ['-object',str(b/n)]]+['-instr-profile='+str(o/'merged.profdata'),'--dump',str(r/'include/qlog/detail/checked_size.hpp')]
p=subprocess.run(cmd,capture_output=True,text=True,check=True)
(o/'mismatch-diagnostic.txt').write_text(p.stdout+p.stderr)
hashes=re.findall(r'hash = (0x[0-9a-fA-F]+)',p.stderr)
result={'mismatch_count':len(hashes),'all_hash_zero':bool(hashes) and all(int(h,16)==0 for h in hashes),'note':'Unused zero-hash inline mappings are retained in the diagnostic. Numerical totals are unfiltered llvm-cov output.'}
(o/'mismatch-summary.json').write_text(json.dumps(result,indent=2));print(result)
if not result['all_hash_zero']:raise SystemExit(1)
