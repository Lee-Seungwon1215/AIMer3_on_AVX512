#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Restartable A/B/C/D Cortex-M55 affine experiment.  A completed log is reused
# only when its command, frozen source tree, and generated ELF still match the
# state recorded beside it.  Invalid and failed attempts are moved below
# partial/ instead of being discarded.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
M55_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
REPO_ROOT=$(CDPATH= cd -- "$M55_ROOT/.." && pwd)
TIMESTAMP=$(date '+%Y%m%d-%H%M%S')
RESULT_DIR=${RESULT_DIR:-$SCRIPT_DIR/results/affine-ablation-$TIMESTAMP}
RUNS=${RUNS:-7}
PARAMS_FORWARD="128f 128s 192f 192s 256f 256s"
PARAMS_REVERSE="256s 256f 192s 192f 128s 128f"
CONFIGS="ref mve_refmat mve_compact mve_affine"
TOOLCHAIN_BIN=${TOOLCHAIN_BIN:-/Users/seungwon/test/.tools/xpack-arm-none-eabi-gcc-15.2.1-1.1/bin}
SIZE="$TOOLCHAIN_BIN/arm-none-eabi-size"
NM="$TOOLCHAIN_BIN/arm-none-eabi-nm"
OBJDUMP="$TOOLCHAIN_BIN/arm-none-eabi-objdump"

mkdir -p "$RESULT_DIR/metadata" "$RESULT_DIR/partial" \
  "$RESULT_DIR/correctness/host" "$RESULT_DIR/correctness/board" \
  "$RESULT_DIR/correctness/disassembly" "$RESULT_DIR/benchmark" \
  "$RESULT_DIR/profile" "$RESULT_DIR/phase-256" "$RESULT_DIR/artifacts"

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
  find "$M55_ROOT" -type f \
    ! -path '*/build/*' ! -path '*/benchmarks/results/*' \
    ! -name '.DS_Store' -exec shasum -a 256 {} + | sort >"$destination"
}

freeze_source()
{
  current="$RESULT_DIR/metadata/m55-source.current.sha256"
  frozen="$RESULT_DIR/metadata/m55-source.sha256"
  write_source_manifest "$current"
  if test -f "$frozen"; then
    if ! cmp -s "$frozen" "$current"; then
      diff -u "$frozen" "$current" \
        >"$RESULT_DIR/metadata/SOURCE_MISMATCH.diff" || true
      printf 'Source changed since this result directory was created.\n' >&2
      printf 'See metadata/SOURCE_MISMATCH.diff; refusing to resume.\n' >&2
      exit 1
    fi
    rm "$current"
  else
    mv "$current" "$frozen"
  fi
  SOURCE_DIGEST=$(sha256_file "$frozen")
  export SOURCE_DIGEST
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
}

run_recorded()
{
  log=$1
  marker=$2
  artifact=$3
  shift 3
  command_text=$(printf '%q ' "$@")
  command_digest=$(sha256_text "$command_text")
  state=${log}.state

  if test -f "$log" && test -f "$state" && grep -q "$marker" "$log" && \
     grep -q "^source_sha256=$SOURCE_DIGEST$" "$state" && \
     grep -q "^command_sha256=$command_digest$" "$state"; then
    if test -z "$artifact"; then
      say "skip verified PASS: ${log#$RESULT_DIR/}"
      return 0
    fi
    recorded_artifact=$(sed -n 's/^artifact_sha256=//p' "$state")
    if test -f "$artifact" && \
       test "$(sha256_file "$artifact")" = "$recorded_artifact"; then
      say "skip verified PASS: ${log#$RESULT_DIR/}"
      return 0
    fi
  fi

  preserve_partial "$log"
  mkdir -p "$(dirname -- "$log")"
  say "run: ${log#$RESULT_DIR/}"
  if ! (
    cd "$M55_ROOT"
    printf 'AFFINE_CONTEXT,result_dir=%s,source_sha256=%s,command=%s\n' \
      "$RESULT_DIR" "$SOURCE_DIGEST" "$command_text"
    "$@"
  ) >"$log" 2>&1; then
    printf 'Command failed; preserving %s as an incomplete run.\n' "$log" >&2
    tail -80 "$log" >&2 || true
    return 1
  fi
  require_marker "$log" "$marker"
  artifact_digest=none
  if test -n "$artifact"; then
    if ! test -f "$artifact"; then
      printf 'Expected artifact missing: %s\n' "$artifact" >&2
      return 1
    fi
    artifact_digest=$(sha256_file "$artifact")
  fi
  {
    printf 'source_sha256=%s\n' "$SOURCE_DIGEST"
    printf 'command_sha256=%s\n' "$command_digest"
    printf 'artifact_sha256=%s\n' "$artifact_digest"
    printf 'marker=%s\n' "$marker"
  } >"$state.tmp"
  mv "$state.tmp" "$state"
}

