#!/usr/bin/env python3
"""Paired old/new single-matvec experiment; never changes machine settings.

Build and validate both libraries first. Binaries must use the SAME benchmark
harness (including optional raw/seed support). This runner intentionally labels
results provisional: CPU affinity alone is not CPU/sibling/IRQ isolation.
"""

import argparse
import csv
import hashlib
import io
import json
import math
import os
from pathlib import Path
import random
import re
import statistics as stats
import subprocess
import sys
import time

PARAMS = ("128f", "128s", "192f", "192s", "256f", "256s")
BACKENDS = ("ref", "avx2", "avx512")
FULL_FIELDS = "backend variant op N min median max mean std cv med_us mean_us ops".split()
KERNEL_FIELDS = "backend variant op work_items N inner min median max mean std cv cycles_per_item".split()


def read(path):
    try:
        return Path(path).read_text().strip()
    except OSError as error:
        return str(error)


def command(args):
    return subprocess.run(args, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, check=False).stdout.strip()


def environment(cpu):
    root = f"/sys/devices/system/cpu/cpu{cpu}"
    paths = ["/proc/cmdline", "/proc/loadavg",
             "/sys/devices/system/cpu/isolated",
             "/sys/devices/system/cpu/intel_pstate/no_turbo"]
    paths += [f"{root}/cpufreq/{name}" for name in
              ("scaling_governor", "scaling_min_freq", "scaling_max_freq",
               "scaling_cur_freq", "energy_performance_preference")]
    paths.append(f"{root}/topology/thread_siblings_list")
    return {"utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "files": {p: read(p) for p in paths},
            "uname": command(["uname", "-a"]),
            "lscpu": command(["lscpu"]),
            "compiler": command(["cc", "--version"]),
            "processes": command(["ps", "-eo", "pid,psr,comm"])}


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def invoke(binary, args, prefix, cpu, seed=None):
    env = dict(os.environ)
    env["AIMER_BENCH_RAW"] = str(prefix.with_suffix(".raw.csv"))
    env.pop("AIMER_BENCH_SEED", None)
    if seed is not None:
        env["AIMER_BENCH_SEED"] = seed
    proc = subprocess.run(["taskset", "-c", str(cpu), str(binary), *args],
                          env=env, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=300)
    prefix.with_suffix(".csv").write_text(proc.stdout)
    prefix.with_suffix(".stderr").write_text(proc.stderr)
    if proc.returncode:
        raise RuntimeError(f"STOP: {binary} {args}: {proc.stderr}")
    return proc.stdout, proc.stderr


