#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Reproducible Cortex-M55 matrix-controlled ablation.
# The script is restartable: a log containing its expected PASS marker is not
# rerun.  Set RESULT_DIR to resume an interrupted experiment in the same tree.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
M55_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
REPO_ROOT=$(CDPATH= cd -- "$M55_ROOT/.." && pwd)
TIMESTAMP=$(date '+%Y%m%d-%H%M%S')
RESULT_DIR=${RESULT_DIR:-$SCRIPT_DIR/results/matvec-ablation-$TIMESTAMP}
RUNS=${RUNS:-7}
PARAMS_FORWARD="128f 128s 192f 192s 256f 256s"
PARAMS_REVERSE="256s 256f 192s 192f 128s 128f"
CONFIGS="ref mve_refmat mve_compact"
TOOLCHAIN_BIN=${TOOLCHAIN_BIN:-${ARM_GCC_DIR:-}}
if test -n "$TOOLCHAIN_BIN"; then
  TOOLCHAIN_BIN=${TOOLCHAIN_BIN%/}
  ARM_GCC_DIR=$TOOLCHAIN_BIN
  export ARM_GCC_DIR
  TOOLCHAIN_PREFIX=$TOOLCHAIN_BIN/
else
  TOOLCHAIN_PREFIX=
fi
CC=${TOOLCHAIN_PREFIX}arm-none-eabi-gcc
SIZE=${TOOLCHAIN_PREFIX}arm-none-eabi-size
NM=${TOOLCHAIN_PREFIX}arm-none-eabi-nm
CORTEXCTL=${CORTEXCTL:-cortexctl}

mkdir -p "$RESULT_DIR/metadata" "$RESULT_DIR/correctness/host" \
  "$RESULT_DIR/correctness/board" "$RESULT_DIR/correctness/disassembly" \
  "$RESULT_DIR/benchmark" "$RESULT_DIR/profile" "$RESULT_DIR/phase-256" \
  "$RESULT_DIR/artifacts"

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
    mve_refmat)
      CFG_BACKEND=mve
      CFG_MATVEC=reference
      CFG_BUILD=mve-refmat
      ;;
    mve_compact)
      CFG_BACKEND=mve
      CFG_MATVEC=compact
      CFG_BUILD=mve
      ;;
    *)
      printf 'Unknown configuration: %s\n' "$1" >&2
      exit 2
      ;;
  esac
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
}

run_make()
{
  log=$1
  marker=$2
  shift 2
  if test -f "$log" && grep -q "$marker" "$log"; then
    say "skip PASS: ${log#$RESULT_DIR/}"
    return 0
  fi
  mkdir -p "$(dirname -- "$log")"
  say "run: ${log#$RESULT_DIR/}"
  if ! (
    cd "$M55_ROOT"
    printf 'ABLATION_CONTEXT,result_dir=%s,command=make %s\n' \
      "$RESULT_DIR" "$*"
    make -B "$@"
  ) >"$log" 2>&1; then
    printf 'Command failed; last log lines from %s:\n' "$log" >&2
    tail -80 "$log" >&2 || true
    return 1
  fi
  require_marker "$log" "$marker"
}

elf_path()
{
  config=$1
  param=$2
  test_name=$3
  configure "$config"
  printf '%s/build/full/%s/%s/%s/aimer-m55-%s-%s-%s.elf' \
    "$M55_ROOT" "$param" "$CFG_BUILD" "$test_name" \
    "$param" "$CFG_BUILD" "$test_name"
}

capture_artifact()
{
  config=$1
  param=$2
  test_name=$3
  elf=$(elf_path "$config" "$param" "$test_name")
  target="$RESULT_DIR/artifacts/$test_name/$config/$param"
  mkdir -p "$target"
  cp "$elf" "$target/"
  "$SIZE" -A "$elf" >"$target/size.txt"
  "$SIZE" "$elf" >"$target/size-summary.txt"
  (cd "$target" && shasum -a 256 "$(basename -- "$elf")" >elf.sha256)
  map=${elf%.elf}.map
  if test -f "$map"; then
    cp "$map" "$target/"
  fi
}

