# Reported results

`table3_median_cycles.csv` contains the signing and verification values used
in the camera-ready performance comparison. Values are median invariant-TSC
cycles over 50 measured iterations with five discarded warm-up iterations.

Measurement environment:

- CPU: Intel Core i7-1165G7 (Tiger Lake)
- Core pinning: logical CPU 2
- CPU-frequency governor: `performance`
- Turbo Boost: disabled
- Compiler: GCC 13.3.0

The AVX2 and AVX-512 columns are the values reported in the submitted paper.
The portable-reference column was measured on 2026-07-30 under the same
benchmark conditions for the camera-ready revision. Its complete raw output,
including key generation, is in `reference_n50_20260730.csv`.

Run `CORE=2 ./bench/run_all.sh 50` from the repository root to produce a new
Reference/AVX2/AVX-512 comparison on the current machine.
