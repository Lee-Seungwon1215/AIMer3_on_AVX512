#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Root isolation wrapper for the final AIMer v3 kernel-paper measurement.

set -euo pipefail

if ((EUID != 0)); then
  echo "run this wrapper with sudo" >&2
  exit 2
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
TARGET_USER="${TARGET_USER:-${SUDO_USER:-}}"
if [[ -z "$TARGET_USER" || "$TARGET_USER" == root ]]; then
  echo "TARGET_USER or SUDO_USER must identify the non-root benchmark owner" >&2
  exit 2
fi
TARGET_UID="$(id -u "$TARGET_USER")"
TARGET_GID="$(id -g "$TARGET_USER")"
TARGET_HOME="$(getent passwd "$TARGET_UID" | cut -d: -f6)"

CORE="${CORE:-2}"
SIBLING_CPU="${SIBLING_CPU:-6}"
HOUSEKEEPING_CPU="${HOUSEKEEPING_CPU:-0}"
FREQUENCY_KHZ="${FREQUENCY_KHZ:-2800000}"
HOUSEKEEPING_FREQUENCY_KHZ="${HOUSEKEEPING_FREQUENCY_KHZ:-800000}"
PACKAGE_ISOLATION="${PACKAGE_ISOLATION:-1}"
IRQ_ISOLATION="${IRQ_ISOLATION:-1}"
BACKEND_COOLDOWN_SECONDS="${BACKEND_COOLDOWN_SECONDS:-0.05}"
RUN_COOLDOWN_SECONDS="${RUN_COOLDOWN_SECONDS:-2}"
PRE_MEASUREMENT_COOLDOWN_SECONDS="${PRE_MEASUREMENT_COOLDOWN_SECONDS:-15}"
RUNS="${RUNS:-15}"
KERNEL_SAMPLES="${KERNEL_SAMPLES:-75}"
KERNEL_WARMUP="${KERNEL_WARMUP:-10}"
TARGET_BATCH_CYCLES="${TARGET_BATCH_CYCLES:-2800000}"
CALIBRATION_SAMPLES="${CALIBRATION_SAMPLES:-31}"
CALIBRATION_INNER="${CALIBRATION_INNER:-256}"
CALIBRATION_WARMUP="${CALIBRATION_WARMUP:-5}"
VERIFY="${VERIFY:-1}"
CFLAGS_USED="${CFLAGS:--O3 -Wall -Wextra -fomit-frame-pointer -std=c11}"
timestamp="$(date +%Y%m%d-%H%M%S)"
RESULT_DIR="${RESULT_DIR:-$PROJECT_ROOT/benchmarks/results/kernel-paper-final-package-exclusive-$timestamp}"
STAGE_ROOT="${STAGE_ROOT:-/dev/shm}"
STAGE_DIR="$STAGE_ROOT/aimer-v3-kernel-paper-$timestamp-$RANDOM"
SERVICE_LOG="$STAGE_ROOT/aimer-v3-kernel-paper-service-$timestamp-$RANDOM.log"
KERNEL_STAGE="$STAGE_ROOT/aimer-v3-bench-kernels-$timestamp-$RANDOM"
UNIT="aimer-v3-kernel-paper-${timestamp}"
LOG_FILE="${LOG_FILE:-/tmp/aimer-v3-kernel-isolation-$timestamp.log}"
touch "$LOG_FILE"
chmod 0644 "$LOG_FILE"
exec > >(tee -a "$LOG_FILE") 2>&1
echo "isolation log: $LOG_FILE"

report_error() {
  local status=$?
  echo "isolation error: status=$status line=${BASH_LINENO[0]} command=$BASH_COMMAND"
  return "$status"
}
trap report_error ERR

if [[ "$PACKAGE_ISOLATION" != 0 && "$PACKAGE_ISOLATION" != 1 ]]; then
  echo "PACKAGE_ISOLATION must be 0 or 1" >&2
  exit 2
fi
if [[ "$IRQ_ISOLATION" != 0 && "$IRQ_ISOLATION" != 1 ]]; then
  echo "IRQ_ISOLATION must be 0 or 1" >&2
  exit 2
