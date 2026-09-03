#!/usr/bin/env python3
"""Build an AIMer v3 AVX-512 causal report from diagnostic CSV files."""

from __future__ import annotations

import argparse
import csv
import statistics
from collections import defaultdict
from pathlib import Path


PARAMS = {
    "128f": {"bits": 128, "l": 2, "t": 33, "n": 16, "exponents": (3, 7, 11)},
    "128s": {"bits": 128, "l": 2, "t": 17, "n": 256, "exponents": (3, 7, 11)},
    "192f": {"bits": 192, "l": 2, "t": 49, "n": 16, "exponents": (11, 23, 47)},
    "192s": {"bits": 192, "l": 2, "t": 25, "n": 256, "exponents": (11, 23, 47)},
    "256f": {"bits": 256, "l": 3, "t": 65, "n": 16, "exponents": (3, 7, 11, 19)},
    "256s": {"bits": 256, "l": 3, "t": 33, "n": 256, "exponents": (3, 7, 11, 19)},
}


def read_rows(path: Path) -> list[list[str]]:
    with path.open(newline="") as stream:
        return [row for row in csv.reader(line for line in stream if not line.startswith("#")) if row]


def read_e2e(path: Path) -> dict[tuple[str, str, str], float]:
    samples = defaultdict(list)
    for row in read_rows(path):
        if row[0] == "backend":
            continue
        if row[0] in ("ref", "avx512") and len(row) >= 7:
            backend, variant, operation = row[:3]
            samples[(variant.removeprefix("AIMER-v3-"), operation, backend)].append(float(row[5]))
    return {cell: statistics.median(values) for cell, values in samples.items()}


def read_comparison(path: Path) -> dict[tuple[str, str, str], float]:
    values = {}
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            if row["backend"] in ("ref", "avx512"):
                values[(row["parameter"], row["operation"], row["backend"])] = float(
                    row["aim3_median_cycles"]
                )
    return values


def read_causes(path: Path) -> dict[tuple[str, str, str], float]:
    samples = defaultdict(list)
    for row in read_rows(path):
        if row[0] == "backend":
            continue
        if row[0] not in ("ref", "avx512") or len(row) < 8:
            continue
        backend, variant, kernel = row[:3]
        samples[(variant.removeprefix("AIMER-v3-"), kernel, backend)].append(float(row[7]))
    return {cell: statistics.median(values) for cell, values in samples.items()}


def require_cells(values: dict, expected: list[tuple], label: str) -> None:
    missing = [cell for cell in expected if cell not in values]
    if missing:
        formatted = ", ".join("/".join(cell) for cell in missing[:8])
        raise SystemExit(f"missing {label} cells: {formatted}")


def fmt_cycles(value: float) -> str:
    sign = "-" if value < 0 else ""
    value = abs(value)
    if value >= 1_000_000:
        return f"{sign}{value / 1_000_000:.3f}M"
    if value >= 1_000:
        return f"{sign}{value / 1_000:.1f}k"
    return f"{sign}{value:.1f}"


def speedup(ref: float, avx: float) -> float:
    return ref / avx