run_make()
{
  log=$1
  marker=$2
  artifact=$3
  shift 3
  run_recorded "$log" "$marker" "$artifact" make -B "$@"
}

capture_artifact()
{
  config=$1
  param=$2
  test_name=$3
  elf=$(elf_path "$config" "$param" "$test_name")
  target="$RESULT_DIR/artifacts/$test_name/$config/$param"
  build_dir=$(dirname -- "$elf")
  mkdir -p "$target/stack-usage"
  cp "$elf" "$target/"
  "$SIZE" -A "$elf" >"$target/size.txt"
  "$SIZE" "$elf" >"$target/size-summary.txt"
  "$NM" -S --size-sort "$elf" >"$target/symbols.txt"
  "$OBJDUMP" -d "$elf" >"$target/$(basename -- "${elf%.elf}.dis")"
  (cd "$target" && shasum -a 256 "$(basename -- "$elf")" >elf.sha256)
  map=${elf%.elf}.map
  test ! -f "$map" || cp "$map" "$target/"
  find "$build_dir" -maxdepth 1 -type f -name '*.su' \
    -exec cp {} "$target/stack-usage/" \;
}

capture_metadata()
{
  if test -f "$RESULT_DIR/metadata/CAPTURED"; then
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
    printf 'configs=A_REF,B_MVE_REFMAT,C_MVE_COMPACT,D_MVE_AFFINE\n'
    printf 'memory=full\n'
    printf 'sign_schedule=lowmem\n'
    printf 'stack_usage_reports=enabled_no_code_instrumentation\n'
    printf 'source_sha256=%s\n' "$SOURCE_DIGEST"
  } >"$RESULT_DIR/metadata/experiment.txt"
  (cd "$REPO_ROOT" && git rev-parse HEAD && git status --short) \
    >"$RESULT_DIR/metadata/git.txt"
  (cd "$REPO_ROOT" && git diff --binary) \
    >"$RESULT_DIR/metadata/source.patch"
  (
    cd "$REPO_ROOT"
    git ls-files --others --exclude-standard -- m55 | \
    while IFS= read -r file; do
      git diff --binary --no-index /dev/null "$file" || test "$?" = 1
    done
  ) >>"$RESULT_DIR/metadata/source.patch"
  (cd "$REPO_ROOT" && git diff --check) \
    >"$RESULT_DIR/metadata/diff-check.txt"
  "$TOOLCHAIN_BIN/arm-none-eabi-gcc" --version \
    >"$RESULT_DIR/metadata/compiler.txt"
  "$TOOLCHAIN_BIN/arm-none-eabi-gcc" -Q -O3 --help=optimizers \
    >>"$RESULT_DIR/metadata/compiler.txt" 2>&1 || true
  /Users/Shared/cortexctl list >"$RESULT_DIR/metadata/boards.txt" 2>&1
  /Users/Shared/cortexctl port m55 >>"$RESULT_DIR/metadata/boards.txt" 2>&1
  cp "$0" "$RESULT_DIR/metadata/"
  cp "$SCRIPT_DIR/analyze_affine_ablation.py" "$RESULT_DIR/metadata/"
  : >"$RESULT_DIR/metadata/CAPTURED"
}

