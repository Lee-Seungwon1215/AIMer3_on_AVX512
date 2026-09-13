# AIMer v3 x86 backend

This directory contains the independent x86-64 implementation. It does not
build against AIMer v2 or Cortex-M55 sources; shared portable and reference
material is used only through `../common/`.

All six parameter sets (`128f`, `128s`, `192f`, `192s`, `256f`,
`256s`) are available through a minimal OQS-compatible API with runtime
selection among reference, AVX2, and AVX-512 backends.

## Requirements

- a Linux x86-64 host;
- GNU Make, a C11 compiler, and an archiver;
- AVX2, PCLMULQDQ, BMI2, POPCNT, and AES-NI for the AVX2 backend;
- AVX-512F/VL/BW/DQ and VPCLMULQDQ in addition to the AVX2 requirements for
  the AVX-512 backend.

The runtime dispatcher checks CPU support before selecting an optimized
backend. Unsupported forced selections are rejected.

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

| Backend | SHAKE/Keccak | GF arithmetic | Party/MPC and affine processing |
|---|---|---|---|
| reference | scalar x1 | reference equations and inversion loop | reference equations |
| AVX2 | AVX2 x1 and four-way SIMD256 | PCLMULQDQ kernels and fixed inversion chains | XMM/YMM grouping and separate AND/XOR affine accumulation |
| AVX-512 | AVX-512VL x1/x4 | PCLMULQDQ/VPCLMULQDQ kernels and fixed inversion chains | ZMM grouping and VPTERNLOGQ affine accumulation |

For inversion in GF(2^n), the optimized backends retain `n - 1` squarings
but reuse intermediate powers to reduce general multiplication:

| Field | Reference multiplications | AVX2/AVX-512 multiplications |
|---|---:|---:|
| GF(2^128) | 127 | 10 |
| GF(2^192) | 191 | 11 |
| GF(2^256) | 255 | 10 |

These implementations change data representation and execution order, not the
AIMer v3 equations, serialization, or public API.

## Build and verify

From the repository root:

```sh
make x86
make x86-test
make x86-kat
make x86-check
```

The same operations can be run directly with `make -C x86`. The complete
check covers six parameter sets, three backends, and 100 official vectors per
backend, totaling 1,800 byte-exact KAT responses. It also runs GF differential,
registry, forced-dispatch, sign/verify, and tamper tests.

The generated library is:

```text
x86/build/lib/liboqs.a
```

## Runtime backend selection

Automatic dispatch prefers AVX-512, then AVX2, then reference. A backend can
be selected explicitly:

```sh
AIMER_V3_IMPL=ref     ./program
AIMER_V3_IMPL=avx2    ./program
AIMER_V3_IMPL=avx512  ./program
```

## Benchmarks

```sh
make -C x86 bench
make -C x86 bench-run BENCH_ITERS=100 BENCH_WARMUP=10 BENCH_CORE=2
```

Read [benchmarks/README.md](benchmarks/README.md) before reporting results.
Paper-grade runs require a fixed performance governor, disabled Turbo Boost,
CPU affinity, sufficient warm-up, repeated runs, and recorded compiler,
revision, and binary hashes.

Generated benchmark outputs and logs are intentionally excluded from this
repository. Since the current fixed inversion-chain changes postdate earlier
local measurements, rerun the benchmark before reporting performance for this
source revision.

## Limitations

This directory provides a minimal OQS-compatible research library. It is not a
complete upstream liboqs checkout or an upstream-ready patch.
