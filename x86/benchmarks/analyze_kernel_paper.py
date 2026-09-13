#!/usr/bin/env python3
"""Aggregate adaptive, independent AIMer v3 kernel benchmark runs."""

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
KERNELS = (
    "gf_mul",
    "gf_sqr",
    "gf_inv",
    "gf_mat_vec",
    "gf_sqr_batch",
    "gf_mul_add_batch",
    "gf_mat_vec_batch",
    "gf_mat_vec_add_batch",
    "aim3_mpc_batch",
    "shake_commit_tape_x1",
    "shake_commit_tape_x4",
)


def read_rows(path: Path) -> list[list[str]]:
    with path.open(newline="") as stream:
        return [
            row
            for row in csv.reader(line for line in stream if not line.startswith("#"))
            if row
        ]


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
    estimates = []
    for _ in range(10_000):
        estimates.append(statistics.median(generator.choice(values) for _ in values))
    estimates.sort()
    return percentile(estimates, 0.025), percentile(estimates, 0.975)


def stats(values: list[float]) -> dict[str, float]:
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


def read_metadata(root: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in (root / "metadata.txt").read_text(errors="replace").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    return values


def read_calibration(root: Path) -> dict[tuple[str, str], int]:
    path = root / "calibration" / "inner_map.csv"
    result: dict[tuple[str, str], int] = {}
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            key = (row["variant"], row["kernel"])
            if key in result:
                raise SystemExit(f"duplicate calibration map row: {key}")
            result[key] = int(row["inner"])
    expected = {(variant, kernel) for variant in VARIANTS for kernel in KERNELS}
    if set(result) != expected:
        raise SystemExit("calibration map does not contain all 66 parameter/kernel cells")
    return result


def collect(root: Path):
    data: dict[tuple[str, str, str], dict[str, dict[str, float | int]]] = defaultdict(dict)
    run_dirs = sorted(path for path in (root / "runs").glob("run-*") if path.is_dir())
    if not run_dirs:
        raise SystemExit(f"no independent runs under {root / 'runs'}")
    for run_dir in run_dirs:
        run = run_dir.name
        for backend in BACKENDS:
            path = run_dir / f"{backend}.csv"
            if not path.is_file():
                raise SystemExit(f"missing raw CSV: {path}")
            row_count = 0
            for row in read_rows(path):
                if len(row) != 13 or row[0] != backend:
                    raise SystemExit(f"invalid kernel row in {path}: {row}")
                variant, kernel = row[1], row[2]
                if variant not in VARIANTS or kernel not in KERNELS:
                    raise SystemExit(f"unexpected benchmark cell: {variant}/{kernel}")
                key = (variant, kernel, backend)
                if run in data[key]:
                    raise SystemExit(f"duplicate benchmark cell: {run}/{key}")
                data[key][run] = {
                    "work_items": int(row[3]),
                    "samples": int(row[4]),
                    "inner": int(row[5]),
                    "median": float(row[7]),
                    "within_cv": float(row[11]),
                    "per_item": float(row[12]),
                }
                row_count += 1
            if row_count != len(VARIANTS) * len(KERNELS):
                raise SystemExit(f"expected 66 rows in {path}, got {row_count}")

    expected_keys = {
        (variant, kernel, backend)
        for variant in VARIANTS
        for kernel in KERNELS
        for backend in BACKENDS
    }
    if set(data) != expected_keys:
        raise SystemExit("raw data does not contain all 198 parameter/kernel/backend cells")
    expected_runs = {path.name for path in run_dirs}
    for key, values in data.items():
        if set(values) != expected_runs:
            raise SystemExit(f"incomplete cell {key}: {sorted(values)}")
    return run_dirs, data


def read_irq_telemetry(root: Path, expected_rows: int) -> tuple[int, int, int]:
    path = root / "irq_telemetry.csv"
    if not path.is_file():
        raise SystemExit(f"missing IRQ telemetry: {path}")
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != expected_rows:
        raise SystemExit(f"expected {expected_rows} IRQ telemetry rows, got {len(rows)}")
    deltas = []
    for row in rows:
        delta = int(row["irq_delta"])
        if delta < 0:
            raise SystemExit(f"negative IRQ delta: {row}")
        deltas.append(delta)
    return len(rows), sum(deltas), max(deltas, default=0)


def validate_inner(data, inner_map: dict[tuple[str, str], int]) -> None:
    for variant in VARIANTS:
        for kernel in KERNELS:
            observed = {
                int(row["inner"])
                for backend in BACKENDS
                for row in data[(variant, kernel, backend)].values()
            }
            expected = inner_map[(variant, kernel)]
            if observed != {expected}:
                raise SystemExit(
                    f"unfair inner counts for {variant}/{kernel}: observed={observed}, expected={expected}"
                )


def paired_ratio(data, variant: str, kernel: str, numerator: str, denominator: str):
    left = data[(variant, kernel, numerator)]
    right = data[(variant, kernel, denominator)]
    values = [
        float(left[run]["median"]) / float(right[run]["median"])
        for run in sorted(left)
    ]
    median = statistics.median(values)
    low, high = bootstrap_median_ci(values, f"{variant}/{kernel}/{numerator}/{denominator}")
    return median, low, high


def fmt_cycles(value: float) -> str:
    if value >= 1_000_000:
        return f"{value / 1_000_000:.3f}M"
    if value >= 1_000:
        return f"{value / 1_000:.1f}k"
    return f"{value:.1f}"


def write_summary(root: Path, run_dirs, data) -> tuple[float, float]:
    path = root / "kernel_summary.csv"
    max_run_cv = 0.0
    max_within_cv = 0.0
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(
            [
                "variant", "kernel", "backend", "independent_runs",
                "samples_per_run", "inner", "work_items", "median_cycles",
                "median_cycles_per_item", "run_mean_cycles", "run_std_cycles",
                "run_cv_percent", "min_run_median", "max_run_median",
                "median_within_run_cv_percent", "max_within_run_cv_percent",
                "ref_speedup", "speedup_ci95_low", "speedup_ci95_high",
            ]
        )
        for variant in VARIANTS:
            for kernel in KERNELS:
                for backend in BACKENDS:
                    rows = data[(variant, kernel, backend)]
                    medians = [float(rows[run]["median"]) for run in sorted(rows)]
                    per_items = [float(rows[run]["per_item"]) for run in sorted(rows)]
                    within = [float(rows[run]["within_cv"]) for run in sorted(rows)]
                    summary = stats(medians)
                    max_run_cv = max(max_run_cv, summary["cv"])
                    max_within_cv = max(max_within_cv, max(within))
                    first = rows[sorted(rows)[0]]
                    speed = (1.0, 1.0, 1.0) if backend == "ref" else paired_ratio(
                        data, variant, kernel, "ref", backend
                    )
                    writer.writerow(
                        [
                            variant, kernel, backend, len(run_dirs), first["samples"],
                            first["inner"], first["work_items"], f"{summary['median']:.2f}",
                            f"{statistics.median(per_items):.2f}", f"{summary['mean']:.2f}",
                            f"{summary['std']:.2f}", f"{summary['cv']:.2f}",
                            f"{summary['min']:.2f}", f"{summary['max']:.2f}",
                            f"{statistics.median(within):.2f}", f"{max(within):.2f}",
                            f"{speed[0]:.4f}", f"{speed[1]:.4f}", f"{speed[2]:.4f}",
                        ]
                    )
    return max_run_cv, max_within_cv


def write_report(
    root: Path, run_dirs, data, max_run_cv: float, max_within_cv: float,
    irq_rows: int, irq_total: int, irq_max: int,
) -> None:
    meta = read_metadata(root)
    governor = meta.get("governor", "unknown")
    epp = meta.get("energy_performance_preference", "unknown")
    turbo = meta.get("turbo", "unknown")
    minimum = meta.get("scaling_min_freq", "unknown")
    maximum = meta.get("scaling_max_freq", "unknown")
    target = int(meta.get("kernel_calibration_target_cycles", "0"))
    conditions = {
        "governor_performance": governor == "performance",
        "epp_performance": epp == "performance",
        "turbo_disabled": turbo in ("intel_pstate/no_turbo=1", "cpufreq/boost=0"),
        "frequency_locked": minimum != "unknown" and minimum == maximum,
        "verification_passed": meta.get("verification") == "1",
        "openocd_stopped": (
            meta.get("openocd_running_start") == "0"
            and meta.get("openocd_running_end") == "0"
        ),
        "exclusive_cpuset": (
            meta.get("exclusive_cpu_required") == "1"
            and meta.get("cgroup_cpuset_effective") == meta.get("core")
        ),
        "smt_sibling_offline": meta.get("sibling_online") == "0",
        "package_quiesced": (
            meta.get("package_isolation") == "1"
            and meta.get("online_cpus") == meta.get("expected_online_cpus")
        ),
        "tmpfs_during_measurement": meta.get("result_storage_fs") == "tmpfs",
        "device_irq_isolated": (
            meta.get("irq_isolation") == "1"
            and irq_rows == len(run_dirs) * len(VARIANTS) * len(KERNELS) * len(BACKENDS)
            and irq_total == 0
        ),
        "at_least_15_runs": len(run_dirs) >= 15,
        "batch_at_least_1m_cycles": target >= 1_000_000,
        "max_run_median_cv_at_most_5pct": max_run_cv <= 5.0,
    }
    paper_ready = all(conditions.values())
    noisy = []
    slower = []
    lines = [
        "# AIMer v3 adaptive kernel benchmark results",
        "",
        f"- 상태: **{'논문 표에 사용 가능한 최종 커널 결과' if paper_ready else '조건부 결과 — 논문 최종 수치로 사용 금지'}**",
        f"- 독립 실행: {len(run_dirs)}회; 각 실행/커널당 표본: {meta.get('kernel_samples_per_run', 'unknown')}개",
        f"- 적응형 배치 목표: 최소 {target:,} TSC cycles",
        f"- CPU: 논리 코어 `{meta.get('core', 'unknown')}`, governor `{governor}`, EPP `{epp}`",
        f"- 전용 cpuset: `{meta.get('cgroup_cpuset_effective', 'unknown')}`; "
        f"SMT sibling `{meta.get('sibling_cpu', 'unknown')}` online=`{meta.get('sibling_online', 'unknown')}`",
        f"- 패키지 격리: `{meta.get('package_isolation', 'unknown')}`; "
        f"online CPUs `{meta.get('online_cpus', 'unknown')}`; housekeeping CPU `{meta.get('housekeeping_cpu', 'unknown')}`",
        f"- 측정 중 결과 저장소: `{meta.get('result_storage_fs', 'unknown')}`; "
        f"device IRQ isolation: `{meta.get('irq_isolation', 'unknown')}`",
        f"- 측정 셀 IRQ 증가량: total `{irq_total}`, max/cell `{irq_max}` ({irq_rows} telemetry rows)",
        f"- 주파수: `{minimum}..{maximum} kHz`; Turbo: `{turbo}`",
        f"- 최대 독립 실행 중앙값 CV: {max_run_cv:.2f}%",
        f"- 최대 단일 실행 내 표본 CV(인터럽트 outlier 포함): {max_within_cv:.2f}%",
        "- 같은 parameter/kernel에서는 REF·AVX2·AVX-512에 동일한 inner를 적용함",
        "- speedup: paired run의 `reference median / target median`; 95% CI: 10,000회 paired bootstrap median",
        "",
    ]
    for variant in VARIANTS:
        lines.extend(
            [
                f"## {variant}",
                "",
                "| Kernel | Items | Inner | Ref cycles/item | AVX2 cycles/item | AVX2 speedup (95% CI) | AVX-512 cycles/item | AVX-512 speedup (95% CI) | AVX-512/AVX2 (95% CI) |",
                "|---|---:|---:|---:|---:|---:|---:|---:|---:|",
            ]
        )
        for kernel in KERNELS:
            ref_rows = data[(variant, kernel, "ref")]
            a2_rows = data[(variant, kernel, "avx2")]
            a5_rows = data[(variant, kernel, "avx512")]
            first = ref_rows[sorted(ref_rows)[0]]
            ref_item = statistics.median(float(row["per_item"]) for row in ref_rows.values())
            a2_item = statistics.median(float(row["per_item"]) for row in a2_rows.values())
            a5_item = statistics.median(float(row["per_item"]) for row in a5_rows.values())
            a2 = paired_ratio(data, variant, kernel, "ref", "avx2")
            a5 = paired_ratio(data, variant, kernel, "ref", "avx512")
            a5a2 = paired_ratio(data, variant, kernel, "avx2", "avx512")
            if a2[0] < 1.0:
                slower.append((variant, kernel, "avx2", a2[0]))
            if a5[0] < 1.0:
                slower.append((variant, kernel, "avx512", a5[0]))
            if a5a2[0] < 1.0:
                slower.append((variant, kernel, "avx512-vs-avx2", a5a2[0]))
            for backend in BACKENDS:
                run_stats = stats(
                    [float(row["median"]) for row in data[(variant, kernel, backend)].values()]
                )
                if run_stats["cv"] > 5.0:
                    noisy.append((variant, kernel, backend, run_stats["cv"]))
            lines.append(
                f"| {kernel} | {first['work_items']} | {first['inner']} | {fmt_cycles(ref_item)} | "
                f"{fmt_cycles(a2_item)} | {a2[0]:.2f}× ({a2[1]:.2f}–{a2[2]:.2f}) | "
                f"{fmt_cycles(a5_item)} | {a5[0]:.2f}× ({a5[1]:.2f}–{a5[2]:.2f}) | "
                f"{a5a2[0]:.2f}× ({a5a2[1]:.2f}–{a5a2[2]:.2f}) |"
            )
        lines.append("")

    lines.extend(["## Stability validation", ""])
    if noisy:
        lines.append("독립 실행 중앙값 CV가 5%를 넘은 셀:")
        lines.append("")
        for variant, kernel, backend, cv in noisy:
            lines.append(f"- {variant} / {kernel} / {backend}: {cv:.2f}%")
    else:
        lines.append("모든 198개 backend 셀의 독립 실행 중앙값 CV가 5% 이하입니다.")

    lines.extend(["", "## Slower observations (no post-hoc tuning)", ""])
    if slower:
        for variant, kernel, comparison, ratio in slower:
            lines.append(f"- {variant} / {kernel} / {comparison}: {ratio:.3f}×")
    else:
        lines.append("기준 비교에서 1.0× 미만인 셀이 없습니다.")

    lines.extend(["", "## Readiness checks", ""])
    for name, passed in conditions.items():
        lines.append(f"- [{'x' if passed else ' '}] {name}")
    (root / "RESULTS.md").write_text("\n".join(lines) + "\n")
    with (root / "validation.txt").open("w") as stream:
        stream.write(f"paper_ready={int(paper_ready)}\n")
        stream.write(f"max_run_median_cv_percent={max_run_cv:.6f}\n")
        stream.write(f"max_within_run_cv_percent={max_within_cv:.6f}\n")
        stream.write(f"irq_telemetry_rows={irq_rows}\n")
        stream.write(f"irq_total_delta={irq_total}\n")
        stream.write(f"irq_max_cell_delta={irq_max}\n")
        for name, passed in conditions.items():
            stream.write(f"{name}={int(passed)}\n")


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} <result-directory>")
    root = Path(sys.argv[1]).resolve()
    run_dirs, data = collect(root)
    inner_map = read_calibration(root)
    validate_inner(data, inner_map)
    irq_rows, irq_total, irq_max = read_irq_telemetry(
        root, len(run_dirs) * len(VARIANTS) * len(KERNELS) * len(BACKENDS)
    )
    max_run_cv, max_within_cv = write_summary(root, run_dirs, data)
    write_report(
        root, run_dirs, data, max_run_cv, max_within_cv,
        irq_rows, irq_total, irq_max,
    )


if __name__ == "__main__":
    main()
