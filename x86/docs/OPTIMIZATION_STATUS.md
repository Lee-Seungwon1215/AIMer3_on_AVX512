# AIMer v3 optimization status

The implementation sources are separated by architecture and parameter set:

```text
common/src/sig/aimer/reference/aimer-{128f,128s,192f,192s,256f,256s}
x86/src/sig/aimer/avx2/aimer-{128f,128s,192f,192s,256f,256s}
x86/src/sig/aimer/avx512/aimer-{128f,128s,192f,192s,256f,256s}
```

The OQS integration under `x86/src/oqs` and `x86/src/sig/aimer_v3` provides runtime
reference, AVX2, and AVX-512 dispatch for all six sets. Automatic selection is
AVX-512, then AVX2, then reference; `AIMER_V3_IMPL` can force a backend during
testing on compatible hardware.

AVX2 uses AVX2 Keccak x1, SIMD256 Keccak x4, PCLMUL scalar field operations,
XMM/YMM party batching, and four-way commitment/tape processing. AVX-512 uses
AVX-512VL Keccak x1/x4, PCLMUL/VPCLMUL field operations, ZMM party batching,
VPTERNLOG matrix accumulation, and the same AIM3-aware four-way processing.
Tree traversal remains sequential in both optimized paths.

All 6 x 3 OQS paths match the official 100-vector KAT files: 1,800 byte-exact
responses. Direct differential tests also compare scalar and batch GF operations
among reference, AVX2, and AVX-512 for every parameter set.

The repository is self-contained: its build and source paths do not depend on
`AIMer_v2`. AIM2 optimization code needed by the port is vendored locally.

Next work is measurement and paper engineering: a fixed-machine benchmark
protocol, SHAKE/GF/MPC ablations, statistical reporting, and reproducibility.
Do not copy AIM2 constants, inverse-Mersenne exponent chains, or AIM2 MPC
equations into AIM3 code.
