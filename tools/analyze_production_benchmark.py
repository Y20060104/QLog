#!/usr/bin/env python3
"""Audit every native trial and calculate the predeclared paired throughput index."""
from pathlib import Path
import csv, hashlib, itertools, json, math, statistics

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / 'docs/validation/native-production-20260920'
PLAN = json.loads((ROOT / 'benchmarks/production/plan.json').read_text())
SCHEDULE = json.loads((EVIDENCE / 'schedule.json').read_text())

def upper(index):
    if index < 64:
        return index
    exponent = index // 64 + 5
    return (1 << exponent) + ((index % 64 + 1) << (exponent - 6)) - 1

def percentile(values, fraction):
    values = sorted(values)
    position = (len(values) - 1) * fraction
    lower = int(position)
    return values[lower] + (values[min(lower + 1, len(values) - 1)] - values[lower]) * (position - lower)

def paired_summary(log_ratios):
    # Exact empirical bootstrap: enumerate all 5**5 resamples instead of Monte Carlo.
    boot = [math.exp(statistics.mean(sample)) for sample in itertools.product(log_ratios, repeat=len(log_ratios))]
    return {'ratio': math.exp(statistics.mean(log_ratios)),
            'ci95': [percentile(boot, .025), percentile(boot, .975)],
            'paired_ratios': [math.exp(x) for x in log_ratios], 'bootstrap_resamples': len(boot)}

def audit(record):
    x = record['result']
    assert x['verified'] and x['errors'] == 0
    assert x['accepted'] + x['rejected'] == x['attempted'] <= x['offered']
    assert x['offered'] - x['attempted'] == x['not_attempted_by_deadline']
    assert sum(pair[1]['count'] for pair in x['producer_digests']) == x['accepted']
    assert x['end_to_end_seconds'] >= x['measure_seconds']
    assert math.isclose(x['goodput'], x['accepted'] / x['end_to_end_seconds'], rel_tol=2e-5)
    assert math.isclose(x['drain_ms'], (x['end_to_end_seconds'] - x['producer_seconds']) * 1000, abs_tol=.11)
    for field in ['accepted_latency', 'rejected_latency', 'scheduled_latency']:
        h = x[field]
        assert sum(count for _, count in h['bins']) == h['samples']
        assert all(0 <= index < 4096 and count > 0 for index, count in h['bins'])
        assert h['bins'] == sorted(h['bins'])
        for name, q in [('p50_ns_upper', .5), ('p95_ns_upper', .95), ('p99_ns_upper', .99), ('p999_ns_upper', .999)]:
            seen = 0
            result = 0
            for index, count in h['bins']:
                seen += count
                if seen >= math.ceil(h['samples'] * q):
                    result = upper(index)
                    break
            assert result == h[name], (field, name, result, h[name])
    samples = x['accepted_latency']['samples'] + x['rejected_latency']['samples']
    assert x['attempted'] / 1021 <= samples <= x['attempted'] / 1021 + x['producers']
    assert x['scheduled_latency']['samples'] == (samples if x['offered_rate'] else 0)
    assert len(x['windows']) == x['measure_seconds']
    previous = dict(seconds=0, accepted=0, attempted=0, file_bytes=0)
    for window in x['windows']:
        assert all(window[k] >= previous[k] for k in previous)
        assert window['accepted'] <= x['accepted'] and window['attempted'] <= x['attempted']
        assert window['file_bytes'] <= x['output_bytes']
        previous = window
    assert x['cpu_seconds'] > 0 and x['max_rss_kib'] >= x['baseline_max_rss_kib']
    assert sum(f['bytes'] for f in record['output_files']) > x['output_bytes']

