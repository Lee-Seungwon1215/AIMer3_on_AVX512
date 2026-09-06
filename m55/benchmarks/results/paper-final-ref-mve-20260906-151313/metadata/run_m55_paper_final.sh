#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Restartable paper-final Cortex-M55 A/REF versus D/MVE measurement.
# Algorithm sources are never modified. Failed attempts are retained in partial/.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RESULT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
M55_ROOT=$(CDPATH= cd -- "$RESULT_DIR/../../.." && pwd)
REPO_ROOT=$(CDPATH= cd -- "$M55_ROOT/.." && pwd)

RUNS=${RUNS:-7}
E2E_SAMPLES=${E2E_SAMPLES:-50}
WARMUP=${WARMUP:-10}
PARAMS_FORWARD="128f 128s 192f 192s 256f 256s"
PARAMS_REVERSE="256s 256f 192s 192f 128s 128f"
CONFIGS="ref mve_affine"

ARM_GCC_DIR=${ARM_GCC_DIR:-/Users/seungwon/test/.tools/xpack-arm-none-eabi-gcc-15.2.1-1.1/bin}
CUBE_N6_DIR=${CUBE_N6_DIR:-/Users/seungwon/STM32CubeN6}
CORTEXCTL=${CORTEXCTL:-/Users/Shared/cortexctl}
OPENOCD=${OPENOCD:-/Users/seungwon/test/.tools/xpack-openocd-0.12.0-7/bin/openocd}
OPENOCD_SCRIPTS=${OPENOCD_SCRIPTS:-/Users/seungwon/test/.tools/xpack-openocd-0.12.0-7/openocd/scripts}
OPENOCD_TARGET_SCRIPTS=${OPENOCD_TARGET_SCRIPTS:-/Users/seungwon/test/.tools/openocd-scripts}

BASE_VALIDATED_COMMIT=352b33156210dd4f373a799330541b9a3b82e5fc
VALIDATION_DIR="$M55_ROOT/benchmarks/results/restructure-validation-20260906-134334"

mkdir -p "$RESULT_DIR/metadata" "$RESULT_DIR/partial" \
  "$RESULT_DIR/correctness/host" "$RESULT_DIR/correctness/board" \
  "$RESULT_DIR/correctness/disassembly" "$RESULT_DIR/benchmark" \
  "$RESULT_DIR/artifacts" "$RESULT_DIR/analysis"

say()
{
  printf '[%s] %s\n' "$(date '+%F %T')" "$*"
}

configure()
{
  case "$1" in
    ref)
      CFG_BACKEND=ref
      CFG_MATVEC=reference
      CFG_BUILD=ref
      ;;
    mve_affine)
      CFG_BACKEND=mve
      CFG_MATVEC=mve
      CFG_BUILD=mve-affine
      ;;
    *)
      printf 'Unknown configuration: %s\n' "$1" >&2
      exit 2
      ;;
  esac
}

make_common_args()
{
  printf '%s\n' \
    "ARM_GCC_DIR=$ARM_GCC_DIR" \
    "CUBE_N6_DIR=$CUBE_N6_DIR" \
    "CORTEXCTL=$CORTEXCTL" \
    "OPENOCD=$OPENOCD" \
    "OPENOCD_SCRIPTS=$OPENOCD_SCRIPTS" \
    "OPENOCD_TARGET_SCRIPTS=$OPENOCD_TARGET_SCRIPTS"
}

elf_path()
{
  configure "$1"
  param=$2
  test_name=$3
  printf '%s/build/full/%s/%s/%s/aimer-m55-%s-%s-%s.elf' \
    "$M55_ROOT" "$param" "$CFG_BUILD" "$test_name" \
    "$param" "$CFG_BUILD" "$test_name"
}

sha256_file()
{
  shasum -a 256 "$1" | awk '{print $1}'
}

sha256_text()
{
  printf '%s' "$1" | shasum -a 256 | awk '{print $1}'
}

write_source_manifest()
{
  destination=$1
  (
    cd "$REPO_ROOT"
    git ls-files -- Makefile common m55 | \
      grep -v '^m55/benchmarks/results/' | \
      while IFS= read -r path; do
        shasum -a 256 "$path"
      done | sort
  ) >"$destination"
}

freeze_source_before()
{
  write_source_manifest "$RESULT_DIR/metadata/source-manifest-before.sha256"
  SOURCE_DIGEST=$(sha256_file "$RESULT_DIR/metadata/source-manifest-before.sha256")
  export SOURCE_DIGEST
}

