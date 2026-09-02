# Repeated end-to-end and kernel benchmark

`make bench-paper` performs a clean `-O3` build, runs all correctness gates, and then measures every AIMer v3 parameter set with reference, AVX2, and AVX-512.

```bash
make -C AIMer_v3 bench-paper \
  PAPER_CORE=2 \
  PAPER_RUNS=7 \
  PAPER_E2E_SAMPLES=50 \
  PAPER_E2E_WARMUP=10 \
  PAPER_KERNEL_SAMPLES=75 \
  PAPER_KERNEL_INNER=32 \
  PAPER_KERNEL_WARMUP=10
```

The runner rotates backend order, reverses parameter order every other run, pins every measurement to one logical CPU, and retains every result without outlier deletion. It records CPU topology, governor, EPP, Turbo, compiler, flags, revision, load, binary hashes, and per-run environment snapshots.

End-to-end rows measure keypair, sign, and verify. Kernel rows measure only the optimized scope:

- GF multiply, square, inverse, and matrix-vector multiply;
- operation-equivalent GF party batches for `N=16` and `N=256`;
- operation-equivalent AIM3 MPC party batches;
- SHAKE x1 and x4 using each security level's actual commitment+tape input/output sizes.

Reference batch numbers execute the same scalar operation over all parties. Static commitment/tape orchestration is intentionally not duplicated in a synthetic harness; its benefit remains part of end-to-end signing.

Each kernel process checks its outputs against reference before timing. The report uses the median from each independent run, paired speedups, and a deterministic 10,000-resample bootstrap 95% interval. It emits raw CSVs, `e2e_summary.csv`, `kernel_summary.csv`, environment metadata, and `RESULTS.md`.

The report automatically marks a run as provisional unless at least five independent runs were made under the `performance` governor. A quiet physical core, an idle SMT sibling, controlled Turbo policy, and stable thermal conditions are still required before using numbers in a publication.

## Final adaptive kernel protocol

The fixed-`inner` kernel rows above are useful for a combined exploratory run,
but very short SIMD kernels need a longer timed interval for publication. Use
the separate adaptive protocol for final kernel tables:

```bash
make -C AIMer_v3 bench-kernel-paper \
  PAPER_CORE=2 \
  PAPER_KERNEL_RUNS=15 \
  PAPER_KERNEL_SAMPLES=75 \
  PAPER_KERNEL_WARMUP=10 \
  PAPER_KERNEL_TARGET_CYCLES=2800000
```

The runner first records an untimed calibration data set. For each of the 66
parameter/kernel cells it finds the fastest backend, rounds the required inner
count up to a power of two, and then applies that exact same count to reference,
AVX2, and AVX-512. The default target makes even the fastest timed batch at
least 2,800,000 invariant-TSC cycles. Final measurements use 15 independent
runs, 75 samples per cell and 10 full-batch warm-ups.

Strict mode refuses to start unless the selected CPU has the `performance`
governor and EPP, a locked min/max frequency, disabled Turbo, and no running
OpenOCD process. Raw calibration, the 66-cell inner map, all raw run CSVs, source and
binary hashes, paired-bootstrap confidence intervals, stability checks, and a
complete result-tree checksum are retained. A result is marked paper-ready only
if every one of the 198 backend cells has run-median CV at most 5%.

For the final exclusive-core run on the documented i7-1165G7 host, use the
root wrapper instead of invoking the runner directly:

```bash
sudo ./benchmarks/run_kernel_paper_isolated.sh
```

The wrapper reserves CPU 2 for the benchmark and CPU 0 for housekeeping,
temporarily offlines all other logical CPUs, and confines normal system/user
work to CPU 0. It also pauses OpenOCD, fixes CPU 2 at 2.8 GHz with Turbo off,
fixes the housekeeping CPU at a low frequency, inserts a predeclared 50 ms
idle interval between backend processes, and records the measurement tree on
`tmpfs` before copying it to the repository. This removes shared-package
power/thermal load and benchmark-generated storage interrupts from the timed
phase. Numeric device IRQ affinities are also moved to CPU 0 and restored; the
runner records the CPU 2 device-IRQ delta around every timed cell and requires
zero total device IRQs for a paper-ready result. CPU online state, IRQ affinity,
frequency policy, Turbo, cgroup CPU masks, and paused OpenOCD process groups are
all restored through an EXIT trap.
