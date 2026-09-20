#!/usr/bin/env python3
"""Package audited results and exact source inputs; does not commit or upload."""
from pathlib import Path
import argparse,gzip,hashlib,io,json,subprocess,tarfile
R=Path(__file__).resolve().parents[1]
E=R/'docs/validation/native-production-20260920'
parser=argparse.ArgumentParser();parser.add_argument('--sources-only',action='store_true');args=parser.parse_args()
manifest=json.loads((E/'source_manifest.json').read_text())
sources=E/'sources';sources.mkdir(exist_ok=True)
def normalized(info):
    info.uid=info.gid=0;info.uname=info.gname='';info.mtime=0
    return info
def source_archive(library):
    target=sources/(library+'-source.tar.gz')
    if target.exists():return
    with target.open('wb') as raw,gzip.GzipFile(fileobj=raw,mode='wb',mtime=0,filename='') as compressed:
        if library=='bqlog':
            data=subprocess.check_output(['git','-C',str(R.parent/'BqLog'),'archive',manifest['bqlog']['git_commit']])
            compressed.write(data)
        else:
            root=R/'build/native-production/qlog-source'
            with tarfile.open(fileobj=compressed,mode='w|') as archive:
                for path in sorted(root.rglob('*')):
                    if path.is_file():archive.add(path,arcname=str(path.relative_to(root)),recursive=False,filter=normalized)
for library in ['qlog','bqlog']:source_archive(library)
archives={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(sources.glob('*.tar.gz'))}
(sources/'archives.json').write_text(json.dumps(archives,indent=2)+'\n')
if args.sources_only:
    print('Source archives created:',archives)
    raise SystemExit(0)
assert json.loads((E/'audit.json').read_text())['passed']
assert json.loads((E/'reproduction_check.json').read_text())['passed']
report=R/'docs/decisions/V1_NATIVE_PRODUCTION_PERFORMANCE_20260920_CHS.md'
assert report.exists()
script_names=['prepare_production_benchmark.py','run_production_benchmark.py','analyze_production_benchmark.py',
    'render_production_report.py','reproduce_production_benchmark.py','package_production_benchmark.py','test_production_evidence_audit.py']
members=[p for p in sorted(E.rglob('*')) if p.is_file() and p.name not in ['SHA256SUMS.txt','delivery.json']]
members += [p for p in sorted((R/'benchmarks/production').rglob('*')) if p.is_file()]
members += [R/'tools'/name for name in script_names]+[report]
checksums=''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(R))+'\n' for p in members)
(E/'SHA256SUMS.txt').write_text(checksums)
members.append(E/'SHA256SUMS.txt')
destination=R/'build/release-artifacts';destination.mkdir(exist_ok=True)
package=destination/'qlog-bqlog-native-performance-20260920.tar.gz'
with package.open('wb') as raw,gzip.GzipFile(fileobj=raw,mode='wb',mtime=0,filename='') as compressed:
    with tarfile.open(fileobj=compressed,mode='w|') as archive:
        for p in members:archive.add(p,arcname='qlog-native-performance/'+str(p.relative_to(R)),recursive=False,filter=normalized)
delivery={'package':str(package),'bytes':package.stat().st_size,'sha256':hashlib.sha256(package.read_bytes()).hexdigest(),
    'file_count':len(members),'uploaded':False,'git_commit_created':False,
    'extract_and_verify':'tar -xzf qlog-bqlog-native-performance-20260920.tar.gz; cd qlog-native-performance; sha256sum -c docs/validation/native-production-20260920/SHA256SUMS.txt'}
(E/'delivery.json').write_text(json.dumps(delivery,indent=2)+'\n')
print(json.dumps(delivery,indent=2))
