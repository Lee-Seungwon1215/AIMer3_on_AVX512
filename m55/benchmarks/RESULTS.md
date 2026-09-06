# Historical v0 NUCLEO-N657X0-Q benchmark results

These measurements precede the dedicated MPC squaring/reduction path and are
kept unchanged as the baseline.  Current results are in
[`RESULTS_MPC_SQUARING.md`](RESULTS_MPC_SQUARING.md) and
[`results_mpc_squaring.csv`](results_mpc_squaring.csv).

Measured on 2026-09-03 on the STM32N657 Cortex-M55 r1p1 at 600 MHz, with
integer/floating-point MVE available (`SCB_GetMVEType() == 2`) and both caches
enabled.  Every row used the low-memory signing schedule and the same
`-O3 -mcpu=cortex-m55 -mthumb -mfloat-abi=hard -mcmse` build.  Keccak/SHAKE
remained the common portable implementation in both backends.

Before benchmarking, the six parameter sets passed 600 reference and 600 MVE
official KAT vectors on this board.  Every benchmark image also completed with
`BENCH_PASS`, `TEST_EXIT=0`, and matching ref/MVE checksums.  The full min,
median, mean, population standard deviation, and max data are in
[`results.csv`](results.csv).

## End-to-end median cycles

Each value is the median of seven samples after one warm-up.  `M/R` is the MVE
median divided by the reference median, so values above one mean that MVE is
slower.

| Param | Keypair ref | Keypair MVE | M/R | Sign ref | Sign MVE | M/R | Verify ref | Verify MVE | M/R |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 128f | 5,113,272 | 8,305,345 | 1.624 | 108,983,410 | 235,219,479 | 2.158 | 60,508,408 | 128,157,517 | 2.118 |
| 128s | 5,115,924 | 8,308,140 | 1.624 | 875,847,882 | 1,865,894,708 | 2.130 | 516,394,233 | 1,104,633,872 | 2.139 |
| 192f | 14,695,933 | 20,800,558 | 1.415 | 329,552,015 | 1,480,364,814 | 4.492 | 186,266,878 | 720,968,449 | 3.871 |
| 192s | 14,706,292 | 20,810,404 | 1.415 | 2,464,158,066 | 11,531,947,057 | 4.680 | 1,493,625,516 | 6,120,369,807 | 4.098 |
| 256f | 44,626,312 | 58,584,192 | 1.313 | 918,764,739 | 1,766,670,967 | 1.923 | 517,562,592 | 912,598,235 | 1.763 |
| 256s | 44,621,502 | 58,612,318 | 1.314 | 7,411,197,192 | 13,271,784,763 | 1.791 | 4,527,008,451 | 7,345,024,817 | 1.622 |

The current MVE backend is slower end to end for every parameter set.  This is
reported without post-measurement tuning or filtering.

## Field and SIMD kernels

These are mean cycles per field element from 31 samples of 64 calls.  The
four-party row is normalized by four; `M/R` again means MVE divided by ref.

| Param | `gf_mul` ref | MVE | M/R | `gf_sqr` ref | MVE | M/R | batch4 ref/item | MVE/item | M/R |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 128f | 4,716.19 | 6,001.78 | 1.273 | 218.46 | 6,005.39 | 27.490 | 4,731.55 | 2,257.63 | 0.477 |
| 128s | 4,716.15 | 6,001.78 | 1.273 | 218.46 | 6,005.37 | 27.490 | 4,731.54 | 2,257.62 | 0.477 |
| 192f | 9,472.13 | 9,344.28 | 0.987 | 368.69 | 9,350.57 | 25.362 | 9,466.30 | 4,944.41 | 0.522 |
| 192s | 9,471.98 | 9,344.27 | 0.987 | 368.71 | 9,350.55 | 25.360 | 9,466.30 | 4,944.44 | 0.522 |
| 256f | 14,207.14 | 12,599.06 | 0.887 | 569.92 | 12,602.28 | 22.112 | 14,235.62 | 9,340.73 | 0.656 |
| 256s | 14,207.19 | 12,599.03 | 0.887 | 569.95 | 12,601.14 | 22.109 | 14,235.62 | 9,340.70 | 0.656 |

The four-party MVE kernel is 2.10x, 1.91x, and 1.52x faster per element at
128, 192, and 256 bits respectively.  General MVE multiplication ranges from
27.3% slower at 128 bits to 11.3% faster at 256 bits.  The dominant regression
is squaring: the reference has a dedicated fast square, while the current MVE
backend routes `gf_sqr` through its general multiply.  That cost, plus the
scalar reduction and packing overhead, outweighs the batch gain in complete
signatures.

## Code and RAM

`text` is the GNU size result for the complete benchmark ELF.  Static RAM is
initialized data plus BSS before the separately reserved stack/heap.  Stack is
a conservative canary high-water mark.  Heap is the maximum `_sbrk` extent
observed across warm-up and measured operations.

| Param | text ref | text MVE | MVE delta | static RAM | stack peak ref/MVE | heap peak ref/MVE |
|---|---:|---:|---:|---:|---:|---:|
| 128f | 71,384 | 67,048 | -4,336 | 10,216 | 7,576 / 7,576 | 16,384 / 16,384 |
| 128s | 71,320 | 66,984 | -4,336 | 7,976 | 7,436 / 7,328 | 61,440 / 61,440 |
| 192f | 73,376 | 68,824 | -4,552 | 18,720 | 14,172 / 14,172 | 28,672 / 28,672 |
| 192s | 73,304 | 68,752 | -4,552 | 13,632 | 13,588 / 13,588 | 94,208 / 94,208 |
| 256f | 75,432 | 70,304 | -5,128 | 34,712 | 23,716 / 23,716 | 61,440 / 61,440 |
| 256s | 75,416 | 70,280 | -5,136 | 23,576 | 22,700 / 22,700 | 167,936 / 167,936 |

The linker reserves 131,072 bytes for stack and exposes a 1,835,008-byte heap
arena in AXISRAM3..6.  The table reports observed high-water use, not those
capacities.

## Measurement details

Short field kernels use the PMU cycle counter.  End-to-end operations use the
CPU-clocked 24-bit SysTick extended by a low-priority overflow handler, so
multi-wrap signatures retain a 64-bit CPU-cycle result.  PMU/SysTick cross
checks in all 12 images differed by 0.0344% to 0.1167%; the check itself is a
short interval where fixed read overhead is visible.  SysTick interrupt cost
is included equally in both backends.

Raw OpenOCD transcripts remain under the ignored `build/full/*/*/benchmark/`
directories.  Rebuild one image with:

```sh
make -B PARAM=128f BACKEND=mve TEST=benchmark MEMORY=full \
  SIGN_SCHEDULE=lowmem build
```