capture_metadata()
{
  if test -f "$RESULT_DIR/metadata/COMPLETE"; then
    return 0
  fi
  say "capture metadata"
  {
    printf 'timestamp_start=%s\n' "$(date -u '+%FT%TZ')"
    printf 'host=%s\n' "$(hostname)"
    printf 'uname=%s\n' "$(uname -a)"
    printf 'runs=%s\n' "$RUNS"
    printf 'kernel_samples=31\n'
    printf 'kernel_inner=64\n'
    printf 'e2e_warmup=1\n'
    printf 'e2e_samples_per_run=7\n'
    printf 'profile_samples=31\n'
    printf 'profile_kernel_inner=64\n'
    printf 'profile_complex_inner=8\n'
    printf 'cpu_hz=600000000\n'
    printf 'configs=A_REF,B_MVE_REFMAT,C_MVE_COMPACT\n'
    printf 'memory=full\n'
    printf 'sign_schedule=lowmem\n'
  } >"$RESULT_DIR/metadata/experiment.txt"
  (cd "$REPO_ROOT" && git rev-parse HEAD && git status --short) \
    >"$RESULT_DIR/metadata/git.txt"
  (cd "$REPO_ROOT" && git diff --binary) \
    >"$RESULT_DIR/metadata/source.patch"
  (cd "$REPO_ROOT" && git diff --check) \
    >"$RESULT_DIR/metadata/diff-check.txt"
  "$CC" --version \
    >"$RESULT_DIR/metadata/compiler.txt"
  "$CC" -Q -O3 --help=optimizers \
    >>"$RESULT_DIR/metadata/compiler.txt" 2>&1 || true
  "$CORTEXCTL" list >"$RESULT_DIR/metadata/boards.txt" 2>&1 || true
  "$CORTEXCTL" port m55 >>"$RESULT_DIR/metadata/boards.txt" 2>&1 || true
  cp "$0" "$RESULT_DIR/metadata/"
  cp "$SCRIPT_DIR/analyze_matvec_ablation.py" "$RESULT_DIR/metadata/"
  find "$M55_ROOT" -type f \
    ! -path '*/build/*' ! -path '*/benchmarks/results/*' \
    -exec shasum -a 256 {} + | sort \
    >"$RESULT_DIR/metadata/m55-source.sha256"
  : >"$RESULT_DIR/metadata/COMPLETE"
}

check_host_correctness()
{
  python_log="$RESULT_DIR/correctness/reference-matvec-source.log"
  if ! test -f "$python_log" || ! grep -q 'MATVEC_SOURCE_PASS width=256' "$python_log"; then
    python3 "$M55_ROOT/tests/check_reference_matvec_source.py" >"$python_log" 2>&1
  fi
  require_marker "$python_log" 'MATVEC_SOURCE_PASS width=128'
  require_marker "$python_log" 'MATVEC_SOURCE_PASS width=192'
  require_marker "$python_log" 'MATVEC_SOURCE_PASS width=256'

  for param in $PARAMS_FORWARD; do
    run_make "$RESULT_DIR/correctness/host/$param-lowmemory.log" \
      'LOW_MEMORY_DIFF_PASS' PARAM="$param" BACKEND=ref MATVEC=reference \
      SIGN_SCHEDULE=lowmem check-low-memory

    run_make "$RESULT_DIR/correctness/host/ref-$param-kat.log" \
      'KAT_PASS' PARAM="$param" BACKEND=ref MATVEC=reference \
      SIGN_SCHEDULE=lowmem check-kat-host

    for config in mve_refmat mve_compact; do
      configure "$config"
      run_make "$RESULT_DIR/correctness/host/$config-$param-field.log" \
        'REF_GF_PASS' PARAM="$param" BACKEND="$CFG_BACKEND" \
        MATVEC="$CFG_MATVEC" check-mve-field-host
      run_make "$RESULT_DIR/correctness/host/$config-$param-kat.log" \
        'KAT_PASS' PARAM="$param" BACKEND="$CFG_BACKEND" \
        MATVEC="$CFG_MATVEC" SIGN_SCHEDULE=lowmem \
        check-kat-mve-host
    done
  done
}

