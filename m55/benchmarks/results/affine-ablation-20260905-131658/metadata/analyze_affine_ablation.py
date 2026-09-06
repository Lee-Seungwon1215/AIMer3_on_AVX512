#!/usr/bin/env python3
"""Analyze the Cortex-M55 A/B/C/D affine controlled ablation."""

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


CONFIGS = ("ref", "mve_refmat", "mve_compact", "mve_affine")
LABEL = {
    "ref": "A/REF",
    "mve_refmat": "B/MVE-REFMAT",
    "mve_compact": "C/MVE-COMPACT",
    "mve_affine": "D/MVE-AFFINE",
}
PARAMS = ("128f", "128s", "192f", "192s", "256f", "256s")
E2E_OPS = ("keypair", "sign", "verify")
PROFILE_OPS = (
    "gf_mul",
    "gf_sqr",
    "gf_sqr_batch4",
    "gf_mul_const_batch4",
    "gf_mat_vec_mul",
    "gf_mat_vec_mul_add",
    "gf_mat_vec_mul_batch4",
    "gf_mat_vec_mul_add_batch4",
    "aim3_mpc_batch4_affine",
    "aim3_mpc_batch4_frobenius",
    "aim3_mpc_batch4",
    "aim3_mpc_scalar",
)
AFFINE_PROFILE_OPS = (
    "gf_mat_vec_mul",
    "gf_mat_vec_mul_add",
    "gf_mat_vec_mul_batch4",
    "gf_mat_vec_mul_add_batch4",
    "aim3_mpc_batch4_affine",
    "aim3_mpc_batch4_frobenius",
    "aim3_mpc_batch4",
)
COMPARISONS = (
    ("A/B", "ref", "mve_refmat"),
    ("B/D", "mve_refmat", "mve_affine"),
    ("C/D", "mve_compact", "mve_affine"),
    ("A/D", "ref", "mve_affine"),
)
BOOTSTRAPS = 10_000


def parse_fields(line: str, marker: str) -> dict[str, str] | None:
    token = marker + ","
    position = line.find(token)
    if position < 0:
        return None
    fields: dict[str, str] = {}
    for item in line[position + len(token) :].strip().split(","):
        if "=" in item:
            key, value = item.strip().split("=", 1)
            fields[key] = value.strip()
    return fields


def describe(values: Iterable[float]) -> dict[str, float | int]:
    data = list(values)
    if not data:
        raise ValueError("cannot summarize an empty sample")
    mean = statistics.fmean(data)
    return {
        "n": len(data),
        "min": min(data),
        "median": statistics.median(data),
        "mean": mean,
        "sd": statistics.pstdev(data),
        "cv_percent": 0.0 if mean == 0.0 else 100.0 * statistics.pstdev(data) / mean,
        "max": max(data),
    }


def percentile(sorted_values: list[float], probability: float) -> float:
    index = probability * (len(sorted_values) - 1)
    lower = math.floor(index)
    upper = math.ceil(index)
    if lower == upper:
        return sorted_values[lower]
    fraction = index - lower
    return sorted_values[lower] * (1.0 - fraction) + sorted_values[upper] * fraction


def bootstrap_paired(values: list[float], seed_text: str) -> tuple[float, float]:
    seed = int.from_bytes(hashlib.sha256(seed_text.encode()).digest()[:8], "big")
    rng = random.Random(seed)
    estimates = []
    for _ in range(BOOTSTRAPS):
        sample = [values[rng.randrange(len(values))] for _ in values]
        estimates.append(statistics.median(sample))
    estimates.sort()
    return percentile(estimates, 0.025), percentile(estimates, 0.975)


def write_csv(path: Path, rows: list[dict[str, object]], fields: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def load_samples(root: Path, kind: str, marker: str) -> list[dict[str, object]]:
    if kind == "benchmark":
        logs = sorted((root / kind).glob("run-*/*/*.log"))
    else:
        logs = sorted((root / kind).glob("*/*.log"))
    rows: list[dict[str, object]] = []
    for log in logs:
        run_match = re.search(r"run-(\d+)", str(log))
        run = int(run_match.group(1)) if run_match else 0
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            fields = parse_fields(line, marker)
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
                    "clock": fields.get("clock", "pmu"),
                    "inner": inner,
                    "items": items,
                    "raw_cycles": raw,
                    "cycles_per_call": raw / inner,
                    "cycles_per_item": raw / inner / items,
                    "source_log": str(log.relative_to(root)),
                }
            )
    return rows