fi
for value in "$CORE" "$SIBLING_CPU" "$HOUSEKEEPING_CPU" "$FREQUENCY_KHZ" "$HOUSEKEEPING_FREQUENCY_KHZ"; do
  [[ "$value" =~ ^[0-9]+$ ]] || { echo "CPU and frequency values must be integers" >&2; exit 2; }
done
if [[ "$CORE" == "$HOUSEKEEPING_CPU" || "$SIBLING_CPU" == "$HOUSEKEEPING_CPU" ]]; then
  echo "HOUSEKEEPING_CPU must be a different physical core from CORE/SIBLING_CPU" >&2
  exit 2
fi
if [[ ! -d "$STAGE_ROOT" || "$(stat -f -c %T "$STAGE_ROOT")" != tmpfs ]]; then
  echo "STAGE_ROOT must be an existing tmpfs: $STAGE_ROOT" >&2
  exit 2
fi
if [[ -e "$RESULT_DIR" ]]; then
  echo "result directory already exists: $RESULT_DIR" >&2
  exit 2
fi
if [[ -e "$STAGE_DIR" || -e "$SERVICE_LOG" || -e "$KERNEL_STAGE" ]]; then
  echo "staging path collision" >&2
  exit 2
fi
if [[ "$(<"/sys/devices/system/cpu/cpu${CORE}/topology/thread_siblings_list")" != *"$SIBLING_CPU"* ]]; then
  echo "CPU $SIBLING_CPU is not the recorded SMT sibling of CPU $CORE" >&2
  exit 2
fi
if [[ ! -x "$PROJECT_ROOT/build/liboqs/bench/bench_kernels" ]]; then
  echo "missing benchmark binary" >&2
  exit 1
fi

configured_cpus="$(getconf _NPROCESSORS_CONF)"
declare -A original_online
for ((cpu = 0; cpu < configured_cpus; cpu++)); do
  online_path="/sys/devices/system/cpu/cpu${cpu}/online"
  if [[ -r "$online_path" ]]; then
    original_online[$cpu]="$(<"$online_path")"
  else
    original_online[$cpu]=1
  fi
done
if [[ "${original_online[$CORE]}" != 1 || "${original_online[$SIBLING_CPU]}" != 1 ||       "${original_online[$HOUSEKEEPING_CPU]}" != 1 ]]; then
  echo "benchmark, sibling, and housekeeping CPUs must initially be online" >&2
  exit 2
fi
frequency_cpus=()
for cpu in "$CORE" "$SIBLING_CPU" "$HOUSEKEEPING_CPU"; do
  seen=0
  for existing in "${frequency_cpus[@]}"; do [[ "$existing" == "$cpu" ]] && seen=1; done
  ((seen == 0)) && frequency_cpus+=("$cpu")
done
declare -A original_governor original_epp original_min original_max
for cpu in "${frequency_cpus[@]}"; do
  base="/sys/devices/system/cpu/cpu${cpu}/cpufreq"
  original_governor[$cpu]="$(<"$base/scaling_governor")"
  original_epp[$cpu]="$(<"$base/energy_performance_preference")"
  original_min[$cpu]="$(<"$base/scaling_min_freq")"
  original_max[$cpu]="$(<"$base/scaling_max_freq")"
done
original_turbo="$(</sys/devices/system/cpu/intel_pstate/no_turbo)"
original_sibling_online="${original_online[$SIBLING_CPU]}"
original_system_allowed="$(systemctl show system.slice -p AllowedCPUs --value)"
original_user_allowed="$(systemctl show user.slice -p AllowedCPUs --value)"
root_cpuset_was_enabled=0
grep -qw cpuset /sys/fs/cgroup/cgroup.subtree_control && root_cpuset_was_enabled=1
IRQ_STATE_FILE="${IRQ_STATE_FILE:-/tmp/aimer-v3-irq-affinity-$timestamp.txt}"
WORKQUEUE_STATE_FILE="${WORKQUEUE_STATE_FILE:-/tmp/aimer-v3-workqueue-affinity-$timestamp.txt}"
: > "$IRQ_STATE_FILE"
: > "$WORKQUEUE_STATE_FILE"
chmod 0600 "$IRQ_STATE_FILE" "$WORKQUEUE_STATE_FILE"
while IFS= read -r path; do
  printf '%s|%s\n' "$path" "$(<"$path")" >> "$WORKQUEUE_STATE_FILE"
