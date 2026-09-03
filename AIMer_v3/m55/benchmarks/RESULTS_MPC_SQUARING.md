# M55 MPC squaring/reduction results

Measured on 2026-09-03 on the STM32N657 Cortex-M55 r1p1 at 600 MHz, with
integer/floating-point MVE available (`SCB_GetMVEType() == 2`) and both caches
enabled.  Reference and MVE images used the same low-memory schedule,
portable Keccak/SHAKE, AXI SRAM layout, compiler flags, inputs, and benchmark
program.  The historical pre-squaring baseline remains in
[`RESULTS.md`](RESULTS.md) and [`results.csv`](results.csv); it was not
overwritten.  Full statistics for this version are in
[`results_mpc_squaring.csv`](results_mpc_squaring.csv).  The follow-up kernel
and phase decomposition is in [`CAUSE_ANALYSIS.md`](CAUSE_ANALYSIS.md); it
explains the 192-bit ratio and separates the 256-bit affine-layer effect from
packed Frobenius speedup.

## Implemented path

- General MVE multiplication keeps the 16-bit polynomial-limb convolution but
  replaces bit-at-a-time reduction with the reference-equivalent 64-bit word
  folds.
- Scalar `gf_sqr` uses the dedicated reference squaring expansion and the fast
  word reduction instead of calling general multiplication.
- Four independent parties are packed into the four 32-bit lanes of an MVE Q
  register.  `VMULLB.P16`/`VMULLT.P16` square both 16-bit halves, and reduction
  is performed in the same structure-of-arrays layout.
- Repeated squaring for `x^(2^e)` stays packed across the full Frobenius chain,
  avoiding a pack/unpack on every square.
- The low-memory signer and verifier call the four-party MPC helper.  AIMer
  equations, field polynomials, transcript order, serialized signature, and
  KAT output are unchanged.  Matrix-vector operations and Keccak/SHAKE remain
  scalar/portable and are outside this optimization.

## Correctness gates

- All six parameter sets passed the host low-memory differential test against
  the upstream signer and the host MVE KAT: 600 official vectors for the
  optimized implementation.
- The independent field oracle covers zero, one, all-ones, single-bit, and
  deterministic random inputs, including batch Frobenius exponents and one to
  four active lanes.  Actual M55/MVE runs passed 259 cases at 128 bits, 323 at
  192 bits, and 387 at 256 bits.
- Actual-board sign/verify/tamper tests passed for all six parameter sets.
- Actual-board MVE KATs passed 100/100 for each field width (`128f`, `192f`,
  and `256f`).  The `s` variants use the same field implementation at each
  width and were additionally covered by their host KAT and actual-board
  end-to-end test.
- Disassembly checks found both `VMULLB.P16` and `VMULLT.P16` in every MVE
  image used for measurement.
- Every benchmark image ended with `BENCH_PASS` and `TEST_EXIT=0`; reference
  and MVE checksums matched for every parameter set.

## End-to-end median cycles

Each value is the median of seven samples after one warm-up.  `M/R` is the MVE
median divided by the reference median, so a value below one is faster.

| Param | Keypair ref | MVE | M/R | Sign ref | MVE | M/R | Verify ref | MVE | M/R |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 128f | 5,112,496 | 3,977,230 | 0.778 | 109,382,484 | 95,562,920 | 0.874 | 62,591,167 | 51,054,916 | 0.816 |
| 128s | 5,115,226 | 3,978,278 | 0.778 | 864,481,446 | 760,548,847 | 0.880 | 512,034,891 | 413,222,700 | 0.807 |
| 192f | 14,698,509 | 10,387,828 | 0.707 | 332,559,372 | 260,691,105 | 0.784 | 195,962,648 | 146,260,423 | 0.746 |
| 192s | 14,706,631 | 10,399,006 | 0.707 | 2,490,555,564 | 1,921,149,450 | 0.771 | 1,498,980,182 | 1,081,437,476 | 0.721 |
| 256f | 44,619,048 | 35,428,625 | 0.794 | 918,249,684 | 689,460,705 | 0.751 | 551,134,148 | 403,194,827 | 0.732 |
| 256s | 44,622,766 | 35,436,609 | 0.794 | 7,343,857,251 | 4,705,205,127 | 0.641 | 4,535,972,912 | 2,821,677,956 | 0.622 |

The optimized MVE image is faster than the same-version reference image for
all 18 end-to-end comparisons.  Median signing improves by 12.0% to 35.9%,
and verification by 18.4% to 37.8%.

## Change from the historical MVE baseline

This table compares against the original MVE measurements in `results.csv`.
It quantifies the complete change set (fast reduction, dedicated squaring,
packed Frobenius/MPC, and signer/verifier integration), not the isolated cost
of one instruction or one kernel.

