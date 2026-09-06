# AIMer v3 Cortex-M55/MVE backend

This directory contains the independent STM32N657 Cortex-M55 implementation.
It links the shared AIMer v3 reference and portable SHAKE sources from
`../common/`, but never links x86, AVX2, AVX-512, or liboqs objects.

## Layout

- `include/`: M55 field, party-batch, and platform interfaces
- `src/mve/`: MVE GF, Frobenius, four-party, and affine kernels
- `src/lowmem/`: bit-exact memory-bounded sign/verify schedule and the
  reference-matvec ablation adapter
- `platform/nucleo-n657x0-q/`: startup platform, linker layouts, and OpenOCD
- `toolchain/`: Arm GNU toolchain configuration
- `tests/`: field differential, KAT, sign/verify, and tamper checks
- `benchmarks/`: board runner, analyses, raw results, and reports

## Final backend and ablations

The public optimized backend is:

```text
BACKEND=mve MATVEC=mve MEMORY=full SIGN_SCHEDULE=lowmem
```

Therefore `BACKEND=mve` defaults to `MATVEC=mve`. The other matrix choices
are retained to reproduce the controlled A/B/C/D experiment:

| Label | BACKEND | MATVEC | Purpose |
|---|---|---|---|
| A | ref | reference | scalar reference baseline |
| B | mve | reference | MVE GF and four-party effect with reference matrix |
| C | mve | compact | compact scalar matrix ablation |
| D | mve | mve | final MVE GF, party, and affine backend |

Portable/scalar Keccak/SHAKE remains common to all M55 configurations.

## Host verification

For one parameter set:

```sh
make PARAM=128f BACKEND=mve MATVEC=mve check-mve-field-host
make PARAM=128f BACKEND=mve MATVEC=mve check-kat-mve-host
```

From the repository root, `make m55-host-check` runs both checks for all six
parameter sets.

## Board build and verification

Override `ARM_GCC_DIR`, `CUBE_N6_DIR`, `CORTEXCTL`, and the OpenOCD
paths as needed for the host machine.

```sh
make -B PARAM=128f BACKEND=mve MATVEC=mve TEST=sign MEMORY=full \
  SIGN_SCHEDULE=lowmem build check-mve-disassembly \
  check-mve-affine-disassembly
```

`check-mve-disassembly` requires `VMULLB.P16` and `VMULLT.P16`.
`check-mve-affine-disassembly` requires VAND/VEOR in the four-party affine
kernel and checks that the MPC batch path calls it.

The complete, validated A/B/C/D board experiment and analysis are archived at
[benchmarks/results/affine-ablation-20260905-131658/REPORT.md](benchmarks/results/affine-ablation-20260905-131658/REPORT.md).
Use [benchmarks/README.md](benchmarks/README.md) for exact measurement and
resume procedures.