def analyze(output):
    metadata = json.loads((output / "metadata.json").read_text())
    if metadata.get("status") != "complete":
        raise RuntimeError("Refusing to analyze an incomplete measurement")
    grouped = {}
    with (output / "run-summaries.csv").open() as source:
        for row in csv.DictReader(source):
            key = row["backend"], row["variant"], row["op"]
            grouped.setdefault(key, {"baseline": {}, "candidate": {}})
            grouped[key][row["implementation"]][int(row["run"])] = float(row["median"])
    rng = random.Random(0xA13)
    results = []
    for key, versions in sorted(grouped.items()):
        runs = sorted(versions["baseline"])
        if runs != sorted(versions["candidate"]) or len(runs) != metadata["runs"]:
            raise RuntimeError(f"Incomplete pairs: {key}")
        old = [versions["baseline"][r] for r in runs]
        new = [versions["candidate"][r] for r in runs]
        speedup = stats.median(old) / stats.median(new)
        bootstrap = []
        for _ in range(10000):
            indices = rng.choices(range(len(runs)), k=len(runs))
            bootstrap.append(stats.median(old[i] for i in indices) /
                             stats.median(new[i] for i in indices))
        bootstrap.sort()
        results.append(dict(zip(("backend", "variant", "op"), key),
                            baseline_tsc=stats.median(old), candidate_tsc=stats.median(new),
                            speedup=speedup, reduction_pct=100 * (1 - 1 / speedup),
                            paired_bootstrap_low=bootstrap[250],
                            paired_bootstrap_high=bootstrap[9749],
                            baseline_run_median_sd=stats.stdev(old),
                            candidate_run_median_sd=stats.stdev(new),
                            baseline_run_median_cv=100 * stats.stdev(old) / stats.mean(old),
                            candidate_run_median_cv=100 * stats.stdev(new) / stats.mean(new)))
    with (output / "comparison.csv").open("w") as target:
        writer = csv.DictWriter(target, fieldnames=list(results[0]))
        writer.writeheader()
        writer.writerows(results)
    lines = ["# Single-matvec intrinsic port: provisional paired comparison", "",
             "NOT a paper-final benchmark. No Turbo/governor/frequency, SMT, IRQ, or "
             "background-process settings were changed by this runner. See metadata.json.", "",
             "Only single-input 192/256-bit matvec changed. Both libraries use identical "
             "-O3 build flags and the same benchmark harness. Old general C may already "
             "auto-vectorize; this is not scalar versus SIMD.", "",
             f"Protocol: {metadata['runs']} paired runs, CPU {metadata['cpu']}; "
             f"E2E {metadata['e2e_samples']} timed samples + {metadata['warmup']} warmups; "
             f"kernel {metadata['kernel_samples']} timed batches + {metadata['warmup']} "
             "warmup batches. Each parameter uses one shared inner count, calibrated "
             "from the fastest old/new/backend median to target >= 2,800,000 TSC ticks.", "",
             "Same deterministic NIST KAT DRBG workload per run/parameter across all "
             "versions/backends; final public-key/signature diagnostic checksums match. "
             "This RNG mode differs from the default system-RNG benchmark. Raw samples "
             "are retained in original order; no outlier removal. Timings are invariant "
             "TSC ticks, NOT actual core clock cycles when frequency varies.", "",
             "Aggregate: median of run medians; within-run E2E uses the existing upper "
             "middle value for even N=50. Speedup = old/new; >1 favors intrinsics. "
             "95% intervals resample paired runs (10,000 bootstrap draws); these do not "
             "remove uncontrolled-environment bias or adjust for multiple comparisons. "
             "Reference and 128-bit cells are unchanged-code controls. SD/CV of run "
             "medians and per-run sample statistics are retained in CSV.", ""]
    for backend in BACKENDS:
        lines += [f"## {backend}", "",
                  "| Parameter | Operation | Old TSC | New TSC | Old/new | Paired 95% interval |",
                  "|---|---|---:|---:|---:|---:|"]
        for row in results:
            if row["backend"] == backend:
                lines.append(f"| {row['variant'].removeprefix('AIMER-v3-')} | {row['op']} | "
                             f"{row['baseline_tsc']:,.1f} | {row['candidate_tsc']:,.1f} | "
                             f"{row['speedup']:.3f}x | {row['paired_bootstrap_low']:.3f}--"
                             f"{row['paired_bootstrap_high']:.3f} |")
        lines.append("")
    (output / "REPORT.md").write_text("\n".join(lines) + "\n")
    print(f"ANALYSIS_PASS: {len(results)} paired cells", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-build", type=Path)
    parser.add_argument("--candidate-build", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cpu", type=int, default=2)
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("--e2e-samples", type=int, default=50)
    parser.add_argument("--kernel-samples", type=int, default=75)
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--analyze-only", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    if args.analyze_only:
        analyze(output)
        return
    if args.baseline_build is None or args.candidate_build is None:
        parser.error("both build directories are required")
    if args.runs < 2 or min(args.e2e_samples, args.kernel_samples, args.warmup) < 1:
        parser.error("need >=2 runs and positive sample/warmup counts")
    if args.cpu not in os.sched_getaffinity(0):
        parser.error("requested CPU is outside the allowed affinity mask")
    output.mkdir(parents=True, exist_ok=False)
    binaries = {name: {kind: root.resolve() / "bench" / f"bench_{kind}"
                       for kind in ("full", "kernels")}
                for name, root in (("baseline", args.baseline_build),
                                   ("candidate", args.candidate_build))}
    metadata = {"status": "running", "classification": "provisional", "cpu": args.cpu,
                "runs": args.runs, "e2e_samples": args.e2e_samples,
                "kernel_samples": args.kernel_samples, "warmup": args.warmup,
                "start": environment(args.cpu), "command": sys.argv,
                "binary_sha256": {str(path): sha256(path) for paths in binaries.values()
                                  for path in paths.values()}}
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    try:
        calibration = output / "calibration"
        calibration.mkdir()
        inner = {}
        for param in PARAMS:
            medians = []
            for backend in BACKENDS:
                for name in binaries:
                    stdout, _ = invoke(binaries[name]["kernels"],
                                       [backend, f"AIMER-v3-{param}", "7", "256", "3", "gf_mat_vec"],
                                       calibration / f"{param}-{backend}-{name}", args.cpu)
                    rows = list(csv.DictReader(io.StringIO(stdout), fieldnames=KERNEL_FIELDS))
                    if len(rows) != 1:
                        raise RuntimeError("Unexpected kernel calibration output")
                    medians.append(float(rows[0]["median"]))
            needed = max(1, math.ceil(2_800_000 / min(medians)))
            inner[param] = 1 << (needed - 1).bit_length()
        metadata["kernel_inner"] = inner
        (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
        checksums = {}
        with (output / "run-summaries.csv").open("w") as summary:
            fields = ["run", "implementation", "backend", "variant", "op", "N",
                      "inner", "min", "median", "max", "mean", "std", "cv"]
            writer = csv.DictWriter(summary, fieldnames=fields)
            writer.writeheader()
            for run in range(1, args.runs + 1):
                directory = output / f"run-{run:02d}"
                directory.mkdir()
                order = BACKENDS[(run - 1) % 3:] + BACKENDS[:(run - 1) % 3]
                for param in (PARAMS if run % 2 else PARAMS[::-1]):
                    seed = hashlib.shake_256(f"single-matvec:{run}:{param}".encode()).hexdigest(48)
                    for backend_index, backend in enumerate(order):
                        names = ("baseline", "candidate") if (run + backend_index) % 2 else ("candidate", "baseline")
                        for kind in ("full", "kernels"):
                            for name in names:
                                cli = [backend, f"AIMER-v3-{param}"]
                                if kind == "full":
                                    cli += [str(args.e2e_samples), str(args.warmup)]
                                else:
                                    cli += [str(args.kernel_samples), str(inner[param]),
                                            str(args.warmup), "gf_mat_vec"]
                                prefix = directory / f"{param}-{backend}-{name}-{kind}"
                                stdout, stderr = invoke(binaries[name][kind], cli, prefix,
                                                        args.cpu, seed if kind == "full" else None)
                                if kind == "full":
                                    match = re.search(r"workload_checksum=([0-9a-f]{16})", stderr)
                                    if match is None:
                                        raise RuntimeError("Missing deterministic workload checksum")
                                    checksum = match.group(1)
                                    if checksums.setdefault((run, param), checksum) != checksum:
                                        raise RuntimeError(f"STOP: workload mismatch {prefix}")
                                fieldnames = FULL_FIELDS if kind == "full" else KERNEL_FIELDS
                                rows = list(csv.DictReader(io.StringIO(stdout), fieldnames=fieldnames))
                                if len(rows) != (3 if kind == "full" else 1):
                                    raise RuntimeError(f"Unexpected summary rows: {prefix}")
                                raw_count = len(prefix.with_suffix(".raw.csv").read_text().splitlines())
                                if raw_count != sum(int(row["N"]) for row in rows):
                                    raise RuntimeError(f"Missing raw samples: {prefix}")
                                for row in rows:
                                    writer.writerow(dict({field: row.get(field, "") for field in fields},
                                                         run=run, implementation=name))
                                summary.flush()
                (directory / "environment.json").write_text(json.dumps(environment(args.cpu), indent=2) + "\n")
                print(f"Paired run {run}/{args.runs} complete; workload checksums match", flush=True)
        for path, digest in metadata["binary_sha256"].items():
            if sha256(path) != digest:
                raise RuntimeError(f"Binary changed during measurement: {path}")
        metadata["status"] = "complete"
    except BaseException as error:
        metadata["status"] = "failed"
        metadata["error"] = str(error)
        raise
    finally:
        metadata["end"] = environment(args.cpu)
        (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    analyze(output)


if __name__ == "__main__":
    main()
