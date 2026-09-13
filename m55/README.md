# AIMer v3 Cortex-M55/MVE backend

This directory contains the independent STM32N657 Cortex-M55 implementation.
It links the shared AIMer v3 reference and scalar Keccak/SHAKE sources from
`../common/`, but never links x86, AVX2, AVX-512, or liboqs objects.

All six parameter sets (`128f`, `128s`, `192f`, `192s`, `256f`,
`256s`) are supported.

## Target and toolchain requirements

- an Arm Cortex-M55 target with integer and floating-point MVE;
- the NUCLEO-N657X0-Q board for the supplied platform configuration;
- an Arm GNU Toolchain with Cortex-M55/MVE support;
- STM32CubeN6 CMSIS, device, HAL, and template files;
- OpenOCD and the project-specific board scripts for flashing and execution.

The Arm GNU tools, OpenOCD, and `cortexctl` are resolved from `PATH` by
default. Board builds require `CUBE_N6_DIR` to identify the STM32CubeN6 root.
Use `ARM_GCC_DIR`, `CORTEXCTL`, `OPENOCD`, `OPENOCD_SCRIPTS`, and
`OPENOCD_TARGET_SCRIPTS` to select tools installed elsewhere.

## Layout

- `include/`: M55 field, party-batch, and platform interfaces
- `src/mve/`: MVE GF, Frobenius, four-party, and affine kernels
- `src/lowmem/`: memory-bounded sign/verify schedule and reference adapters
- `platform/nucleo-n657x0-q/`: startup platform, linker layouts, and OpenOCD
- `toolchain/`: Arm GNU toolchain configuration
- `tests/`: field differential, KAT, sign/verify, and tamper checks
- `benchmarks/`: board runners, analysis scripts, and measurement documentation

## Final backend

The public optimized configuration is:

```text
BACKEND=mve MATVEC=mve MEMORY=full SIGN_SCHEDULE=lowmem
```

`BACKEND=mve` therefore selects `MATVEC=mve` by default. Reference and
alternative matrix implementations remain available only for controlled
ablation; see [benchmarks/README.md](benchmarks/README.md).

## Optimization boundary

| Component | Reference | MVE backend |
|---|---|---|
| field multiplication | scalar reference | 16-bit limbs with `VMULLB.P16` and `VMULLT.P16` |
| field inversion | reference loop | fixed addition chains using the MVE multiplication and squaring kernels |
| party/MPC processing | scalar parties | four parties arranged by corresponding 32-bit words |
| affine processing | scalar matrix-vector operations | `VAND` selection and `VEOR` accumulation |
| Keccak/SHAKE | shared scalar implementation | shared scalar implementation |

The fixed inversion chains retain `n - 1` field squarings while reducing
general multiplication:

| Field | Reference multiplications | MVE fixed-chain multiplications |
|---|---:|---:|
| GF(2^128) | 127 | 10 |
| GF(2^192) | 191 | 11 |
| GF(2^256) | 255 | 10 |

The MVE backend changes representation and scheduling only. AIMer v3
parameters, equations, encoding, randomness consumption, and domain separation
remain unchanged.

## Host verification

For one MVE parameter set:

```sh
make -C m55 PARAM=128f BACKEND=mve MATVEC=mve check-mve-field-host
make -C m55 PARAM=128f BACKEND=mve MATVEC=mve check-kat-mve-host
```

For the corresponding reference KAT:

```sh
make -C m55 PARAM=128f BACKEND=ref check-kat-host
```

From the repository root, the following command runs the MVE field and KAT
checks for all six parameter sets:

```sh
make m55-host-check
```

The current source passes 600 reference and 600 host-portable MVE KAT vectors.
The MVE field differential tests also pass for all six parameter sets.

## Board build and verification

```sh
make -C m55 -B PARAM=128f BACKEND=mve MATVEC=mve TEST=sign \
  MEMORY=full SIGN_SCHEDULE=lowmem build \
  check-mve-disassembly check-mve-affine-disassembly
```

`check-mve-disassembly` requires `VMULLB.P16` and `VMULLT.P16`.
`check-mve-affine-disassembly` requires `VAND` and `VEOR` in the
four-party affine kernel and verifies its use by the MPC batch path.

The current fixed inversion chains pass host-side KATs and source-level
Cortex-M55 cross-compilation for all six parameter sets. They have not yet
been rerun on the physical board. Full firmware linking on a new host also
requires the external STM32CubeN6 files listed above.

## Benchmarks

Use [benchmarks/README.md](benchmarks/README.md) for the measurement protocol,
ablation configurations, board runner, and resume procedure.

Generated logs, reports, and datasets remain under the ignored `build/` and
`benchmarks/results/` directories and are not distributed. Because the fixed
M55 inversion chains have not yet been rerun on the physical board, rerun the
board checks and measurements before reporting current-source performance.

## Limitations

- Keccak/SHAKE is not vectorized with MVE.
- The supplied platform and linker setup target the NUCLEO-N657X0-Q.
- The M55 backend is standalone and does not expose the x86 minimal OQS API.
