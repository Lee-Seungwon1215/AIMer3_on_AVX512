#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Paper-grade adaptive AIMer v3 optimized-kernel benchmark.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
KERNEL_BIN="${KERNEL_BIN_OVERRIDE:-$PROJECT_ROOT/build/liboqs/bench/bench_kernels}"

RUNS="${RUNS:-15}"
KERNEL_SAMPLES="${KERNEL_SAMPLES:-75}"
KERNEL_WARMUP="${KERNEL_WARMUP:-10}"
CALIBRATION_SAMPLES="${CALIBRATION_SAMPLES:-31}"
CALIBRATION_INNER="${CALIBRATION_INNER:-256}"
CALIBRATION_WARMUP="${CALIBRATION_WARMUP:-5}"
TARGET_BATCH_CYCLES="${TARGET_BATCH_CYCLES:-2800000}"
VERIFY="${VERIFY:-1}"
REBUILD="${REBUILD:-1}"
STRICT_ENV="${STRICT_ENV:-1}"
REQUIRE_EXCLUSIVE_CPU="${REQUIRE_EXCLUSIVE_CPU:-0}"
SIBLING_CPU="${SIBLING_CPU:-}"
PACKAGE_ISOLATION="${PACKAGE_ISOLATION:-0}"
IRQ_ISOLATION="${IRQ_ISOLATION:-0}"
HOUSEKEEPING_CPU="${HOUSEKEEPING_CPU:-}"
EXPECTED_ONLINE_CPUS="${EXPECTED_ONLINE_CPUS:-}"
BACKEND_COOLDOWN_SECONDS="${BACKEND_COOLDOWN_SECONDS:-0.05}"
RUN_COOLDOWN_SECONDS="${RUN_COOLDOWN_SECONDS:-2}"
PRE_MEASUREMENT_COOLDOWN_SECONDS="${PRE_MEASUREMENT_COOLDOWN_SECONDS:-15}"
CFLAGS_USED="${CFLAGS:--O3 -Wall -Wextra -fomit-frame-pointer -std=c11}"
CC_NAME="${CC:-cc}"

for value in "$RUNS" "$KERNEL_SAMPLES" "$KERNEL_WARMUP" \
             "$CALIBRATION_SAMPLES" "$CALIBRATION_INNER" \
             "$CALIBRATION_WARMUP" "$TARGET_BATCH_CYCLES"; do
  if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
    echo "all run/sample/warmup/inner/target values must be positive integers" >&2
    exit 2
  fi
done
for value in "$VERIFY" "$REBUILD" "$STRICT_ENV" "$REQUIRE_EXCLUSIVE_CPU" "$PACKAGE_ISOLATION" "$IRQ_ISOLATION"; do
  if [[ "$value" != 0 && "$value" != 1 ]]; then
    echo "VERIFY, REBUILD, STRICT_ENV, REQUIRE_EXCLUSIVE_CPU, PACKAGE_ISOLATION, and IRQ_ISOLATION must be 0 or 1" >&2
    exit 2
  fi
