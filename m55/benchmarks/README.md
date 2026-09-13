# Cortex-M55 benchmarks

Before measurement, the runners verify all six parameter sets with the
reference and MVE KATs on the NUCLEO-N657X0-Q (1,200 official vectors total),
the host differential tests, the independent field oracle, and the MVE
disassembly checks.

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

`BACKEND=mve` selects the final `MATVEC=mve` affine implementation by
default. The matrix-controlled experiment retains all four explicit builds:

```sh
# A: reference GF, reference matrix, scalar party kernels
make -B PARAM=128f BACKEND=ref MATVEC=reference TEST=benchmark \
  MEMORY=full SIGN_SCHEDULE=lowmem build

# B: MVE GF and four-party kernels, official reference matrix only
make -B PARAM=128f BACKEND=mve MATVEC=reference TEST=benchmark \
  MEMORY=full SIGN_SCHEDULE=lowmem build

# C: compact scalar matrix ablation
make -B PARAM=128f BACKEND=mve MATVEC=compact TEST=benchmark \
  MEMORY=full SIGN_SCHEDULE=lowmem build

# D: MVE GF/Frobenius plus single-input and four-party MVE affine
make -B PARAM=128f BACKEND=mve MATVEC=mve TEST=benchmark \
  MEMORY=full SIGN_SCHEDULE=lowmem build
```

The B build keeps the MVE multiplication, squaring/reduction, packing,
Frobenius, constant multiplication, and MPC integration.  It replaces only
`gf_mat_vec_mul`; the shared `gf_mat_vec_mul_add` consequently dispatches to
that selected implementation as well.  The source-copy checker and field
differential test cover both matrix entry points.

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

The complete restartable experiment (correctness gates, seven rotated E2E
runs, profiles, 256-bit phase diagnostics, artifact capture, paired bootstrap
analysis, and Korean Markdown report) is:

```sh
./benchmarks/run_matvec_ablation.sh
```

The corresponding four-way affine experiment measures A/B/C/D together and
never combines samples from separate runs:

```sh
./benchmarks/run_affine_ablation.sh
```

Its resume state validates the frozen M55 source manifest, exact command, and
ELF checksum before accepting an existing PASS log. Partial or mismatched
runs are retained below `partial/`. The official speedup is always baseline
cycles divided by target cycles and includes affine packing/unpacking.

Set `RESULT_DIR` to an existing result directory to resume an interrupted run.
Outputs are written under the ignored `benchmarks/results/<experiment>-<timestamp>/`
tree and are not distributed. A `COMPLETE` file is added only after every gate
and the analyzer have passed.

For causal analysis, `TEST=profile` measures inversion, matrix-vector,
real-exponent Frobenius, complete MPC, and scalar MPC kernels and prints the
static call counts for the selected parameter set.  `TEST=phase` instruments
one sign/verify test.  `TEST=phasebench` applies the same phase counters to the
complete benchmark program so code-footprint-sensitive effects can be
checked.  These are diagnostic images; their counters and `printf` calls mean
their end-to-end values must not replace the official `TEST=benchmark`
results. Generated profiles and analyzer reports remain in the ignored local
result directory.
