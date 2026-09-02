#!/usr/bin/env python3
"""Aggregate independent AIMer v2 E2E runs into publication tables."""

from __future__ import annotations

import csv
import hashlib
import math
import random
import statistics
import sys
from collections import defaultdict
from pathlib import Path

BACKENDS = ("ref", "avx2", "avx512")
VARIANTS = tuple(f"AIMER-{p}" for p in ("128f", "128s", "192f", "192s", "256f", "256s"))
OPERATIONS = ("keypair", "sign", "verify")


def read_rows(path: Path) -> list[list[str]]:
    with path.open(newline="") as stream:
        return [row for row in csv.reader(line for line in stream if not line.startswith("#")) if row]


def percentile(values: list[float], fraction: float) -> float:
    if len(values) == 1:
        return values[0]
    position = fraction * (len(values) - 1)
    lower, upper = math.floor(position), math.ceil(position)
    weight = position - lower
    return values[lower] * (1.0 - weight) + values[upper] * weight


def bootstrap_median_ci(values: list[float], seed: str) -> tuple[float, float]:
    if len(values) < 2:
        return values[0], values[0]
    number = int.from_bytes(hashlib.sha256(seed.encode()).digest()[:8], "little")
    generator = random.Random(number)
    estimates = [statistics.median(generator.choice(values) for _ in values) for _ in range(10000)]
    estimates.sort()
    return percentile(estimates, 0.025), percentile(estimates, 0.975)


def summarize(values: list[float]) -> dict[str, float]:
    mean = statistics.fmean(values)
    deviation = statistics.stdev(values) if len(values) > 1 else 0.0
    return {
        "median": statistics.median(values), "mean": mean, "std": deviation,
        "cv": deviation / mean * 100.0 if mean else 0.0,
        "min": min(values), "max": max(values),
    }


def collect(root: Path):
    data = defaultdict(dict)
    run_dirs = sorted(path for path in (root / "runs").glob("run-*") if path.is_dir())
    if not run_dirs:
        raise SystemExit(f"no independent runs under {root / 'runs'}")
    for run_dir in run_dirs:
        for backend in BACKENDS:
            path = run_dir / "e2e" / f"{backend}.csv"
            if not path.is_file():
                raise SystemExit(f"missing benchmark CSV: {path}")
            for row in read_rows(path):
                if len(row) != 13 or row[0] != backend:
                    raise SystemExit(f"invalid row in {path}: {row}")
                key = (row[1], row[2], backend)
                if run_dir.name in data[key]:
                    raise SystemExit(f"duplicate cell in {path}: {key}")
                data[key][run_dir.name] = {
                    "samples": int(row[3]), "median": float(row[5]),
                    "median_us": float(row[10]),
                }
    expected_runs = {path.name for path in run_dirs}
    expected_keys = {(variant, operation, backend) for variant in VARIANTS
                     for operation in OPERATIONS for backend in BACKENDS}
    if set(data) != expected_keys:
        missing = sorted(expected_keys - set(data))
        extra = sorted(set(data) - expected_keys)
        raise SystemExit(f"unexpected result cells; missing={missing}, extra={extra}")
    for key, runs in data.items():
        if set(runs) != expected_runs:
            raise SystemExit(f"incomplete cell {key}: {sorted(runs)}")
    return run_dirs, data


def paired_ratio(data, variant: str, operation: str, numerator: str, denominator: str):
    first = data[(variant, operation, numerator)]
    second = data[(variant, operation, denominator)]
    ratios = [first[run]["median"] / second[run]["median"] for run in sorted(first)]
    median = statistics.median(ratios)
    low, high = bootstrap_median_ci(ratios, f"{variant}/{operation}/{numerator}/{denominator}")
    return median, low, high


def metadata(root: Path) -> dict[str, str]:
    values = {}
    for line in (root / "metadata.txt").read_text(errors="replace").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    return values


def fmt_cycles(value: float) -> str:
    if value >= 1_000_000:
        return f"{value / 1_000_000:.3f}M"
    if value >= 1_000:
        return f"{value / 1_000:.1f}k"
    return f"{value:.1f}"


def write_summary(path: Path, data) -> None:
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow([
            "variant", "operation", "backend", "independent_runs", "samples_per_run",
            "inner", "work_items", "median_cycles", "run_mean_cycles", "run_std_cycles",
            "run_cv_percent", "min_run_median", "max_run_median", "ref_speedup",
            "speedup_ci95_low", "speedup_ci95_high",
        ])
        for variant in VARIANTS:
            for operation in OPERATIONS:
                for backend in BACKENDS:
                    rows = data[(variant, operation, backend)]
                    medians = [rows[run]["median"] for run in sorted(rows)]
                    stats = summarize(medians)
                    speed = (1.0, 1.0, 1.0) if backend == "ref" else paired_ratio(
                        data, variant, operation, "ref", backend)
                    writer.writerow([
                        variant, operation, backend, len(rows), next(iter(rows.values()))["samples"],
                        1, 1, f"{stats['median']:.2f}", f"{stats['mean']:.2f}",
                        f"{stats['std']:.2f}", f"{stats['cv']:.2f}", f"{stats['min']:.2f}",
                        f"{stats['max']:.2f}", f"{speed[0]:.4f}", f"{speed[1]:.4f}",
                        f"{speed[2]:.4f}",
                    ])