def main() -> None:
    parser = argparse.ArgumentParser()
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--e2e", type=Path, help="bench_full ref+avx512 CSV")
    source.add_argument("--comparison", type=Path, help="full_comparison.csv")
    parser.add_argument("--causes", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    e2e = read_e2e(args.e2e) if args.e2e else read_comparison(args.comparison)
    causes = read_causes(args.causes)
    variants = list(PARAMS)
    operations = ("keypair", "sign", "verify")
    kernels = (
        "gf_mul",
        "gf_mul_add",
        "gf_sqr",
        "gf_inv",
        "gf_mat_vec",
        "aim3_generate_linear",
        "aim3_mpc_affine_setup",
        "aim3_mpc_frobenius",
        "aim3_mpc_batch",
    )
    require_cells(
        e2e,
        [(variant, operation, backend) for variant in variants for operation in operations for backend in ("ref", "avx512")],
        "end-to-end",
    )
    require_cells(
        causes,
        [(variant, kernel, backend) for variant in variants for kernel in kernels for backend in ("ref", "avx512")],
        "cause-profile",
    )

    def saving(variant: str, kernel: str) -> float:
        return causes[(variant, kernel, "ref")] - causes[(variant, kernel, "avx512")]

    lines = [
        "# AIMer v3 AVX-512 causal decomposition",
        "",
        "This is a diagnostic model. It keeps the production implementations unchanged and combines end-to-end medians with independently timed components. The unmodeled residual includes SHAKE/tree orchestration, allocation, memory-context effects, and model error.",
        "",
        "## End-to-end",
        "",
        "| Parameter | Operation | Ref | AVX-512 | Speedup | Saved cycles |",
        "|---|---|---:|---:|---:|---:|",
    ]
    for variant in variants:
        for operation in operations:
            ref = e2e[(variant, operation, "ref")]
            avx = e2e[(variant, operation, "avx512")]
            lines.append(
                f"| {variant} | {operation} | {fmt_cycles(ref)} | {fmt_cycles(avx)} | "
                f"{speedup(ref, avx):.3f}x | {fmt_cycles(ref - avx)} |"
            )

    lines.extend(
        [
            "",
            "## Static work counts",
            "",
            "| Parameter | L | T | N | Frobenius exponents | Sum | MPC party-evaluations T*N |",
            "|---|---:|---:|---:|---|---:|---:|",
        ]
    )
    for variant, p in PARAMS.items():
        exponent_text = ", ".join(map(str, p["exponents"]))
        lines.append(
            f"| {variant} | {p['l']} | {p['t']} | {p['n']} | {exponent_text} | "
            f"{sum(p['exponents'])} | {p['t'] * p['n']:,} |"
        )

    lines.extend(
        [
            "",
            "## Keypair model",
            "",
            "The model is `generate_linear + 2L*matvec + (L+1)*inv + sum(e)*sqr + (L+1)*mul`. Inversion is measured as one complete kernel, so its internal multiplications and squarings are not counted twice.",
            "",
            "| Parameter | Linear-gen saving | Direct matvec | Inversion | Explicit sqr | Final mul | Predicted | Observed | Coverage |",
            "|---|---:|---:|---:|---:|---:|---:|---:|---:|",
        ]
    )
    for variant, p in PARAMS.items():
        parts = [
            saving(variant, "aim3_generate_linear"),
            2 * p["l"] * saving(variant, "gf_mat_vec"),
            (p["l"] + 1) * saving(variant, "gf_inv"),
            sum(p["exponents"]) * saving(variant, "gf_sqr"),
            (p["l"] + 1) * saving(variant, "gf_mul"),
        ]
        predicted = sum(parts)
        observed = e2e[(variant, "keypair", "ref")] - e2e[(variant, "keypair", "avx512")]
        coverage = predicted / observed * 100 if observed else 0.0
        lines.append(
            f"| {variant} | " + " | ".join(fmt_cycles(value) for value in parts) +
            f" | {fmt_cycles(predicted)} | {fmt_cycles(observed)} | {coverage:.1f}% |"
        )

    lines.extend(
        [
            "",
            "## Sign and verify model",
            "",
            "Known components are linear generation, signing-only S-box preparation, complete MPC, and scalar challenge multiplications. The AVX-512 signing source still performs challenge `gf_mul_add` party by party; its speedup comes from scalar PCLMUL, whereas the MPC row includes ZMM party batching.",
            "",
            "| Parameter | Op | Linear/S-box saving | MPC saving | Challenge GF saving | Known total | E2E saving | Residual |",
            "|---|---|---:|---:|---:|---:|---:|---:|",
        ]
    )
    for variant, p in PARAMS.items():
        linear = saving(variant, "aim3_generate_linear")
        sign_sbox = (
            p["l"] * saving(variant, "gf_mat_vec")
            + p["l"] * saving(variant, "gf_inv")
            + sum(p["exponents"][: p["l"]]) * saving(variant, "gf_sqr")
            + p["l"] * saving(variant, "gf_mul")
        )
        mpc = p["t"] * saving(variant, "aim3_mpc_batch")
        sign_mul_count = (3 * p["n"] + 1) * (p["l"] + 1) * p["t"]
        verify_mul_count = 3 * (p["n"] - 1) * (p["l"] + 1) * p["t"]
        for operation, preparation, count in (
            ("sign", linear + sign_sbox, sign_mul_count),
            ("verify", linear, verify_mul_count),
        ):
            challenge = count * saving(variant, "gf_mul_add")
            known = preparation + mpc + challenge
            observed = e2e[(variant, operation, "ref")] - e2e[(variant, operation, "avx512")]
            residual = observed - known
            lines.append(
                f"| {variant} | {operation} | {fmt_cycles(preparation)} | "
                f"{fmt_cycles(mpc)} | {fmt_cycles(challenge)} | {fmt_cycles(known)} | "
                f"{fmt_cycles(observed)} | {fmt_cycles(residual)} |"
            )

    lines.extend(
        [
            "",
            "## MPC split",
            "",
            "`affine_setup` and `frobenius` are diagnostic replicas of the two contiguous regions in the production MPC routine. Their sum is compared with the independently measured complete MPC call; the difference is an additivity/context residual, not assigned to either component.",
            "",
            "| Parameter | Region | Ref/call | AVX-512/call | Speedup | Saved over all T |",
            "|---|---|---:|---:|---:|---:|",
        ]
    )
    for variant, p in PARAMS.items():
        for kernel, label in (
            ("aim3_mpc_affine_setup", "affine+setup"),
            ("aim3_mpc_frobenius", "Frobenius"),
            ("aim3_mpc_batch", "complete MPC"),
        ):
            ref = causes[(variant, kernel, "ref")]
            avx = causes[(variant, kernel, "avx512")]
            lines.append(
                f"| {variant} | {label} | {fmt_cycles(ref)} | {fmt_cycles(avx)} | "
                f"{speedup(ref, avx):.3f}x | {fmt_cycles((ref - avx) * p['t'])} |"
            )
        ref_residual = causes[(variant, "aim3_mpc_batch", "ref")] - (
            causes[(variant, "aim3_mpc_affine_setup", "ref")]
            + causes[(variant, "aim3_mpc_frobenius", "ref")]
        )
        avx_residual = causes[(variant, "aim3_mpc_batch", "avx512")] - (
            causes[(variant, "aim3_mpc_affine_setup", "avx512")]
            + causes[(variant, "aim3_mpc_frobenius", "avx512")]
        )
        lines.append(
            f"| {variant} | split residual | {fmt_cycles(ref_residual)} | "
            f"{fmt_cycles(avx_residual)} | - | {fmt_cycles((ref_residual - avx_residual) * p['t'])} |"
        )

    lines.extend(
        [
            "",
            "## Layout interpretation",
            "",
            "- 128-bit squaring/multiplication packs four complete field elements into the four 128-bit ZMM lanes.",
            "- 192-bit also processes four parties at once, but represents every party as two 128-bit chunks and zeroes the upper 64 bits of the second chunk. Thus 64 of 256 represented bits per party are padding (25%).",
            "- 256-bit uses two full 128-bit chunks per party, so the same four-party schedule has no field-width padding.",
            "- Matrix batching has a different schedule: 128-bit shares broadcasts across 16 parties per block, while 192- and 256-bit process eight parties per block. The 192-bit matrix path uses masked three-word loads and does not over-read a padded fourth word.",
            "- Therefore a large 192-bit Frobenius speedup can coexist with padding: the exponent sum is 81, so the four-party batch amortization repeats far more times than at 128 bits (21) or 256 bits (40).",
            "",
            "## Interpretation rules",
            "",
            "- Use the E2E rows as the result. Component rows explain it; they do not replace it.",
            "- Do not force the modeled residual to zero. A large residual is evidence to add exact phase timing or memory/cache counters.",
            "- This diagnostic is not a pure AVX-512-instruction ablation: the optimized backend also changes scalar GF, matrix code, SHAKE x1/x4, and party scheduling.",
        ]
    )
    args.output.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
