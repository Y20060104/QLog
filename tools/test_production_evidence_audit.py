#!/usr/bin/env python3
"""Negative controls for the report audit, using an actual preflight record."""
from pathlib import Path
import copy, importlib.util, json
r=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('audit',r/'tools/analyze_production_benchmark.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
e=r/'docs/validation/native-production-20260920'
records=[json.loads(p.read_text()) for p in sorted((e/'preflight').glob('*.json')) if '.failure.' not in p.name]
assert len(records)==14
for record in records:module.audit(record)
reference=next(x for x in records if x['scenario']['id']=='small-1' and x['library']=='qlog')
def detects(name,mutate):
    damaged=copy.deepcopy(reference);mutate(damaged['result'])
    try:module.audit(damaged)
    except AssertionError:return name
    raise AssertionError('Failed to detect corrupted evidence: '+name)
tests=[
    detects('accepted count corruption',lambda x:x.update(accepted=x['accepted']+1)),
    detects('throughput denominator corruption',lambda x:x.update(goodput=x['goodput']*1.1)),
    detects('producer count corruption',lambda x:x['producer_digests'][0][1].update(count=0)),
    detects('histogram quantile corruption',lambda x:x['accepted_latency'].update(p99_ns_upper=0)),
    detects('histogram sample count corruption',lambda x:x['accepted_latency'].update(samples=0)),
    detects('progress beyond final accepted count',lambda x:x['windows'][0].update(accepted=x['accepted']+1)),
    detects('unattempted request count corruption',lambda x:x.update(not_attempted_by_deadline=1)),
]
(e/'audit_negative_controls.json').write_text(json.dumps({'passed':True,'valid_preflight_records':len(records),'detected_corruptions':tests},indent=2)+'\n')
print('Audit controls passed: 14 valid records and',len(tests),'deliberate corruptions')
