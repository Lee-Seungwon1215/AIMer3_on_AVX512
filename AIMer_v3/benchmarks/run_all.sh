#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Run AIMer v3 reference, AVX2, and AVX-512 benchmarks on one pinned CPU.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
BENCH_BIN="$PROJECT_ROOT/build/liboqs/bench/bench_full"
ITERS="${1:-${ITERS:-50}}"
WARMUP="${WARMUP:-}"
VERIFY_KAT="${VERIFY_KAT:-1}"
CC_NAME="${CC:-cc}"
CFLAGS_USED="${CFLAGS:--O3 -Wall -Wextra -fomit-frame-pointer -std=c11}"
CPPFLAGS_USED="${CPPFLAGS:-}"
LDFLAGS_USED="${LDFLAGS:-}"

if ! command -v taskset >/dev/null 2>&1; then
	echo "taskset is required for single-core pinning" >&2
	exit 1
fi

affinity_list="$(taskset -pc "$$" | sed 's/.*: //')"
first_affinity="${affinity_list%%,*}"
default_core="${first_affinity%%-*}"
CORE="${CORE:-$default_core}"

if [[ ! "$ITERS" =~ ^[1-9][0-9]*$ ]]; then
	echo "iterations must be a positive integer" >&2
	exit 2
fi
if [[ -n "$WARMUP" && ! "$WARMUP" =~ ^[1-9][0-9]*$ ]]; then
	echo "warmup must be a positive integer" >&2
	exit 2
fi
if [[ "$VERIFY_KAT" != "0" && "$VERIFY_KAT" != "1" ]]; then
	echo "VERIFY_KAT must be 0 or 1" >&2
	exit 2
fi
effective_warmup="$WARMUP"
if [[ -z "$effective_warmup" ]]; then
	effective_warmup=$((ITERS / 10))
	if ((effective_warmup < 5)); then
		effective_warmup=5
	fi
fi

if ! taskset -c "$CORE" true >/dev/null 2>&1; then
	echo "CPU $CORE is outside this process affinity ($affinity_list)" >&2
	exit 2
fi

if [[ ! -x "$BENCH_BIN" ]]; then
	echo "Missing $BENCH_BIN; run 'make -C AIMer_v3 bench' first." >&2
	exit 1
fi

if [[ "$VERIFY_KAT" == "1" ]]; then
	echo "Verifying all reference/AVX2/AVX-512 KATs before measurement..."
	make -C "$PROJECT_ROOT" -f Makefile.liboqs.multi oqs-kat
fi

timestamp="$(date +%Y%m%d-%H%M%S)"
RESULT_DIR="${RESULT_DIR:-$SCRIPT_DIR/results/$timestamp}"
mkdir -p "$RESULT_DIR"

governor_path="/sys/devices/system/cpu/cpu${CORE}/cpufreq/scaling_governor"
governor="unknown"
if [[ -r "$governor_path" ]]; then
	governor="$(<"$governor_path")"
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
	echo "turbo=$turbo"
	echo "iterations=$ITERS"
	echo "warmup=$effective_warmup"
	echo "kat_verified=$VERIFY_KAT"
	echo "cc=$CC_NAME"
	echo "cppflags=$CPPFLAGS_USED"
	echo "cflags=$CFLAGS_USED"
	echo "ldflags=$LDFLAGS_USED"
	if command -v sha256sum >/dev/null 2>&1; then
		echo "benchmark_sha256=$(sha256sum "$BENCH_BIN")"
		echo "liboqs_sha256=$(sha256sum "$PROJECT_ROOT/build/lib/liboqs.a")"
	fi
	"$CC_NAME" --version | head -n 1
	echo
	lscpu
} > "$RESULT_DIR/metadata.txt"

if [[ "$governor" != "performance" ]]; then
	echo "warning: CPU $CORE governor is '$governor', not 'performance'" >&2
fi

variants=(
	AIMER-v3-128f AIMER-v3-128s
	AIMER-v3-192f AIMER-v3-192s
	AIMER-v3-256f AIMER-v3-256s
)
implementations=(ref avx2 avx512)

for implementation in "${implementations[@]}"; do
	printf '%s\n' '# backend,variant,op,N,min,median,max,mean,std,cv,med_us,mean_us,ops' \
		> "$RESULT_DIR/$implementation.csv"
done

echo "core=$CORE governor=$governor samples=$ITERS results=$RESULT_DIR"
for variant in "${variants[@]}"; do
	for implementation in "${implementations[@]}"; do
		echo "[$implementation] $variant"
		bench_command=(taskset -c "$CORE" "$BENCH_BIN" "$implementation"
			"$variant" "$ITERS" "$effective_warmup")
		"${bench_command[@]}" >> "$RESULT_DIR/$implementation.csv"
	done
done

NO_COLOR=1 python3 "$SCRIPT_DIR/compare.py" \
	"$RESULT_DIR/ref.csv" "$RESULT_DIR/avx2.csv" "$RESULT_DIR/avx512.csv" \
	| tee "$RESULT_DIR/comparison.txt"

echo "Benchmark results: $RESULT_DIR"