def analyze_e2e(samples: list[dict[str, object]]) -> tuple[
    list[dict[str, object]], list[dict[str, object]], list[dict[str, object]]
]:
    grouped: dict[tuple[str, str, str], list[float]] = defaultdict(list)
    per_run: dict[tuple[int, str, str, str], list[float]] = defaultdict(list)
    for row in samples:
        if row["operation"] not in E2E_OPS:
            continue
        key = (str(row["param"]), str(row["operation"]), str(row["config"]))
        grouped[key].append(float(row["cycles_per_call"]))
        per_run[(int(row["run"]), *key)].append(float(row["cycles_per_call"]))

    summary: list[dict[str, object]] = []
    for param in PARAMS:
        for operation in E2E_OPS:
            for config in CONFIGS:
                key = (param, operation, config)
                stats = describe(grouped[key])
                if stats["n"] != 49:
                    raise ValueError(f"{key}: expected 49 samples, got {stats['n']}")
                summary.append(
                    {"param": param, "operation": operation, "config": config,
                     "config_label": LABEL[config], **stats}
                )

    run_rows: list[dict[str, object]] = []
    run_index: dict[tuple[int, str, str, str], float] = {}
    for key, values in sorted(per_run.items()):
        if len(values) != 7:
            raise ValueError(f"{key}: expected seven within-run samples, got {len(values)}")
        run, param, operation, config = key
        median = statistics.median(values)
        run_index[key] = median
        run_rows.append(
            {"run": run, "param": param, "operation": operation,
             "config": config, "config_label": LABEL[config],
             "median_cycles": median}
        )

    paired: list[dict[str, object]] = []
    for param in PARAMS:
        for operation in E2E_OPS:
            for name, baseline, target in COMPARISONS:
                speedups = [
                    run_index[(run, param, operation, baseline)]
                    / run_index[(run, param, operation, target)]
                    for run in range(1, 8)
                ]
                low, high = bootstrap_paired(speedups, f"{param}:{operation}:{name}")
                paired.append(
                    {"param": param, "operation": operation, "comparison": name,
                     "baseline": baseline, "target": target,
                     "paired_run_median_speedup": statistics.median(speedups),
                     "bootstrap_95_low": low, "bootstrap_95_high": high,
                     "run_speedups": ";".join(f"{value:.9f}" for value in speedups)}
                )
    return summary, run_rows, paired


def summarize_profile(samples: list[dict[str, object]]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, str, str], list[dict[str, object]]] = defaultdict(list)
    for row in samples:
        grouped[(str(row["param"]), str(row["operation"]), str(row["config"]))].append(row)
    output: list[dict[str, object]] = []
    for key, rows in sorted(grouped.items()):
        param, operation, config = key
        call_stats = describe(float(row["cycles_per_call"]) for row in rows)
        item_stats = describe(float(row["cycles_per_item"]) for row in rows)
        if call_stats["n"] != 31:
            raise ValueError(f"profile {key}: expected 31 samples, got {call_stats['n']}")
        output.append(
            {"param": param, "operation": operation, "config": config,
             "config_label": LABEL[config], **call_stats, "items": int(rows[0]["items"]),
             "median_cycles_per_item": item_stats["median"],
             "mean_cycles_per_item": item_stats["mean"]}
        )
    for param in PARAMS:
        for operation in PROFILE_OPS:
            for config in CONFIGS:
                if (param, operation, config) not in grouped:
                    raise ValueError(f"missing profile row {(param, operation, config)}")
    return output


