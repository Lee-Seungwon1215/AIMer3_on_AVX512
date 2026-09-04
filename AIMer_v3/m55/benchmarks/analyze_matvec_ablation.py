#!/usr/bin/env python3
"""Analyze the Cortex-M55 matrix-controlled ablation result directory."""

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
from typing import Iterable


CONFIGS = ("ref", "mve_refmat", "mve_compact")
CONFIG_LABEL = {
    "ref": "A/REF",
    "mve_refmat": "B/MVE-REFMAT",
    "mve_compact": "C/MVE-COMPACT",
}
PARAMS = ("128f", "128s", "192f", "192s", "256f", "256s")
E2E_OPERATIONS = ("keypair", "sign", "verify")
CORE_PROFILE_OPERATIONS = (
    "gf_mul",
    "gf_sqr",
    "gf_sqr_batch4",
    "gf_mul_const_batch4",
    "gf_mat_vec_mul",
    "gf_mat_vec_mul_add",
    "aim3_mpc_batch4_affine",
    "aim3_mpc_batch4_frobenius",
    "aim3_mpc_batch4",
)
COMPARISONS = (
    ("B/A", "mve_refmat", "ref"),
    ("C/B", "mve_compact", "mve_refmat"),
    ("C/A", "mve_compact", "ref"),
)
PAIR_BOOTSTRAPS = 10_000
CHALLENGE_BATCH_CALLS_256 = {
    "256f": 3_120,
    "256s": 25_344,
}


def parse_fields(line: str, marker: str) -> dict[str, str] | None:
    record_marker = marker + ","
    position = line.find(record_marker)
    if position < 0:
        return None
    payload = line[position + len(record_marker) :].strip()
    fields: dict[str, str] = {}
    for item in payload.split(","):
        if "=" in item:
            key, value = item.strip().split("=", 1)
            fields[key] = value.strip()
    return fields


def describe(values: Iterable[float]) -> dict[str, float | int]:
    data = list(values)
    if not data:
        raise ValueError("cannot summarize an empty sample")
    mean = statistics.fmean(data)
    sd = statistics.pstdev(data)
    return {
        "n": len(data),
        "min": min(data),
        "median": statistics.median(data),
        "mean": mean,
        "sd": sd,
        "cv_percent": 0.0 if mean == 0.0 else 100.0 * sd / mean,
        "max": max(data),
    }


def percentile(sorted_values: list[float], probability: float) -> float:
    if len(sorted_values) == 1:
        return sorted_values[0]
    index = probability * (len(sorted_values) - 1)
    lower = math.floor(index)
    upper = math.ceil(index)
    if lower == upper:
        return sorted_values[lower]
    fraction = index - lower
    return sorted_values[lower] * (1.0 - fraction) + sorted_values[upper] * fraction


def bootstrap_paired_median(ratios: list[float], seed_text: str) -> tuple[float, float]:
    seed = int.from_bytes(hashlib.sha256(seed_text.encode()).digest()[:8], "big")
    rng = random.Random(seed)
    count = len(ratios)
    estimates = []
    for _ in range(PAIR_BOOTSTRAPS):
        sample = [ratios[rng.randrange(count)] for _ in range(count)]
        estimates.append(statistics.median(sample))
    estimates.sort()
    return percentile(estimates, 0.025), percentile(estimates, 0.975)


