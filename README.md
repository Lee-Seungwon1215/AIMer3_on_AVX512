# AIMer v3 SIMD implementations

This repository contains one AIMer v3 codebase with two independent hardware
backends. The x86 implementation provides reference, AVX2, and AVX-512
selection through a minimal OQS-compatible API. The Cortex-M55 implementation
uses Arm MVE for GF arithmetic, four-party execution, and affine processing.

The optimized backends preserve the AIMer v3 parameters, equations, byte
encoding, random-byte consumption, and SHAKE domain separation. An official
KAT mismatch is a stop condition.

## Repository layout

```text
.
├── common/   portable primitives, six reference namespaces, official KATs,
│             and the untouched upstream reference snapshot
├── x86/      OQS glue, AVX2/AVX-512 sources, tests, benchmarks, and results
└── m55/      Cortex-M55 MVE sources, platform support, tests, and results
```

The dependency direction is deliberately one-way:

```text
x86 ──> common <── m55
```

The x86 and M55 directories do not link to or include one another.

## Quick start: x86

```sh
make x86
make x86-test
make x86-kat
make x86-check
```

The library is written to `x86/build/lib/liboqs.a`. Runtime dispatch can be
forced with `AIMER_V3_IMPL=ref`, `AIMER_V3_IMPL=avx2`, or
`AIMER_V3_IMPL=avx512`.

This is a self-contained, minimal OQS-compatible research library. It is not
yet a patch against the complete upstream liboqs repository.

## Quick start: Cortex-M55

Host-side differential and KAT checks for the final MVE-affine backend:

```sh
make m55-host-check
```

A board build is performed inside `m55/`:

```sh
make -C m55 -B PARAM=128f BACKEND=mve MATVEC=mve \
  TEST=benchmark MEMORY=full SIGN_SCHEDULE=lowmem build
```

For `BACKEND=mve`, the public default is the final
`MATVEC=mve` configuration. The reference and compact matrix variants remain
available only for controlled ablation.

See [x86/README.md](x86/README.md) and [m55/README.md](m55/README.md) for
backend-specific requirements and commands. The completed x86 measurements
are under `x86/benchmarks/results/`; the completed Cortex-M55 affine
ablation is under
`m55/benchmarks/results/affine-ablation-20260905-131658/`.

## Recovery point

The repository state before this directory-only reorganization is preserved
by the annotated tag `pre-restructure-validated-20260906` and the branch
`backup/pre-restructure-20260906`.

## License

See [LICENSE](LICENSE) and [Open Source Notice.txt](Open%20Source%20Notice.txt).
