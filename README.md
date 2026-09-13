# AIMer v3 SIMD implementations

This repository provides one AIMer v3 codebase with independent x86 and
Cortex-M55 backends. The x86 implementation exposes reference, AVX2, and
AVX-512 implementations through a minimal OQS-compatible API. The Cortex-M55
implementation uses Arm MVE for field arithmetic, four-party execution, and
affine processing.

The optimized backends preserve the AIMer v3 parameters, equations, byte
encoding, random-byte consumption, and SHAKE domain separation. An official
known-answer test (KAT) mismatch is a stop condition.

## Supported implementations

| Target | Backends | Optimized components | Interface |
|---|---|---|---|
| x86-64 | reference, AVX2, AVX-512 | GF arithmetic, fixed inversion chains, party/MPC processing, affine operations, SHAKE | minimal OQS-compatible API |
| Cortex-M55 | reference, MVE | GF arithmetic, fixed inversion chains, four-party processing, affine operations | standalone embedded implementation |

The fixed inversion chains retain the required number of field squarings but
reduce the number of general field multiplications:

| Field | Reference multiplications | Fixed-chain multiplications |
|---|---:|---:|
| GF(2^128) | 127 | 10 |
| GF(2^192) | 191 | 11 |
| GF(2^256) | 255 | 10 |

## Repository layout

```text
.
├── common/   portable primitives, six reference namespaces, official KATs,
│             and the untouched upstream reference snapshot
├── x86/      OQS glue, AVX2/AVX-512 sources, tests, and benchmark tools
└── m55/      Cortex-M55 MVE sources, platform support, tests, and benchmark tools
```

The dependency direction is deliberately one-way:

```text
x86 ──> common <── m55
```

The x86 and M55 directories do not link to or include one another.

## Quick start

Build and verify the x86 implementation:

```sh
make x86
make x86-test
make x86-kat
make x86-check
```

Run the host-side MVE field and KAT checks for all six parameter sets:

```sh
make m55-host-check
```

The x86 library is written to `x86/build/lib/liboqs.a`. See
[x86/README.md](x86/README.md) and [m55/README.md](m55/README.md) for ISA,
toolchain, board, and backend-specific instructions.

## Validation status

The current x86 source passes all 1,800 official responses: six parameter sets,
three backends, and 100 vectors per backend. The current Cortex-M55 source
passes 600 reference and 600 host-portable MVE responses, as well as the MVE
field differential tests. The modified MVE field source also cross-compiles
for all six Cortex-M55 parameter sets.

Benchmark outputs and logs are intentionally excluded from this repository.
Performance results for the current source must therefore be regenerated
locally. In particular, the fixed M55 inversion chains have not yet been
rerun on the physical board.

## Benchmark reproduction

- [x86 benchmark protocol](x86/benchmarks/README.md)
- [Cortex-M55 benchmark protocol](m55/benchmarks/README.md)

Same-platform speedup ratios are the primary comparison. Absolute x86 and
Cortex-M55 cycle counts are not directly comparable because the processors,
cycle counters, clocks, and execution environments differ.

## Limitations

- The x86 tree is a self-contained, minimal OQS-compatible research library,
  not a patch against the complete upstream liboqs repository.
- Cortex-M55 uses the shared scalar Keccak/SHAKE implementation.
- Current Cortex-M55 performance claims require post-inversion-change board
  remeasurement.

## Recovery point

The repository state before the directory-only reorganization is preserved by
the annotated tag `pre-restructure-validated-20260906` and the branch
`backup/pre-restructure-20260906`.

## License and attribution

See [LICENSE](LICENSE) and [Open Source Notice.txt](Open%20Source%20Notice.txt).