check_disassembly()
{
  for param in $PARAMS_FORWARD; do
    config=mve_refmat
    configure "$config"
    log="$RESULT_DIR/correctness/disassembly/$config-$param.log"
    run_make "$log" 'MVE polynomial multiply instructions' \
      PARAM="$param" BACKEND="$CFG_BACKEND" MATVEC="$CFG_MATVEC" \
      MEMORY=full TEST=reference build check-mve-disassembly
    elf=$(elf_path "$config" "$param" reference)
    dis=${elf%.elf}.dis
    {
      printf 'param=%s config=%s\n' "$param" "$config"
      rg -n '\bvmull[bt]\.p16\b' "$dis" | head -20
      "$NM" -S "$elf" | rg 'gf_mat_vec_mul(_add)?$'
      rg -n 'matvec_reference|gf_mat_vec_mul' "${elf%.elf}.map" | head -40
    } >"$RESULT_DIR/correctness/disassembly/$config-$param-evidence.txt"
    cp "$elf" "$dis" "$RESULT_DIR/correctness/disassembly/"
    (
      cd "$RESULT_DIR/correctness/disassembly"
      shasum -a 256 "$(basename -- "$elf")" "$(basename -- "$dis")" \
        >"$config-$param.sha256"
    )
  done
}

check_board_correctness()
{
  for param in $PARAMS_FORWARD; do
    for config in $CONFIGS; do
      configure "$config"
      log="$RESULT_DIR/correctness/board/$config-$param-sign.log"
      run_make "$log" 'SIGN_TEST_PASS' PARAM="$param" \
        BACKEND="$CFG_BACKEND" MATVEC="$CFG_MATVEC" TEST=sign \
        MEMORY=full SIGN_SCHEDULE=lowmem run-board
    done

    checksums=$(grep -h 'SIGN_TEST_PASS' \
      "$RESULT_DIR/correctness/board/"*"-$param-sign.log" \
      | sed -E 's/.* checksum=([0-9a-fA-F]+).*/\1/' | sort -u)
    checksum_count=$(printf '%s\n' "$checksums" | sed '/^$/d' | wc -l | tr -d ' ')
    if test "$checksum_count" != 1; then
      printf 'Board checksum mismatch for %s:\n%s\n' "$param" "$checksums" >&2
      exit 1
    fi
  done

  # The new hybrid configuration receives the preferred on-board official
  # 100-vector KAT for every parameter. A and C receive the same official KAT
  # on the host plus deterministic board sign/verify/tamper checks above.
  for param in $PARAMS_FORWARD; do
    configure mve_refmat
    run_make "$RESULT_DIR/correctness/board/mve_refmat-$param-kat.log" \
      'KAT_PASS' PARAM="$param" BACKEND="$CFG_BACKEND" \
      MATVEC="$CFG_MATVEC" TEST=kat MEMORY=full SIGN_SCHEDULE=lowmem run-board
  done
}

