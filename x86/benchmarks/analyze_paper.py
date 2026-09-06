#!/usr/bin/env python3
"""Aggregate independent AIMer v3 benchmark runs and produce paper tables."""

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
VARIANTS = tuple(f"AIMER-v3-{p}" for p in ("128f", "128s", "192f", "192s", "256f", "256s"))


def read_rows(path: Path) -> list[list[str]]:
    with path.open(newline="") as stream:
        return [row for row in csv.reader(line for line in stream if not line.startswith("#")) if row]


def percentile(values: list[float], fraction: float) -> float:
    if len(values) == 1:
        return values[0]
    position = fraction * (len(values) - 1)
    lower = math.floor(position)
    upper = math.ceil(position)
    weight = position - lower
    return values[lower] * (1.0 - weight) + values[upper] * weight


def bootstrap_median_ci(values: list[float], seed: str) -> tuple[float, float]:
    if len(values) < 2:
        return values[0], values[0]
    number = int.from_bytes(hashlib.sha256(seed.encode()).digest()[:8], "little")
    generator = random.Random(number)
    estimates = []
    for _ in range(10000):
        estimates.append(statistics.median(generator.choice(values) for _ in values))
    estimates.sort()
    return percentile(estimates, 0.025), percentile(estimates, 0.975)


def summary(values: list[float]) -> dict[str, float]:
    mean = statistics.fmean(values)
    deviation = statistics.stdev(values) if len(values) > 1 else 0.0
    return {
        "median": statistics.median(values),
        "mean": mean,
        "std": deviation,
        "cv": deviation / mean * 100.0 if mean else 0.0,
        "min": min(values),
        "max": max(values),
    }


def collect(root: Path):
    e2e = defaultdict(dict)
    kernel = defaultdict(dict)
    run_dirs = sorted(path for path in (root / "runs").glob("run-*") if path.is_dir())
    if not run_dirs:
        raise SystemExit(f"no independent runs under {root / 'runs'}")
    for run_dir in run_dirs:
        run = run_dir.name
        for backend in BACKENDS:
            e2e_path = run_dir / "e2e" / f"{backend}.csv"
            kernel_path = run_dir / "kernels" / f"{backend}.csv"
            if not e2e_path.is_file() or not kernel_path.is_file():
                raise SystemExit(f"missing benchmark CSV in {run_dir}")
            for row in read_rows(e2e_path):
                if len(row) != 13 or row[0] != backend:
                    raise SystemExit(f"invalid end-to-end row in {e2e_path}: {row}")
                key = (row[1], row[2], backend)
                e2e[key][run] = {
                    "samples": int(row[3]),
                    "median": float(row[5]),
                    "median_us": float(row[10]),
                }
            for row in read_rows(kernel_path):
                if len(row) != 13 or row[0] != backend:
                    raise SystemExit(f"invalid kernel row in {kernel_path}: {row}")
                key = (row[1], row[2], backend)
                kernel[key][run] = {
                    "work_items": int(row[3]),
                    "samples": int(row[4]),
                    "inner": int(row[5]),
                    "median": float(row[7]),
                    "per_item": float(row[12]),
                }
    return run_dirs, e2e, kernel


def validate_complete(run_dirs, data, kind: str):
    runs = {path.name for path in run_dirs}
    for key, values in data.items():
        if set(values) != runs:
            raise SystemExit(f"incomplete {kind} cell {key}: {sorted(values)}")


def paired_ratio(data, variant: str, operation: str, numerator: str,
                 denominator: str, field: str):
    reference = data[(variant, operation, numerator)]
    target = data[(variant, operation, denominator)]
    ratios = [reference[run][field] / target[run][field] for run in sorted(reference)]
    median = statistics.median(ratios)
    seed = f"{variant}/{operation}/{numerator}/{denominator}/{field}"
    low, high = bootstrap_median_ci(ratios, seed)
    return median, low, high


def paired_speedup(data, variant: str, operation: str, backend: str, field: str):
    return paired_ratio(data, variant, operation, "ref", backend, field)


def write_summary_csv(path: Path, data, kernel: bool):
    operation_name = "kernel" if kernel else "operation"
    header = [
        "variant", operation_name, "backend", "independent_runs",
        "samples_per_run", "inner", "work_items", "median_cycles",
        "run_mean_cycles", "run_std_cycles", "run_cv_percent", "min_run_median",
        "max_run_median", "ref_speedup", "speedup_ci95_low", "speedup_ci95_high",
    ]
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(header)
        for variant in VARIANTS:
            operations = sorted({key[1] for key in data if key[0] == variant})
            for operation in operations:
                for backend in BACKENDS:
                    rows = data[(variant, operation, backend)]
                    medians = [rows[run]["median"] for run in sorted(rows)]
                    stats = summary(medians)
                    first = rows[sorted(rows)[0]]
                    if backend == "ref":
                        speedup = low = high = 1.0
                    else:
                        speedup, low, high = paired_speedup(
                            data, variant, operation, backend, "median"
                        )
                    writer.writerow([
                        variant, operation, backend, len(rows), first["samples"],
                        first.get("inner", 1), first.get("work_items", 1),
                        f"{stats['median']:.2f}", f"{stats['mean']:.2f}",
                        f"{stats['std']:.2f}", f"{stats['cv']:.2f}",
                        f"{stats['min']:.2f}", f"{stats['max']:.2f}",
                        f"{speedup:.4f}", f"{low:.4f}", f"{high:.4f}",
                    ])


