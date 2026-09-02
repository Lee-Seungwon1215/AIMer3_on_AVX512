#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
KAT_BIN="$REPO_ROOT/build/tests/kat_sig"

if [[ ! -x "$KAT_BIN" ]]; then
  echo "Missing $KAT_BIN; run 'make -j' first." >&2
  exit 1
fi

cd "$REPO_ROOT"

variants=(
  AIMER-128f AIMER-128s
  AIMER-192f AIMER-192s
  AIMER-256f AIMER-256s
)

for impl in ref avx2 avx512; do
  echo "[implementation: $impl]"
  for variant in "${variants[@]}"; do
    AIMER_IMPL="$impl" AIMER_KECCAK="$impl" "$KAT_BIN" "$variant"
  done
done
