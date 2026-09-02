# AIMer v3 benchmarks

This benchmark follows the AIMer_v2 `bench_full` CSV and comparison format,
adapted to the AIMer v3 reference, AVX2, and AVX-512 runtime backends.

## Build and run

```bash
make -C AIMer_v3 bench
make -C AIMer_v3 bench-run
```

`bench-run` verifies all 1,800 official KAT responses before measuring, pins
each process to one CPU, measures all six parameter sets, and writes timestamped
results under `benchmarks/results/`.

Useful controls:

```bash
make -C AIMer_v3 bench-run \
  BENCH_ITERS=100 BENCH_WARMUP=10 BENCH_CORE=2

make -C AIMer_v3 bench-run \
  BENCH_ITERS=10 BENCH_VERIFY_KAT=0 \
  BENCH_RESULT_DIR=/tmp/aimer-v3-bench
```

Skipping KAT is only intended for repeated measurements after an unchanged
binary has already passed `make check`.

A single backend/parameter can be measured directly:

```bash
AIMer_v3/build/liboqs/bench/bench_full \
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
