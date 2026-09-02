#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Repeated, interleaved AIMer v3 end-to-end and optimized-kernel benchmark.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
E2E_BIN="$PROJECT_ROOT/build/liboqs/bench/bench_full"
KERNEL_BIN="$PROJECT_ROOT/build/liboqs/bench/bench_kernels"

RUNS="${RUNS:-7}"
E2E_SAMPLES="${E2E_SAMPLES:-50}"
E2E_WARMUP="${E2E_WARMUP:-10}"
KERNEL_SAMPLES="${KERNEL_SAMPLES:-75}"
KERNEL_INNER="${KERNEL_INNER:-32}"
KERNEL_WARMUP="${KERNEL_WARMUP:-10}"
VERIFY="${VERIFY:-1}"
REBUILD="${REBUILD:-1}"
CFLAGS_USED="${CFLAGS:--O3 -Wall -Wextra -fomit-frame-pointer -std=c11}"
CC_NAME="${CC:-cc}"

for value in "$RUNS" "$E2E_SAMPLES" "$E2E_WARMUP" "$KERNEL_SAMPLES" \
             "$KERNEL_INNER" "$KERNEL_WARMUP"; do
  if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
    echo "all run/sample/warmup/inner values must be positive integers" >&2
    exit 2
  fi
done
if [[ "$VERIFY" != 0 && "$VERIFY" != 1 ]]; then
  echo "VERIFY must be 0 or 1" >&2
  exit 2
fi
if [[ "$REBUILD" != 0 && "$REBUILD" != 1 ]]; then
  echo "REBUILD must be 0 or 1" >&2
  exit 2
fi
if [[ " $CFLAGS_USED " != *" -O3 "* ]]; then
  echo "paper benchmark requires -O3 in CFLAGS" >&2
  exit 2
fi
if ! command -v taskset >/dev/null 2>&1; then
  echo "taskset is required" >&2
  exit 1
fi

affinity_list="$(taskset -pc "$$" | sed 's/.*: //')"
first_affinity="${affinity_list%%,*}"
default_core="${first_affinity%%-*}"
CORE="${CORE:-$default_core}"
if ! taskset -c "$CORE" true >/dev/null 2>&1; then
  echo "CPU $CORE is outside process affinity $affinity_list" >&2
  exit 2
fi

if [[ "$REBUILD" == 1 ]]; then
  make -C "$PROJECT_ROOT" clean
  make -C "$PROJECT_ROOT" -j2 CFLAGS="$CFLAGS_USED" bench
fi
if [[ ! -x "$E2E_BIN" || ! -x "$KERNEL_BIN" ]]; then
  echo "missing benchmark binaries; run make -C AIMer_v3 bench" >&2
  exit 1
fi
if [[ "$VERIFY" == 1 ]]; then
  make -C "$PROJECT_ROOT" -j2 CFLAGS="$CFLAGS_USED" oqs-check
fi

timestamp="$(date +%Y%m%d-%H%M%S)"
RESULT_DIR="${RESULT_DIR:-$SCRIPT_DIR/results/paper-$timestamp}"
mkdir -p "$RESULT_DIR/runs"

governor_path="/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_governor"
governor="unknown"
if [[ -r "$governor_path" ]]; then
  governor="$(<"$governor_path")"
fi
epp_path="/sys/devices/system/cpu/cpu${CORE}/cpufreq/energy_performance_preference"
epp="unknown"
if [[ -r "$epp_path" ]]; then
  epp="$(<"$epp_path")"
fi
scaling_min_path="/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_min_freq"
scaling_max_path="/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_max_freq"
scaling_min_freq="unknown"
scaling_max_freq="unknown"
if [[ -r "$scaling_min_path" ]]; then
  scaling_min_freq="$(<"$scaling_min_path")"
fi
if [[ -r "$scaling_max_path" ]]; then
  scaling_max_freq="$(<"$scaling_max_path")"
fi
intel_pstate_min_perf_pct="unknown"
intel_pstate_max_perf_pct="unknown"
if [[ -r /sys/devices/system/cpu/intel_pstate/min_perf_pct ]]; then
  intel_pstate_min_perf_pct="$(</sys/devices/system/cpu/intel_pstate/min_perf_pct)"
fi
if [[ -r /sys/devices/system/cpu/intel_pstate/max_perf_pct ]]; then
  intel_pstate_max_perf_pct="$(</sys/devices/system/cpu/intel_pstate/max_perf_pct)"