check_host_correctness()
{
  source_log="$RESULT_DIR/correctness/reference-matvec-source.log"
  run_recorded "$source_log" 'MATVEC_SOURCE_PASS width=256' "" \
    python3 tests/check_reference_matvec_source.py
  require_marker "$source_log" 'MATVEC_SOURCE_PASS width=128'
  require_marker "$source_log" 'MATVEC_SOURCE_PASS width=192'

  for param in $PARAMS_FORWARD; do
    run_make "$RESULT_DIR/correctness/host/$param-lowmemory.log" \
      'LOW_MEMORY_DIFF_PASS' "" PARAM="$param" BACKEND=ref \
      MATVEC=reference SIGN_SCHEDULE=lowmem check-low-memory

    for config in $CONFIGS; do
      configure "$config"
      run_make "$RESULT_DIR/correctness/host/$config-$param-field.log" \
        'REF_GF_PASS' "" PARAM="$param" BACKEND="$CFG_BACKEND" \
        MATVEC="$CFG_MATVEC" check-mve-field-host
      if test "$config" = mve_affine; then
        require_marker "$RESULT_DIR/correctness/host/$config-$param-field.log" \
          'AFFINE_DIFF_PASS'
      fi

      if test "$config" = ref; then
        kat_target=check-kat-host
      else
        kat_target=check-kat-mve-host
      fi
      run_make "$RESULT_DIR/correctness/host/$config-$param-kat.log" \
        'KAT_PASS' "" PARAM="$param" BACKEND="$CFG_BACKEND" \
        MATVEC="$CFG_MATVEC" SIGN_SCHEDULE=lowmem "$kat_target"
    done
  done
}

check_board_correctness()
{
  for param in $PARAMS_FORWARD; do
    config=mve_affine
    configure "$config"
    elf=$(elf_path "$config" "$param" reference)
    run_make "$RESULT_DIR/correctness/board/$config-$param-affine.log" \
      'AFFINE_DIFF_PASS' "$elf" PARAM="$param" BACKEND="$CFG_BACKEND" \
      MATVEC="$CFG_MATVEC" TEST=reference MEMORY=full STACK_USAGE=1 \
      run-board

    for config in $CONFIGS; do
      configure "$config"
      elf=$(elf_path "$config" "$param" sign)
      log="$RESULT_DIR/correctness/board/$config-$param-sign.log"
      run_make "$log" 'SIGN_TEST_PASS' "$elf" PARAM="$param" \
        BACKEND="$CFG_BACKEND" MATVEC="$CFG_MATVEC" TEST=sign \
        MEMORY=full SIGN_SCHEDULE=lowmem STACK_USAGE=1 run-board
    done

    checksums=$(grep -h 'SIGN_TEST_PASS' \
      "$RESULT_DIR/correctness/board/"*"-$param-sign.log" | \
      sed -E 's/.* checksum=([0-9a-fA-F]+).*/\1/' | sort -u)
    checksum_count=$(printf '%s\n' "$checksums" | sed '/^$/d' | wc -l | tr -d ' ')
    if test "$checksum_count" != 1; then
      printf 'Board checksum mismatch for %s:\n%s\n' "$param" "$checksums" >&2
      exit 1
    fi

    configure mve_affine
    elf=$(elf_path mve_affine "$param" kat)
    run_make "$RESULT_DIR/correctness/board/mve_affine-$param-kat.log" \
      'KAT_PASS' "$elf" PARAM="$param" BACKEND="$CFG_BACKEND" \
      MATVEC="$CFG_MATVEC" TEST=kat MEMORY=full SIGN_SCHEDULE=lowmem \
      STACK_USAGE=1 run-board
  done
}

check_disassembly()
{
  for param in $PARAMS_FORWARD; do
    configure mve_affine
    elf=$(elf_path mve_affine "$param" sign)
    log="$RESULT_DIR/correctness/disassembly/mve_affine-$param.log"
    run_make "$log" 'MVE affine instructions' "$elf" PARAM="$param" \
      BACKEND="$CFG_BACKEND" MATVEC="$CFG_MATVEC" TEST=sign MEMORY=full \
      SIGN_SCHEDULE=lowmem STACK_USAGE=1 build check-mve-disassembly \
      check-mve-affine-disassembly
    dis=${elf%.elf}.dis
    affine_dis=${elf%.elf}.affine.dis
    target="$RESULT_DIR/correctness/disassembly/$param"
    mkdir -p "$target"
    cp "$elf" "$dis" "$affine_dis" "${elf%.elf}.map" "$target/"
    "$NM" -S --size-sort "$elf" | \
      rg 'm55_gf_mat_vec|gf_mat_vec_mul|m55_aim3_mpc_batch4' \
      >"$target/symbols.txt"
    "$OBJDUMP" -d --disassemble=m55_aim3_mpc_batch4 "$elf" \
      >"$target/mpc-call.dis"
    find "$(dirname -- "$elf")" -maxdepth 1 -type f -name '*.su' \
      -exec cp {} "$target/" \;
    (cd "$target" && find . -type f -exec shasum -a 256 {} + | sort \
      >manifest.sha256)
  done
}

