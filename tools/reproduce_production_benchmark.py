#!/usr/bin/env python3
"""Build frozen published sources in a NEW destination; never overwrite a prior run.

Usage: python3 tools/reproduce_production_benchmark.py /absolute/new/directory
Then follow the printed commands. Requires Linux, Python >=3.12, CMake, GCC 13+.
"""
from pathlib import Path
import argparse, hashlib, json, shutil, subprocess, tarfile

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    published = Path(__file__).resolve().parents[1]
    old_evidence = published / 'docs/validation/native-production-20260920'
    destination = args.destination.expanduser().resolve()
    if destination.exists():
        raise SystemExit('Destination already exists. Choose a NEW directory; no deletion is performed.')
    frozen = json.loads((old_evidence/'source_manifest.json').read_text())
    archives = json.loads((old_evidence/'sources/archives.json').read_text())
    for name, digest in archives.items():
        file = old_evidence/'sources'/name
        if hashlib.sha256(file.read_bytes()).hexdigest() != digest:
            raise SystemExit('Source archive checksum mismatch: '+name)
    base = destination/'build/native-production'
    evidence = destination/'docs/validation/native-production-20260920'
    base.mkdir(parents=True); evidence.mkdir(parents=True)
    for lib in ['qlog','bqlog']:
        source = base/(lib+'-source'); source.mkdir()
        with tarfile.open(old_evidence/'sources'/(lib+'-source.tar.gz')) as archive:
            archive.extractall(source, filter='data')
        for member in frozen[lib]['files']:
            if hashlib.sha256((source/member['path']).read_bytes()).hexdigest() != member['sha256']:
                raise SystemExit('Frozen source checksum mismatch: '+member['path'])
    for name in ['src','include','cmake']:
        source_directory = base/'qlog-source'/name
        if source_directory.exists():
            shutil.copytree(source_directory, destination/name)
        else:
            (destination/name).mkdir()  # Archives need not contain empty optional directories.
    shutil.copy2(base/'qlog-source/CMakeLists.txt', destination/'CMakeLists.txt')
    for name in ['benchmarks/production', 'tools']:
        shutil.copytree(published/name, destination/name)
    shutil.copy2(destination/'benchmarks/production/native_benchmark.cpp', base/'native_benchmark.cpp')
    original_commands = json.loads((old_evidence/'build_commands.json').read_text())
    # The original root is derived from the first CMake source argument, not user shell input.
    original_source = Path(original_commands[0][1][2])
    original_root = str(original_source.parents[2])
    commands = []
    for name, command in original_commands:
        relocated = [str(destination)+arg[len(original_root):] if arg.startswith(original_root) else
                     '-I'+str(destination)+arg[len('-I'+original_root):] if arg.startswith('-I'+original_root) else arg
                     for arg in command]
        completed = subprocess.run(relocated, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (evidence/(name+'.txt')).write_text(completed.stdout)
        if completed.returncode:
            raise SystemExit(completed.stdout[-6000:])
        commands.append((name,relocated))
    frozen['binaries'] = {lib:hashlib.sha256((base/(lib+'-native')).read_bytes()).hexdigest() for lib in ['qlog','bqlog']}
    frozen['reproduced_from_source_manifest_sha256'] = hashlib.sha256((old_evidence/'source_manifest.json').read_bytes()).hexdigest()
    (evidence/'source_manifest.json').write_text(json.dumps(frozen,indent=2)+'\n')
    (evidence/'build_commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    shutil.copy2(old_evidence/'protocol_files.json', evidence/'protocol_files.json')
    print('Frozen-source build complete. New results will not overwrite the published reference.')
    print('Working directory:', destination)
    print('python3 tools/run_production_benchmark.py --preflight')
    print('python3 tools/run_production_benchmark.py')
    print('python3 tools/analyze_production_benchmark.py')

if __name__ == '__main__':
    main()