run_benchmarks()
{
  run=1
  while test "$run" -le "$RUNS"; do
    shift_count=$(( (run - 1) % 3 ))
    case "$shift_count" in
      0) order="ref mve_refmat mve_compact" ;;
      1) order="mve_refmat mve_compact ref" ;;
      2) order="mve_compact ref mve_refmat" ;;
    esac
    if test $((run % 2)) -eq 0; then
      param_order=$PARAMS_REVERSE
    else
      param_order=$PARAMS_FORWARD
    fi
    printf 'run=%s config_order=%s param_order=%s\n' \
      "$run" "$order" "$param_order" \
      >"$RESULT_DIR/benchmark/run-$(printf '%02d' "$run")-order.txt"
    for config in $order; do
      configure "$config"
      for param in $param_order; do
        log="$RESULT_DIR/benchmark/run-$(printf '%02d' "$run")/$config/$param.log"
        run_make "$log" 'BENCH_PASS' PARAM="$param" \
          BACKEND="$CFG_BACKEND" MATVEC="$CFG_MATVEC" TEST=benchmark \
          MEMORY=full SIGN_SCHEDULE=lowmem BENCH_KERNEL_SAMPLES=31 \
          BENCH_KERNEL_INNER=64 BENCH_E2E_SAMPLES=7 BENCH_WARMUP=1 run-board
        timer_error=$(grep 'BENCH_TIMER_CHECK' "$log" \
          | sed -E 's/.*error_ppm=([0-9]+).*/\1/' | tail -1)
        if test -z "$timer_error" || test "$timer_error" -gt 20000; then
          printf 'Timer check failed in %s\n' "$log" >&2
          exit 1
        fi
        e2e_count=$(grep -E 'operation=(keypair|sign|verify),' "$log" \
          | grep -c '^BENCH_SAMPLE')
        if test "$e2e_count" != 21; then
          printf 'Expected 21 end-to-end samples in %s, found %s\n' \
            "$log" "$e2e_count" >&2
          exit 1
        fi
        if test "$run" -eq 1; then
          capture_artifact "$config" "$param" benchmark
        fi
      done
    done
    run=$((run + 1))
  done
}

run_profiles()
{
  for config in $CONFIGS; do
    configure "$config"
    for param in $PARAMS_FORWARD; do
      log="$RESULT_DIR/profile/$config/$param.log"
      run_make "$log" 'PROFILE_PASS' PARAM="$param" \
        BACKEND="$CFG_BACKEND" MATVEC="$CFG_MATVEC" TEST=profile \
        MEMORY=full SIGN_SCHEDULE=lowmem PROFILE_SAMPLES=31 \
        PROFILE_KERNEL_INNER=64 PROFILE_COMPLEX_INNER=8 run-board
      capture_artifact "$config" "$param" profile
    done
  done
}

run_phase_256()
{
  for config in $CONFIGS; do
    configure "$config"
    for param in 256f 256s; do
      log="$RESULT_DIR/phase-256/$config/$param.log"
      run_make "$log" 'BENCH_PASS' PARAM="$param" \
        BACKEND="$CFG_BACKEND" MATVEC="$CFG_MATVEC" TEST=phasebench \
        MEMORY=full SIGN_SCHEDULE=lowmem BENCH_KERNEL_SAMPLES=31 \
        BENCH_KERNEL_INNER=64 BENCH_E2E_SAMPLES=7 BENCH_WARMUP=1 run-board
      capture_artifact "$config" "$param" phasebench
    done
  done
}

finalize()
{
  cp "$SCRIPT_DIR/analyze_matvec_ablation.py" "$RESULT_DIR/metadata/"
  (
    cd "$RESULT_DIR"
    find artifacts correctness/disassembly -type f -name '*.elf' \
      -exec shasum -a 256 {} + | sort >metadata/elf-manifest.sha256
  )
  python3 "$SCRIPT_DIR/analyze_matvec_ablation.py" "$RESULT_DIR"
  date -u '+%FT%TZ' >"$RESULT_DIR/COMPLETE"
  (
    cd "$RESULT_DIR"
    find . -type f ! -path './metadata/result-manifest.sha256' \
      -exec shasum -a 256 {} + | sort >metadata/result-manifest.sha256
  )
  say "complete: $RESULT_DIR"
}

capture_metadata
check_host_correctness
check_disassembly
check_board_correctness
run_benchmarks
run_profiles
run_phase_256
finalize
