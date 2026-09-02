#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Repeated and interleaved AIMer v2 end-to-end paper benchmark.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
E2E_BIN="${E2E_BIN_OVERRIDE:-$PROJECT_ROOT/build/tests/bench_full_paper}"
RUNS="${RUNS:-7}"
E2E_SAMPLES="${E2E_SAMPLES:-50}"
E2E_WARMUP="${E2E_WARMUP:-10}"
VERIFY="${VERIFY:-1}"
REBUILD="${REBUILD:-0}"
STRICT_ENV="${STRICT_ENV:-1}"
CFLAGS_USED="-O3 -Wall -Wextra -fomit-frame-pointer"
CC_NAME="${CC:-/usr/bin/cc}"

for value in "$RUNS" "$E2E_SAMPLES" "$E2E_WARMUP"; do
  [[ "$value" =~ ^[1-9][0-9]*$ ]] || { echo "run/sample/warmup values must be positive integers" >&2; exit 2; }
done
for value in "$VERIFY" "$REBUILD" "$STRICT_ENV"; do
  [[ "$value" == 0 || "$value" == 1 ]] || { echo "VERIFY, REBUILD, and STRICT_ENV must be 0 or 1" >&2; exit 2; }
done
command -v taskset >/dev/null 2>&1 || { echo "taskset is required" >&2; exit 1; }

affinity_list="$(taskset -pc "$$" | sed 's/.*: //')"
first_affinity="${affinity_list%%,*}"
default_core="${first_affinity%%-*}"
CORE="${CORE:-$default_core}"
taskset -c "$CORE" true >/dev/null 2>&1 || { echo "CPU $CORE is unavailable" >&2; exit 2; }

if [[ "$REBUILD" == 1 ]]; then
  make -C "$PROJECT_ROOT" -j2 build/tests/bench_full_paper
fi
[[ -x "$E2E_BIN" ]] || { echo "missing $E2E_BIN; build AIMer_v2 first" >&2; exit 1; }

timestamp="$(date +%Y%m%d-%H%M%S)"
RESULT_DIR="${RESULT_DIR:-$SCRIPT_DIR/results/paper-$timestamp}"
[[ ! -e "$RESULT_DIR" ]] || { echo "result directory already exists: $RESULT_DIR" >&2; exit 2; }
mkdir -p "$RESULT_DIR/runs"

