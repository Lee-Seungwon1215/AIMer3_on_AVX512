# AIMer v3 benchmarks

This benchmark follows the AIMer_v2 `bench_full` CSV and comparison format,
adapted to the AIMer v3 reference, AVX2, and AVX-512 runtime backends.

## Build and run

```bash
make -C x86 bench
make -C x86 bench-run
```

`bench-run` verifies all 1,800 official KAT responses before measuring, pins
each process to one CPU, measures all six parameter sets, and writes timestamped
results under `benchmarks/results/`.

Useful controls:

```bash
make -C x86 bench-run \
  BENCH_ITERS=100 BENCH_WARMUP=10 BENCH_CORE=2

make -C x86 bench-run \
  BENCH_ITERS=10 BENCH_VERIFY_KAT=0 \
  BENCH_RESULT_DIR=/tmp/aimer-v3-bench
```

Skipping KAT is only intended for repeated measurements after an unchanged
binary has already passed `make check`.

A single backend/parameter can be measured directly:

```bash
x86/build/liboqs/bench/bench_full \
  avx2 AIMER-v3-128f 100 10
```

The benchmark itself sets `AIMER_V3_IMPL` and checks `OQS_SIG.alg_version`, so
a mislabeled dispatch result is rejected. It also refuses AVX2 or AVX-512 when
the current CPU does not provide the required extensions.

## Measurements

For keypair, sign, and verify, `bench_full` records:

- sample count, minimum, median, maximum, and arithmetic mean TSC cycles;
- sample standard deviation and coefficient of variation;
- median and mean microseconds, calibrated from the invariant TSC;
- operations per second derived from median time.

Cycle intervals use serialized `lfence/rdtsc` and `rdtscp/lfence`. They are TSC
reference cycles, not retired core cycles. Same-machine speedup ratios are the
primary comparison; absolute cycle values must be reported together with CPU,
governor, Turbo, compiler, revision, affinity, warm-up, and sample count.

`run_all.sh` saves these details in `metadata.txt`, raw rows in `ref.csv`,
`avx2.csv`, and `avx512.csv`, and the readable speedup table in
`comparison.txt`.

Quick runs validate the harness but are not paper results. Paper measurements
should use a fixed performance governor, a quiet pinned core, adequate warm-up,
multiple independent runs, and a predeclared aggregation policy.
For repeated end-to-end measurements and the separate adaptive 15-run final
kernel protocol, see [`PAPER_BENCHMARK.md`](PAPER_BENCHMARK.md).

## AVX-512 causal decomposition

[`AVX512_CAUSE_ANALYSIS.md`](AVX512_CAUSE_ANALYSIS.md) records the decomposition
that can be established from the checked-in E2E medians and static work counts.
For the missing component cycles, build the separate diagnostic and run it on
an AVX-512 Linux host:

```bash
make -C x86 bench-causes
CORE=2 VERIFY=1 x86/benchmarks/run_avx512_causes.sh
```

The diagnostic splits MPC into setup/affine and Frobenius regions, measures the
scalar kernels used outside MPC, and emits a count-based keypair/sign/verify
model with an explicit residual. It does not alter the production AVX-512
objects or the official paper benchmark's predeclared kernel list.

## Single-input matvec intrinsic ablation

`run_single_matvec_ablation.py` compares two **already built and KAT-validated**
libraries using the same current benchmark harness. It does not build code,
configure the governor/Turbo, stop processes, or isolate CPUs/IRQs. Its reports
are explicitly provisional; use the paper environment protocol before making
final performance claims.

```bash
python3 x86/benchmarks/run_single_matvec_ablation.py \
  --baseline-build /absolute/path/to/baseline/build \
  --candidate-build /absolute/path/to/candidate/build \
  --output /absolute/path/to/new-result-directory --cpu 2

python3 x86/benchmarks/run_single_matvec_ablation.py \
  --output /absolute/path/to/new-result-directory --analyze-only
```

Defaults: all six parameters, reference/AVX2/AVX-512, seven paired runs,
50 E2E samples and 75 single-matvec batches per run, and ten warmups. Kernel
batch size is calibrated once per parameter and shared by both versions and
all three backends. Version order alternates, backend order rotates, and
parameter order reverses. No outliers are removed. Bootstrap intervals resample
paired run medians and are not a substitute for environmental control.

Both benchmark executables accept `AIMER_BENCH_RAW=/path/to/file.csv` to append
individual samples in original order (no header: backend, variant, operation,
sample index, TSC ticks per operation). The paired runner uses a fresh path for
each invocation. `bench_full` additionally accepts `AIMER_BENCH_SEED` containing
96 lowercase hexadecimal digits to seed the NIST KAT DRBG for identical paired
workloads. This is a **benchmark-only deterministic mode**, not a production RNG
setting; the default system RNG is unchanged when the variable is absent.
The final public-key/signature diagnostic checksum is compared across versions
and backends. Any execution or checksum failure stops the experiment.

The 192/256-bit experiment compares auto-vectorizable general C with explicit
AIMer v2-style 256-bit intrinsics in both x86 backends. It does not compare
non-vectorized code against vectorized code. Reference and 128-bit results are
unchanged-code controls. The existing paper results are not overwritten.
