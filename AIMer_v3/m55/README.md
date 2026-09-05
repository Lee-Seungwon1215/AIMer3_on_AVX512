# AIMer v3 Cortex-M55/MVE backend

This is the independent STM32N657 Cortex-M55 port of AIMer v3. It reuses the
portable AIMer v3 API and SHAKE implementation, but never links AIMer v2,
AVX2, AVX-512, or liboqs objects.

## Layout

- `include/`: M55 platform and four-party batch interfaces
- `src/ref/`: bit-exact, memory-bounded signing schedule
- `src/mve/`: 16-bit-limb MVE GF arithmetic and party batching
- `tests/`: field differential, sign/verify/tamper, and official KAT runners
- `toolchain/board/nucleo-n657x0-q/`: startup platform, linker layouts, OpenOCD
- `benchmarks/`: on-board benchmark runner, complete statistics, and report

## Correctness gates

The six valid values of `PARAM` are `128f`, `128s`, `192f`, `192s`, `256f`,
and `256s`.

Host checks compare the low-memory signer byte-for-byte with the original
reference, exercise an independent bit-level field oracle, and consume all
100 vectors in the official RSP file:

```sh
make PARAM=128f check-low-memory
make PARAM=128f check-mve-field-host
make PARAM=128f check-kat-host
make PARAM=128f check-kat-mve-host
```

The explicit affine backend keeps the existing MVE GF and packed Frobenius
path while selecting the Cortex-M55 VAND/VEOR matrix kernels:

```sh
make PARAM=128f BACKEND=mve MATVEC=mve check-mve-field-host
make PARAM=128f BACKEND=mve MATVEC=mve TEST=sign MEMORY=full \
  build check-mve-disassembly check-mve-affine-disassembly
```

`BACKEND=mve` without `MATVEC` still selects `compact`; the new backend does
not change any existing default.

The M55 reference and MVE field tests use the verified 256 KiB bring-up
layout:

```sh
make PARAM=128f BACKEND=ref TEST=reference check-mve-config build
make PARAM=128f BACKEND=mve TEST=reference check-mve-config \
  build check-mve-disassembly
```

End-to-end signing and KAT images use a split AXI SRAM layout. Startup data
and the 128 KiB stack remain in the already-active SRAM2 window; the malloc
arena begins at `0x34200000` after the platform enables AXISRAM3-6:

```sh
make PARAM=128f BACKEND=ref TEST=sign MEMORY=full build
make PARAM=128f BACKEND=mve TEST=kat MEMORY=full \
  build check-mve-disassembly
```

`check-mve-disassembly` fails unless both `VMULLB.P16` and `VMULLT.P16` are
present in the final ELF. `check-mve-affine-disassembly` additionally requires
VAND/VEOR in the four-party affine function and verifies its call from the MPC
batch function. The historical v0 implementation passed 1,200
board KAT vectors.  After adding the MPC squaring/reduction path, all six
parameter sets passed their 100-vector host KAT, all six passed actual-board
sign/verify/tamper tests, and actual-board MVE KATs passed for one parameter at
each field width (`128f`, `192f`, and `256f`).

Build the benchmark image with the same full-memory layout and low-memory
signing schedule:

```sh
make -B PARAM=128f BACKEND=mve TEST=benchmark MEMORY=full \
  SIGN_SCHEDULE=lowmem run-board
```

The current MPC squaring/reduction measurements are in
[`benchmarks/RESULTS_MPC_SQUARING.md`](benchmarks/RESULTS_MPC_SQUARING.md),
with full statistics in
[`benchmarks/results_mpc_squaring.csv`](benchmarks/results_mpc_squaring.csv).
The parameter-dependent causes, including the 192-bit padding question and
the 256-bit matrix-layer confounder, are analyzed in
[`benchmarks/CAUSE_ANALYSIS.md`](benchmarks/CAUSE_ANALYSIS.md).
The original v0 measurement rows and CSV remain in `benchmarks/RESULTS.md` and
`benchmarks/results.csv` as the performance baseline.

The restartable four-configuration affine experiment is driven by
`benchmarks/run_affine_ablation.sh`. It remeasures A/B/C/D in one environment,
checks source/command/ELF hashes before reusing a completed run, and preserves
invalid or failed attempts separately under the selected result directory.

`ARM_GCC_DIR` and `CUBE_N6_DIR` may be overridden on the make command line.
Generated files stay below ignored `build/`; no firmware artifact belongs in
Git.