| Param | v0 sign | Current sign | Speedup | v0 verify | Current verify | Speedup |
|---|---:|---:|---:|---:|---:|---:|
| 128f | 235,219,479 | 95,562,920 | 2.46x | 128,157,517 | 51,054,916 | 2.51x |
| 128s | 1,865,894,708 | 760,548,847 | 2.45x | 1,104,633,872 | 413,222,700 | 2.67x |
| 192f | 1,480,364,814 | 260,691,105 | 5.68x | 720,968,449 | 146,260,423 | 4.93x |
| 192s | 11,531,947,057 | 1,921,149,450 | 6.00x | 6,120,369,807 | 1,081,437,476 | 5.66x |
| 256f | 1,766,670,967 | 689,460,705 | 2.56x | 912,598,235 | 403,194,827 | 2.26x |
| 256s | 13,271,784,763 | 4,705,205,127 | 2.82x | 7,345,024,817 | 2,821,677,956 | 2.60x |

## Field and SIMD kernels

Values are mean cycles per field element from 31 samples of 64 calls.  Batch
rows are normalized by four.  The current MVE-to-reference ratio is shown in
the last column of each group.

| Param | `gf_mul` ref | MVE | M/R | `gf_sqr` ref | MVE | M/R | batch4 square ref/item | MVE/item | M/R |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 128f | 4,716.15 | 741.08 | 0.157 | 216.46 | 248.41 | 1.148 | 233.54 | 116.40 | 0.498 |
| 128s | 4,716.14 | 741.08 | 0.157 | 216.46 | 248.41 | 1.148 | 233.54 | 116.40 | 0.498 |
| 192f | 9,472.06 | 1,420.09 | 0.150 | 366.79 | 404.55 | 1.103 | 389.35 | 163.98 | 0.421 |
| 192s | 9,472.07 | 1,420.09 | 0.150 | 366.72 | 404.59 | 1.103 | 388.34 | 163.98 | 0.422 |
| 256f | 14,208.28 | 2,289.19 | 0.161 | 570.90 | 567.83 | 0.995 | 595.59 | 377.12 | 0.633 |
| 256s | 14,208.43 | 2,290.04 | 0.161 | 570.87 | 567.85 | 0.995 | 595.68 | 377.12 | 0.633 |

The scalar MVE squaring entry point is 14.8% and 10.3% slower than the highly
tuned reference at 128 and 192 bits, and is at parity at 256 bits.  The MPC
path does not use that entry point party by party: its four-party square costs
only 49.8%, 42.1%, and 63.3% of the scalar-reference batch cost per element.
Compared with the historical MVE `gf_sqr=gf_mul(a,a)` path, one scalar MVE
square is now 24.2x, 23.1x, and 22.2x faster at 128, 192, and 256 bits.

The existing four-party constant multiplication remains faster per element:
2,257.90 versus 4,731.55 cycles at 128 bits, about 4,945 versus 9,466 at 192
bits, and about 9,341 versus 14,237 at 256 bits.

## Code and RAM

`text` is the GNU size result for the complete benchmark ELF.  Static RAM is
initialized data plus BSS before the separately reserved stack and heap.
Stack and heap figures are observed high-water marks from the benchmark run.

| Param | text ref | text MVE | MVE delta | static RAM | stack peak ref/MVE | heap peak ref/MVE |
|---|---:|---:|---:|---:|---:|---:|
| 128f | 73,024 | 70,792 | -2,232 | 10,216 | 7,984 / 7,984 | 16,384 / 16,384 |
| 128s | 72,792 | 70,560 | -2,232 | 7,976 | 7,736 / 7,736 | 61,440 / 61,440 |
| 192f | 75,408 | 74,040 | -1,368 | 18,720 | 14,788 / 14,680 | 28,672 / 28,672 |
| 192s | 75,304 | 73,936 | -1,368 | 13,632 | 14,204 / 14,204 | 94,208 / 94,208 |
| 256f | 78,096 | 77,008 | -1,088 | 34,712 | 24,708 / 24,708 | 61,440 / 61,440 |
| 256s | 77,864 | 76,776 | -1,088 | 23,576 | 23,700 / 23,700 | 167,936 / 167,936 |

The linker reserves 131,072 bytes for stack and exposes a 1,835,008-byte heap
arena in AXISRAM3..6.  No benchmark exceeded those bounds.

## Measurement notes

Short kernels use the PMU cycle counter.  End-to-end operations use the
CPU-clocked 24-bit SysTick extended by a low-priority overflow handler.  The
PMU/SysTick cross-check error across these 12 images was 0.037% to 0.823%,
below the 2% rejection threshold.  Raw OpenOCD transcripts are generated only
under ignored temporary/build locations and are not repository artifacts.