config_order()
{
  case $(( ($1 - 1) % 4 )) in
    0) printf '%s' 'ref mve_refmat mve_compact mve_affine' ;;
    1) printf '%s' 'mve_refmat mve_compact mve_affine ref' ;;
    2) printf '%s' 'mve_compact mve_affine ref mve_refmat' ;;
    3) printf '%s' 'mve_affine ref mve_refmat mve_compact' ;;
  esac
}

run_benchmarks()
{
  run=1
  while test "$run" -le "$RUNS"; do
    order=$(config_order "$run")
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
        elf=$(elf_path "$config" "$param" benchmark)
        run_make "$log" 'BENCH_PASS' "$elf" PARAM="$param" \
          BACKEND="$CFG_BACKEND" MATVEC="$CFG_MATVEC" TEST=benchmark \
          MEMORY=full SIGN_SCHEDULE=lowmem STACK_USAGE=1 \
          BENCH_KERNEL_SAMPLES=31 BENCH_KERNEL_INNER=64 \
          BENCH_E2E_SAMPLES=7 BENCH_WARMUP=1 run-board
        timer_error=$(grep 'BENCH_TIMER_CHECK' "$log" | \
          sed -E 's/.*error_ppm=([0-9]+).*/\1/' | tail -1)
        if test -z "$timer_error" || test "$timer_error" -gt 20000; then
          printf 'Timer check failed in %s\n' "$log" >&2
          exit 1
        fi
        e2e_count=$(grep -E 'operation=(keypair|sign|verify),' "$log" | \
          grep -c '^BENCH_SAMPLE')
        if test "$e2e_count" != 21; then
          printf 'Expected 21 E2E samples in %s, found %s\n' \
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
      elf=$(elf_path "$config" "$param" profile)
      run_make "$log" 'PROFILE_PASS' "$elf" PARAM="$param" \
        BACKEND="$CFG_BACKEND" MATVEC="$CFG_MATVEC" TEST=profile \
        MEMORY=full SIGN_SCHEDULE=lowmem STACK_USAGE=1 \
        PROFILE_SAMPLES=31 PROFILE_KERNEL_INNER=64 \
        PROFILE_COMPLEX_INNER=8 run-board
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
      elf=$(elf_path "$config" "$param" phasebench)
      run_make "$log" 'BENCH_PASS' "$elf" PARAM="$param" \
        BACKEND="$CFG_BACKEND" MATVEC="$CFG_MATVEC" TEST=phasebench \
        MEMORY=full SIGN_SCHEDULE=lowmem STACK_USAGE=1 \
        BENCH_KERNEL_SAMPLES=31 BENCH_KERNEL_INNER=64 \
        BENCH_E2E_SAMPLES=7 BENCH_WARMUP=1 run-board
      capture_artifact "$config" "$param" phasebench
    done
  done
}

finalize()
{
  cp "$SCRIPT_DIR/analyze_affine_ablation.py" "$RESULT_DIR/metadata/"
  (
    cd "$RESULT_DIR"
    find artifacts correctness/disassembly -type f -name '*.elf' \
      -exec shasum -a 256 {} + | sort >metadata/elf-manifest.sha256
  )
  python3 "$SCRIPT_DIR/analyze_affine_ablation.py" "$RESULT_DIR"
  date -u '+%FT%TZ' >"$RESULT_DIR/COMPLETE"
  (
    cd "$RESULT_DIR"
    find . -type f ! -path './metadata/result-manifest.sha256' \
      -exec shasum -a 256 {} + | sort >metadata/result-manifest.sha256
  )
  say "complete: $RESULT_DIR"
}

freeze_source
capture_metadata
check_host_correctness
check_board_correctness
check_disassembly
freeze_source
run_benchmarks
run_profiles
run_phase_256
finalize