done < <(find /sys/devices/virtual/workqueue -maxdepth 2 -type f -name cpumask | sort)
original_default_irq_affinity="$(</proc/irq/default_smp_affinity)"
original_irqbalance_state="$(systemctl is-active irqbalance.service 2>/dev/null || true)"
mapfile -t openocd_pgids < <(
  ps -C openocd -o pgid= | awk '{$1=$1; if ($1 != "" && !seen[$1]++) print $1}' | sort -n
)
PGID_FILE="${PGID_FILE:-/tmp/aimer-v3-openocd-pgids-$timestamp.txt}"
: > "$PGID_FILE"
printf '%s\n' "${openocd_pgids[@]}" | awk 'NF && !seen[$0]++' > "$PGID_FILE"
chmod 0644 "$PGID_FILE"
watchdog_pid=""

cleanup_started=0
cleanup() {
  local status=$?
  if ((cleanup_started)); then
    exit "$status"
  fi
  cleanup_started=1
  trap - EXIT INT TERM ERR
  set +e
  if [[ -n "$watchdog_pid" ]]; then
    kill "$watchdog_pid" >/dev/null 2>&1
    wait "$watchdog_pid" >/dev/null 2>&1
  fi
  systemctl stop "$UNIT.service" >/dev/null 2>&1
  rm -f -- "$KERNEL_STAGE" >/dev/null 2>&1
  systemctl set-property --runtime system.slice "AllowedCPUs=$original_system_allowed" >/dev/null 2>&1
  systemctl set-property --runtime user.slice "AllowedCPUs=$original_user_allowed" >/dev/null 2>&1
  for ((cpu = 0; cpu < configured_cpus; cpu++)); do
    online_path="/sys/devices/system/cpu/cpu${cpu}/online"
    if [[ "${original_online[$cpu]}" == 1 && -w "$online_path" ]]; then
      current_online="$(<"$online_path")"
      [[ "$current_online" == 0 ]] && echo 1 > "$online_path"
    fi
  done
  if ((IRQ_ISOLATION == 1)); then
    while IFS='|' read -r path mask; do
      [[ -n "$path" && -e "$path" ]] || continue
      { printf '%s\n' "$mask" > "$path"; } 2>/dev/null || true
    done < "$WORKQUEUE_STATE_FILE"
    while IFS='|' read -r irq affinity; do
      [[ -n "$irq" && -d "/proc/irq/$irq" ]] || continue
      { printf '%s\n' "$affinity" > "/proc/irq/$irq/smp_affinity_list"; } 2>/dev/null || true
    done < "$IRQ_STATE_FILE"
    printf '%s\n' "$original_default_irq_affinity" > /proc/irq/default_smp_affinity 2>/dev/null || true
    if [[ "$original_irqbalance_state" == active ]]; then
      systemctl start irqbalance.service >/dev/null 2>&1
    fi
  fi
  for _ in {1..50}; do
    ready=1
    for cpu in "${frequency_cpus[@]}"; do
      [[ -d "/sys/devices/system/cpu/cpu${cpu}/cpufreq" ]] || ready=0
    done
    ((ready)) && break
    sleep 0.1
  done
  echo "$original_turbo" > /sys/devices/system/cpu/intel_pstate/no_turbo
  for cpu in "${frequency_cpus[@]}"; do
    base="/sys/devices/system/cpu/cpu${cpu}/cpufreq"
    echo "${original_min[$cpu]}" > "$base/scaling_min_freq"
    echo "${original_max[$cpu]}" > "$base/scaling_max_freq"
    echo "${original_governor[$cpu]}" > "$base/scaling_governor"
    echo "${original_epp[$cpu]}" > "$base/energy_performance_preference"
  done
  for ((cpu = 1; cpu < configured_cpus; cpu++)); do
    online_path="/sys/devices/system/cpu/cpu${cpu}/online"
    if [[ "${original_online[$cpu]}" == 0 && -w "$online_path" && "$(<"$online_path")" == 1 ]]; then
      echo 0 > "$online_path"
    fi
  done
  mapfile -t restore_pgids < "$PGID_FILE"
  for pgid in "${restore_pgids[@]}"; do
    kill -CONT -- "-$pgid" >/dev/null 2>&1
  done
  if ((root_cpuset_was_enabled == 0)); then
    echo -cpuset > /sys/fs/cgroup/cgroup.subtree_control 2>/dev/null
  fi
  echo "isolation state restored (exit=$status)"
  exit "$status"
}
trap cleanup EXIT INT TERM

