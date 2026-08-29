# AIMer v3 optimization status

The `_ref` directories are parameter-specific copies of the official AIMer v3
reference implementation. They are the local correctness baseline.

The `_opt` directories currently contain a correctness-first AIMer v3 baseline.
Their public symbols use an `_opt` namespace, but no AIM2 AVX-512 kernel has been
connected yet. This is intentional: every optimization stage must continue to
match the official AIMer v3 KAT byte for byte.

Planned migration stages:

1. Replace scalar Keccak/SHAKE with the existing x4 and AVX-512VL backend.
2. Replace scalar GF multiplication, squaring, and matrix-vector operations.
3. Add AIM3-specific MPC-party batching and deferred reduction.
4. Benchmark every parameter set and keep KAT checks enabled at each stage.

Do not copy AIM2 constants, inverse-Mersenne exponent chains, or AIM2 MPC
equations into these directories. AIM3 needs its own affine matrices, exponent
maps, proof layout, and zero-input handling.