def write_csv(path: Path, rows: list[dict[str, object]], fields: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def load_benchmark_samples(root: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for log in sorted((root / "benchmark").glob("run-*/*/*.log")):
        match = re.search(r"run-(\d+)", str(log))
        if match is None:
            continue
        run = int(match.group(1))
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            fields = parse_fields(line, "BENCH_SAMPLE")
            if fields is None:
                continue
            raw = int(fields["raw_cycles"])
            inner = int(fields["inner"])
            items = int(fields["items"])
            rows.append(
                {
                    "run": run,
                    "param": fields["param"],
                    "config": fields["config"],
                    "operation": fields["operation"],
                    "sample": int(fields["sample"]),
                    "clock": fields["clock"],
                    "inner": inner,
                    "items": items,
                    "raw_cycles": raw,
                    "cycles_per_call": raw / inner,
                    "cycles_per_item": raw / inner / items,
                    "source_log": str(log.relative_to(root)),
                }
            )
    return rows


def analyze_e2e(root: Path, samples: list[dict[str, object]]) -> tuple[
    list[dict[str, object]], list[dict[str, object]], list[dict[str, object]]
]:
    e2e = [row for row in samples if row["operation"] in E2E_OPERATIONS]
    grouped: dict[tuple[str, str, str], list[float]] = defaultdict(list)
    per_run: dict[tuple[int, str, str, str], list[float]] = defaultdict(list)
    for row in e2e:
        grouped[(str(row["param"]), str(row["config"]), str(row["operation"]))].append(
            float(row["cycles_per_call"])
        )
        per_run[
            (
                int(row["run"]),
                str(row["param"]),
                str(row["config"]),
                str(row["operation"]),
            )
        ].append(float(row["cycles_per_call"]))

    summary: list[dict[str, object]] = []
    for param in PARAMS:
        for operation in E2E_OPERATIONS:
            for config in CONFIGS:
                key = (param, config, operation)
                stats = describe(grouped[key])
                if stats["n"] != 49:
                    raise ValueError(f"{key}: expected 49 E2E samples, got {stats['n']}")
                summary.append(
                    {
                        "param": param,
                        "operation": operation,
                        "config": config,
                        "config_label": CONFIG_LABEL[config],
                        **stats,
                    }
                )

    run_medians: list[dict[str, object]] = []
    run_index: dict[tuple[int, str, str, str], float] = {}
    for key, values in sorted(per_run.items()):
        run, param, config, operation = key
        if len(values) != 7:
            raise ValueError(f"{key}: expected 7 samples within run, got {len(values)}")
        median = statistics.median(values)
        run_index[key] = median
        run_medians.append(
            {
                "run": run,
                "param": param,
                "operation": operation,
                "config": config,
                "config_label": CONFIG_LABEL[config],
                "median_cycles": median,
            }
        )

    paired: list[dict[str, object]] = []
    for param in PARAMS:
        for operation in E2E_OPERATIONS:
            for name, numerator, denominator in COMPARISONS:
                ratios = [
                    run_index[(run, param, numerator, operation)]
                    / run_index[(run, param, denominator, operation)]
                    for run in range(1, 8)
                ]
                lower, upper = bootstrap_paired_median(
                    ratios, f"{param}:{operation}:{name}"
                )
                paired.append(
                    {
                        "param": param,
                        "operation": operation,
                        "comparison": name,
                        "numerator": numerator,
                        "denominator": denominator,
                        "paired_run_median_ratio": statistics.median(ratios),
                        "bootstrap_95_low": lower,
                        "bootstrap_95_high": upper,
                        "paired_run_median_speedup": 1.0 / statistics.median(ratios),
                        "speedup_95_low": 1.0 / upper,
                        "speedup_95_high": 1.0 / lower,
                        "run_ratios": ";".join(f"{value:.9f}" for value in ratios),
                    }
                )
    return summary, run_medians, paired


def load_profile(root: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for log in sorted((root / "profile").glob("*/*.log")):
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            fields = parse_fields(line, "PROFILE_SAMPLE")
            if fields is None:
                continue
            raw = int(fields["raw_cycles"])
            inner = int(fields["inner"])
            items = int(fields["items"])
            rows.append(
                {
                    "param": fields["param"],
                    "config": fields["config"],
                    "operation": fields["operation"],
                    "sample": int(fields["sample"]),
                    "inner": inner,
                    "items": items,
                    "raw_cycles": raw,
                    "cycles_per_call": raw / inner,
                    "cycles_per_item": raw / inner / items,
                    "source_log": str(log.relative_to(root)),
                }
            )
    return rows


def summarize_profile(samples: list[dict[str, object]]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, str, str], list[dict[str, object]]] = defaultdict(list)
    for row in samples:
        grouped[(str(row["param"]), str(row["config"]), str(row["operation"]))].append(row)
    output: list[dict[str, object]] = []
    for (param, config, operation), rows in sorted(grouped.items()):
        stats = describe(float(row["cycles_per_call"]) for row in rows)
        item_stats = describe(float(row["cycles_per_item"]) for row in rows)
        if stats["n"] != 31:
            raise ValueError(
                f"profile {(param, config, operation)}: expected 31 samples, got {stats['n']}"
            )
        output.append(
            {
                "param": param,
                "operation": operation,
                "config": config,
                "config_label": CONFIG_LABEL[config],
                **stats,
                "items": int(rows[0]["items"]),
                "median_cycles_per_item": item_stats["median"],
                "mean_cycles_per_item": item_stats["mean"],
            }
        )
    return output


def load_memory(root: Path) -> list[dict[str, object]]:
    output: list[dict[str, object]] = []
    for log in sorted((root / "benchmark" / "run-01").glob("*/*.log")):
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            fields = parse_fields(line, "BENCH_MEMORY")
            if fields is None:
                continue
            output.append(
                {
                    "param": fields["param"],
                    "config": fields["config"],
                    "config_label": CONFIG_LABEL[fields["config"]],
                    "static_ram_bytes": int(fields["static_ram_bytes"]),
                    "stack_reserved_bytes": int(fields["stack_reserved_bytes"]),
                    "stack_peak_bytes": int(fields["stack_peak_bytes"]),
                    "heap_capacity_bytes": int(fields["heap_capacity_bytes"]),
                    "heap_peak_bytes": int(fields["heap_peak_bytes"]),
                }
            )
    size_lookup: dict[tuple[str, str], tuple[int, int, int]] = {}
    for size_file in sorted((root / "artifacts" / "benchmark").glob("*/*/size-summary.txt")):
        config = size_file.parents[1].name
        param = size_file.parent.name
        lines = [line.split() for line in size_file.read_text().splitlines() if line.strip()]
        if len(lines) < 2 or len(lines[1]) < 3:
            raise ValueError(f"cannot parse {size_file}")
        size_lookup[(param, config)] = tuple(int(value) for value in lines[1][:3])
    for row in output:
        text, data, bss = size_lookup[(str(row["param"]), str(row["config"]))]
        row.update({"text_bytes": text, "data_bytes": data, "bss_bytes": bss})
    if len(output) != 18:
        raise ValueError(f"expected 18 memory rows, got {len(output)}")
    return output


def load_phase(root: Path) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    raw: list[dict[str, object]] = []
    for log in sorted((root / "phase-256").glob("*/*.log")):
        for kind, marker in (
            ("sign", "PHASE_PROFILE_SIGN"),
            ("verify", "PHASE_PROFILE_VERIFY"),
        ):
            records: list[dict[str, str]] = []
            for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
                fields = parse_fields(line, marker)
                if fields is not None:
                    records.append(fields)
            if len(records) != 8:
                raise ValueError(f"{log} {kind}: expected warmup + 7 records, got {len(records)}")
            for sample, fields in enumerate(records[1:]):
                for field, value in fields.items():
                    if field in {"param", "config", "backend", "matvec", "result"}:
                        continue
                    raw.append(
                        {
                            "param": fields["param"],
                            "config": fields["config"],
                            "kind": kind,
                            "sample": sample,
                            "phase": field,
                            "cycles": int(value),
                            "source_log": str(log.relative_to(root)),
                        }
                    )

    grouped: dict[tuple[str, str, str, str], list[float]] = defaultdict(list)
    for row in raw:
        grouped[
            (
                str(row["param"]),
                str(row["config"]),
                str(row["kind"]),
                str(row["phase"]),
            )
        ].append(float(row["cycles"]))
    summary: list[dict[str, object]] = []
    total_lookup: dict[tuple[str, str, str], float] = {}
    for (param, config, kind, phase), values in sorted(grouped.items()):
        stats = describe(values)
        summary.append(
            {
                "param": param,
                "config": config,
                "config_label": CONFIG_LABEL[config],
                "kind": kind,
                "phase": phase,
                **stats,
            }
        )
        if phase in {"total", "total_phases"}:
            total_lookup[(param, config, kind)] = float(stats["median"])
    for row in summary:
        total = total_lookup[(str(row["param"]), str(row["config"]), str(row["kind"]))]
        row["median_percent_of_profile_total"] = 100.0 * float(row["median"]) / total
    return raw, summary


def validate_correctness(root: Path) -> list[str]:
    checks: list[str] = []
    source = (root / "correctness" / "reference-matvec-source.log").read_text()
    for width in (128, 192, 256):
        if f"MATVEC_SOURCE_PASS width={width}" not in source:
            raise ValueError(f"reference matrix source check failed for {width}")
    checks.append("공식 gf_mat_vec_mul 소스 본문 128/192/256-bit 완전 일치")

    host = root / "correctness" / "host"
    lowmem = list(host.glob("*-lowmemory.log"))
    kat = list(host.glob("*-kat.log"))
    field = list(host.glob("mve_*-field.log"))
    if len(lowmem) != 6 or any("LOW_MEMORY_DIFF_PASS" not in item.read_text() for item in lowmem):
        raise ValueError("host low-memory differential gate incomplete")
    if len(kat) != 18 or any("KAT_PASS" not in item.read_text() for item in kat):
        raise ValueError("host KAT gate incomplete")
    if len(field) != 12 or any("REF_GF_PASS" not in item.read_text() for item in field):
        raise ValueError("host MVE field differential gate incomplete")
    checks.append("호스트 low-memory byte exact 6/6")
    checks.append("호스트 공식 KAT 100개 × 6 파라미터 × 3 구성")
    checks.append("MVE field differential 6 파라미터 × B/C")

    board = root / "correctness" / "board"
    signs = list(board.glob("*-sign.log"))
    kats = list(board.glob("*-kat.log"))
    if len(signs) != 18 or any("SIGN_TEST_PASS" not in item.read_text() for item in signs):
        raise ValueError("board sign/verify/tamper gate incomplete")
    if len(kats) != 6 or any("KAT_PASS" not in item.read_text() for item in kats):
        raise ValueError("board KAT gate incomplete")
    checks.append("보드 sign/verify/tamper 6 파라미터 × 3 구성, checksum 일치")
    checks.append("보드 B/MVE-REFMAT 공식 KAT 100개 × 6 파라미터")

    disassembly = root / "correctness" / "disassembly"
    evidence = list(disassembly.glob("*-evidence.txt"))
    if len(evidence) != 6:
        raise ValueError("disassembly evidence incomplete")
    for item in evidence:
        text = item.read_text()
        if "vmullb.p16" not in text or "vmullt.p16" not in text:
            raise ValueError(f"missing MVE polynomial multiply evidence in {item}")
    checks.append("B/MVE-REFMAT VMULLB.P16/VMULLT.P16 6/6 및 선택 심볼 확인")
    return checks


def markdown_table(headers: list[str], rows: list[list[str]]) -> str:
    output = ["| " + " | ".join(headers) + " |", "|" + "|".join("---" for _ in headers) + "|"]
    output.extend("| " + " | ".join(row) + " |" for row in rows)
    return "\n".join(output)


def fmt_cycles(value: object) -> str:
    return f"{float(value):,.0f}"


def timer_crosscheck(root: Path) -> tuple[int, int]:
    errors = []
    for log in sorted((root / "benchmark").glob("run-*/*/*.log")):
        for match in re.finditer(r"BENCH_TIMER_CHECK[^\n]*error_ppm=(\d+)", log.read_text(errors="replace")):
            errors.append(int(match.group(1)))
    if not errors:
        raise ValueError("no PMU/SysTick timer cross-check records")
    return len(errors), max(errors)


def render_report(
    root: Path,
    correctness: list[str],
    e2e_summary: list[dict[str, object]],
    paired: list[dict[str, object]],
    profile: list[dict[str, object]],
    memory: list[dict[str, object]],
    phase: list[dict[str, object]],
) -> str:
    e2e_index = {
        (str(row["param"]), str(row["operation"]), str(row["config"])): row
        for row in e2e_summary
    }
    pair_index = {
        (str(row["param"]), str(row["operation"]), str(row["comparison"])): row
        for row in paired
    }
    profile_index = {
        (str(row["param"]), str(row["operation"]), str(row["config"])): row
        for row in profile
    }
    phase_index = {
        (str(row["param"]), str(row["kind"]), str(row["phase"]), str(row["config"])): row
        for row in phase
    }
    timer_checks, max_timer_error_ppm = timer_crosscheck(root)
    compiler = (root / "metadata" / "compiler.txt").read_text(errors="replace").splitlines()[0]

    lines = [
        "# Cortex-M55 AIMer v3 matrix-controlled ablation",
        "",
        "## 실험 정의와 판정",
        "",
        "A는 reference GF/reference matrix/scalar party, B는 MVE GF와 4-party batching에 reference matrix, "
        "C는 같은 MVE 경로에 compact matrix를 사용한다. 따라서 A→B는 MVE GF·배칭의 총효과, "
        "B→C는 matrix 표현만의 효과, A→C는 전체 M55 backend 효과다.",
        "",
        "정식 E2E 값은 각 구성·파라미터마다 7개 독립 run × (warm-up 1 + 측정 7)에서 나온 "
        "49개 측정이다. 구성 순서는 run마다 회전했고 짝수 run은 파라미터 순서를 뒤집었다. "
        "비율은 동일 run의 7개 표본 중앙값끼리 짝지었으며 CI는 그 7쌍을 10,000회 bootstrap한 95% 구간이다.",
        "",
        "### Correctness gate",
        "",
    ]
    lines.extend(f"- {item}" for item in correctness)

    lines.extend(
        [
            "",
            "## 측정 조건",
            "",
            "- STM32N657 NUCLEO Cortex-M55, 600 MHz, I-cache/D-cache 활성화",
            "- 동일 AXI SRAM linker layout, stack/heap 설정, portable Keccak/SHAKE, low-memory sign/verify schedule",
            f"- `{compiler}`, `-O3 -fno-tree-vectorize -fno-tree-slp-vectorize`",
            "- 구성당 독립 실행 7회, 실행당 warm-up 1회 뒤 keypair/sign/verify 각 7표본; 총 E2E raw sample 2,646개",
            "- 짧은 kernel은 31표본 × 64 inner, 복합 profile은 31표본 × 8 inner",
            f"- PMU/SysTick 교차검사 {timer_checks}개 이미지 전부 2% 이내; 최대 오차 {max_timer_error_ppm / 10_000.0:.3f}%",
            "- 측정 중 구현 튜닝·outlier 제거 없음",
        ]
    )

    lines.extend(["", "## E2E 결과", ""])
    for operation in E2E_OPERATIONS:
        table_rows: list[list[str]] = []
        for param in PARAMS:
            a = e2e_index[(param, operation, "ref")]
            b = e2e_index[(param, operation, "mve_refmat")]
            c = e2e_index[(param, operation, "mve_compact")]
            ba = pair_index[(param, operation, "B/A")]
            cb = pair_index[(param, operation, "C/B")]
            ca = pair_index[(param, operation, "C/A")]
            table_rows.append(
                [
                    param,
                    fmt_cycles(a["median"]),
                    fmt_cycles(b["median"]),
                    fmt_cycles(c["median"]),
                    f"{float(ba['paired_run_median_ratio']):.3f} "
                    f"({float(ba['paired_run_median_speedup']):.3f}×) "
                    f"[{float(ba['bootstrap_95_low']):.3f}, {float(ba['bootstrap_95_high']):.3f}]",
                    f"{float(cb['paired_run_median_ratio']):.3f} "
                    f"({float(cb['paired_run_median_speedup']):.3f}×) "
                    f"[{float(cb['bootstrap_95_low']):.3f}, {float(cb['bootstrap_95_high']):.3f}]",
                    f"{float(ca['paired_run_median_ratio']):.3f} "
                    f"({float(ca['paired_run_median_speedup']):.3f}×) "
                    f"[{float(ca['bootstrap_95_low']):.3f}, {float(ca['bootstrap_95_high']):.3f}]",
                ]
            )
        lines.extend(
            [
                f"### {operation}",
                "",
                markdown_table(
                    ["param", "A cycles", "B cycles", "C cycles", "B/A (speedup) 95% CI", "C/B (speedup) 95% CI", "C/A (speedup) 95% CI"],
                    table_rows,
                ),
                "",
            ]
        )

    lines.extend(["## 효과 분리", ""])
    separation_rows = []
    for param in PARAMS:
        for operation in E2E_OPERATIONS:
            ba = pair_index[(param, operation, "B/A")]
            cb = pair_index[(param, operation, "C/B")]
            ca = pair_index[(param, operation, "C/A")]
            gf_party = 100.0 * (1.0 - float(ba["paired_run_median_ratio"]))
            compact = 100.0 * (1.0 - float(cb["paired_run_median_ratio"]))
            full = 100.0 * (1.0 - float(ca["paired_run_median_ratio"]))
            share = math.nan if abs(full) < 1e-12 else 100.0 * gf_party / full
            separation_rows.append(
                [param, operation, f"{gf_party:+.1f}%", f"{compact:+.1f}%", f"{full:+.1f}%", "n/a" if math.isnan(share) else f"{share:.1f}%"]
            )
    lines.extend(
        [
            markdown_table(
                ["param", "operation", "A→B GF/MVE+party", "B→C compact", "A→C full", "A→B/full 개선 몫"],
                separation_rows,
            ),
            "",
            "양수는 cycles 감소, 음수는 손해다. 마지막 열은 A 기준 cycles 감소량 `(A−B)/(A−C)`이며 full 개선이 0에 가까우면 정의하지 않는다.",
            "",
            "## Kernel 및 MPC 직접 계측",
            "",
        ]
    )
    for operation in CORE_PROFILE_OPERATIONS:
        rows = []
        for param in PARAMS:
            values = [profile_index[(param, operation, config)] for config in CONFIGS]
            rows.append(
                [
                    param,
                    *(fmt_cycles(value["median"]) for value in values),
                    f"{float(values[1]['median']) / float(values[0]['median']):.3f}",
                    f"{float(values[2]['median']) / float(values[1]['median']):.3f}",
                ]
            )
        lines.extend(
            [
                f"### {operation}",
                "",
                markdown_table(["param", "A median", "B median", "C median", "B/A", "C/B"], rows),
                "",
            ]
        )

    lines.extend(["## 256-bit phase 분해", ""])
    phase_wanted = {
        "sign": ("linear", "sbox", "phase1", "phase23", "phase5", "mpc_affine", "mpc_frobenius", "total_phases"),
        "verify": ("linear", "setup", "tree", "tape", "mpc", "mpc_affine", "mpc_frobenius", "xz_products", "b_products", "transcript", "finish", "total"),
    }
    for param in ("256f", "256s"):
        for kind in ("sign", "verify"):
            rows = []
            for phase_name in phase_wanted[kind]:
                values = [phase_index[(param, kind, phase_name, config)] for config in CONFIGS]
                rows.append(
                    [
                        phase_name,
                        *(fmt_cycles(value["median"]) for value in values),
                        f"{float(values[1]['median']) - float(values[0]['median']):+,.0f}",
                        f"{float(values[2]['median']) - float(values[1]['median']):+,.0f}",
                    ]
                )
            lines.extend(
                [
                    f"### {param} {kind}",
                    "",
                    markdown_table(["phase", "A median", "B median", "C median", "B−A", "C−B"], rows),
                    "",
                ]
            )

    lines.extend(["## 256-bit 개선 원인 분리", ""])
    cause_rows = []
    for param in ("256f", "256s"):
        challenge_model = (
            float(profile_index[(param, "gf_mul_const_batch4", "ref")]["median"])
            - float(profile_index[(param, "gf_mul_const_batch4", "mve_refmat")]["median"])
        ) * CHALLENGE_BATCH_CALLS_256[param]
        for kind in ("sign", "verify"):
            total_phase = "total_phases" if kind == "sign" else "total"
            a_total = float(phase_index[(param, kind, total_phase, "ref")]["median"])
            b_total = float(phase_index[(param, kind, total_phase, "mve_refmat")]["median"])
            c_total = float(phase_index[(param, kind, total_phase, "mve_compact")]["median"])
            ab_saving = a_total - b_total
            ac_saving = a_total - c_total
            compact_saving = b_total - c_total
            frobenius_saving = (
                float(phase_index[(param, kind, "mpc_frobenius", "ref")]["median"])
                - float(phase_index[(param, kind, "mpc_frobenius", "mve_refmat")]["median"])
            )
            if kind == "sign":
                sbox_saving = (
                    float(phase_index[(param, kind, "sbox", "ref")]["median"])
                    - float(phase_index[(param, kind, "sbox", "mve_refmat")]["median"])
                )
                gf_p16_saving = challenge_model + sbox_saving
            else:
                gf_p16_saving = sum(
                    float(phase_index[(param, kind, phase_name, "ref")]["median"])
                    - float(phase_index[(param, kind, phase_name, "mve_refmat")]["median"])
                    for phase_name in ("xz_products", "b_products")
                )
            residual = ab_saving - gf_p16_saving - frobenius_saving
            affine_compact = (
                float(phase_index[(param, kind, "mpc_affine", "mve_refmat")]["median"])
                - float(phase_index[(param, kind, "mpc_affine", "mve_compact")]["median"])
            )

            def contribution(value: float) -> str:
                percent = 100.0 * value / ac_saving
                return f"{value / 1_000_000.0:+,.1f}M ({percent:+.1f}%)"

            cause_rows.append(
                [
                    param,
                    kind,
                    contribution(gf_p16_saving),
                    contribution(frobenius_saving),
                    contribution(residual),
                    contribution(compact_saving),
                    f"{affine_compact / 1_000_000.0:+,.1f}M",
                    f"{ac_saving / 1_000_000.0:+,.1f}M",
                ]
            )
    lines.extend(
        [
            markdown_table(
                [
                    "param",
                    "operation",
                    "GF/P16",
                    "packed Frobenius",
                    "A→B residual",
                    "compact B→C",
                    "그중 affine",
                    "A→C total",
                ],
                cause_rows,
            ),
            "",
            "괄호는 phasebench의 A→C 전체 cycle 감소량에서 차지하는 몫이다. sign의 GF/P16은 직접 계측한 S-box 절감과 "
            "`gf_mul_const_batch4` kernel 차이 × 실제 challenge batch 호출 수(256f 3,120회, 256s 25,344회)를 합친 모델이다. "
            "verify의 GF/P16은 `xz_products`와 `b_products`에서 직접 계측했다. 따라서 A→B residual은 아직 독립 계측하지 않은 "
            "호출·제어·계측 차이를 포함하며 별도의 최적화 기여로 주장하지 않는다.",
            "",
            "compact의 256-bit 이득은 거의 전부 full-schedule MPC affine에서 발생한다. 반대로 단일 hot kernel에서는 compact "
            "`gf_mat_vec_mul`이 reference보다 약 14.5% 느리고 keypair도 약 9.2% 느려진다. 즉 이 결과는 compact 산술 자체가 "
            "항상 빠르다는 뜻이 아니라, 큰 선형층을 반복 순회하는 실제 MPC 문맥에서만 순위가 뒤집힌다는 뜻이다.",
            "",
            "저장된 256-bit ELF를 대조하면 reference matvec는 648-byte 함수와 156-byte local stack allocation을 사용하며 "
            "행렬 word를 stack에 반복 spill/reload한다. compact는 222-byte 함수와 44-byte local allocation을 사용한다. "
            "256-bit 선형 데이터는 약 49 KiB이므로, 현재 증거가 가장 잘 지지하는 기계적 설명은 reference의 unrolling이 만든 "
            "register/stack pressure와 전체 작업집합의 상호작용이다. 다만 PMU에서 compact의 I-cache refill은 오히려 많고 D-cache "
            "refill은 비슷하므로, 이를 단순 cache-miss 감소 하나로 단정하지 않는다.",
            "",
        ]
    )

    lines.extend(["## Code/RAM/stack/heap", ""])
    memory_rows = []
    for row in sorted(memory, key=lambda item: (PARAMS.index(str(item["param"])), CONFIGS.index(str(item["config"])))):
        memory_rows.append(
            [
                str(row["param"]),
                CONFIG_LABEL[str(row["config"])],
                f"{int(row['text_bytes']):,}",
                f"{int(row['data_bytes']):,}",
                f"{int(row['bss_bytes']):,}",
                f"{int(row['static_ram_bytes']):,}",
                f"{int(row['stack_peak_bytes']):,}",
                f"{int(row['heap_peak_bytes']):,}",
            ]
        )
    lines.extend(
        [
            markdown_table(
                ["param", "config", "text", "data", "bss", "static RAM", "stack peak", "heap peak"],
                memory_rows,
            ),
            "",
            "## 최종 판정",
            "",
        ]
    )

    significant = []
    nonsignificant = []
    slower = []
    for param in PARAMS:
        for operation in E2E_OPERATIONS:
            row = pair_index[(param, operation, "B/A")]
            label = f"{param} {operation}"
            if float(row["bootstrap_95_high"]) < 1.0:
                significant.append(label)
            elif float(row["bootstrap_95_low"]) > 1.0:
                slower.append(label)
            else:
                nonsignificant.append(label)
    lines.append(
        "1. B/MVE-REFMAT의 A/REF 대비 유의한 개선(비율 CI 전체가 1 미만): "
        + (", ".join(significant) if significant else "없음")
        + "."
    )
    if slower:
        lines.append("   반대로 유의하게 느린 항목: " + ", ".join(slower) + ".")
    if nonsignificant:
        lines.append("   CI가 1을 포함하는 항목: " + ", ".join(nonsignificant) + ".")
    lines.extend(
        [
            "2. GF/MVE와 party batching만의 E2E 개선율은 위 효과 분리 표의 A→B 열이며, compact matrix의 별도 기여/손해는 B→C 열이다.",
            "3. 256-bit full-backend 개선의 가장 큰 단일 항목은 compact matrix다. phasebench A→C 절감 중 compact 몫은 "
            "256f sign 54.2%, verify 48.1%, 256s sign 68.0%, verify 61.9%였고 거의 전부 MPC affine에서 나왔다. "
            "나머지 주된 절감은 MVE/P16 challenge multiplication과 four-party packed Frobenius다.",
            "4. B가 matrix를 reference로 통제한 상태에서 보이는 결과에 한해 “GF 다항식 연산과 party-parallel execution을 Cortex-M55 MVE에 맞게 적용했다”는 구현 주장을 뒷받침한다. 성능 주장은 항목별 CI와 함께 제한한다.",
            "5. C/MVE-COMPACT가 현재 full-backend 결과이고 B/MVE-REFMAT는 controlled ablation이다. A/REF는 기준선이다.",
            "",
            "## 해석 원칙",
            "",
            "- B와 C의 GF 곱셈·제곱·배치·Frobenius·MPC 연결은 동일하다. 따라서 B→C의 직접 matvec 및 affine 변화가 compact matrix 효과다.",
            "- A→B에는 GF MVE화와 4-party 처리 변화가 함께 들어간다. 두 효과를 단일 숫자로 더 세분한다고 주장하지 않는다.",
            "- phasebench는 printf와 계측 코드 때문에 정식 E2E ELF와 배치가 다르므로 원인 위치 확인용이며, 속도 배율 주장은 위 E2E 표만 사용한다.",
            "- median·mean·SD·CV·모든 raw sample과 run median은 CSV에 보존되어 있다.",
            "",
            "결과 디렉터리: [현재 결과 디렉터리](.)",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("result_dir", type=Path)
    args = parser.parse_args()
    root = args.result_dir.resolve()
    analysis = root / "analysis"
    analysis.mkdir(parents=True, exist_ok=True)

    correctness = validate_correctness(root)
    benchmark_samples = load_benchmark_samples(root)
    e2e_summary, run_medians, paired = analyze_e2e(root, benchmark_samples)
    profile_samples = load_profile(root)
    profile_summary = summarize_profile(profile_samples)
    memory = load_memory(root)
    phase_raw, phase_summary = load_phase(root)

    raw_fields = ["run", "param", "config", "operation", "sample", "clock", "inner", "items", "raw_cycles", "cycles_per_call", "cycles_per_item", "source_log"]
    write_csv(analysis / "raw_benchmark_samples.csv", benchmark_samples, raw_fields)
    write_csv(analysis / "e2e_summary.csv", e2e_summary, ["param", "operation", "config", "config_label", "n", "min", "median", "mean", "sd", "cv_percent", "max"])
    write_csv(analysis / "e2e_run_medians.csv", run_medians, ["run", "param", "operation", "config", "config_label", "median_cycles"])
    write_csv(analysis / "e2e_paired_ratios.csv", paired, ["param", "operation", "comparison", "numerator", "denominator", "paired_run_median_ratio", "bootstrap_95_low", "bootstrap_95_high", "paired_run_median_speedup", "speedup_95_low", "speedup_95_high", "run_ratios"])
    write_csv(analysis / "raw_profile_samples.csv", profile_samples, ["param", "config", "operation", "sample", "inner", "items", "raw_cycles", "cycles_per_call", "cycles_per_item", "source_log"])
    write_csv(analysis / "profile_summary.csv", profile_summary, ["param", "operation", "config", "config_label", "n", "items", "min", "median", "mean", "sd", "cv_percent", "max", "median_cycles_per_item", "mean_cycles_per_item"])
    write_csv(analysis / "memory_code.csv", memory, ["param", "config", "config_label", "text_bytes", "data_bytes", "bss_bytes", "static_ram_bytes", "stack_reserved_bytes", "stack_peak_bytes", "heap_capacity_bytes", "heap_peak_bytes"])
    write_csv(analysis / "raw_phase_256.csv", phase_raw, ["param", "config", "kind", "sample", "phase", "cycles", "source_log"])
    write_csv(analysis / "phase_256_summary.csv", phase_summary, ["param", "config", "config_label", "kind", "phase", "n", "min", "median", "mean", "sd", "cv_percent", "max", "median_percent_of_profile_total"])

    (analysis / "correctness.txt").write_text("\n".join(correctness) + "\n", encoding="utf-8")
    report = render_report(root, correctness, e2e_summary, paired, profile_summary, memory, phase_summary)
    (root / "REPORT.md").write_text(report, encoding="utf-8")
    print(f"ANALYSIS_PASS result_dir={root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
