#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# One-machine diagnostic run. This does not replace the paper benchmark.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
CAUSE_BIN="$PROJECT_ROOT/build/liboqs/bench/bench_avx512_causes"
E2E_BIN="$PROJECT_ROOT/build/liboqs/bench/bench_full"
SAMPLES="${SAMPLES:-31}"
WARMUP="${WARMUP:-5}"
E2E_SAMPLES="${E2E_SAMPLES:-21}"
E2E_WARMUP="${E2E_WARMUP:-5}"
RUNS="${RUNS:-7}"
VERIFY="${VERIFY:-0}"

command -v taskset >/dev/null 2>&1 || {
  echo "taskset is required" >&2
  exit 1
}
affinity_list="$(taskset -pc "$$" | sed 's/.*: //')"
first_affinity="${affinity_list%%,*}"
CORE="${CORE:-${first_affinity%%-*}}"
taskset -c "$CORE" true >/dev/null 2>&1 || {
  echo "CPU $CORE is outside process affinity $affinity_list" >&2
  exit 2
}

for value in "$SAMPLES" "$WARMUP" "$E2E_SAMPLES" "$E2E_WARMUP" "$RUNS"; do
  [[ "$value" =~ ^[1-9][0-9]*$ ]] || {
    echo "sample and warmup values must be positive integers" >&2
    exit 2
  }
done
[[ "$VERIFY" == 0 || "$VERIFY" == 1 ]] || {
  echo "VERIFY must be 0 or 1" >&2
  exit 2
}

make -C "$PROJECT_ROOT" -j2 bench-causes
if [[ "$VERIFY" == 1 ]]; then
  make -C "$PROJECT_ROOT" -j2 oqs-check
fi

timestamp="$(date +%Y%m%d-%H%M%S)"
RESULT_DIR="${RESULT_DIR:-$SCRIPT_DIR/results/avx512-causes-$timestamp}"
if [[ -e "$RESULT_DIR" ]]; then
  echo "result directory already exists: $RESULT_DIR" >&2
  exit 2
fi
mkdir -p "$RESULT_DIR"

{
  echo "timestamp=$(date --iso-8601=seconds)"
  echo "git_revision=$(git -C "$PROJECT_ROOT" rev-parse HEAD)"
  echo "core=$CORE"
  echo "process_affinity=$affinity_list"
  echo "samples=$SAMPLES"
  echo "warmup=$WARMUP"
  echo "e2e_samples=$E2E_SAMPLES"
  echo "e2e_warmup=$E2E_WARMUP"
  echo "independent_runs=$RUNS"
  echo "verification=$VERIFY"
  echo "kernel=$(uname -srmo)"
  command -v lscpu >/dev/null 2>&1 && lscpu
} > "$RESULT_DIR/metadata.txt"

printf '%s\n' '# backend,variant,op,N,min,median,max,mean,std,cv,med_us,mean_us,ops' > "$RESULT_DIR/e2e.csv"
printf '%s\n' '# backend,variant,kernel,work_items,samples,inner,min,median,max,mean,std,cv,cycles_per_item' > "$RESULT_DIR/causes.csv"

variants=(128f 128s 192f 192s 256f 256s)
backends=(ref avx512)
operations=(
  gf_mul gf_mul_add gf_sqr gf_inv gf_mat_vec aim3_generate_linear
  aim3_mpc_affine_setup aim3_mpc_frobenius aim3_mpc_batch
)

inner_for() {
  case "$1" in
    gf_mul|gf_mul_add|gf_sqr) echo 32768 ;;
    gf_inv) echo 32 ;;
    gf_mat_vec) echo 1024 ;;
    aim3_generate_linear) echo 2 ;;
    aim3_mpc_affine_setup|aim3_mpc_frobenius) echo 4 ;;
    aim3_mpc_batch) echo 2 ;;
  esac
}

for ((run = 1; run <= RUNS; run++)); do
  echo "independent run $run/$RUNS"
  run_variants=("${variants[@]}")
  run_backends=("${backends[@]}")
  if ((run % 2 == 0)); then
    run_variants=(256s 256f 192s 192f 128s 128f)
    run_backends=(avx512 ref)
  fi
  for variant in "${run_variants[@]}"; do
    full_variant="AIMER-v3-$variant"
    for backend in "${run_backends[@]}"; do
      echo "[$backend] $full_variant end-to-end"
      taskset -c "$CORE" "$E2E_BIN" "$backend" "$full_variant" \
        "$E2E_SAMPLES" "$E2E_WARMUP" >> "$RESULT_DIR/e2e.csv"
      for operation in "${operations[@]}"; do
        inner="$(inner_for "$operation")"
        echo "[$backend] $full_variant $operation inner=$inner"
        taskset -c "$CORE" "$CAUSE_BIN" "$backend" "$full_variant" \
          "$SAMPLES" "$inner" "$WARMUP" "$operation" >> "$RESULT_DIR/causes.csv"
      done
    done
  done
done

python3 "$SCRIPT_DIR/analyze_avx512_causes.py" \
  --e2e "$RESULT_DIR/e2e.csv" \
  --causes "$RESULT_DIR/causes.csv" \
  --output "$RESULT_DIR/AVX512_CAUSE_ANALYSIS.md"

echo "AVX-512 causal report: $RESULT_DIR/AVX512_CAUSE_ANALYSIS.md"