other_cpus=()
offline_cpus=("$SIBLING_CPU")
for ((cpu = 0; cpu < configured_cpus; cpu++)); do
  if ((cpu != CORE && cpu != SIBLING_CPU)); then
    other_cpus+=("$cpu")
  fi
  if ((PACKAGE_ISOLATION == 1 && cpu != CORE && cpu != HOUSEKEEPING_CPU && cpu != SIBLING_CPU)); then
    offline_cpus+=("$cpu")
  fi
done
if ((PACKAGE_ISOLATION == 1)); then
  slice_cpu_list="$HOUSEKEEPING_CPU"
  expected_online_cpus="$(printf '%s\n' "$HOUSEKEEPING_CPU" "$CORE" | sort -n | paste -sd, -)"
else
  slice_cpu_list="$(IFS=,; echo "${other_cpus[*]}")"
  expected_online_cpus=""
fi

cpulist_has_cpu() {
  local list="$1"
  local wanted="$2"
  local token start end
  local -a tokens
  IFS=',' read -r -a tokens <<< "$list"
  for token in "${tokens[@]}"; do
    if [[ "$token" == *-* ]]; then
      start="${token%-*}"
      end="${token#*-}"
    else
      start="$token"
      end="$token"
    fi
    [[ "$start" =~ ^[0-9]+$ && "$end" =~ ^[0-9]+$ ]] || continue
    ((wanted >= start && wanted <= end)) && return 0
  done
  return 1
}

redirect_device_irqs() {
  local mask irq path original changed failed workqueue_changed
  mask="$(printf '%x' "$((1 << HOUSEKEEPING_CPU))")"
  printf '%s\n' "$mask" > /proc/irq/default_smp_affinity
  changed=0
  failed=0
  : > "$IRQ_STATE_FILE"
  for path in /proc/irq/[0-9]*; do
    [[ -d "$path" && -r "$path/smp_affinity_list" ]] || continue
    irq="${path##*/}"
    original="$(<"$path/smp_affinity_list")"
    [[ "$original" == "$HOUSEKEEPING_CPU" ]] && continue
    if { printf '%s\n' "$HOUSEKEEPING_CPU" > "$path/smp_affinity_list"; } 2>/dev/null; then
      printf '%s|%s\n' "$irq" "$original" >> "$IRQ_STATE_FILE"
      ((changed += 1))
    else
      ((failed += 1))
    fi
  done
  workqueue_changed=0
  while IFS='|' read -r path original; do
    [[ -n "$path" && -w "$path" ]] || continue
    if { printf '%s\n' "$mask" > "$path"; } 2>/dev/null; then
      ((workqueue_changed += 1))
    fi
  done < "$WORKQUEUE_STATE_FILE"
  echo "IRQ affinity redirect: changed=$changed unsupported=$failed workqueues=$workqueue_changed default_mask=$mask"
}

