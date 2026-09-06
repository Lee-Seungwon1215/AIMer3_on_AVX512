# AIMer v3 x86 backend

This directory contains the independent x86 implementation. It builds without
AIMer v2 or Cortex-M55 sources and uses shared portable/reference material only
through `../common/`.

All six parameter sets (`128f`, `128s`, `192f`, `192s`, `256f`,
`256s`) are exposed through a minimal OQS-compatible API with runtime
selection among reference, AVX2, and AVX-512 backends.

## Layout

- `include/oqs/`: public minimal OQS-compatible headers
- `src/sig/aimer/avx2/`: parameter-specific AVX2 namespaces
- `src/sig/aimer/avx512/`: parameter-specific AVX-512 namespaces
- `src/sig/aimer_v3/`: OQS wrappers and runtime dispatch
- `src/oqs/`: OQS glue and optimized SHAKE, GF, MPC, and affine kernels
- `tests/`: KAT, GF differential, registry, sign/verify, and tamper tests
- `benchmarks/`: end-to-end and optimized-kernel measurements
- `docs/`: porting record, equivalence boundary, and optimization status

## Optimization boundary

| Backend | SHAKE/Keccak | GF, affine, and party processing |
|---|---|---|
| reference | portable scalar x1 | reference equations |
| AVX2 | AVX2 x1 and four-way SIMD256 | PCLMUL GF, XMM/YMM party batching, AND/XOR affine accumulation |
| AVX-512 | AVX-512VL x1/x4 | PCLMUL/VPCLMUL GF, ZMM party batching, ternary-logic affine accumulation |

The optimized implementations change data representation and execution
schedule, not AIMer v3 mathematics or serialization.

## Build and verify

From the repository root:

```sh
make x86
make x86-test
make x86-kat
make x86-check
make x86-bench
```

Or run the same targets directly with `make -C x86`. The library is
`x86/build/lib/liboqs.a`. The KAT target checks
6 parameter sets x 3 backends x 100 vectors, totaling 1,800 byte-exact
responses.

Runtime dispatch prefers AVX-512, then AVX2, then reference. Force a backend
with `AIMER_V3_IMPL=ref`, `AIMER_V3_IMPL=avx2`, or
`AIMER_V3_IMPL=avx512`.

See [benchmarks/README.md](benchmarks/README.md) before producing measurements.
Paper-grade runs additionally require the recorded CPU governor, Turbo Boost,
core-affinity, compiler, and binary-hash metadata.

This is a minimal OQS-compatible library, not a full upstream liboqs checkout.