fi
turbo="unknown"
if [[ -r /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
  turbo="intel_pstate/no_turbo=$(</sys/devices/system/cpu/intel_pstate/no_turbo)"
elif [[ -r /sys/devices/system/cpu/cpufreq/boost ]]; then
  turbo="cpufreq/boost=$(</sys/devices/system/cpu/cpufreq/boost)"
fi
revision="not-a-git-worktree"
if git -C "$PROJECT_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  revision="$(git -C "$PROJECT_ROOT" rev-parse HEAD)"
fi

{
  echo "timestamp=$(date --iso-8601=seconds)"
  echo "project_root=$PROJECT_ROOT"
  echo "git_revision=$revision"
  echo "kernel=$(uname -srmo)"
  echo "core=$CORE"
  echo "process_affinity=$affinity_list"
  echo "governor=$governor"
  echo "energy_performance_preference=$epp"
  echo "scaling_min_freq=$scaling_min_freq"
  echo "scaling_max_freq=$scaling_max_freq"
  echo "intel_pstate_min_perf_pct=$intel_pstate_min_perf_pct"
  echo "intel_pstate_max_perf_pct=$intel_pstate_max_perf_pct"
  echo "turbo=$turbo"
  echo "independent_runs=$RUNS"
  echo "e2e_samples_per_run=$E2E_SAMPLES"
  echo "e2e_warmup=$E2E_WARMUP"
  echo "kernel_samples_per_run=$KERNEL_SAMPLES"
  echo "kernel_inner=$KERNEL_INNER"
  echo "kernel_warmup=$KERNEL_WARMUP"
  echo "verification=$VERIFY"
  echo "rebuild=$REBUILD"
  echo "cc=$CC_NAME"
  echo "cflags=$CFLAGS_USED"
  echo "cppflags=${CPPFLAGS:-}"
  echo "ldflags=${LDFLAGS:-}"
  echo "loadavg_start=$(</proc/loadavg)"
  if [[ -r /proc/sys/kernel/perf_event_paranoid ]]; then
    echo "perf_event_paranoid=$(</proc/sys/kernel/perf_event_paranoid)"
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    echo "e2e_binary_sha256=$(sha256sum "$E2E_BIN" | cut -d' ' -f1)"
    echo "kernel_binary_sha256=$(sha256sum "$KERNEL_BIN" | cut -d' ' -f1)"
    echo "liboqs_sha256=$(sha256sum "$PROJECT_ROOT/build/lib/liboqs.a" | cut -d' ' -f1)"
  fi
  "$CC_NAME" --version | head -n 1
  echo
  lscpu
  echo
  lscpu -p=CPU,CORE,SOCKET
} > "$RESULT_DIR/metadata.txt"

# Preserve the implementation and benchmark build inputs used for measurement.
if command -v sha256sum >/dev/null 2>&1; then
  (
    cd "$PROJECT_ROOT"
    find Makefile Makefile.liboqs.multi include src \
      benchmarks/Makefile.kernels.inc benchmarks/Makefile.paper.inc \
      benchmarks/bench_full.c benchmarks/bench_kernels.c \
      benchmarks/kernel_wrapper.c \
      benchmarks/run_paper.sh benchmarks/analyze_paper.py \
      -type f -print0 \
      | sort -z \
      | xargs -0 sha256sum
  ) > "$RESULT_DIR/source_checksums.sha256"
fi

if [[ "$governor" != performance ]]; then
  echo "warning: governor is '$governor'; results will be marked provisional" >&2
fi

if [[ "$epp" != performance ]]; then
  echo "warning: energy performance preference is $epp; results will be marked provisional" >&2
fi
if [[ "$scaling_min_freq" == unknown || "$scaling_min_freq" != "$scaling_max_freq" ]]; then
  echo "warning: CPU frequency range is ${scaling_min_freq}..${scaling_max_freq}; results will be marked provisional" >&2
fi

base_variants=(
  AIMER-v3-128f AIMER-v3-128s AIMER-v3-192f
  AIMER-v3-192s AIMER-v3-256f AIMER-v3-256s
)

snapshot() {
  local label="$1"
  local destination="$2"
  {
    echo "label=$label"
    echo "timestamp=$(date --iso-8601=seconds)"
    echo "loadavg=$(</proc/loadavg)"
    if [[ -r "/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_cur_freq" ]]; then
      echo "scaling_cur_freq=$(</sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_cur_freq)"
    fi
    if command -v sensors >/dev/null 2>&1; then
      sensors || true
    fi
  } >> "$destination"
}

for ((run = 1; run <= RUNS; run++)); do
  run_name="$(printf 'run-%02d' "$run")"
  run_dir="$RESULT_DIR/runs/$run_name"
  mkdir -p "$run_dir/e2e" "$run_dir/kernels"
  for backend in ref avx2 avx512; do
    echo '# backend,variant,op,N,min,median,max,mean,std,cv,med_us,mean_us,ops' \
      > "$run_dir/e2e/$backend.csv"
    echo '# backend,variant,kernel,work_items,N,inner,min,median,max,mean,std,cv,cycles_per_item' \
      > "$run_dir/kernels/$backend.csv"
  done

  case $(((run - 1) % 3)) in
    0) backends=(ref avx2 avx512) ;;
    1) backends=(avx2 avx512 ref) ;;
    2) backends=(avx512 ref avx2) ;;
  esac
  if ((run % 2 == 1)); then
    variants=("${base_variants[@]}")
  else
    variants=()
    for ((index = ${#base_variants[@]} - 1; index >= 0; index--)); do
      variants+=("${base_variants[index]}")
    done
  fi

  snapshot start "$run_dir/environment.txt"
  echo "[$run_name/$RUNS] backend order: ${backends[*]}"
  for variant in "${variants[@]}"; do
    for backend in "${backends[@]}"; do
      echo "[$run_name] $variant $backend end-to-end"
      taskset -c "$CORE" "$E2E_BIN" "$backend" "$variant" \
        "$E2E_SAMPLES" "$E2E_WARMUP" >> "$run_dir/e2e/$backend.csv"
      echo "[$run_name] $variant $backend kernels"
      taskset -c "$CORE" "$KERNEL_BIN" "$backend" "$variant" \
        "$KERNEL_SAMPLES" "$KERNEL_INNER" "$KERNEL_WARMUP" \
        >> "$run_dir/kernels/$backend.csv"
    done
  done
  snapshot end "$run_dir/environment.txt"
done

echo "loadavg_end=$(</proc/loadavg)" >> "$RESULT_DIR/metadata.txt"
PYTHONPYCACHEPREFIX="${TMPDIR:-/tmp}/aimer-v3-paper-pycache" \
  python3 "$SCRIPT_DIR/analyze_paper.py" "$RESULT_DIR"
echo "Paper benchmark results: $RESULT_DIR"
