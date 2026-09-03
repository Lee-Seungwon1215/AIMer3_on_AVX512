# Cortex-M55 benchmarks

The original benchmark gate opened after all six parameter sets passed the
reference and MVE KATs on the NUCLEO-N657X0-Q (1,200 official vectors total),
the host differential tests, the independent field oracle, and the MVE
disassembly checks.  The later MPC squaring/reduction measurements document
their own post-change correctness gates in `RESULTS_MPC_SQUARING.md`.

Both backends use the same Cortex-M55 flags, 600 MHz clock configuration,
SRAM layout, low-memory signing schedule, portable Keccak/SHAKE code, and
measurement program.  `gf_mul`, `gf_sqr`, four-party squaring, and four-party
constant multiplication use the 32-bit PMU cycle counter.  The batch rows are
reported both per call and normalized per field element.  End-to-end keypair,
sign, and verify extend a CPU-clocked 24-bit SysTick counter in its overflow
handler.  The reported unit is therefore still CPU cycles while avoiding the
PMU's roughly 7.16-second wrap limit.  At 600 MHz, the low-priority overflow
handler runs about 36 times per second and its small cost is included equally
in both backends.

The runner emits machine-readable `BENCH_RESULT` records containing the
sample count, inner-loop count, minimum, median, population standard
deviation, mean, maximum, and mean cycles per processed item.  Defaults are
31 kernel samples, 7 end-to-end samples, 64 calls per kernel sample, and one
end-to-end warm-up.  Override them through the corresponding Make variables.
The final `BENCH_MEMORY` record reports static data/BSS, the fixed stack and
heap capacities, a canary-based conservative stack high-water mark, and the
newlib heap high-water mark observed across the run.

Example build:

```sh
make -B PARAM=128f BACKEND=mve TEST=benchmark MEMORY=full \
  SIGN_SCHEDULE=lowmem build
```

With the configured NUCLEO board attached, build and run through the shared
board lock with:

```sh
make -B PARAM=128f BACKEND=mve TEST=benchmark MEMORY=full \
  SIGN_SCHEDULE=lowmem run-board
```

The OpenOCD and board-arbitration paths are Make variables and can be
overridden for another host installation.

Raw board logs belong under the ignored `build/` tree.  A summarized result
report is added here only from completed board runs; failed or partial runs
must not be presented as measurements.

For causal analysis, `TEST=profile` measures inversion, matrix-vector,
real-exponent Frobenius, complete MPC, and scalar MPC kernels and prints the
static call counts for the selected parameter set.  `TEST=phase` instruments
one sign/verify test.  `TEST=phasebench` applies the same phase counters to the
complete benchmark program so code-footprint-sensitive effects can be
checked.  These are diagnostic images; their counters and `printf` calls mean
their end-to-end values must not replace the official `TEST=benchmark`
results.  See [`CAUSE_ANALYSIS.md`](CAUSE_ANALYSIS.md) for the findings.
