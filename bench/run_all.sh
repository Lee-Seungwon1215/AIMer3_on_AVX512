#!/usr/bin/env bash
# Benchmark the Reference, AVX2, and AVX-512 implementations under identical
# conditions. Run from any directory; results are written below bench/results.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
RESULT_DIR="${RESULT_DIR:-$SCRIPT_DIR/results}"
CORE="${CORE:-2}"
N="${1:-${ITERS:-50}}"
BENCH_BIN="$REPO_ROOT/build/tests/bench_full"

if [[ ! -x "$BENCH_BIN" ]]; then
  echo "Missing $BENCH_BIN; run 'make -j' first." >&2
  exit 1
fi

if ! command -v taskset >/dev/null 2>&1; then
  echo "taskset is required for single-core pinning." >&2
  exit 1
fi

mkdir -p "$RESULT_DIR"

governor_path="/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_governor"
governor="unknown"
if [[ -r "$governor_path" ]]; then
  governor="$(<"$governor_path")"
fi

echo "core=$CORE governor=$governor samples=$N"
if [[ "$governor" != "performance" ]]; then
  echo "warning: the CPU-frequency governor is not 'performance'" >&2
fi

variants=(
  AIMER-128f AIMER-128s
  AIMER-192f AIMER-192s
  AIMER-256f AIMER-256s
)

for impl in ref avx2 avx512; do
  result_file="$RESULT_DIR/$impl.csv"
  : > "$result_file"
  for variant in "${variants[@]}"; do
    echo "[$impl] $variant"
    AIMER_IMPL="$impl" AIMER_KECCAK="$impl" \
      taskset -c "$CORE" "$BENCH_BIN" "$impl" "$variant" "$N" \
      >> "$result_file"
  done
done

python3 "$SCRIPT_DIR/compare.py" \
  "$RESULT_DIR/ref.csv" "$RESULT_DIR/avx2.csv" "$RESULT_DIR/avx512.csv"
