# AIMer v3 reference and optimized implementations

This directory is a standalone AIMer v3 development project. It keeps the
official release intact and separates the local reference and optimized
implementations for correctness testing and performance work.

## Layout

```text
AIMer_v3/
├── Reference_Implementation/       official AIMer v3 source snapshot
├── KAT/                            official known-answer tests
├── src/
│   ├── common/                     AES, SHAKE, and deterministic KAT RNG
│   └── sig/aimer/
│       ├── aimer-128f_ref/
│       ├── aimer-128f_opt/
│       ├── ...
│       ├── aimer-256s_ref/
│       └── aimer-256s_opt/
├── tests/                          shared sign/verify and KAT drivers
├── benchmarks/                     future benchmark programs and results
└── Makefile
```

There are six parameter sets: `128f`, `128s`, `192f`, `192s`, `256f`, and
`256s`. Each has a `_ref` and `_opt` implementation. The optimized directories
currently form a clean AIM3 correctness baseline; AVX-512 kernels will be
migrated into them incrementally.

The detailed AVX-512 handoff and implementation checklist is in
[`PORTING_PLAN_AVX512.md`](PORTING_PLAN_AVX512.md).

## Build

```sh
make -C AIMer_v3 all
```

Executables are written below `AIMer_v3/build/aimer-<parameter>/<implementation>/`.

Useful targets:

```sh
make -C AIMer_v3 ref
make -C AIMer_v3 opt
make -C AIMer_v3 test
make -C AIMer_v3 kat PARAM=128f IMPL=ref
make -C AIMer_v3 kat PARAM=128f IMPL=opt
make -C AIMer_v3 check
```

`make kat` regenerates a response file and compares it byte for byte with the
official AIMer v3 KAT. Run it after every SHAKE, GF, or MPC optimization change.

## Optimization rule

Only implementation details may change. SHAKE call order, domain separation,
field representation, serialization, random-byte consumption, and AIM3's
zero-input handling must remain compatible with the official implementation.

The original source is from the AIMer project and complies with the Korean
Industrial Standards specification. See <https://aimer-signature.org>.
