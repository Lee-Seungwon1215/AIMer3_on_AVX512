#!/usr/bin/env python3
"""Analyze the paper-final Cortex-M55 A/REF versus D/MVE experiment."""

from __future__ import annotations

import argparse
import csv
import hashlib
import math
import random
import re
import statistics
from collections import defaultdict
from pathlib import Path


PARAMS = ("128f", "128s", "192f", "192s", "256f", "256s")
CONFIGS = ("ref", "mve_affine")
LABELS = {"ref": "Reference", "mve_affine": "MVE"}
OPS = ("keypair", "sign", "verify")
RUNS = 7
SAMPLES = 50
BOOTSTRAPS = 10_000


def fields(line: str, marker: str) -> dict[str, str] | None:
    token = marker + ","
    position = line.find(token)
    if position < 0:
        return None
    output: dict[str, str] = {}
    for item in line[position + len(token):].strip().split(","):
        if "=" in item:
            key, value = item.split("=", 1)
            output[key.strip()] = value.strip()
    return output


def percentile(values: list[float], probability: float) -> float:
    index = probability * (len(values) - 1)
    lower = math.floor(index)
    upper = math.ceil(index)
    if lower == upper:
        return values[lower]
    fraction = index - lower
    return values[lower] * (1.0 - fraction) + values[upper] * fraction


def bootstrap_median(values: list[float], seed_text: str) -> tuple[float, float]:
    seed = int.from_bytes(hashlib.sha256(seed_text.encode()).digest()[:8], "big")
    rng = random.Random(seed)
    estimates = []
    for _ in range(BOOTSTRAPS):
        sample = [values[rng.randrange(len(values))] for _ in values]
        estimates.append(statistics.median(sample))
    estimates.sort()
    return percentile(estimates, 0.025), percentile(estimates, 0.975)


def describe(values: list[float]) -> dict[str, float | int]:
    mean = statistics.fmean(values)
    sd = statistics.pstdev(values)
    return {
        "n": len(values),
        "min": min(values),
        "median": statistics.median(values),
        "mean": mean,
        "sd": sd,
        "cv_percent": 0.0 if mean == 0.0 else 100.0 * sd / mean,
        "max": max(values),
    }


