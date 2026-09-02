#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Reproduce the locked CPU conditions used by the final AIMer v3 E2E run.

set -euo pipefail

if ((EUID != 0)); then
  echo "run this wrapper with sudo" >&2
  exit 2
fi
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
TARGET_USER="${TARGET_USER:-${SUDO_USER:-}}"
[[ -n "$TARGET_USER" && "$TARGET_USER" != root ]] || { echo "TARGET_USER or SUDO_USER must identify the result owner" >&2; exit 2; }
TARGET_HOME="$(getent passwd "$TARGET_USER" | cut -d: -f6)"
CORE="${CORE:-2}"
FREQUENCY_KHZ="${FREQUENCY_KHZ:-2800000}"
RUNS="${RUNS:-7}"
E2E_SAMPLES="${E2E_SAMPLES:-50}"
E2E_WARMUP="${E2E_WARMUP:-10}"
timestamp="$(date +%Y%m%d-%H%M%S)"
RESULT_DIR="${RESULT_DIR:-$SCRIPT_DIR/results/paper-final-locked-$timestamp}"
STATE_FILE="/tmp/aimer-v2-openocd-pgids-$timestamp-$RANDOM"

[[ -x "$PROJECT_ROOT/build/tests/bench_full_paper" ]] || { echo "missing paper benchmark binary; run make -C AIMer_v2 first" >&2; exit 1; }
[[ ! -e "$RESULT_DIR" ]] || { echo "result directory already exists: $RESULT_DIR" >&2; exit 2; }
for value in "$CORE" "$FREQUENCY_KHZ" "$RUNS" "$E2E_SAMPLES" "$E2E_WARMUP"; do
  [[ "$value" =~ ^[1-9][0-9]*$ ]] || { echo "numeric settings must be positive integers" >&2; exit 2; }
done

freq_base="/sys/devices/system/cpu/cpu${CORE}/cpufreq"
[[ -d "$freq_base" ]] || { echo "missing CPU frequency controls for CPU $CORE" >&2; exit 1; }
original_governor="$(<"$freq_base/scaling_governor")"
original_epp="$(<"$freq_base/energy_performance_preference")"
original_min="$(<"$freq_base/scaling_min_freq")"
original_max="$(<"$freq_base/scaling_max_freq")"
original_turbo="$(</sys/devices/system/cpu/intel_pstate/no_turbo)"
: > "$STATE_FILE"
watchdog_pid=""
cleanup_started=0
cleanup() {
  local status=$?
  ((cleanup_started == 0)) || exit "$status"
  cleanup_started=1
  trap - EXIT INT TERM
  set +e
  if [[ -n "$watchdog_pid" ]]; then
    kill "$watchdog_pid" >/dev/null 2>&1
    wait "$watchdog_pid" >/dev/null 2>&1
  fi
  echo "$original_min" > "$freq_base/scaling_min_freq"
  echo "$original_max" > "$freq_base/scaling_max_freq"
  echo "$original_governor" > "$freq_base/scaling_governor"
  echo "$original_epp" > "$freq_base/energy_performance_preference"
  echo "$original_turbo" > /sys/devices/system/cpu/intel_pstate/no_turbo
  sort -un "$STATE_FILE" 2>/dev/null | while read -r pgid; do
    [[ -n "$pgid" ]] && kill -CONT -- "-$pgid" >/dev/null 2>&1
  done
  rm -f -- "$STATE_FILE"
  echo "CPU/OpenOCD state restored (exit=$status)"
  exit "$status"
}
trap cleanup EXIT INT TERM

pause_openocd() {
  local pgid
  while read -r pgid; do
    [[ -n "$pgid" ]] || continue
    grep -qx "$pgid" "$STATE_FILE" || echo "$pgid" >> "$STATE_FILE"
    kill -STOP -- "-$pgid" 2>/dev/null || true
  done < <(ps -C openocd -o pgid=,stat= 2>/dev/null | awk '$2 !~ /^[TZ]/ {$1=$1; if (!seen[$1]++) print $1}')
}
pause_openocd
watch_openocd() {
  while true; do
    pause_openocd
    sleep 1
  done
}
taskset -c 0 bash -c "$(declare -f pause_openocd watch_openocd); STATE_FILE='$STATE_FILE'; watch_openocd" &
watchdog_pid=$!
sleep 1

# Exact core policy recorded for the final AIMer v3 E2E result.
echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo
echo performance > "$freq_base/scaling_governor"
echo performance > "$freq_base/energy_performance_preference"
echo "$FREQUENCY_KHZ" > "$freq_base/scaling_max_freq"
echo "$FREQUENCY_KHZ" > "$freq_base/scaling_min_freq"

echo "AIMer v2 locked paper run: core=$CORE frequency=${FREQUENCY_KHZ}kHz runs=$RUNS samples=$E2E_SAMPLES"
runuser -u "$TARGET_USER" -- env \
  HOME="$TARGET_HOME" CORE="$CORE" RUNS="$RUNS" \
  E2E_SAMPLES="$E2E_SAMPLES" E2E_WARMUP="$E2E_WARMUP" \
  RESULT_DIR="$RESULT_DIR" VERIFY=1 REBUILD=0 STRICT_ENV=1 \
  "$SCRIPT_DIR/run_paper.sh"

echo "AIMer v2 final paper result: $RESULT_DIR"
