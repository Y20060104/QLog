#!/usr/bin/env python3
"""Run the aligned upstream suites sequentially in isolated output directories."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import resource
import statistics
import subprocess
import threading
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def manifest(root, folders):
    result = {}
    for folder in folders:
        for path in sorted((root / folder).rglob("*")):
            if path.is_file():
                result[str(path.relative_to(root))] = digest(path)
    if (root / "CMakeLists.txt").exists():
        result["CMakeLists.txt"] = digest(root / "CMakeLists.txt")
    return result


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n")


def parse(text):
    throughput = []
    latency = []
    title = None
    for line in text.splitlines():
        if "Begin " in line:
            title = line.strip("= ")
        match = re.search(r"Time Cost:\s*(\d+)", line)
        if match:
            assert title is not None
            throughput.append({"case": title, "milliseconds": int(match[1]),
                               "mode": "fast" if "BQ_LOG_FAST_INFO" in title else "normal"})
        if "| ns per log |" in line:
            fields = dict((key, float(value)) for key, value in
                          re.findall(r"(50th|75th|90th|95th|99th|99\.9th|worst) ([\d.]+)", line))
            latency.append({"mode": "fast" if "BQ_LOG_FAST_INFO()" in line else "normal",
                            "threads": int(re.search(r"threads (\d+)", line)[1]),
                            "batch_mean_ns_per_log": fields})
    return throughput, latency


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qlog-exe", type=Path, required=True)
    parser.add_argument("--bqlog-exe", type=Path, required=True)
    parser.add_argument("--bqlog-source", type=Path, required=True)
    parser.add_argument("--data-root", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--threads", type=int, nargs="+", default=[1, 4])
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--min-host-free-gib", type=float, default=20.0,
                        help="WSL Windows system-drive reserve; checked in addition to guest space")
    parser.add_argument("--cleanup-output", action="store_true",
                        help="Hash and remove only each completed run output directory after recording results")
    args = parser.parse_args()
    if args.data_root.exists() or args.evidence_dir.exists():
        parser.error("data-root and evidence-dir must be new directories")
    if args.repeats < 1 or any(n < 1 for n in args.threads):
        parser.error("threads and repeats must be positive")
    root = Path(__file__).resolve().parents[2]
    host_volume = Path("/mnt/c") if "microsoft" in platform.release().lower() else None
    if host_volume is not None and not host_volume.is_dir():
        parser.error("WSL host volume /mnt/c is unavailable; cannot check VHD backing space")
    def free_space():
        result = {"guest_bytes": shutil.disk_usage(args.data_root).free}
        if host_volume is not None:
            result["host_bytes"] = shutil.disk_usage(host_volume).free
        return result
    args.data_root.mkdir(parents=True)
    args.evidence_dir.mkdir(parents=True)
    executables = {"qlog": args.qlog_exe.resolve(), "bqlog": args.bqlog_exe.resolve()}
    initial = manifest(root, ["include", "src", "benchmark/cpp"])
    reference = manifest(args.bqlog_source, ["include", "src", "benchmark/cpp"])
    environment = {"started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                   "platform": platform.platform(), "cpu": subprocess.check_output(["lscpu"], text=True),
                   "affinity": sorted(os.sched_getaffinity(0)),
                   "compiler": subprocess.check_output(["g++", "--version"], text=True),
                   "qlog_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
                   "bqlog_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=args.bqlog_source, text=True).strip(),
                   "binary_sha256": {key: digest(path) for key, path in executables.items()},
                   "source_manifest": initial, "reference_manifest": reference,
                   "host_volume": str(host_volume) if host_volume is not None else None,
                   "host_reserve_bytes": int(args.min_host_free_gib * 1024**3),
                   "settings": {"threads": args.threads, "repeats": args.repeats,
                                "logs_per_thread_per_throughput_case": 2000000,
                                "BENCH_POOL_SIZE": 50000, "latency_iterations": 10000,
                                "latency_messages_per_iteration": 20}}
    save(args.evidence_dir / "environment.json", environment)
    raw = []
    child_environment = os.environ.copy()
    child_environment["BENCH_POOL_SIZE"] = "50000"
    for threads in args.threads:
        for repeat in range(args.repeats):
            order = ["qlog", "bqlog"] if repeat % 2 == 0 else ["bqlog", "qlog"]
            for backend in order:
                name = f"t{threads}-r{repeat}-{backend}"
                output = args.data_root / name
                output.mkdir()
                logfile = args.evidence_dir / f"{name}.log"
                command = [str(executables[backend])]
                print(json.dumps({"event": "start", "run": name}), flush=True)
                space_before = free_space()
                if space_before["guest_bytes"] < 8 * 1024**3:
                    raise RuntimeError("Guest filesystem has less than 8 GiB free")
                if space_before.get("host_bytes", float("inf")) < environment["host_reserve_bytes"]:
                    raise RuntimeError("WSL host drive is below the requested free-space reserve")
                before = resource.getrusage(resource.RUSAGE_CHILDREN)
                begin = time.monotonic()
                clock_offset = time.time() - begin
                clock_jumps = [0.0]
                validation_errors = []
                stop_monitor = threading.Event()
                def monitor():
                    while not stop_monitor.wait(1.0):
                        jump = abs((time.time() - time.monotonic()) - clock_offset)
                        clock_jumps.append(jump)
                        space = free_space()
                        if jump > 0.05:
                            validation_errors.append("system_clock shifted relative to monotonic by over 50 ms")
                        if space["guest_bytes"] < 8 * 1024**3:
                            validation_errors.append("guest free space fell below 8 GiB")
                        if space.get("host_bytes", float("inf")) < environment["host_reserve_bytes"]:
                            validation_errors.append("WSL host free space fell below the reserve")
                        if validation_errors:
                            process.kill()
                            return
                with logfile.open("w") as target:
                    process = subprocess.Popen(command, cwd=output, env=child_environment,
                                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                               stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                                               errors="replace", bufsize=1)
                    watcher = threading.Thread(target=monitor, daemon=True)
                    watcher.start()
                    timer = threading.Timer(args.timeout, process.kill)
                    timer.daemon = True
                    timer.start()
                    try:
                        process.stdin.write(f"{threads}\n")
                        process.stdin.close()
                        current = "initialization"
                        for line in process.stdout:
                            target.write(line)
                            target.flush()
                            if re.search(r"failed|error(?: code|:)|assertion|terminate called", line, re.I):
                                validation_errors.append("library diagnostic reported failure; see raw log")
                                process.kill()
                            if "Begin " in line:
                                current = line.strip().strip("= ")
                            if "Time Cost:" in line or "| ns per log |" in line:
                                print(json.dumps({"event": "measurement", "run": name,
                                                  "case": current, "result": line.strip()}), flush=True)
                        code = process.wait()
                    finally:
                        timer.cancel()
                        stop_monitor.set()
                        watcher.join()
                elapsed = time.monotonic() - begin
                after = resource.getrusage(resource.RUSAGE_CHILDREN)
                clock_jumps.append(abs((time.time() - time.monotonic()) - clock_offset))
                if max(clock_jumps) > 0.05:
                    validation_errors.append("system_clock shifted relative to monotonic by over 50 ms")
                if code or validation_errors:
                    save(args.evidence_dir / "invalid-run.json", {
                        "run": name, "exit_code": code, "reasons": validation_errors,
                        "max_clock_offset_change_seconds": max(clock_jumps), "raw_log": str(logfile)})
                    raise RuntimeError(f"{name} exited {code}: {validation_errors}; see {logfile}")
                throughputs, latencies = parse(logfile.read_text())
                assert len(throughputs) == (13 if backend == "qlog" else 19), (name, len(throughputs))
                assert len(latencies) == ((1 if threads == 1 else 2) * (1 if backend == "qlog" else 2)), (name, latencies)
                assert all(item["milliseconds"] > 0 for item in throughputs)
                assert sum(item["milliseconds"] for item in throughputs) <= elapsed * 1000 + 100, (
                    name, "reported case timers exceed whole-process monotonic duration")
                files = [{"path": str(path.relative_to(output)), "bytes": path.stat().st_size}
                         for path in sorted(output.rglob("*")) if path.is_file()]
                row = {"run": name, "backend": backend, "threads": threads, "repeat": repeat,
                       "command": command, "cwd": str(output), "elapsed_seconds": elapsed,
                       "process_cpu_seconds": (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime),
                       "max_clock_offset_change_seconds": max(clock_jumps),
                       "free_space_before": space_before, "free_space_after": free_space(),
                       "throughput": throughputs, "latency": latencies, "retained_files": files}
                row["output_cleanup"] = {"requested": args.cleanup_output, "completed": False}
                if args.cleanup_output:
                    for item in files:
                        item["sha256"] = digest(output / item["path"])
                raw.append(row)
                save(args.evidence_dir / "raw-results.json", raw)
                if args.cleanup_output:
                    resolved_output = output.resolve()
                    if output.is_symlink() or resolved_output.parent != args.data_root.resolve():
                        raise RuntimeError("Run output escaped the explicit data-root")
                    shutil.rmtree(resolved_output)
                    row["output_cleanup"]["completed"] = True
                    save(args.evidence_dir / "raw-results.json", raw)
                print(json.dumps({"event": "complete", "run": name, "seconds": elapsed,
                                  "retained_bytes": sum(f["bytes"] for f in files)}), flush=True)
    summary = []
    for threads in args.threads:
        cases = raw[[r["threads"] for r in raw].index(threads)]["throughput"]
        for case in cases:
            if case["mode"] != "normal":
                continue
            item = {"threads": threads, "case": case["case"]}
            for backend in executables:
                samples = [entry["milliseconds"] for run in raw
                           if run["threads"] == threads and run["backend"] == backend
                           for entry in run["throughput"] if entry["case"] == case["case"]]
                assert len(samples) == args.repeats, (backend, case, samples)
                item[backend] = {"milliseconds": samples, "median_ms": statistics.median(samples),
                                 "min_ms": min(samples), "max_ms": max(samples),
                                 "nominal_million_calls_per_second": 2000000 * threads / statistics.median(samples) / 1000}
            item["qlog_bqlog_throughput_ratio"] = item["bqlog"]["median_ms"] / item["qlog"]["median_ms"]
            summary.append(item)
    environment["source_drift"] = manifest(root, ["include", "src", "benchmark/cpp"]) != initial
    environment["reference_drift"] = manifest(args.bqlog_source, ["include", "src", "benchmark/cpp"]) != reference
    environment["completed_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    save(args.evidence_dir / "environment.json", environment)
    save(args.evidence_dir / "summary.json", summary)
    assert not environment["source_drift"] and not environment["reference_drift"]
    print("All suites completed; source manifests unchanged.", flush=True)


if __name__ == "__main__":
    main()