def write_csv(path: Path, rows: list[dict[str, object]], names: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=names, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def load(root: Path) -> tuple[list[dict[str, object]], list[int]]:
    rows: list[dict[str, object]] = []
    timer_errors: list[int] = []
    logs = sorted((root / "benchmark").glob("run-*/*/*.log"))
    if len(logs) != RUNS * len(CONFIGS) * len(PARAMS):
        raise ValueError(f"expected 84 logs, found {len(logs)}")
    for log in logs:
        run_match = re.search(r"run-(\d+)", str(log))
        if run_match is None:
            raise ValueError(f"cannot parse run from {log}")
        run = int(run_match.group(1))
        text = log.read_text(encoding="utf-8", errors="replace")
        if "BENCH_PASS" not in text or "BENCH_FAIL" in text:
            raise ValueError(f"failed or incomplete benchmark: {log}")
        config_lines = [item for line in text.splitlines()
                        if (item := fields(line, "BENCH_CONFIG")) is not None]
        if len(config_lines) != 1:
            raise ValueError(f"expected one BENCH_CONFIG in {log}")
        config_line = config_lines[0]
        if (config_line.get("cpu_hz") != "600000000" or
                config_line.get("e2e_samples") != "50" or
                config_line.get("warmup") != "10"):
            raise ValueError(f"wrong measurement configuration in {log}: {config_line}")
        timer = re.findall(r"BENCH_TIMER_CHECK[^\n]*error_ppm=(\d+)", text)
        if len(timer) != 1:
            raise ValueError(f"expected one timer check in {log}")
        timer_errors.append(int(timer[0]))
        for line in text.splitlines():
            item = fields(line, "BENCH_SAMPLE")
            if item is None or item.get("operation") not in OPS:
                continue
            rows.append({
                "run": run,
                "param": item["param"],
                "config": item["config"],
                "config_label": LABELS[item["config"]],
                "operation": item["operation"],
                "sample": int(item["sample"]),
                "clock": item["clock"],
                "cycles": int(item["raw_cycles"]) / int(item["inner"]),
                "source_log": str(log.relative_to(root)),
            })
    expected = RUNS * len(CONFIGS) * len(PARAMS) * len(OPS) * SAMPLES
    if len(rows) != expected:
        raise ValueError(f"expected {expected} E2E samples, found {len(rows)}")
    if max(timer_errors) > 20_000:
        raise ValueError(f"timer cross-check exceeded 2%: {max(timer_errors)} ppm")
    return rows, timer_errors


def analyze(rows: list[dict[str, object]]) -> tuple[
        list[dict[str, object]], list[dict[str, object]],
        list[dict[str, object]], list[dict[str, object]]]:
    grouped: dict[tuple[int, str, str, str], list[float]] = defaultdict(list)
    raw_grouped: dict[tuple[str, str, str], list[float]] = defaultdict(list)
    for row in rows:
        grouped[(int(row["run"]), str(row["param"]), str(row["operation"]),
                 str(row["config"]))].append(float(row["cycles"]))
        raw_grouped[(str(row["param"]), str(row["operation"]),
                     str(row["config"]))].append(float(row["cycles"]))

    run_rows: list[dict[str, object]] = []
    run_index: dict[tuple[int, str, str, str], float] = {}
    for key, values in sorted(grouped.items()):
        if len(values) != SAMPLES:
            raise ValueError(f"{key}: expected {SAMPLES} samples, got {len(values)}")
        run, param, operation, config = key
        median = statistics.median(values)
        run_index[key] = median
        run_rows.append({"run": run, "param": param, "operation": operation,
                         "config": config, "config_label": LABELS[config],
                         "median_cycles": median})

    summary: list[dict[str, object]] = []
    run_summary: list[dict[str, object]] = []
    for param in PARAMS:
        for operation in OPS:
            for config in CONFIGS:
                raw = raw_grouped[(param, operation, config)]
                if len(raw) != RUNS * SAMPLES:
                    raise ValueError(f"missing raw samples for {(param, operation, config)}")
                summary.append({"param": param, "operation": operation,
                                "config": config, "config_label": LABELS[config],
                                **describe(raw)})
                medians = [run_index[(run, param, operation, config)]
                           for run in range(1, RUNS + 1)]
                run_summary.append({"param": param, "operation": operation,
                                    "config": config, "config_label": LABELS[config],
                                    **describe(medians)})

    paired: list[dict[str, object]] = []
    for param in PARAMS:
        for operation in OPS:
            speedups = [
                run_index[(run, param, operation, "ref")] /
                run_index[(run, param, operation, "mve_affine")]
                for run in range(1, RUNS + 1)
            ]
            low, high = bootstrap_median(speedups, f"{param}:{operation}:A/D:50")
            paired.append({
                "param": param,
                "operation": operation,
                "baseline": "ref",
                "target": "mve_affine",
                "paired_run_median_speedup": statistics.median(speedups),
                "bootstrap_95_low": low,
                "bootstrap_95_high": high,
                "run_speedups": ";".join(f"{value:.9f}" for value in speedups),
            })
    return summary, run_rows, run_summary, paired


def render(root: Path, run_summary: list[dict[str, object]],
           paired: list[dict[str, object]], timer_errors: list[int]) -> str:
    rindex = {(str(row["param"]), str(row["operation"]), str(row["config"])): row
              for row in run_summary}
    pindex = {(str(row["param"]), str(row["operation"])): row for row in paired}
    git_hash = (root / "metadata" / "git.txt").read_text().splitlines()[0]
    compiler = (root / "metadata" / "compiler.txt").read_text().splitlines()[0]
    source_status = (root / "metadata" / "source-manifest-summary.txt").read_text().strip()
    max_cv = max(float(row["cv_percent"]) for row in run_summary)

    lines = [
        "# Cortex-M55 paper-final Reference vs MVE measurement", "",
        "## 범위", "",
        f"- Git 커밋: `{git_hash}`",
        "- 보드: STM32N657 NUCLEO-N657X0-Q, Cortex-M55, single-core bare-metal, 600 MHz",
        f"- 툴체인: `{compiler}`",
        "- Reference: `BACKEND=ref MATVEC=reference MEMORY=full SIGN_SCHEDULE=lowmem`",
        "- MVE: `BACKEND=mve MATVEC=mve MEMORY=full SIGN_SCHEDULE=lowmem`",
        "- 공통 옵션: `-O3 -fno-tree-vectorize -fno-tree-slp-vectorize`",
        "- 두 구성 모두 동일한 AXI SRAM layout, stack/heap, portable Keccak/SHAKE를 사용했다.",
        "- A/REF와 D/MVE만 재측정했으며 B/C ablation과 성능 튜닝은 수행하지 않았다.", "",
        "## Correctness와 측정 gate", "",
        "- 현재 소스 host GF differential 6/6, affine differential 6/6, MVE KAT 600/600",
        "- 현재 소스 Reference host low-memory 및 KAT 600/600",
        "- 실제 보드 Reference/MVE keypair/sign/verify/tamper 12/12",
        "- 동일 소스에 연결된 restructure validation의 실제 보드 MVE KAT 600/600 (`KAT_FAIL` 0)",
        "- `__ARM_FEATURE_MVE=3`, VMULLB/VMULLT, affine VAND/VEOR와 MPC batch 호출 6/6",
        f"- PMU/SysTick 교차검증: {len(timer_errors)}/84, 최대 오차 {max(timer_errors) / 10000:.3f}%",
        f"- source manifest: {source_status}", "",
        "## 통계 조건", "",
        "- 7 independent runs, run당 50 measured samples, warm-up 10",
        "- 홀수 run은 Reference→MVE, 짝수 run은 MVE→Reference 순서로 실행",
        "- run별 50개 표본의 중앙값을 만들고, 같은 run의 Reference/MVE 중앙값 비율을 paired speedup으로 계산",
        "- 7개 paired speedup의 중앙값과 10,000회 paired bootstrap percentile 95% CI 보고",
        "- outlier 및 실행 결과 제거 없음",
        f"- run-median 최대 CV: {max_cv:.3f}%", "",
        "## 결과", "",
        "절대 cycle 수는 Cortex-M55 내부 비교만을 위해 제시한다.", "",
        "| Parameter | Operation | Reference cycles | MVE cycles | Speedup | Bootstrap 95% CI |",
        "|---|---|---:|---:|---:|---:|",
    ]
    for param in PARAMS:
        for operation in OPS:
            ref = rindex[(param, operation, "ref")]
            mve = rindex[(param, operation, "mve_affine")]
            speedup = pindex[(param, operation)]
            lines.append(
                f"| {param} | {operation} | {float(ref['median']):,.0f} | "
                f"{float(mve['median']):,.0f} | "
                f"{float(speedup['paired_run_median_speedup']):.3f}× | "
                f"[{float(speedup['bootstrap_95_low']):.3f}, "
                f"{float(speedup['bootstrap_95_high']):.3f}] |"
            )
    lines.extend([
        "", "## 해석 제한", "",
        "이 결과는 Cortex-M55의 플랫폼-local Reference 대비 MVE 가속률이다. x86의 절대 cycle과 직접 비교하지 않는다. "
        "Cortex-M55에서는 Keccak/SHAKE를 portable 구현으로 유지했으므로, 공통 최적화 범위는 GF 연산, affine 연산과 party 병렬화이다.",
        "", "측정 중 소스 수정, outlier 제거, 결과 선택 또는 추가 성능 튜닝을 하지 않았다.", "",
    ])
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("result_dir", type=Path)
    args = parser.parse_args()
    root = args.result_dir.resolve()
    rows, timer_errors = load(root)
    summary, run_rows, run_summary, paired = analyze(rows)
    analysis = root / "analysis"
    write_csv(analysis / "raw_e2e_samples.csv", rows,
              ["run", "param", "config", "config_label", "operation",
               "sample", "clock", "cycles", "source_log"])
    write_csv(analysis / "e2e_raw_summary.csv", summary,
              ["param", "operation", "config", "config_label", "n", "min",
               "median", "mean", "sd", "cv_percent", "max"])
    write_csv(analysis / "e2e_run_medians.csv", run_rows,
              ["run", "param", "operation", "config", "config_label",
               "median_cycles"])
    write_csv(analysis / "e2e_run_median_summary.csv", run_summary,
              ["param", "operation", "config", "config_label", "n", "min",
               "median", "mean", "sd", "cv_percent", "max"])
    write_csv(analysis / "e2e_paired_speedups.csv", paired,
              ["param", "operation", "baseline", "target",
               "paired_run_median_speedup", "bootstrap_95_low",
               "bootstrap_95_high", "run_speedups"])
    (analysis / "timer-check.txt").write_text(
        f"checks={len(timer_errors)}\nmax_error_ppm={max(timer_errors)}\n"
        f"max_error_percent={max(timer_errors) / 10000:.6f}\n",
        encoding="utf-8")
    (root / "REPORT.md").write_text(
        render(root, run_summary, paired, timer_errors), encoding="utf-8")


if __name__ == "__main__":
    main()