done
for value in "$BACKEND_COOLDOWN_SECONDS" "$RUN_COOLDOWN_SECONDS" "$PRE_MEASUREMENT_COOLDOWN_SECONDS"; do
  if [[ ! "$value" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "cooldown values must be non-negative decimal seconds" >&2
    exit 2
  fi
done
if [[ "$REQUIRE_EXCLUSIVE_CPU" == 1 && ! "$SIBLING_CPU" =~ ^[0-9]+$ ]]; then
  echo "SIBLING_CPU must name the offline SMT sibling in exclusive mode" >&2
  exit 2
fi
if ((TARGET_BATCH_CYCLES < 1000000)); then
  echo "TARGET_BATCH_CYCLES must be at least 1000000 for paper measurements" >&2
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
if [[ ! -x "$KERNEL_BIN" ]]; then
  echo "missing kernel benchmark binary; run make -C x86 bench" >&2
  exit 1
fi

timestamp="$(date +%Y%m%d-%H%M%S)"
RESULT_DIR="${RESULT_DIR:-$SCRIPT_DIR/results/kernel-paper-$timestamp}"
if [[ -e "$RESULT_DIR" ]]; then
  echo "result directory already exists: $RESULT_DIR" >&2
  exit 2
fi

governor_path="/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_governor"
epp_path="/sys/devices/system/cpu/cpu${CORE}/cpufreq/energy_performance_preference"
scaling_min_path="/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_min_freq"
scaling_max_path="/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_max_freq"
siblings_path="/sys/devices/system/cpu/cpu${CORE}/topology/thread_siblings_list"
governor="unknown"
epp="unknown"
scaling_min_freq="unknown"
scaling_max_freq="unknown"
thread_siblings="unknown"
[[ -r "$governor_path" ]] && governor="$(<"$governor_path")"
[[ -r "$epp_path" ]] && epp="$(<"$epp_path")"
[[ -r "$scaling_min_path" ]] && scaling_min_freq="$(<"$scaling_min_path")"
[[ -r "$scaling_max_path" ]] && scaling_max_freq="$(<"$scaling_max_path")"
[[ -r "$siblings_path" ]] && thread_siblings="$(<"$siblings_path")"
turbo="unknown"
if [[ -r /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
  turbo="intel_pstate/no_turbo=$(</sys/devices/system/cpu/intel_pstate/no_turbo)"
elif [[ -r /sys/devices/system/cpu/cpufreq/boost ]]; then
  turbo="cpufreq/boost=$(</sys/devices/system/cpu/cpufreq/boost)"
fi
openocd_total="$(pgrep -xc openocd || true)"
openocd_running="$({ ps -C openocd -o stat= 2>/dev/null || true; } | awk '$1 !~ /^[TZ]/ {n++} END {print n+0}')"
cgroup_path="$(cut -d: -f3 /proc/self/cgroup)"
cgroup_cpuset_effective="unknown"
if [[ -r "/sys/fs/cgroup${cgroup_path}/cpuset.cpus.effective" ]]; then
  cgroup_cpuset_effective="$(<"/sys/fs/cgroup${cgroup_path}/cpuset.cpus.effective")"
fi
sibling_online="unknown"
if [[ -n "$SIBLING_CPU" ]]; then
  sibling_online=1
  [[ -r "/sys/devices/system/cpu/cpu${SIBLING_CPU}/online" ]] && \
    sibling_online="$(<"/sys/devices/system/cpu/cpu${SIBLING_CPU}/online")"
fi

environment_errors=()
[[ "$governor" == performance ]] || environment_errors+=("governor=$governor")
[[ "$epp" == performance ]] || environment_errors+=("epp=$epp")
[[ "$scaling_min_freq" != unknown && "$scaling_min_freq" == "$scaling_max_freq" ]] || \
  environment_errors+=("frequency=${scaling_min_freq}..${scaling_max_freq}")
[[ "$turbo" == "intel_pstate/no_turbo=1" || "$turbo" == "cpufreq/boost=0" ]] || \
  environment_errors+=("turbo=$turbo")
[[ "$openocd_running" == 0 ]] || environment_errors+=("openocd_running=$openocd_running")
if [[ "$REQUIRE_EXCLUSIVE_CPU" == 1 ]]; then
  [[ "$cgroup_cpuset_effective" == "$CORE" ]] || environment_errors+=("cpuset=$cgroup_cpuset_effective")
  [[ "$sibling_online" == 0 ]] || environment_errors+=("sibling_cpu_${SIBLING_CPU}_online=$sibling_online")
fi
if ((STRICT_ENV == 1 && ${#environment_errors[@]} != 0)); then
  printf 'paper environment check failed: %s\n' "${environment_errors[*]}" >&2
  exit 2
fi
mkdir -p "$RESULT_DIR/calibration/raw" "$RESULT_DIR/runs"
online_cpus="$(</sys/devices/system/cpu/online)"
result_storage_fs="$(stat -f -c %T "$RESULT_DIR")"
if [[ "$PACKAGE_ISOLATION" == 1 ]]; then
  [[ -n "$HOUSEKEEPING_CPU" ]] || environment_errors+=("housekeeping_cpu=missing")
  [[ -n "$EXPECTED_ONLINE_CPUS" && "$online_cpus" == "$EXPECTED_ONLINE_CPUS" ]] || \
    environment_errors+=("online_cpus=$online_cpus expected=$EXPECTED_ONLINE_CPUS")
  [[ "$result_storage_fs" == tmpfs ]] || environment_errors+=("result_storage_fs=$result_storage_fs")
  [[ "$IRQ_ISOLATION" == 1 ]] || environment_errors+=("irq_isolation=$IRQ_ISOLATION")
fi
if ((STRICT_ENV == 1 && ${#environment_errors[@]} != 0)); then
  printf 'paper environment check failed: %s\n' "${environment_errors[*]}" >&2
  exit 2
fi

revision="not-a-git-worktree"
if git -C "$PROJECT_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  revision="$(git -C "$PROJECT_ROOT" rev-parse HEAD)"
fi

if [[ "$VERIFY" == 1 ]]; then
  make -C "$PROJECT_ROOT" -j2 CFLAGS="$CFLAGS_USED" oqs-check \
    > "$RESULT_DIR/verification.log" 2>&1
fi

{
  echo "timestamp=$(date --iso-8601=seconds)"
  echo "project_root=$PROJECT_ROOT"
  echo "git_revision=$revision"
  echo "kernel=$(uname -srmo)"
  echo "core=$CORE"
  echo "thread_siblings=$thread_siblings"
  echo "process_affinity=$affinity_list"
  echo "exclusive_cpu_required=$REQUIRE_EXCLUSIVE_CPU"
  echo "cgroup_path=$cgroup_path"
  echo "cgroup_cpuset_effective=$cgroup_cpuset_effective"
  echo "sibling_cpu=${SIBLING_CPU:-unknown}"
  echo "sibling_online=$sibling_online"
  echo "package_isolation=$PACKAGE_ISOLATION"
  echo "irq_isolation=$IRQ_ISOLATION"
  echo "housekeeping_cpu=${HOUSEKEEPING_CPU:-unknown}"
  echo "expected_online_cpus=${EXPECTED_ONLINE_CPUS:-unknown}"
  echo "online_cpus=$online_cpus"
  echo "result_storage_fs=$result_storage_fs"
  echo "governor=$governor"
  echo "energy_performance_preference=$epp"
  echo "scaling_min_freq=$scaling_min_freq"
  echo "scaling_max_freq=$scaling_max_freq"
  if [[ -r /sys/devices/system/cpu/intel_pstate/min_perf_pct ]]; then
    echo "intel_pstate_min_perf_pct=$(</sys/devices/system/cpu/intel_pstate/min_perf_pct)"
  fi
  if [[ -r /sys/devices/system/cpu/intel_pstate/max_perf_pct ]]; then
    echo "intel_pstate_max_perf_pct=$(</sys/devices/system/cpu/intel_pstate/max_perf_pct)"
  fi
  echo "turbo=$turbo"
  echo "openocd_processes_start=$openocd_total"
  echo "openocd_running_start=$openocd_running"
  echo "independent_runs=$RUNS"
  echo "kernel_samples_per_run=$KERNEL_SAMPLES"
  echo "kernel_warmup_batches=$KERNEL_WARMUP"
  echo "kernel_calibration_samples=$CALIBRATION_SAMPLES"
  echo "kernel_calibration_inner=$CALIBRATION_INNER"
  echo "kernel_calibration_warmup_batches=$CALIBRATION_WARMUP"
  echo "kernel_calibration_target_cycles=$TARGET_BATCH_CYCLES"
  echo "backend_cooldown_seconds=$BACKEND_COOLDOWN_SECONDS"
  echo "run_cooldown_seconds=$RUN_COOLDOWN_SECONDS"
  echo "pre_measurement_cooldown_seconds=$PRE_MEASUREMENT_COOLDOWN_SECONDS"
  echo "verification=$VERIFY"
  echo "rebuild=$REBUILD"
  echo "strict_environment=$STRICT_ENV"
  echo "isolation_driver=${ISOLATION_DRIVER:-none}"
  echo "cc=$CC_NAME"
  echo "cflags=$CFLAGS_USED"
  echo "cppflags=${CPPFLAGS:-}"
  echo "ldflags=${LDFLAGS:-}"
  echo "loadavg_start=$(</proc/loadavg)"
  [[ -r /proc/sys/kernel/perf_event_paranoid ]] && \
    echo "perf_event_paranoid=$(</proc/sys/kernel/perf_event_paranoid)"
  echo "kernel_binary_sha256=$(sha256sum "$KERNEL_BIN" | cut -d' ' -f1)"
  echo "liboqs_sha256=$(sha256sum "$PROJECT_ROOT/build/lib/liboqs.a" | cut -d' ' -f1)"
  "$CC_NAME" --version | head -n 1
  echo
  lscpu
  echo
  lscpu -p=CPU,CORE,SOCKET
} > "$RESULT_DIR/metadata.txt"

(
  cd "$PROJECT_ROOT"
  find Makefile Makefile.liboqs.multi include src \
    benchmarks/Makefile.kernels.inc benchmarks/Makefile.paper.inc \
    benchmarks/bench_kernels.c benchmarks/kernel_wrapper.c \
    benchmarks/calibrate_kernels.py benchmarks/analyze_kernel_paper.py \
    benchmarks/run_kernel_paper.sh \
    benchmarks/run_kernel_paper_isolated.sh \
    -type f -print0 \
    | sort -z \
    | xargs -0 sha256sum
) > "$RESULT_DIR/source_checksums.sha256"

sleep "$PRE_MEASUREMENT_COOLDOWN_SECONDS"
variants=(
  AIMER-v3-128f AIMER-v3-128s AIMER-v3-192f
  AIMER-v3-192s AIMER-v3-256f AIMER-v3-256s
)
kernels=(
  gf_mul gf_sqr gf_inv gf_mat_vec gf_sqr_batch gf_mul_add_batch
  gf_mat_vec_batch gf_mat_vec_add_batch aim3_mpc_batch
  shake_commit_tape_x1 shake_commit_tape_x4
)

read_numeric_irq_total() {
  awk -v wanted="CPU${CORE}" '
    NR == 1 { for (i = 1; i <= NF; i++) if ($i == wanted) column = i + 1; next }
    $1 ~ /^[0-9]+:$/ && column { total += $column }
    END { printf "%.0f\n", total + 0 }
  ' /proc/interrupts
}

snapshot() {
  local label="$1"
  local destination="$2"
  {
    echo "label=$label"
    echo "timestamp=$(date --iso-8601=seconds)"
    echo "loadavg=$(</proc/loadavg)"
    if [[ -r "/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_cur_freq" ]]; then
      echo "scaling_cur_freq=$(<"/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_cur_freq")"
    fi
    if command -v sensors >/dev/null 2>&1; then
      sensors || true
    fi
  } >> "$destination"
}

for backend in ref avx2 avx512; do
  raw="$RESULT_DIR/calibration/raw/$backend.csv"
  echo '# backend,variant,kernel,work_items,N,inner,min,median,max,mean,std,cv,cycles_per_item' > "$raw"
  for variant in "${variants[@]}"; do
    echo "[calibration] $variant $backend"
    sleep "$BACKEND_COOLDOWN_SECONDS"
    taskset -c "$CORE" "$KERNEL_BIN" "$backend" "$variant" \
      "$CALIBRATION_SAMPLES" "$CALIBRATION_INNER" "$CALIBRATION_WARMUP" >> "$raw"
  done
done
python3 "$SCRIPT_DIR/calibrate_kernels.py" "$RESULT_DIR/calibration/raw" \
  "$TARGET_BATCH_CYCLES" "$RESULT_DIR/calibration/inner_map.csv"

lookup_inner() {
  local variant="$1"
  local kernel="$2"
  awk -F, -v variant="$variant" -v kernel="$kernel" \
    'NR > 1 && $1 == variant && $2 == kernel { print $3; found = 1; exit } END { if (!found) exit 1 }' \
    "$RESULT_DIR/calibration/inner_map.csv"
}

echo 'phase,run,variant,kernel,backend,irq_before,irq_after,irq_delta' > "$RESULT_DIR/irq_telemetry.csv"
for ((run = 1; run <= RUNS; run++)); do
  sleep "$RUN_COOLDOWN_SECONDS"
  run_name="$(printf 'run-%02d' "$run")"
  run_dir="$RESULT_DIR/runs/$run_name"
  mkdir -p "$run_dir"
  for backend in ref avx2 avx512; do
    echo '# backend,variant,kernel,work_items,N,inner,min,median,max,mean,std,cv,cycles_per_item' \
      > "$run_dir/$backend.csv"
  done
  if ((run % 2 == 1)); then
    run_variants=("${variants[@]}")
    run_kernels=("${kernels[@]}")
  else
    run_variants=()
    run_kernels=()
    for ((index = ${#variants[@]} - 1; index >= 0; index--)); do
      run_variants+=("${variants[index]}")
    done
    for ((index = ${#kernels[@]} - 1; index >= 0; index--)); do
      run_kernels+=("${kernels[index]}")
    done
  fi
  snapshot start "$run_dir/environment.txt"
  cell=0
  for variant in "${run_variants[@]}"; do
    echo "[$run_name/$RUNS] $variant"
    for kernel in "${run_kernels[@]}"; do
      inner="$(lookup_inner "$variant" "$kernel")"
      case $(((run + cell - 1) % 3)) in
        0) run_backends=(ref avx2 avx512) ;;
        1) run_backends=(avx2 avx512 ref) ;;
        2) run_backends=(avx512 ref avx2) ;;
      esac
      for backend in "${run_backends[@]}"; do
        sleep "$BACKEND_COOLDOWN_SECONDS"
        irq_before="$(read_numeric_irq_total)"
        measurement="$(taskset -c "$CORE" "$KERNEL_BIN" "$backend" "$variant" \
          "$KERNEL_SAMPLES" "$inner" "$KERNEL_WARMUP" "$kernel")"
        irq_after="$(read_numeric_irq_total)"
        printf '%s\n' "$measurement" >> "$run_dir/$backend.csv"
        printf 'measurement,%s,%s,%s,%s,%s,%s,%s\n' \
          "$run_name" "$variant" "$kernel" "$backend" "$irq_before" "$irq_after" \
          "$((irq_after - irq_before))" >> "$RESULT_DIR/irq_telemetry.csv"
      done
      ((cell += 1))
    done
  done
  snapshot end "$run_dir/environment.txt"
done

echo "loadavg_end=$(</proc/loadavg)" >> "$RESULT_DIR/metadata.txt"
echo "openocd_processes_end=$(pgrep -xc openocd || true)" >> "$RESULT_DIR/metadata.txt"
echo "openocd_running_end=$({ ps -C openocd -o stat= 2>/dev/null || true; } | awk '$1 !~ /^[TZ]/ {n++} END {print n+0}')" >> "$RESULT_DIR/metadata.txt"
PYTHONPYCACHEPREFIX="${TMPDIR:-/tmp}/aimer-v3-paper-pycache" \
  python3 "$SCRIPT_DIR/analyze_kernel_paper.py" "$RESULT_DIR"
(
  cd "$RESULT_DIR"
  find . -type f ! -name results_checksums.sha256 -print0 \
    | sort -z \
    | xargs -0 sha256sum
) > "$RESULT_DIR/results_checksums.sha256"
(
  cd "$RESULT_DIR"
  sha256sum -c results_checksums.sha256 >/dev/null
)
echo "Paper kernel benchmark results: $RESULT_DIR"