def main():
    assert (EVIDENCE / 'run_complete.json').exists(), 'Suite is not complete'
    assert not list((EVIDENCE / 'trials').glob('*.failure.*'))
    frozen = json.loads((EVIDENCE / 'preregistration.json').read_text())
    assert hashlib.sha256((ROOT / 'benchmarks/production/plan.json').read_bytes()).hexdigest() == frozen['plan_sha256']
    for name, digest in json.loads((EVIDENCE / 'protocol_files.json').read_text()).items():
        assert hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == digest, name
    manifest = json.loads((EVIDENCE / 'source_manifest.json').read_text())
    for lib in ['qlog', 'bqlog']:
        source = ROOT / 'build/native-production' / (lib + '-source')
        for item in manifest[lib]['files']:
            assert hashlib.sha256((source / item['path']).read_bytes()).hexdigest() == item['sha256']
            if lib == 'qlog':
                assert hashlib.sha256((ROOT / item['path']).read_bytes()).hexdigest() == item['sha256'], 'Production source changed during run'
    records = {}
    for item in SCHEDULE:
        name = f"{item['scenario']}-{item['rep']}-{item['library']}"
        record = json.loads((EVIDENCE / 'trials' / (name + '.json')).read_text())
        assert record['plan_sha256'] == frozen['plan_sha256'] and not record['preflight']
        x = record['result']
        assert x['warmup_seconds'] == item['warmup'] and x['measure_seconds'] == item['seconds']
        assert x['library'] == item['library'] == record['library']
        audit(record)
        records[(item['scenario'], item['rep'], item['library'])] = record
    assert len(records) == len(SCHEDULE) == 102
    rows = []
    for (scenario, rep, lib), record in records.items():
        x = record['result']
        rows.append(dict(scenario=scenario, repetition=rep, library=lib,
            goodput=x['goodput'], accepted=x['accepted'], attempted=x['attempted'], rejected=x['rejected'],
            offered=x['offered'], not_attempted=x['not_attempted_by_deadline'],
            unaccepted_percent=100 * (x['offered'] - x['accepted']) / x['offered'],
            cpu_seconds=x['cpu_seconds'], cpu_us_per_accepted=1e6 * x['cpu_seconds'] / x['accepted'],
            accepted_p99_ns_upper=x['accepted_latency']['p99_ns_upper'],
            accepted_p999_ns_upper=x['accepted_latency']['p999_ns_upper'], accepted_max_ns=x['accepted_latency']['max_ns'],
            rejected_p99_ns_upper=x['rejected_latency']['p99_ns_upper'],
            scheduled_p99_ns_upper=x['scheduled_latency']['p99_ns_upper'],
            scheduled_p999_ns_upper=x['scheduled_latency']['p999_ns_upper'], scheduled_max_ns=x['scheduled_latency']['max_ns'],
            rss_mib=x['max_rss_kib']/1024, drain_ms=x['drain_ms'],
            cold_start_us=x['cold_start_us'], output_mib_per_second=x['output_bytes']/x['end_to_end_seconds']/1048576,
            bytes_per_accepted=x['output_bytes']/x['accepted'], end_to_end_seconds=x['end_to_end_seconds']))
    with (EVIDENCE / 'trials.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    cases = {}
    for scenario in PLAN['scenarios']:
        name = scenario['id']
        pair = [math.log(records[(name, rep, 'qlog')]['result']['goodput'] / records[(name, rep, 'bqlog')]['result']['goodput']) for rep in range(5)]
        cases[name] = {'primary':scenario['primary'], 'paired_goodput':paired_summary(pair)}
        for lib in ['qlog','bqlog']:
            selected = [row for row in rows if row['scenario']==name and row['library']==lib]
            numeric = [k for k in selected[0] if k not in ['scenario','repetition','library']]
            cases[name][lib] = {k:statistics.median(row[k] for row in selected) for k in numeric}
            cases[name][lib]['unaccepted_percent_min_max'] = [min(row['unaccepted_percent'] for row in selected), max(row['unaccepted_percent'] for row in selected)]
            cases[name][lib]['total_offered'] = sum(row['offered'] for row in selected)
            cases[name][lib]['total_accepted'] = sum(row['accepted'] for row in selected)
            cases[name][lib]['total_rejected'] = sum(row['rejected'] for row in selected)
            cases[name][lib]['total_not_attempted'] = sum(row['not_attempted'] for row in selected)
            cases[name][lib]['aggregate_unaccepted_percent'] = 100 * (1 - cases[name][lib]['total_accepted']/cases[name][lib]['total_offered'])
            cases[name][lib]['accepted_max_observed_ns'] = max(row['accepted_max_ns'] for row in selected)
            cases[name][lib]['scheduled_max_observed_ns'] = max(row['scheduled_max_ns'] for row in selected)
            cases[name][lib]['goodput_min_max'] = [min(row['goodput'] for row in selected), max(row['goodput'] for row in selected)]
    primary = [c['id'] for c in PLAN['scenarios'] if c['primary']]
    block_means = [statistics.mean(math.log(cases[name]['paired_goodput']['paired_ratios'][rep]) for name in primary) for rep in range(5)]
    summary = {'overall':paired_summary(block_means),'scenarios':cases,
        'soak':[row for row in rows if row['scenario']=='soak-mixed-4'],
        'trial_count':len(rows),'accepted_measured_total':sum(row['accepted'] for row in rows),
        'verified_lines_including_warmup_and_fanout':sum(sum(d['count'] for pair in v['result']['producer_digests'] for d in pair)*v['result']['targets'] for v in records.values()),
        'generated_output_bytes':sum(sum(f['bytes'] for f in v['output_files']) for v in records.values()),
        'statistics':'Equal-weight paired log ratios; exact empirical block bootstrap of the five repetitions (3125 resamples), percentile 95% interval; no multiplicity correction; only five repetition blocks, assumed exchangeable but independence not established; does not cover host-to-host uncertainty.'}
    (EVIDENCE/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    (EVIDENCE/'audit.json').write_text(json.dumps({'passed':True,'trials':102,'checks':['plan/protocol/source hashes','all expected trials and no failure files','delivery counters and producer digests','all histogram bin counts and quantiles','latency sampling counts','monotone one-second progress','timing and throughput arithmetic','output file byte accounting']},indent=2)+'\n')
    print(json.dumps({'overall':summary['overall'],'trials':102,'verified_lines':summary['verified_lines_including_warmup_and_fanout']},indent=2))

if __name__ == '__main__':
    main()