def write_report(root: Path, run_dirs, data) -> None:
    meta = metadata(root)
    max_cv = max(summarize([row["median"] for row in runs.values()])["cv"] for runs in data.values())
    samples = {row["samples"] for runs in data.values() for row in runs.values()}
    turbo_disabled = meta.get("turbo") in ("intel_pstate/no_turbo=1", "cpufreq/boost=0")
    frequency_locked = meta.get("scaling_min_freq", "unknown") != "unknown" and (
        meta.get("scaling_min_freq") == meta.get("scaling_max_freq"))
    ready = (len(run_dirs) >= 7 and samples == {50} and meta.get("verification") == "1"
             and meta.get("governor") == "performance"
             and meta.get("energy_performance_preference") == "performance"
             and turbo_disabled and frequency_locked and max_cv <= 5.0)
    lines = [
        "# AIMer v2 Ref/AVX2/AVX-512 end-to-end benchmark",
        "",
        f"- 상태: **{'논문 표에 사용 가능한 환경' if ready else '조건부 결과 — 논문 최종 수치로 사용 금지'}**",
        f"- 독립 실행: {len(run_dirs)}회; 각 연산 표본: {next(iter(samples)) if len(samples) == 1 else sorted(samples)}회",
        f"- CPU: `{meta.get('core', 'unknown')}`; governor/EPP: `{meta.get('governor', 'unknown')}`/`{meta.get('energy_performance_preference', 'unknown')}`",
        f"- 주파수: `{meta.get('scaling_min_freq', 'unknown')}..{meta.get('scaling_max_freq', 'unknown')} kHz`; `{meta.get('turbo', 'unknown')}`",
        f"- 최대 run-median CV: {max_cv:.2f}%",
        "- speedup: 같은 독립 실행의 `Ref median / target median`; 95% CI는 paired bootstrap median 10,000회",
        "",
        "| Parameter | Operation | Ref cycles | AVX2 cycles | AVX2 speedup (95% CI) | AVX-512 cycles | AVX-512 speedup (95% CI) | AVX-512 vs AVX2 (95% CI) |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    regressions, a5_slower, noisy = [], [], []
    for variant in VARIANTS:
        for operation in OPERATIONS:
            stats = {backend: summarize([row["median"] for row in data[(variant, operation, backend)].values()]) for backend in BACKENDS}
            a2 = paired_ratio(data, variant, operation, "ref", "avx2")
            a5 = paired_ratio(data, variant, operation, "ref", "avx512")
            a5_a2 = paired_ratio(data, variant, operation, "avx2", "avx512")
            lines.append(
                f"| {variant.removeprefix('AIMER-')} | {operation} | {fmt_cycles(stats['ref']['median'])} | "
                f"{fmt_cycles(stats['avx2']['median'])} | {a2[0]:.2f}× ({a2[1]:.2f}–{a2[2]:.2f}) | "
                f"{fmt_cycles(stats['avx512']['median'])} | {a5[0]:.2f}× ({a5[1]:.2f}–{a5[2]:.2f}) | "
                f"{a5_a2[0]:.2f}× ({a5_a2[1]:.2f}–{a5_a2[2]:.2f}) |")
            for backend in BACKENDS:
                if stats[backend]["cv"] > 5.0:
                    noisy.append((variant, operation, backend, stats[backend]["cv"]))
            for backend, speed in (("avx2", a2), ("avx512", a5)):
                if speed[0] < 1.0:
                    regressions.append((variant, operation, backend, speed[0]))
            if a5_a2[0] < 1.0:
                a5_slower.append((variant, operation, *a5_a2))
    lines.extend(["", "## Reference-relative regressions (no post-measurement tuning)", ""])
    lines.extend([f"- {v} `{o}` {b}: {s:.3f}×" for v, o, b, s in regressions] or ["- None."])
    lines.extend(["", "## AVX-512 slower than AVX2 (no post-measurement tuning)", ""])
    lines.extend([f"- {v} `{o}`: {s:.3f}× (95% CI {lo:.3f}–{hi:.3f})" for v, o, s, lo, hi in a5_slower] or ["- None."])
    lines.extend(["", "## Stability flags", ""])
    lines.extend([f"- {v} `{o}` {b}: run-median CV {cv:.2f}%" for v, o, b, cv in noisy] or ["- All run-median CV values are at most 5%."])
    lines.extend(["", "## Validation", "", "- Ref/AVX2/AVX-512의 공식 KAT 18개 조합을 모두 통과한 뒤 측정했습니다.", "- 논문용 바이너리는 AIMer v3와 같은 LFENCE/RDTSC 및 RDTSCP/LFENCE 경계를 사용합니다.", "- 느린 결과도 제외하거나 사후 튜닝하지 않습니다."])
    if not ready:
        lines.extend(["", "> 논문 조건 중 하나 이상이 충족되지 않았으므로 최종 성능 표에 사용하지 마십시오."])
    (root / "RESULTS.md").write_text("\n".join(lines) + "\n")


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <result-directory>", file=sys.stderr)
        return 2
    root = Path(sys.argv[1]).resolve()
    run_dirs, data = collect(root)
    write_summary(root / "e2e_summary.csv", data)
    write_report(root, run_dirs, data)
    print(root / "RESULTS.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