def load_memory(root: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for log in sorted((root / "benchmark" / "run-01").glob("*/*.log")):
        for line in log.read_text(errors="replace").splitlines():
            fields = parse_fields(line, "BENCH_MEMORY")
            if fields is not None:
                rows.append(
                    {"param": fields["param"], "config": fields["config"],
                     "config_label": LABEL[fields["config"]],
                     "static_ram_bytes": int(fields["static_ram_bytes"]),
                     "stack_reserved_bytes": int(fields["stack_reserved_bytes"]),
                     "stack_peak_bytes": int(fields["stack_peak_bytes"]),
                     "heap_capacity_bytes": int(fields["heap_capacity_bytes"]),
                     "heap_peak_bytes": int(fields["heap_peak_bytes"])}
                )
    sizes: dict[tuple[str, str], tuple[int, int, int]] = {}
    for path in (root / "artifacts" / "benchmark").glob("*/*/size-summary.txt"):
        config = path.parents[1].name
        param = path.parent.name
        lines = [line.split() for line in path.read_text().splitlines() if line.strip()]
        sizes[(param, config)] = tuple(int(value) for value in lines[1][:3])
    for row in rows:
        text, data, bss = sizes[(str(row["param"]), str(row["config"]))]
        row.update({"text_bytes": text, "data_bytes": data, "bss_bytes": bss})
    if len(rows) != 24:
        raise ValueError(f"expected 24 memory rows, got {len(rows)}")
    return rows


def load_phase(root: Path) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    raw: list[dict[str, object]] = []
    for log in sorted((root / "phase-256").glob("*/*.log")):
        for kind, marker in (("sign", "PHASE_PROFILE_SIGN"),
                             ("verify", "PHASE_PROFILE_VERIFY")):
            records = [fields for line in log.read_text(errors="replace").splitlines()
                       if (fields := parse_fields(line, marker)) is not None]
            if len(records) != 8:
                raise ValueError(f"{log} {kind}: expected warmup + seven records")
            for sample, fields in enumerate(records[1:]):
                for phase, value in fields.items():
                    if phase in {"param", "config", "backend", "matvec", "result"}:
                        continue
                    raw.append(
                        {"param": fields["param"], "config": fields["config"],
                         "kind": kind, "sample": sample, "phase": phase,
                         "cycles": int(value), "source_log": str(log.relative_to(root))}
                    )
    grouped: dict[tuple[str, str, str, str], list[float]] = defaultdict(list)
    for row in raw:
        grouped[(str(row["param"]), str(row["kind"]), str(row["phase"]),
                 str(row["config"]))].append(float(row["cycles"]))
    summary = []
    for (param, kind, phase, config), values in sorted(grouped.items()):
        summary.append(
            {"param": param, "kind": kind, "phase": phase, "config": config,
             "config_label": LABEL[config], **describe(values)}
        )
    return raw, summary


def load_stack_usage(root: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    wanted = ("m55_gf_mat_vec_mul_add", "m55_gf_mat_vec_mul_add_batch4",
              "m55_aim3_mpc_batch4")
    for path in sorted((root / "artifacts" / "benchmark").glob("*/*/stack-usage/*.su")):
        config = path.parents[2].name
        param = path.parents[1].name
        for line in path.read_text(errors="replace").splitlines():
            parts = line.split("\t")
            if len(parts) < 3:
                continue
            function = parts[0].rsplit(":", 1)[-1]
            if any(function.endswith(name) for name in wanted):
                rows.append(
                    {"param": param, "config": config, "function": function,
                     "stack_bytes": int(parts[1]), "kind": parts[2]}
                )
    return rows


def validate_correctness(root: Path) -> list[str]:
    checks: list[str] = []
    source = (root / "correctness" / "reference-matvec-source.log").read_text()
    for width in (128, 192, 256):
        if f"MATVEC_SOURCE_PASS width={width}" not in source:
            raise ValueError(f"official reference matrix source check failed for {width}")
    checks.append("공식 reference matrix 함수 128/192/256-bit 소스 일치")

    host = root / "correctness" / "host"
    lowmem = list(host.glob("*-lowmemory.log"))
    fields = list(host.glob("*-field.log"))
    kats = list(host.glob("*-kat.log"))
    if len(lowmem) != 6 or any("LOW_MEMORY_DIFF_PASS" not in p.read_text() for p in lowmem):
        raise ValueError("host low-memory gate incomplete")
    if len(fields) != 24 or any("REF_GF_PASS" not in p.read_text() for p in fields):
        raise ValueError("host field differential gate incomplete")
    d_fields = list(host.glob("mve_affine-*-field.log"))
    if len(d_fields) != 6 or any("AFFINE_DIFF_PASS" not in p.read_text() for p in d_fields):
        raise ValueError("D host affine differential gate incomplete")
    if len(kats) != 24 or any("KAT_PASS" not in p.read_text() for p in kats):
        raise ValueError("host KAT gate incomplete")
    checks.extend(("host low-memory byte exact 6/6", "A/B/C/D host field differential 24/24",
                   "D host affine edge/alias/active-lane differential 6/6",
                   "A/B/C/D host 공식 KAT 2,400/2,400"))

    board = root / "correctness" / "board"
    affine = list(board.glob("mve_affine-*-affine.log"))
    signs = list(board.glob("*-sign.log"))
    kats = list(board.glob("mve_affine-*-kat.log"))
    if len(affine) != 6 or any("AFFINE_DIFF_PASS" not in p.read_text() or
                               "MVE=2" not in p.read_text() for p in affine):
        raise ValueError("D board affine gate incomplete")
    if len(signs) != 24 or any("SIGN_TEST_PASS" not in p.read_text() for p in signs):
        raise ValueError("board sign gate incomplete")
    if len(kats) != 6 or any("KAT_PASS" not in p.read_text() for p in kats):
        raise ValueError("D board KAT gate incomplete")
    for param in PARAMS:
        values = set()
        for path in board.glob(f"*-{param}-sign.log"):
            match = re.search(r"SIGN_TEST_PASS[^\n]*checksum=([0-9a-fA-F]+)", path.read_text())
            if match:
                values.add(match.group(1))
        if len(values) != 1:
            raise ValueError(f"board checksum mismatch for {param}: {values}")
    checks.extend(("D actual-board MVE affine differential 6/6 (MVE=2)",
                   "A/B/C/D board keypair/sign/verify/tamper 24/24 및 checksum 일치",
                   "D board 공식 KAT 600/600"))

    evidence = list((root / "correctness" / "disassembly").glob("*/aimer-*.affine.dis"))
    calls = list((root / "correctness" / "disassembly").glob("*/mpc-call.dis"))
    if len(evidence) != 6 or any("vand" not in p.read_text() or "veor" not in p.read_text()
                                 for p in evidence):
        raise ValueError("VAND/VEOR disassembly evidence incomplete")
    if len(calls) != 6 or any("m55_gf_mat_vec_mul" not in p.read_text() for p in calls):
        raise ValueError("MPC affine call evidence incomplete")
    checks.append("D VAND/VEOR 및 MPC affine 호출 연결 disassembly 6/6")
    return checks


def timer_check(root: Path) -> tuple[int, int]:
    errors = []
    for log in (root / "benchmark").glob("run-*/*/*.log"):
        errors.extend(int(value) for value in re.findall(
            r"BENCH_TIMER_CHECK[^\n]*error_ppm=(\d+)", log.read_text(errors="replace")))
    if len(errors) != 168:
        raise ValueError(f"expected 168 timer checks, got {len(errors)}")
    if max(errors) > 20_000:
        raise ValueError(f"timer gate exceeded: {max(errors)} ppm")
    return len(errors), max(errors)


def table(headers: list[str], rows: list[list[str]]) -> str:
    output = ["| " + " | ".join(headers) + " |",
              "|" + "|".join("---" for _ in headers) + "|"]
    output.extend("| " + " | ".join(row) + " |" for row in rows)
    return "\n".join(output)


def cycles(value: object) -> str:
    return f"{float(value):,.0f}"


def render_report(root: Path, correctness: list[str],
                  e2e: list[dict[str, object]], paired: list[dict[str, object]],
                  profile: list[dict[str, object]], memory: list[dict[str, object]],
                  phase: list[dict[str, object]], stack: list[dict[str, object]]) -> str:
    eidx = {(str(r["param"]), str(r["operation"]), str(r["config"])): r for r in e2e}
    pidx = {(str(r["param"]), str(r["operation"]), str(r["comparison"])): r for r in paired}
    kidx = {(str(r["param"]), str(r["operation"]), str(r["config"])): r for r in profile}
    phidx = {(str(r["param"]), str(r["kind"]), str(r["phase"]), str(r["config"])): r
             for r in phase}
    timer_count, max_ppm = timer_check(root)
    compiler = (root / "metadata" / "compiler.txt").read_text(errors="replace").splitlines()[0]

    lines = [
        "# Cortex-M55 AIMer v3 affine MVE controlled ablation", "",
        "## 실제 변경 범위", "",
        "D/MVE-AFFINE는 B/C의 MVE GF 곱셈·제곱·reduction·packed Frobenius를 그대로 사용하고, "
        "단일 affine와 4-party affine만 MVE VAND/VEOR 누산으로 교체한다. 4-party Q register의 "
        "32-bit lane 네 개는 서로 다른 party이며, 두 output word accumulator가 같은 mask와 matrix word를 공유한다.", "",
        "AIMer_v2, AIMer_v3/src, AVX2/AVX-512, GF/Frobenius, Keccak/SHAKE, low-memory schedule, "
        "linker layout과 직렬화는 변경하지 않았다. packed affine→Frobenius fusion도 하지 않았다.", "",
        "## 구성과 재현 명령", "",
        "- A/REF: `BACKEND=ref MATVEC=reference`", "- B/MVE-REFMAT: `BACKEND=mve MATVEC=reference`",
        "- C/MVE-COMPACT: `BACKEND=mve MATVEC=compact`", "- D/MVE-AFFINE: `BACKEND=mve MATVEC=mve`", "",
        "```sh", "make -B PARAM=128f BACKEND=mve MATVEC=mve TEST=benchmark \\",
        "  MEMORY=full SIGN_SCHEDULE=lowmem run-board", "./benchmarks/run_affine_ablation.sh", "```", "",
        "## Correctness gates", "",
    ]
    lines.extend(f"- {item}" for item in correctness)
    lines.extend([
        "", "Host scalar emulation은 API·edge·alias 검증이며 실제 MVE 실행 증거가 아니다. 실제 보드 differential, "
        "MVE=2 출력과 disassembly VAND/VEOR를 별도 gate로 사용했다. 이는 functional/구현 증거이며 "
        "constant-time의 완전한 증명은 아니다.", "", "## 측정 조건과 통계", "",
        "- STM32N657 NUCLEO Cortex-M55, 600 MHz, I-cache/D-cache 활성화", 
        "- 동일 AXI SRAM layout, stack/heap, portable Keccak/SHAKE, low-memory sign/verify schedule",
        f"- `{compiler}`, `-O3 -fno-tree-vectorize -fno-tree-slp-vectorize`",
        "- 구성·파라미터·연산당 7 independent run × 7 measured sample = 49; 각 run에 warm-up 1회",
        "- 표준편차는 49개 raw cycle 표본의 population SD(ddof=0); profile도 31표본의 population SD",
        "- speedup은 baseline cycles / target cycles. 1보다 크면 개선, 1보다 작으면 성능 저하",
        "- paired speedup은 동일 run의 7표본 중앙값끼리 계산한 7개 ratio의 중앙값; 95% CI는 "
        "그 7개 paired ratio를 10,000회 bootstrap",
        f"- PMU/SysTick 교차검사 {timer_count}/168 통과, 최대 오차 {max_ppm / 10_000.0:.3f}%", "",
        "## E2E cycle 분포", "",
    ])
    distribution_rows = []
    for param in PARAMS:
        for operation in E2E_OPS:
            for config in CONFIGS:
                row = eidx[(param, operation, config)]
                distribution_rows.append([param, operation, LABEL[config], cycles(row["median"]),
                                          cycles(row["mean"]), cycles(row["sd"]), str(row["n"])])
    lines.extend([table(["param", "operation", "config", "median", "mean", "population SD", "n"],
                        distribution_rows), "", "## E2E paired speedup", ""])
    for operation in E2E_OPS:
        rows = []
        for param in PARAMS:
            values = [eidx[(param, operation, config)] for config in CONFIGS]
            ratios = [pidx[(param, operation, name)] for name, _, _ in COMPARISONS]
            rows.append([param, *(cycles(value["median"]) for value in values),
                         *(f"{float(r['paired_run_median_speedup']):.3f}× "
                           f"[{float(r['bootstrap_95_low']):.3f}, {float(r['bootstrap_95_high']):.3f}]"
                           for r in ratios)])
        lines.extend([f"### {operation}", "",
                      table(["param", "A cycles", "B cycles", "C cycles", "D cycles",
                             "A/B speedup [95% CI]", "B/D speedup [95% CI]",
                             "C/D speedup [95% CI]", "A/D speedup [95% CI]"], rows), ""])

    lines.extend(["## Affine kernel과 MPC", "",
                  "4-party 행은 같은 입력 네 개를 처리한 scalar 기준(A/B/C fallback)과 D를 비교한다. "
                  "call cycles에는 packing/unpacking이 포함되고 per-party는 call/4다. 제외 수치는 공식 개선율로 사용하지 않았다.", ""])
    for operation in AFFINE_PROFILE_OPS:
        rows = []
        for param in PARAMS:
            values = [kidx[(param, operation, config)] for config in CONFIGS]
            b_d = float(values[1]["median"]) / float(values[3]["median"])
            c_d = float(values[2]["median"]) / float(values[3]["median"])
            a_d = float(values[0]["median"]) / float(values[3]["median"])
            rows.append([param, *(cycles(value["median"]) for value in values),
                         cycles(values[3]["median_cycles_per_item"]),
                         f"{b_d:.3f}×", f"{c_d:.3f}×", f"{a_d:.3f}×"])
        lines.extend([f"### {operation}", "",
                      table(["param", "A call", "B call", "C call", "D call", "D per item",
                             "B/D", "C/D", "A/D"], rows), ""])

    lines.extend(["## GF/Frobenius 회귀 관찰", ""])
    regression_rows = []
    for param in PARAMS:
        for operation in ("gf_mul", "gf_sqr", "gf_sqr_batch4", "gf_mul_const_batch4"):
            values = [kidx[(param, operation, config)] for config in CONFIGS]
            regression_rows.append([param, operation, *(cycles(v["median"]) for v in values),
                                    f"{float(values[3]['median']) / float(values[1]['median']):.3f}"])
    lines.extend([table(["param", "operation", "A", "B", "C", "D", "D/B cycles ratio"],
                        regression_rows), "", "D와 B의 GF/Frobenius 소스는 동일하다. 위 차이는 binary layout·측정 잡음을 포함한 "
                        "회귀 관찰이며 새 GF 알고리즘의 효과로 해석하지 않는다.", ""])

    lines.extend(["## 256-bit phase 진단", ""])
    phase_rows = []
    for param in ("256f", "256s"):
        for kind in ("sign", "verify"):
            total_name = "total_phases" if kind == "sign" else "total"
            for phase_name in ("mpc_affine", "mpc_frobenius", total_name):
                values = [phidx[(param, kind, phase_name, config)] for config in CONFIGS]
                phase_rows.append([param, kind, phase_name, *(cycles(v["median"]) for v in values),
                                   f"{float(values[1]['median']) / float(values[3]['median']):.3f}×",
                                   f"{float(values[2]['median']) / float(values[3]['median']):.3f}×"])
    lines.extend([table(["param", "kind", "phase", "A", "B", "C", "D", "B/D", "C/D"],
                        phase_rows), "", "phasebench는 printf와 계측으로 공식 benchmark ELF의 배치를 바꾸므로 원인 위치 진단용이다. "
                        "E2E speedup 주장에는 사용하지 않는다.", ""])

    lines.extend(["## Code/RAM/stack/heap", ""])
    memory_rows = []
    for row in sorted(memory, key=lambda item: (PARAMS.index(str(item["param"])),
                                                 CONFIGS.index(str(item["config"])))):
        memory_rows.append([str(row["param"]), LABEL[str(row["config"])],
                            f"{int(row['text_bytes']):,}", f"{int(row['data_bytes']):,}",
                            f"{int(row['bss_bytes']):,}", f"{int(row['static_ram_bytes']):,}",
                            f"{int(row['stack_peak_bytes']):,}", f"{int(row['heap_peak_bytes']):,}"])
    lines.extend([table(["param", "config", "text", "data", "bss", "static RAM",
                         "stack peak", "heap peak"], memory_rows), ""])
    if stack:
        stack_rows = [[str(r["param"]), LABEL[str(r["config"])], str(r["function"]),
                       str(r["stack_bytes"]), str(r["kind"])] for r in stack]
        lines.extend(["### Compiler stack-usage evidence", "",
                      table(["param", "config", "function", "bytes", "kind"], stack_rows), ""])
    lines.extend([
        "D disassembly는 VAND/VEOR와 `m55_aim3_mpc_batch4` 호출 연결을 확인했다. 저장된 함수별 disassembly와 "
        "`.su` 파일이 register save/restore, spill/reload, code size 검토의 근거다. intrinsic 사용만으로 "
        "레지스터 유지나 개선을 단정하지 않는다.", "", "## 논문에서 주장 가능한 내용과 한계", "",
        "- 주장 가능: 동일 공식 행렬과 AIMer 파라미터를 유지하면서 Cortex-M55 MVE에 맞춘 masked AND/XOR, "
        "4-party data reuse와 작은 accumulator block을 독립 D backend로 구현했고 byte-exact/공식 KAT를 통과했다.",
        "- 성능 주장은 위 paired A/B, B/D, C/D, A/D 결과와 CI가 직접 지지하는 파라미터·연산에 한정한다.",
        "- B→D는 SIMD 논리 연산, matrix reuse, scheduling, packing/unpacking의 결합효과다. VAND 또는 VEOR "
        "한 명령의 독립 기여율로 분해하지 않는다.",
        "- KAT와 fixed test는 functional correctness 증거이지 constant-time 완전 증명이 아니다.",
        "- D가 모든 항목에서 최선이라고 가정하지 않으며 parameter별 최솟값을 합성 backend처럼 제시하지 않는다.",
        "- packed affine→Frobenius fusion, GF/Frobenius·Keccak 변경과 혼합 dispatch는 이번 범위 밖이다.", "",
        "전체 raw samples, population 통계, 7-run paired ratio, ELF/MAP/disassembly, stack usage와 checksum은 "
        "이 결과 디렉터리에 보존되어 있다.", "", "결과 디렉터리: [현재 결과 디렉터리](.)", "",
    ])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("result_dir", type=Path)
    args = parser.parse_args()
    root = args.result_dir.resolve()
    analysis = root / "analysis"
    analysis.mkdir(parents=True, exist_ok=True)

    correctness = validate_correctness(root)
    benchmark = load_samples(root, "benchmark", "BENCH_SAMPLE")
    e2e, run_medians, paired = analyze_e2e(benchmark)
    profile_raw = load_samples(root, "profile", "PROFILE_SAMPLE")
    profile = summarize_profile(profile_raw)
    memory = load_memory(root)
    phase_raw, phase = load_phase(root)
    stack = load_stack_usage(root)

    raw_fields = ["run", "param", "config", "operation", "sample", "clock", "inner",
                  "items", "raw_cycles", "cycles_per_call", "cycles_per_item", "source_log"]
    write_csv(analysis / "raw_benchmark_samples.csv", benchmark, raw_fields)
    write_csv(analysis / "e2e_summary.csv", e2e,
              ["param", "operation", "config", "config_label", "n", "min", "median",
               "mean", "sd", "cv_percent", "max"])
    write_csv(analysis / "e2e_run_medians.csv", run_medians,
              ["run", "param", "operation", "config", "config_label", "median_cycles"])
    write_csv(analysis / "e2e_paired_speedups.csv", paired,
              ["param", "operation", "comparison", "baseline", "target",
               "paired_run_median_speedup", "bootstrap_95_low", "bootstrap_95_high",
               "run_speedups"])
    write_csv(analysis / "raw_profile_samples.csv", profile_raw, raw_fields)
    write_csv(analysis / "profile_summary.csv", profile,
              ["param", "operation", "config", "config_label", "n", "items", "min",
               "median", "mean", "sd", "cv_percent", "max", "median_cycles_per_item",
               "mean_cycles_per_item"])
    write_csv(analysis / "memory_code.csv", memory,
              ["param", "config", "config_label", "text_bytes", "data_bytes", "bss_bytes",
               "static_ram_bytes", "stack_reserved_bytes", "stack_peak_bytes",
               "heap_capacity_bytes", "heap_peak_bytes"])
    write_csv(analysis / "raw_phase_256.csv", phase_raw,
              ["param", "config", "kind", "sample", "phase", "cycles", "source_log"])
    write_csv(analysis / "phase_256_summary.csv", phase,
              ["param", "kind", "phase", "config", "config_label", "n", "min", "median",
               "mean", "sd", "cv_percent", "max"])
    write_csv(analysis / "stack_usage.csv", stack,
              ["param", "config", "function", "stack_bytes", "kind"])
    (analysis / "correctness.txt").write_text("\n".join(correctness) + "\n", encoding="utf-8")
    (root / "REPORT.md").write_text(
        render_report(root, correctness, e2e, paired, profile, memory, phase, stack),
        encoding="utf-8",
    )
    print(f"ANALYSIS_PASS result_dir={root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