def fmt_cycles(value: float) -> str:
    if value >= 1_000_000:
        return f"{value / 1_000_000:.3f}M"
    if value >= 1_000:
        return f"{value / 1_000:.1f}k"
    return f"{value:.1f}"


def metadata(root: Path) -> dict[str, str]:
    values = {}
    path = root / "metadata.txt"
    if path.is_file():
        for line in path.read_text(errors="replace").splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                values[key] = value
    return values


def write_report(root: Path, run_dirs, e2e, kernel):
    meta = metadata(root)
    governor = meta.get("governor", "unknown")
    epp = meta.get("energy_performance_preference", "unknown")
    turbo = meta.get("turbo", "unknown")
    scaling_min_freq = meta.get("scaling_min_freq", "unknown")
    scaling_max_freq = meta.get("scaling_max_freq", "unknown")
    e2e_max_cv = max(
        summary([row["median"] for row in runs.values()])["cv"]
        for runs in e2e.values()
    )
    turbo_disabled = turbo in ("intel_pstate/no_turbo=1", "cpufreq/boost=0")
    frequency_locked = (
        scaling_min_freq != "unknown" and scaling_min_freq == scaling_max_freq
    )
    paper_ready = (
        governor == "performance" and epp == "performance" and turbo_disabled
        and frequency_locked and len(run_dirs) >= 5
        and meta.get("verification") == "1" and e2e_max_cv <= 5.0
    )
    regressions = []
    avx512_slower = []
    noisy = []
    lines = [
        "# AIMer v3 AVX2/AVX-512 benchmark results",
        "",
        f"- 상태: **{'논문 표에 사용 가능한 환경' if paper_ready else '조건부 결과 — 논문 최종 수치로 사용 금지'}**",
        f"- 독립 실행: {len(run_dirs)}회",
        f"- CPU governor: `{governor}`",
        f"- EPP: `{epp}`",
        f"- Turbo/boost: `{turbo}`",
        f"- CPU frequency range: `{scaling_min_freq}..{scaling_max_freq} kHz`",
        f"- Maximum end-to-end run-median CV: {e2e_max_cv:.2f}%",
        f"- 컴파일 옵션: `{meta.get('cflags', 'unknown')}`",
        f"- 고정 논리 CPU: `{meta.get('core', 'unknown')}`",
        "- speedup 정의: 같은 독립 실행에서 `reference median / target median`; 95% CI는 paired bootstrap median",
        "",
        "## End-to-end",
        "",
        "| Parameter | Operation | Ref cycles | AVX2 cycles | AVX2 speedup (95% CI) | AVX-512 cycles | AVX-512 speedup (95% CI) | AVX-512 vs AVX2 (95% CI) |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for variant in VARIANTS:
        for operation in ("keypair", "sign", "verify"):
            ref_stats = summary([v["median"] for v in e2e[(variant, operation, "ref")].values()])
            a2_stats = summary([v["median"] for v in e2e[(variant, operation, "avx2")].values()])
            a5_stats = summary([v["median"] for v in e2e[(variant, operation, "avx512")].values()])
            a2 = paired_speedup(e2e, variant, operation, "avx2", "median")
            a5 = paired_speedup(e2e, variant, operation, "avx512", "median")
            a5_vs_a2 = paired_ratio(e2e, variant, operation, "avx2", "avx512", "median")
            if a5_vs_a2[0] < 1.0:
                avx512_slower.append(("end-to-end", variant, operation, *a5_vs_a2))
            lines.append(
                f"| {variant.removeprefix('AIMER-v3-')} | {operation} | {fmt_cycles(ref_stats['median'])} | "
                f"{fmt_cycles(a2_stats['median'])} | {a2[0]:.2f}× ({a2[1]:.2f}–{a2[2]:.2f}) | "
                f"{fmt_cycles(a5_stats['median'])} | {a5[0]:.2f}× ({a5[1]:.2f}–{a5[2]:.2f}) | "
                f"{a5_vs_a2[0]:.2f}× ({a5_vs_a2[1]:.2f}–{a5_vs_a2[2]:.2f}) |"
            )
            if ref_stats["cv"] > 5.0:
                noisy.append(("end-to-end", variant, operation, "ref", ref_stats["cv"]))
            for backend, stats, speed in (("avx2", a2_stats, a2), ("avx512", a5_stats, a5)):
                if speed[0] < 1.0:
                    regressions.append(("end-to-end", variant, operation, backend, speed[0]))
                if stats["cv"] > 5.0:
                    noisy.append(("end-to-end", variant, operation, backend, stats["cv"]))

    lines.extend(["", "## Optimized-scope kernels", ""])
    for variant in VARIANTS:
        lines.extend([
            f"### {variant}",
            "",
            "| Kernel | Work items | Ref cycles/item | AVX2 speedup (95% CI) | AVX-512 speedup (95% CI) | AVX-512 vs AVX2 (95% CI) |",
            "|---|---:|---:|---:|---:|---:|",
        ])
        operations = sorted({key[1] for key in kernel if key[0] == variant})
        for operation in operations:
            ref_rows = kernel[(variant, operation, "ref")]
            first = next(iter(ref_rows.values()))
            ref_item = summary([v["per_item"] for v in ref_rows.values()])["median"]
            a2 = paired_speedup(kernel, variant, operation, "avx2", "median")
            a5 = paired_speedup(kernel, variant, operation, "avx512", "median")
            a5_vs_a2 = paired_ratio(kernel, variant, operation, "avx2", "avx512", "median")
            if a5_vs_a2[0] < 1.0:
                avx512_slower.append(("kernel", variant, operation, *a5_vs_a2))
            lines.append(
                f"| {operation} | {first['work_items']} | {fmt_cycles(ref_item)} | "
                f"{a2[0]:.2f}× ({a2[1]:.2f}–{a2[2]:.2f}) | "
                f"{a5[0]:.2f}× ({a5[1]:.2f}–{a5[2]:.2f}) | "
                f"{a5_vs_a2[0]:.2f}× ({a5_vs_a2[1]:.2f}–{a5_vs_a2[2]:.2f}) |"
            )
            ref_stats = summary([v["median"] for v in ref_rows.values()])
            if ref_stats["cv"] > 5.0:
                noisy.append(("kernel", variant, operation, "ref", ref_stats["cv"]))
            for backend, speed in (("avx2", a2), ("avx512", a5)):
                stats = summary([v["median"] for v in kernel[(variant, operation, backend)].values()])
                if speed[0] < 1.0:
                    regressions.append(("kernel", variant, operation, backend, speed[0]))
                if stats["cv"] > 5.0:
                    noisy.append(("kernel", variant, operation, backend, stats["cv"]))

    lines.extend(["", "## Reference-relative regressions (no post-measurement tuning)", ""])
    if regressions:
        for kind, variant, operation, backend, value in regressions:
            lines.append(f"- {kind}: {variant} `{operation}` {backend}: {value:.3f}×")
    else:
        lines.append("- None.")
    lines.extend(["", "## AVX-512 slower than AVX2 (no post-measurement tuning)", ""])
    if avx512_slower:
        for kind, variant, operation, value, low, high in avx512_slower:
            certainty = "CI entirely below 1" if high < 1.0 else "CI crosses 1"
            lines.append(
                f"- {kind}: {variant} `{operation}`: {value:.3f}× "
                f"(95% CI {low:.3f}–{high:.3f}; {certainty})"
            )
    else:
        lines.append("- None.")
    lines.extend(["", "## Stability flags", ""])
    if noisy:
        for kind, variant, operation, backend, value in noisy:
            lines.append(f"- {kind}: {variant} `{operation}` {backend}: run-median CV {value:.2f}%")
    else:
        lines.append("- All run-median CV values are at most 5%.")
    lines.extend([
        "",
        "## Validation and scope",
        "",
        "- Measurement was allowed only after all official KATs and GF/OQS differential/API tests passed.",
        "- Every kernel process rechecks its selected implementation output against reference before timing.",
        "- SHAKE x1/x4 uses the actual commitment+tape input/output sizes for each security level.",
        "- Batch reference is the operation-equivalent scalar loop over the same 16 or 256 parties.",
        "- Static commitment/tape orchestration is not copied into a synthetic microbenchmark; it remains covered by end-to-end signing.",
        "- No regression-triggered tuning or result filtering was performed.",
    ])
    if not paper_ready:
        lines.extend([
            "",
            "> 이 실행은 성능 정책·고정 주파수·검증·반복 수·end-to-end CV 중 하나 이상의 논문 조건을 충족하지 못했습니다. 비교 경향과 구현 회귀 확인에는 사용할 수 있지만 논문의 최종 성능 표에는 사용하지 마십시오.",
        ])
    (root / "RESULTS.md").write_text("\n".join(lines) + "\n")


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <paper-result-directory>", file=sys.stderr)
        return 2
    root = Path(sys.argv[1]).resolve()
    run_dirs, e2e, kernel = collect(root)
    validate_complete(run_dirs, e2e, "end-to-end")
    validate_complete(run_dirs, kernel, "kernel")
    write_summary_csv(root / "e2e_summary.csv", e2e, False)
    write_summary_csv(root / "kernel_summary.csv", kernel, True)
    write_report(root, run_dirs, e2e, kernel)
    print(root / "RESULTS.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