quiet_checks=0
for _ in {1..150}; do
  mapfile -t running_pgids < <(
    ps -C openocd -o pgid=,stat= \
      | awk '$2 !~ /^[TZ]/ {$1=$1; if (!seen[$1]++) print $1}' \
      | sort -n
  )
  if ((${#running_pgids[@]} == 0)); then
    ((quiet_checks += 1))
    if ((quiet_checks >= 25)); then
      break
    fi
  else
    quiet_checks=0
    for pgid in "${running_pgids[@]}"; do
      known=0
      for recorded in "${openocd_pgids[@]}"; do
        [[ "$recorded" == "$pgid" ]] && known=1
      done
      if ((known == 0)); then
        openocd_pgids+=("$pgid")
        echo "$pgid" >> "$PGID_FILE"
      fi
      kill -STOP -- "-$pgid" 2>/dev/null || true
    done
  fi
  sleep 0.2
done
if [[ "$(ps -C openocd -o stat= | awk '$1 !~ /^[TZ]/ {n++} END {print n+0}')" != 0 ]]; then
  echo "failed to pause every OpenOCD process" >&2
  exit 1
fi
echo "OpenOCD groups paused: ${openocd_pgids[*]:-none}"

watch_openocd() {
  while true; do
    mapfile -t new_pgids < <(
      ps -C openocd -o pgid=,stat= \
        | awk '$2 !~ /^[TZ]/ {$1=$1; if (!seen[$1]++) print $1}' \
        | sort -n
    )
    for pgid in "${new_pgids[@]}"; do
      grep -qx "$pgid" "$PGID_FILE" || echo "$pgid" >> "$PGID_FILE"
      kill -STOP -- "-$pgid" 2>/dev/null || true
    done
    sleep 1
  done
}
watch_openocd &
watchdog_pid=$!
echo "OpenOCD watchdog started: pid=$watchdog_pid state=$PGID_FILE"

echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo
echo "OpenOCD paused; locking CPU frequency"
for cpu in "$CORE" "$SIBLING_CPU"; do
  base="/sys/devices/system/cpu/cpu${cpu}/cpufreq"
  echo performance > "$base/scaling_governor"
  echo performance > "$base/energy_performance_preference"
  echo "$FREQUENCY_KHZ" > "$base/scaling_max_freq"
  echo "$FREQUENCY_KHZ" > "$base/scaling_min_freq"
done
if ((PACKAGE_ISOLATION == 1)); then
  base="/sys/devices/system/cpu/cpu${HOUSEKEEPING_CPU}/cpufreq"
  echo performance > "$base/scaling_governor"
  echo performance > "$base/energy_performance_preference"
  echo "$HOUSEKEEPING_FREQUENCY_KHZ" > "$base/scaling_max_freq"
  echo "$HOUSEKEEPING_FREQUENCY_KHZ" > "$base/scaling_min_freq"
fi

echo "enabling cpuset controller and quiescing CPU package"
echo +cpuset > /sys/fs/cgroup/cgroup.subtree_control
systemctl set-property --runtime system.slice "AllowedCPUs=$slice_cpu_list"
systemctl set-property --runtime user.slice "AllowedCPUs=$slice_cpu_list"
if ((IRQ_ISOLATION == 1)); then
  [[ "$original_irqbalance_state" == active ]] && systemctl stop irqbalance.service
  redirect_device_irqs
fi
for cpu in "${offline_cpus[@]}"; do
  online_path="/sys/devices/system/cpu/cpu${cpu}/online"
  if [[ -w "$online_path" && "$(<"$online_path")" == 1 ]]; then
    echo 0 > "$online_path"
  fi
done
online_cpus="$(</sys/devices/system/cpu/online)"
if ((PACKAGE_ISOLATION == 1)) && [[ "$online_cpus" != "$expected_online_cpus" ]]; then
  echo "unexpected online CPU set: $online_cpus (expected $expected_online_cpus)" >&2
  exit 1
fi
if ((IRQ_ISOLATION == 1)); then
  sleep 0.2
  remaining_irqs=()
  for path in /proc/irq/[0-9]*; do
    [[ -r "$path/effective_affinity_list" ]] || continue
    effective="$(<"$path/effective_affinity_list")"
    if cpulist_has_cpu "$effective" "$CORE"; then
      remaining_irqs+=("${path##*/}:$effective")
    fi
  done
  irq_unmovable_count="${#remaining_irqs[@]}"
  if ((irq_unmovable_count != 0)); then
    echo "unmovable device IRQs tracked by per-cell telemetry on CPU $CORE: ${remaining_irqs[*]}"
  else
    echo "device IRQ isolation verified: no numeric IRQ effective on CPU $CORE"
  fi
else
  irq_unmovable_count=0
fi
echo "slice masks: system=$(</sys/fs/cgroup/system.slice/cpuset.cpus.effective) user=$(</sys/fs/cgroup/user.slice/cpuset.cpus.effective) online=$online_cpus"

if [[ "$(<"/sys/fs/cgroup/system.slice/cpuset.cpus.effective")" == *"$CORE"* ]]; then
  echo "system.slice still contains benchmark CPU $CORE" >&2
  exit 1
fi
if [[ "$(<"/sys/fs/cgroup/user.slice/cpuset.cpus.effective")" == *"$CORE"* ]]; then
  echo "user.slice still contains benchmark CPU $CORE" >&2
  exit 1
fi

touch "$SERVICE_LOG"
cp -- "$PROJECT_ROOT/build/liboqs/bench/bench_kernels" "$KERNEL_STAGE"
chown "$TARGET_UID:$TARGET_GID" "$SERVICE_LOG" "$KERNEL_STAGE"
chmod 0644 "$SERVICE_LOG"
chmod 0755 "$KERNEL_STAGE"
echo "exclusive measurement starting: core=$CORE sibling=$SIBLING_CPU online=$online_cpus stage=$STAGE_DIR"
systemd-run --system --wait --quiet --collect \
  --unit="$UNIT" --slice=aimer-benchmark.slice \
  --uid="$TARGET_UID" --gid="$TARGET_GID" \
  --property="AllowedCPUs=$CORE" --property="CPUAffinity=$CORE" \
  --property="Nice=-20" --property="WorkingDirectory=$PROJECT_ROOT" \
  --setenv="HOME=$TARGET_HOME" --setenv="CORE=$CORE" \
  --setenv="BENCH_RUNNER=$PROJECT_ROOT/benchmarks/run_kernel_paper.sh" \
  --setenv="BENCH_SERVICE_LOG=$SERVICE_LOG" \
  --setenv="KERNEL_BIN_OVERRIDE=$KERNEL_STAGE" \
  --setenv="IRQ_UNMOVABLE_COUNT=$irq_unmovable_count" \
  --setenv="SIBLING_CPU=$SIBLING_CPU" --setenv="REQUIRE_EXCLUSIVE_CPU=1" \
  --setenv="PACKAGE_ISOLATION=$PACKAGE_ISOLATION" \
  --setenv="IRQ_ISOLATION=$IRQ_ISOLATION" \
  --setenv="HOUSEKEEPING_CPU=$HOUSEKEEPING_CPU" \
  --setenv="EXPECTED_ONLINE_CPUS=$expected_online_cpus" \
  --setenv="BACKEND_COOLDOWN_SECONDS=$BACKEND_COOLDOWN_SECONDS" \
  --setenv="RUN_COOLDOWN_SECONDS=$RUN_COOLDOWN_SECONDS" \
  --setenv="PRE_MEASUREMENT_COOLDOWN_SECONDS=$PRE_MEASUREMENT_COOLDOWN_SECONDS" \
  --setenv="ISOLATION_DRIVER=systemd-cpuset-package-quiesced-tmpfs" \
  --setenv="RUNS=$RUNS" --setenv="KERNEL_SAMPLES=$KERNEL_SAMPLES" \
  --setenv="KERNEL_WARMUP=$KERNEL_WARMUP" \
  --setenv="CALIBRATION_SAMPLES=$CALIBRATION_SAMPLES" --setenv="CALIBRATION_INNER=$CALIBRATION_INNER" \
  --setenv="CALIBRATION_WARMUP=$CALIBRATION_WARMUP" \
  --setenv="TARGET_BATCH_CYCLES=$TARGET_BATCH_CYCLES" \
  --setenv="RESULT_DIR=$STAGE_DIR" --setenv="VERIFY=$VERIFY" \
  --setenv="REBUILD=0" --setenv="STRICT_ENV=1" \
  --setenv="CFLAGS=$CFLAGS_USED" \
  /bin/bash -c 'exec "$BENCH_RUNNER" >"$BENCH_SERVICE_LOG" 2>&1'

if [[ ! -f "$STAGE_DIR/results_checksums.sha256" ]]; then
  echo "measurement service did not produce a complete result tree; log=$SERVICE_LOG" >&2
  exit 1
fi
(
  cd "$STAGE_DIR"
  sha256sum -c results_checksums.sha256 >/dev/null
)
mkdir -p "$(dirname -- "$RESULT_DIR")"
cp -a -- "$STAGE_DIR" "$RESULT_DIR"
cp -- "$SERVICE_LOG" "$RESULT_DIR/service.log"
cp -- "$LOG_FILE" "$RESULT_DIR/isolation.log"
chown -R "$TARGET_UID:$TARGET_GID" "$RESULT_DIR"
sync "$RESULT_DIR"
rm -rf -- "$STAGE_DIR"
rm -f -- "$SERVICE_LOG"
echo "exclusive paper measurement finished: $RESULT_DIR"
