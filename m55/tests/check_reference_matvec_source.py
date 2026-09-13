#!/usr/bin/env python3
"""Verify that the M55 matrix-only ablation copies official AIMer v3 code."""

from __future__ import annotations

import difflib
import hashlib
from pathlib import Path


SCRIPT = Path(__file__).resolve()
M55_ROOT = SCRIPT.parents[1]
AIMER_ROOT = SCRIPT.parents[2]
MATRIX_SOURCE = M55_ROOT / "src" / "ref" / "matvec_reference.c"
OFFICIAL = {
    128: AIMER_ROOT / "src" / "sig" / "aimer" / "reference" / "aimer-128f" / "field128.c",
    192: AIMER_ROOT / "src" / "sig" / "aimer" / "reference" / "aimer-192f" / "field192.c",
    256: AIMER_ROOT / "src" / "sig" / "aimer" / "reference" / "aimer-256f" / "field256.c",
}


def extract_function(source: str) -> str:
    start = source.index("void gf_mat_vec_mul(")
    brace = source.index("{", start)
    depth = 0
    for position in range(brace, len(source)):
        character = source[position]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return source[start : position + 1]
    raise ValueError("unterminated gf_mat_vec_mul")


def selected_branch(source: str, width: int) -> str:
    marker = ("#if" if width == 128 else "#elif") + f" SECURITY_BITS == {width}"
    start = source.index(marker) + len(marker)
    ends = [
        position
        for token in ("\n#elif SECURITY_BITS", "\n#else")
        if (position := source.find(token, start)) >= 0
    ]
    if not ends:
        raise ValueError(f"cannot find end of {width}-bit branch")
    return source[start : min(ends)]


def main() -> int:
    matrix_text = MATRIX_SOURCE.read_text(encoding="utf-8")
    failed = False
    for width, official_path in OFFICIAL.items():
        expected = extract_function(official_path.read_text(encoding="utf-8"))
        actual = extract_function(selected_branch(matrix_text, width))
        if expected != actual:
            failed = True
            print(f"MATVEC_SOURCE_FAIL width={width}")
            print(
                "".join(
                    difflib.unified_diff(
                        expected.splitlines(keepends=True),
                        actual.splitlines(keepends=True),
                        fromfile=str(official_path),
                        tofile=str(MATRIX_SOURCE),
                    )
                )
            )
            continue
        digest = hashlib.sha256(actual.encode("utf-8")).hexdigest()
        print(f"MATVEC_SOURCE_PASS width={width} sha256={digest}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