check_source_after()
{
  write_source_manifest "$RESULT_DIR/metadata/source-manifest-after.sha256"
  if ! cmp -s "$RESULT_DIR/metadata/source-manifest-before.sha256" \
      "$RESULT_DIR/metadata/source-manifest-after.sha256"; then
    diff -u "$RESULT_DIR/metadata/source-manifest-before.sha256" \
      "$RESULT_DIR/metadata/source-manifest-after.sha256" \
      >"$RESULT_DIR/metadata/SOURCE_MISMATCH.diff" || true
    printf 'Source manifest changed; stopping before analysis.\n' >&2
    exit 1
  fi
  printf 'PASS source manifest unchanged\n' \
    >"$RESULT_DIR/metadata/source-manifest-summary.txt"
}

preserve_partial()
{
  log=$1
  state=${log}.state
  if test -e "$log" || test -e "$state"; then
    relative=${log#"$RESULT_DIR/"}
    target="$RESULT_DIR/partial/$relative.$(date '+%Y%m%d-%H%M%S')"
    mkdir -p "$(dirname -- "$target")"
    test ! -e "$log" || mv "$log" "$target"
    test ! -e "$state" || mv "$state" "$target.state"
  fi
}

require_marker()
{
  log=$1
  marker=$2
  if ! grep -q "$marker" "$log"; then
    printf 'Missing marker %s in %s\n' "$marker" "$log" >&2
    tail -80 "$log" >&2 || true
    return 1
  fi
  if grep -Eq 'KAT_FAIL|SIGN_TEST_FAIL|BENCH_FAIL' "$log"; then
    printf 'Failure marker found in %s\n' "$log" >&2
    tail -80 "$log" >&2 || true
    return 1
  fi
}

run_recorded()
{
  log=$1
  marker=$2
  shift 2
  command_text=$(printf '%q ' "$@")
  command_digest=$(sha256_text "$command_text")
  state=${log}.state

  if test -f "$log" && test -f "$state" && \
     grep -q "$marker" "$log" && \
     grep -q "^source_sha256=$SOURCE_DIGEST$" "$state" && \
     grep -q "^command_sha256=$command_digest$" "$state"; then
    require_marker "$log" "$marker"
    say "skip verified PASS: ${log#$RESULT_DIR/}"
    return 0
  fi

  preserve_partial "$log"
  mkdir -p "$(dirname -- "$log")"
  say "run: ${log#$RESULT_DIR/}"
  if ! (
    printf 'PAPER_M55_CONTEXT,result_dir=%s,source_sha256=%s,command=%s\n' \
      "$RESULT_DIR" "$SOURCE_DIGEST" "$command_text"
    "$@"
  ) >"$log" 2>&1; then
    printf 'Command failed; preserving incomplete log %s\n' "$log" >&2
    tail -80 "$log" >&2 || true
    return 1
  fi
  require_marker "$log" "$marker"
  {
    printf 'source_sha256=%s\n' "$SOURCE_DIGEST"
    printf 'command_sha256=%s\n' "$command_digest"
    printf 'marker=%s\n' "$marker"
  } >"$state.tmp"
  mv "$state.tmp" "$state"
}

run_m55_make()
{
  log=$1
  marker=$2
  shift 2
  run_recorded "$log" "$marker" make -B -C "$M55_ROOT" "$@" \
    "ARM_GCC_DIR=$ARM_GCC_DIR" "CUBE_N6_DIR=$CUBE_N6_DIR" \
    "CORTEXCTL=$CORTEXCTL" "OPENOCD=$OPENOCD" \
    "OPENOCD_SCRIPTS=$OPENOCD_SCRIPTS" \
    "OPENOCD_TARGET_SCRIPTS=$OPENOCD_TARGET_SCRIPTS"
}

capture_metadata()
{
  say 'capture metadata'
  {
    printf 'timestamp_start=%s\n' "$(date -u '+%FT%TZ')"
    printf 'runs=%s\n' "$RUNS"
    printf 'e2e_samples_per_run=%s\n' "$E2E_SAMPLES"
    printf 'e2e_warmup=%s\n' "$WARMUP"
    printf 'paired_unit=run_median\n'
    printf 'bootstrap_resamples=10000\n'
    printf 'outlier_removal=none\n'
    printf 'configs=A_REF,D_MVE_AFFINE\n'
    printf 'parameters=%s\n' "$PARAMS_FORWARD"
    printf 'memory=full\n'
    printf 'sign_schedule=lowmem\n'
    printf 'keccak_shake=portable\n'
    printf 'cpu_hz=600000000\n'
    printf 'cflags=-O3 -fno-tree-vectorize -fno-tree-slp-vectorize\n'
    printf 'source_manifest_sha256=%s\n' "$SOURCE_DIGEST"
  } >"$RESULT_DIR/metadata/experiment.txt"
  {
    printf 'ARM_GCC_DIR=%s\n' "$ARM_GCC_DIR"
    printf 'CUBE_N6_DIR=%s\n' "$CUBE_N6_DIR"
    printf 'CORTEXCTL=%s\n' "$CORTEXCTL"
    printf 'OPENOCD=%s\n' "$OPENOCD"
    printf 'OPENOCD_SCRIPTS=%s\n' "$OPENOCD_SCRIPTS"
    printf 'OPENOCD_TARGET_SCRIPTS=%s\n' "$OPENOCD_TARGET_SCRIPTS"
  } >"$RESULT_DIR/metadata/tool-paths.txt"
  (
    cd "$REPO_ROOT"
    git rev-parse HEAD
    git status --short --branch
    git remote -v
  ) >"$RESULT_DIR/metadata/git.txt"
  "$ARM_GCC_DIR/arm-none-eabi-gcc" --version \
    >"$RESULT_DIR/metadata/compiler.txt"
  "$OPENOCD" --version >"$RESULT_DIR/metadata/openocd.txt" 2>&1
  "$CORTEXCTL" list >"$RESULT_DIR/metadata/boards.txt" 2>&1
  "$CORTEXCTL" port m55 >>"$RESULT_DIR/metadata/boards.txt" 2>&1
  if (
    cd "$REPO_ROOT"
    git diff --quiet "$BASE_VALIDATED_COMMIT"..HEAD -- \
      Makefile common x86 m55/Makefile m55/src m55/include m55/platform m55/tests
  ); then
    printf 'PASS current measurement sources equal validated commit %s\n' \
      "$BASE_VALIDATED_COMMIT" \
      >"$RESULT_DIR/metadata/validated-source-equivalence.txt"
  else
    (
      cd "$REPO_ROOT"
      git diff --stat "$BASE_VALIDATED_COMMIT"..HEAD -- \
        Makefile common x86 m55/Makefile m55/src m55/include m55/platform m55/tests
    ) >"$RESULT_DIR/metadata/validated-source-equivalence.txt"
    printf 'Measurement sources differ from validated commit; stopping.\n' >&2
    exit 1
  fi
  cp "$VALIDATION_DIR/REPORT.md" "$RESULT_DIR/metadata/restructure-validation-REPORT.md"
  cp "$VALIDATION_DIR/metadata/board-kat-summary.txt" \
    "$RESULT_DIR/metadata/restructure-validation-board-kat-summary.txt"
  cp "$VALIDATION_DIR/metadata/sign-summary.txt" \
    "$RESULT_DIR/metadata/restructure-validation-sign-summary.txt"
}

check_host_correctness()
{
  log="$RESULT_DIR/correctness/host/mve-all.log"
  run_recorded "$log" 'KAT_PASS param=256s vectors=100' \
    make -B -C "$REPO_ROOT" m55-host-check
  test "$(grep -c 'REF_GF_PASS' "$log")" = 6
  test "$(grep -c 'AFFINE_DIFF_PASS' "$log")" = 6
  test "$(grep -c 'KAT_PASS param=' "$log")" = 6

  for param in $PARAMS_FORWARD; do
    run_m55_make "$RESULT_DIR/correctness/host/ref-$param.log" \
      "KAT_PASS param=$param vectors=100" \
      "PARAM=$param" BACKEND=ref MATVEC=reference \
      SIGN_SCHEDULE=lowmem check-low-memory check-kat-host
  done
}

check_board_and_disassembly()
{
  for param in $PARAMS_FORWARD; do
    run_m55_make "$RESULT_DIR/correctness/board/ref-$param-sign.log" \
      'SIGN_TEST_PASS' "PARAM=$param" BACKEND=ref MATVEC=reference \
      TEST=sign MEMORY=full SIGN_SCHEDULE=lowmem STACK_USAGE=1 run-board

    log="$RESULT_DIR/correctness/board/mve_affine-$param-sign.log"
    run_m55_make "$log" 'SIGN_TEST_PASS' "PARAM=$param" BACKEND=mve \
      MATVEC=mve TEST=sign MEMORY=full SIGN_SCHEDULE=lowmem STACK_USAGE=1 \
      build check-mve-config check-mve-disassembly \
      check-mve-affine-disassembly run-board
    require_marker "$log" '__ARM_FEATURE_MVE=3'
    require_marker "$log" 'VMULLB.P16 + VMULLT.P16'
    require_marker "$log" 'VAND + VEOR; MPC batch call connected'

    elf=$(elf_path mve_affine "$param" sign)
    target="$RESULT_DIR/correctness/disassembly/$param"
    mkdir -p "$target"
    cp "$elf" "${elf%.elf}.map" "${elf%.elf}.dis" \
      "${elf%.elf}.affine.dis" "$target/"
    "$ARM_GCC_DIR/arm-none-eabi-objdump" -d \
      --disassemble=m55_aim3_mpc_batch4 "$elf" >"$target/mpc-call.dis"
    (
      cd "$target"
      find . -type f -exec shasum -a 256 {} + | sort >manifest.sha256
    )
  done
}

config_order()
{
  if test $(( $1 % 2 )) -eq 1; then
    printf '%s' 'ref mve_affine'
  else
    printf '%s' 'mve_affine ref'
  fi
}

capture_benchmark_artifact()
{
  config=$1
  param=$2
  elf=$(elf_path "$config" "$param" benchmark)
  target="$RESULT_DIR/artifacts/benchmark/$config/$param"
  mkdir -p "$target"
  cp "$elf" "$target/"
  test ! -f "${elf%.elf}.map" || cp "${elf%.elf}.map" "$target/"
  "$ARM_GCC_DIR/arm-none-eabi-size" "$elf" >"$target/size-summary.txt"
  "$ARM_GCC_DIR/arm-none-eabi-nm" -S --size-sort "$elf" \
    >"$target/symbols.txt"
  (
    cd "$target"
    shasum -a 256 "$(basename -- "$elf")" >elf.sha256
  )
}

validate_benchmark_log()
{
  log=$1
  require_marker "$log" 'BENCH_PASS'
  require_marker "$log" 'cpu_hz=600000000'
  require_marker "$log" "e2e_samples=$E2E_SAMPLES"
  require_marker "$log" "warmup=$WARMUP"
  count=$(grep '^BENCH_SAMPLE' "$log" | \
    grep -Ec 'operation=(keypair|sign|verify),')
  if test "$count" != $((3 * E2E_SAMPLES)); then
    printf 'Expected %s E2E samples in %s, found %s\n' \
      "$((3 * E2E_SAMPLES))" "$log" "$count" >&2
    return 1
  fi
  timer_error=$(grep 'BENCH_TIMER_CHECK' "$log" | \
    sed -E 's/.*error_ppm=([0-9]+).*/\1/' | tail -1)
  if test -z "$timer_error" || test "$timer_error" -gt 20000; then
    printf 'Timer cross-check failed in %s: %s ppm\n' \
      "$log" "${timer_error:-missing}" >&2
    return 1
  fi
}

run_benchmarks()
{
  run=1
  while test "$run" -le "$RUNS"; do
    order=$(config_order "$run")
    if test $((run % 2)) -eq 1; then
      param_order=$PARAMS_FORWARD
    else
      param_order=$PARAMS_REVERSE
    fi
    printf 'run=%s config_order=%s param_order=%s\n' \
      "$run" "$order" "$param_order" \
      >"$RESULT_DIR/benchmark/run-$(printf '%02d' "$run")-order.txt"
    for config in $order; do
      configure "$config"
      for param in $param_order; do
        log="$RESULT_DIR/benchmark/run-$(printf '%02d' "$run")/$config/$param.log"
        run_m55_make "$log" 'BENCH_PASS' "PARAM=$param" \
          "BACKEND=$CFG_BACKEND" "MATVEC=$CFG_MATVEC" TEST=benchmark \
          MEMORY=full SIGN_SCHEDULE=lowmem STACK_USAGE=1 \
          BENCH_KERNEL_SAMPLES=31 BENCH_KERNEL_INNER=64 \
          "BENCH_E2E_SAMPLES=$E2E_SAMPLES" "BENCH_WARMUP=$WARMUP" run-board
        validate_benchmark_log "$log"
        if test "$run" -eq 1; then
          capture_benchmark_artifact "$config" "$param"
        fi
      done
    done
    run=$((run + 1))
  done
}

finalize()
{
  check_source_after
  python3 "$SCRIPT_DIR/analyze_m55_paper_final.py" "$RESULT_DIR"
  date -u '+%FT%TZ' >"$RESULT_DIR/COMPLETE"
  make -C "$M55_ROOT" clean >"$RESULT_DIR/metadata/clean.log" 2>&1
  (
    cd "$REPO_ROOT"
    git status --short --branch
  ) >"$RESULT_DIR/metadata/git-status-after.txt"
  (
    cd "$RESULT_DIR"
    find . -type f ! -path './metadata/result-manifest.sha256' \
      -exec shasum -a 256 {} + | sort \
      >metadata/result-manifest.sha256
  )
  say "complete: $RESULT_DIR"
}

if test "$RUNS" != 7 || test "$E2E_SAMPLES" != 50 || test "$WARMUP" != 10; then
  printf 'Paper-final protocol requires RUNS=7 E2E_SAMPLES=50 WARMUP=10.\n' >&2
  exit 2
fi

if test -f "$RESULT_DIR/COMPLETE" && test -f "$RESULT_DIR/REPORT.md"; then
  say "already complete: $RESULT_DIR"
  exit 0
fi

freeze_source_before
capture_metadata
check_host_correctness
check_board_and_disassembly
check_source_after
run_benchmarks
finalize