governor="unknown"
epp="unknown"
scaling_min_freq="unknown"
scaling_max_freq="unknown"
[[ -r "/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_governor" ]] && governor="$(<"/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_governor")"
[[ -r "/sys/devices/system/cpu/cpu${CORE}/cpufreq/energy_performance_preference" ]] && epp="$(<"/sys/devices/system/cpu/cpu${CORE}/cpufreq/energy_performance_preference")"
[[ -r "/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_min_freq" ]] && scaling_min_freq="$(<"/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_min_freq")"
[[ -r "/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_max_freq" ]] && scaling_max_freq="$(<"/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_max_freq")"
turbo="unknown"
if [[ -r /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
  turbo="intel_pstate/no_turbo=$(</sys/devices/system/cpu/intel_pstate/no_turbo)"
elif [[ -r /sys/devices/system/cpu/cpufreq/boost ]]; then
  turbo="cpufreq/boost=$(</sys/devices/system/cpu/cpufreq/boost)"
fi
openocd_total="$(pgrep -xc openocd || true)"
openocd_running="$({ ps -C openocd -o stat= 2>/dev/null || true; } | awk '$1 !~ /^[TZ]/ {n++} END {print n+0}')"
environment_errors=()
[[ "$governor" == performance ]] || environment_errors+=("governor=$governor")
[[ "$epp" == performance ]] || environment_errors+=("epp=$epp")
[[ "$scaling_min_freq" != unknown && "$scaling_min_freq" == "$scaling_max_freq" ]] || environment_errors+=("frequency=${scaling_min_freq}..${scaling_max_freq}")
[[ "$turbo" == "intel_pstate/no_turbo=1" || "$turbo" == "cpufreq/boost=0" ]] || environment_errors+=("turbo=$turbo")
[[ "$openocd_running" == 0 ]] || environment_errors+=("openocd_running=$openocd_running")
if ((STRICT_ENV == 1 && ${#environment_errors[@]} != 0)); then
  printf 'paper environment check failed: %s\n' "${environment_errors[*]}" >&2
  exit 2
fi

if [[ "$VERIFY" == 1 ]]; then
  "$PROJECT_ROOT/tests/run_kat_all.sh" > "$RESULT_DIR/verification.log" 2>&1
fi
revision="not-a-git-worktree"
git -C "$PROJECT_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1 && revision="$(git -C "$PROJECT_ROOT" rev-parse HEAD)"
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
  echo "turbo=$turbo"
  echo "openocd_processes_start=$openocd_total"
  echo "openocd_running_start=$openocd_running"
  echo "online_cpus=$(</sys/devices/system/cpu/online)"
  echo "independent_runs=$RUNS"
  echo "e2e_samples_per_run=$E2E_SAMPLES"
  echo "e2e_warmup=$E2E_WARMUP"
  echo "verification=$VERIFY"
  echo "rebuild=$REBUILD"
  echo "strict_environment=$STRICT_ENV"
  echo "measurement_boundary=LFENCE-RDTSC-LFENCE/RDTSCP-LFENCE"
  echo "cc=$CC_NAME"
  echo "cflags=$CFLAGS_USED"
  echo "loadavg_start=$(</proc/loadavg)"
  echo "e2e_binary_sha256=$(sha256sum "$E2E_BIN" | cut -d' ' -f1)"
  echo "liboqs_sha256=$(sha256sum "$PROJECT_ROOT/build/lib/liboqs.a" | cut -d' ' -f1)"
  "$CC_NAME" --version | head -n 1
  echo
  lscpu
  echo
  lscpu -p=CPU,CORE,SOCKET
} > "$RESULT_DIR/metadata.txt"
(
  cd "$PROJECT_ROOT"
  find Makefile include src tests bench/bench_full_paper.c bench/run_paper.sh bench/analyze_paper.py -type f -print0 \
    | sort -z | xargs -0 sha256sum
) > "$RESULT_DIR/source_checksums.sha256"

variants=(AIMER-128f AIMER-128s AIMER-192f AIMER-192s AIMER-256f AIMER-256s)
snapshot() {
  local label="$1" destination="$2"
  {
    echo "label=$label"
    echo "timestamp=$(date --iso-8601=seconds)"
    echo "loadavg=$(</proc/loadavg)"
    [[ -r "/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_cur_freq" ]] && echo "scaling_cur_freq=$(<"/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_cur_freq")"
    command -v sensors >/dev/null 2>&1 && sensors || true
  } >> "$destination"
}

for ((run = 1; run <= RUNS; run++)); do
  run_name="$(printf 'run-%02d' "$run")"
  run_dir="$RESULT_DIR/runs/$run_name"
  mkdir -p "$run_dir/e2e"
  for backend in ref avx2 avx512; do
    echo '# backend,variant,op,N,min,median,max,mean,std,cv,med_us,mean_us,ops' > "$run_dir/e2e/$backend.csv"
  done
  case $(((run - 1) % 3)) in
    0) backends=(ref avx2 avx512) ;;
    1) backends=(avx2 avx512 ref) ;;
    2) backends=(avx512 ref avx2) ;;
  esac
  if ((run % 2 == 1)); then
    ordered_variants=("${variants[@]}")
  else
    ordered_variants=()
    for ((index = ${#variants[@]} - 1; index >= 0; index--)); do
      ordered_variants+=("${variants[index]}")
    done
  fi
  snapshot start "$run_dir/environment.txt"
  echo "[$run_name/$RUNS] backend order: ${backends[*]}"
  for variant in "${ordered_variants[@]}"; do
    for backend in "${backends[@]}"; do
      echo "[$run_name] $variant $backend end-to-end"
      taskset -c "$CORE" "$E2E_BIN" "$backend" "$variant" "$E2E_SAMPLES" "$E2E_WARMUP" >> "$run_dir/e2e/$backend.csv"
    done
  done
  snapshot end "$run_dir/environment.txt"
done

echo "loadavg_end=$(</proc/loadavg)" >> "$RESULT_DIR/metadata.txt"
PYTHONPYCACHEPREFIX="${TMPDIR:-/tmp}/aimer-v2-paper-pycache" python3 "$SCRIPT_DIR/analyze_paper.py" "$RESULT_DIR"
(
  cd "$RESULT_DIR"
  find . -type f ! -name checksums.sha256 -print0 | sort -z | xargs -0 sha256sum
) > "$RESULT_DIR/checksums.sha256"
echo "Paper benchmark results: $RESULT_DIR"
