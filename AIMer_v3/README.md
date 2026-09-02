# AIMer v3: reference, AVX2, and AVX-512

This directory is a self-contained AIMer v3/liboqs-compatible research project.
It builds and runs without AIMer_v2 source files or build settings. Optimization
code derived from the AIMer v2 artifact is vendored inside this directory and
adapted to AIMer v3 constants, field arithmetic, MPC equations, and serialization.

The project provides all six parameter sets: `128f`, `128s`, `192f`, `192s`,
`256f`, and `256s`. Each is registered as one `OQS_SIG` algorithm with runtime
dispatch among reference, AVX2, and AVX-512 backends.

## Layout

```text
AIMer_v3/
├── Reference_Implementation/        official AIMer v3 source snapshot
├── KAT/                             official known-answer responses
├── include/oqs/                     minimal public OQS-compatible API
├── src/
│   ├── common/                      AES, portable SHAKE, and KAT RNG
│   ├── oqs/                         OQS glue plus SIMD SHAKE/GF/MPC kernels
│   └── sig/
│       ├── aimer/
│       │   ├── reference/aimer-*    six reference namespaces
│       │   ├── avx2/aimer-*         six AVX2 namespaces
│       │   └── avx512/aimer-*       six AVX-512 namespaces
│       └── aimer_v3/                OQS wrappers and runtime dispatch
├── tests/                           KAT, GF differential, and OQS API tests
├── Makefile                         canonical project entry point
└── Makefile.liboqs.multi            complete three-backend build rules
```

This is a minimal OQS-compatible library, not yet a patch against the complete
upstream liboqs repository.

## Optimization scope

| Backend | SHAKE/Keccak | GF and MPC batching |
|---|---|---|
| reference | portable scalar x1 | scalar reference equations |
| AVX2 | AVX2 x1 plus four-way SIMD256 SHAKE | PCLMUL scalar GF; XMM/YMM party batching; four-way commitments/tapes |
| AVX-512 | AVX-512VL x1/x4 SHAKE | PCLMUL/VPCLMUL GF; ZMM party batching; ternary-logic matrix accumulation; four-way commitments/tapes |

The AVX2 port follows the same algorithmic boundary as the current AVX-512
port. Tree traversal remains sequential, while its individual hash calls use the
selected x1 SHAKE backend. The optimized backends change representation and
parallel scheduling only; they do not change AIMer v3 constants or equations.

## Build and test

```sh
make -C AIMer_v3
make -C AIMer_v3 oqs-test
make -C AIMer_v3 oqs-kat
make -C AIMer_v3 check
make -C AIMer_v3 bench
make -C AIMer_v3 bench-run
```

`make` creates `AIMer_v3/build/lib/liboqs.a`. `make oqs-kat` compares all
6 parameter sets x 3 backends x 100 official vectors, or 1,800 byte-exact KAT
responses. `make oqs-test` also runs direct scalar/batch GF differential tests
and OQS registry, context, sign/verify, and tamper tests.

`make bench` builds the AIMer_v2-style CSV benchmark. `make bench-run` verifies
KATs, pins measurement to one CPU, measures keypair/sign/verify for all six sets
and three backends, stores environment metadata, and prints speedup tables. See
[`benchmarks/README.md`](benchmarks/README.md) before producing paper results.
For independent repeated end-to-end and kernel measurements, use `make bench-paper`; see [`benchmarks/PAPER_BENCHMARK.md`](benchmarks/PAPER_BENCHMARK.md).

A single parameter can be checked with:

```sh
make -C AIMer_v3 kat PARAM=128f
```

For testing on a CPU that supports the requested instruction set, force dispatch
with `AIMER_V3_IMPL=ref`, `AIMER_V3_IMPL=avx2`, or
`AIMER_V3_IMPL=avx512`. Automatic dispatch prefers AVX-512, then AVX2, then
reference. AVX2 requires AES, AVX2, BMI2, PCLMULQDQ, and POPCNT; AVX-512
also requires AVX512F/VL and VPCLMULQDQ.

## Correctness boundary

SHAKE rate, padding, domain separation, absorb/finalize/squeeze order, field
moduli and little-endian word layout, affine matrices, MPC equations,
serialization, random-byte consumption, and AIMer v3 zero-input handling must
remain unchanged. Any official KAT difference is a stop condition for this port.

See `PORTING_PLAN_AVX512.md`, `PORTING_STATUS_AVX512.md`, and
`MATHEMATICAL_EQUIVALENCE_AVX512.md` for the design record and proof boundary.
