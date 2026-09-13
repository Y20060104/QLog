from pathlib import Path
import json,hashlib
r=Path(__file__).resolve().parents[1];o=r/'build/validation/i1d-acceptance/coverage'
d=json.loads((o/'export.json').read_text())['data'][0]
branches={}
for f in d['files']:
    for b in f['branches']:
        key=(f['filename'],*b[:4]);counts=branches.setdefault(key,[0,0]);counts[0]+=b[4];counts[1]+=b[5]
rules={
 ('include/qlog/detail/record_measure.hpp','if (!encoded_size(arguments[index], argument_size)) {'):
   'PreparedRecord normalization admits only valid tags; invalid normalized input cannot be produced by checked measure.',
 ('include/qlog/detail/record_measure.hpp','if (!checked_add(args_size, argument_size, new_args_size)) {'):
   'Tier-1 64-bit size_t: at most 32 arguments, each <= UINT32_MAX+5 bytes; sum < 2^64. checked_add overflow itself is directly tested.',
 ('include/qlog/detail/record_encoder.hpp','switch (error) {'):
   'Internal mapping receives only the five enumerators returned by the shared validator; default terminates on invariant violation.',
 ('include/qlog/detail/record_encoder.hpp','} else if (argument.tag == ArgumentTag::NullUtf8) {'):
   'All admitted tags are exhausted before the terminal defense; private PreparedRecord construction prevents unknown tags.',
 ('src/record_decoder.cpp','switch (error) {'):
   'Internal metadata mapping receives only enumerators emitted by shared validation; payload bytes cannot directly supply this enum.',
}
waived=[];external=[];unexpected=[]
for key,counts in branches.items():
    if all(counts):continue
    path=Path(key[0]);rel=str(path.relative_to(r));line=key[1];text=path.read_text().splitlines()[line-1].strip()
    item={'file':rel,'line':line,'source':text,'counts':counts}
    if (rel,text) in rules:
        item['reason']=rules[(rel,text)];waived.append(item)
    elif rel=='src/format_hash.cpp' and text.startswith('if (__get_cpuid(1U,'):
        result=json.loads((r/'build/validation/i1d-acceptance/cpuid-leaf0/results.json').read_text())
        expected=hashlib.sha256(path.read_bytes()).hexdigest()
        if result['returncode']!=0 or result.get('source_sha256',{}).get(rel)!=expected:
            unexpected.append(item)
        else:
            item['evidence']='cpuid-leaf0/results.json; real production code, no-libc startup; coverage profile unavailable in this probe';external.append(item)
    else:unexpected.append(item)
numeric=all(d['totals'][name]['percent']>=value for name,value in {'functions':100,'lines':95,'branches':90}.items())
result={'passed':numeric and not unexpected,'numerical_pass':numeric,'totals':d['totals'],'documented_unreachable':waived,'external_branch_evidence':external,'unexpected_uncovered':unexpected}
(o/'branch-audit.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2));raise SystemExit(0 if result['passed'] else 1)
