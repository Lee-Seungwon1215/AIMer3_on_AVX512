#!/usr/bin/env python3
"""Choose one fair inner-loop count per AIMer v3 parameter/kernel cell."""

from __future__ import annotations

import csv
import math
import sys
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


def next_power_of_two(value: int) -> int:
    return 1 << (value - 1).bit_length()


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit(
            f"usage: {sys.argv[0]} <calibration-dir> <target-batch-cycles> <output.csv>"
        )
    calibration_dir = Path(sys.argv[1])
    target = int(sys.argv[2])
    output = Path(sys.argv[3])
    if target < 1_000_000:
        raise SystemExit("target batch must be at least 1,000,000 TSC cycles")

    measurements: dict[tuple[str, str], dict[str, tuple[float, int, int]]] = {}
    for backend in BACKENDS:
        path = calibration_dir / f"{backend}.csv"
        if not path.is_file():
            raise SystemExit(f"missing calibration file: {path}")
        for row in read_rows(path):
            if len(row) != 13 or row[0] != backend:
                raise SystemExit(f"invalid calibration row in {path}: {row}")
            variant, kernel = row[1], row[2]
            if variant not in VARIANTS or kernel not in KERNELS:
                raise SystemExit(f"unexpected calibration cell: {variant}/{kernel}")
            key = (variant, kernel)
            if backend in measurements.setdefault(key, {}):
                raise SystemExit(f"duplicate calibration cell: {backend}/{variant}/{kernel}")
            measurements[key][backend] = (float(row[7]), int(row[4]), int(row[5]))

    expected = {(variant, kernel) for variant in VARIANTS for kernel in KERNELS}
    if set(measurements) != expected:
        missing = sorted(expected - set(measurements))
        extra = sorted(set(measurements) - expected)
        raise SystemExit(f"incomplete calibration: missing={missing}, extra={extra}")

    with output.open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(
            [
                "variant",
                "kernel",
                "inner",
                "target_batch_cycles",
                "fastest_backend",
                "fastest_median_cycles",
                "estimated_min_batch_cycles",
                "calibration_samples",
                "calibration_inner",
            ]
        )
        for variant in VARIANTS:
            for kernel in KERNELS:
                rows = measurements[(variant, kernel)]
                if set(rows) != set(BACKENDS):
                    raise SystemExit(f"missing backend calibration: {variant}/{kernel}")
                fastest_backend = min(BACKENDS, key=lambda backend: rows[backend][0])
                fastest, samples, calibration_inner = rows[fastest_backend]
                if not math.isfinite(fastest) or fastest <= 0:
                    raise SystemExit(f"invalid median for {variant}/{kernel}: {fastest}")
                required = max(1, math.ceil(target / fastest))
                inner = next_power_of_two(required)
                if inner > (1 << 30):
                    raise SystemExit(f"unsafe inner count for {variant}/{kernel}: {inner}")
                writer.writerow(
                    [
                        variant,
                        kernel,
                        inner,
                        target,
                        fastest_backend,
                        f"{fastest:.2f}",
                        f"{fastest * inner:.2f}",
                        samples,
                        calibration_inner,
                    ]
                )


if __name__ == "__main__":
    main()
