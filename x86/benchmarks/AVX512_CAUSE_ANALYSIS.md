# AVX-512 result decomposition

This note separates what can already be concluded from the checked-in final
end-to-end medians and source-level operation counts from what still requires
an AVX-512 diagnostic run.  It does not replace the final paper benchmark.

## Confirmed end-to-end shape

The medians below come from
[`../../comparisons/aim2-aim3-e2e-20260901/full_comparison.csv`](../../comparisons/aim2-aim3-e2e-20260901/full_comparison.csv).
`Speedup` in this table is the ratio of the displayed aggregate medians.  The
paper report's paired-run estimator differs slightly.

| Parameter | Operation | Reference | AVX-512 | Speedup | Saved cycles |
|---|---|---:|---:|---:|---:|
| 128f | keypair | 534,684 | 198,615 | 2.692x | 336,069 |
| 128f | sign | 6,660,830 | 1,437,143 | 4.635x | 5,223,687 |
| 128f | verify | 6,202,890 | 1,350,273 | 4.594x | 4,852,617 |
| 128s | keypair | 561,131 | 209,800 | 2.675x | 351,331 |
| 128s | sign | 51,178,023 | 10,857,488 | 4.714x | 40,320,535 |
| 128s | verify | 51,549,767 | 10,862,508 | 4.746x | 40,687,259 |
| 192f | keypair | 1,381,351 | 430,714 | 3.207x | 950,637 |
| 192f | sign | 20,021,160 | 3,673,634 | 5.450x | 16,347,526 |
| 192f | verify | 18,540,649 | 3,560,857 | 5.207x | 14,979,792 |
| 192s | keypair | 1,430,276 | 446,328 | 3.205x | 983,948 |
| 192s | sign | 154,039,547 | 27,492,679 | 5.603x | 126,546,868 |
| 192s | verify | 154,062,878 | 27,461,637 | 5.610x | 126,601,241 |
| 256f | keypair | 3,739,739 | 1,169,926 | 3.197x | 2,569,813 |
| 256f | sign | 42,658,267 | 6,905,437 | 6.178x | 35,752,830 |
| 256f | verify | 39,108,566 | 6,633,555 | 5.896x | 32,475,011 |
| 256s | keypair | 3,826,910 | 1,204,815 | 3.176x | 2,622,095 |
| 256s | sign | 317,747,173 | 49,546,895 | 6.413x | 268,200,278 |
| 256s | verify | 318,911,018 | 48,687,468 | 6.550x | 270,223,550 |

Absolute keypair time is monotonic with field width: 128 is fastest, then 192,
then 256.  The notable 192-bit value is its relative speedup, not an absolute
time reversal.

## Why keypair is not evidence about 192-bit batch padding

Keypair calls `aim3_nozeroinpsb`, which uses the scalar `gf_inv`, `gf_sqr`,
`gf_mul`, and `gf_mat_vec_mul` routines.  It never calls `gf_sqr_N` or another
party-batch kernel.  The AVX-512-named scalar 192-bit implementation stores an
element as exactly three 64-bit words and loads the third word separately; it
does not extend every field element to four stored words.

The exact keypair work is:

| Width | Inversions | Multiplications | Squarings | Matrix-vector calls |
|---:|---:|---:|---:|---:|
| 128 | 3 | 384 | 402 | 516 |
| 192 | 3 | 576 | 654 | 772 |
| 256 | 4 | 1,024 | 1,060 | 1,542 |

The matrix counts include `2*L*field_bits` calls made while constructing the
linear layer.  The multiplication and squaring counts include the simple
inversion loop.  Therefore the 192-bit keypair ratio must be decomposed using
scalar PCLMUL/inversion/matrix costs; the four-party ZMM padding is irrelevant
to this operation.

## Where the 192-bit padding actually is

The 192-bit batch squaring and constant multiplication kernels transpose each
party into two 128-bit chunks.  The second chunk contains the third 64-bit
word and an explicit zero word.  Four parties are still processed together by
VPCLMULQDQ, but 64 of the 256 represented bits per party are padding.

This does not apply uniformly to the whole backend:

- scalar 192-bit field operations use three words;
- batch squaring and batch constant multiplication use two 128-bit chunks,
  with 25% representational padding;
- the batch matrix path uses masked three-word 256-bit loads and stores, so it
  neither pads the field to a stored fourth word nor over-reads it;
- 128-bit batch GF uses four complete 128-bit party lanes per ZMM;
- 256-bit batch GF uses two full 128-bit chunks for each of four parties.

Thus “192 has padding” is true only for particular batch kernels, not for
keypair or every MPC sub-operation.

## Static MPC and challenge work

Each complete MPC call contains `2L` batched affine/matrix operations and
`sum(exponents)` batched squarings.  Signing and verification invoke one
complete all-party MPC per repetition.  Challenge multiplication remains a
scalar `gf_mul_add` loop in the AVX-512 sign source; it benefits from scalar
PCLMUL but not from the ZMM party-batch routine.

| Parameter | T*N | Affine party-ops | Frobenius party-squares | Sign scalar mul-adds | Verify scalar mul-adds |
|---|---:|---:|---:|---:|---:|
| 128f | 528 | 2,112 | 11,088 | 4,851 | 4,455 |
| 128s | 4,352 | 17,408 | 91,392 | 39,219 | 39,015 |
| 192f | 784 | 3,136 | 63,504 | 7,203 | 6,615 |
| 192s | 6,400 | 25,600 | 518,400 | 57,675 | 57,375 |
| 256f | 1,040 | 6,240 | 41,600 | 12,740 | 11,700 |
| 256s | 8,448 | 50,688 | 337,920 | 101,508 | 100,980 |

The sign count includes the last-share adjustment.  The verify count excludes
the missing party.  The important 192-bit fact is the exponent set
`{11,23,47}`, whose sum is 81.  Consequently 192s executes 518,400 scalar-
equivalent Frobenius squares, more than 256s despite the smaller field.  A
four-party AVX-512 schedule can amortize its padding over this unusually long
chain.

The `s`/`f` end-to-end ratio also follows party work rather than field padding.
At every width, `T*N` is about 8.1 times larger for `s`; measured sign time is
about 7.2--7.7 times larger.  Fixed per-sign costs account for the remaining
difference.

## What the current medians suggest

The relative sign speedup rises from roughly 4.6--4.7x at 128 bits, through
5.45--5.60x at 192 bits, to 6.18--6.41x at 256 bits.  This is consistent with
three effects that grow or accumulate with field width: scalar PCLMUL replacing
the reference multiplier, all-party MPC batching, and x4 SHAKE for commitment
and tape expansion.  The checked-in E2E medians alone cannot assign exact cycle
shares to those effects.

In particular, it would be incorrect to label all of the sign/verify saving as
“MPC squaring.”  The optimized signing source uses:

1. scalar PCLMUL field operations outside MPC;
2. ZMM party batching inside MPC;
3. AVX-512 ternary-logic matrix accumulation;
4. SHAKE x1/x4 assembly; and
5. a different evaluation schedule over the same equations.

## Added diagnostic

`bench_avx512_causes` is a separate executable; none of the production AVX-512
sources or official paper-kernel lists are changed.  It measures reference and
AVX-512 versions of:

- scalar `gf_mul`, `gf_mul_add`, `gf_sqr`, `gf_inv`, and matrix-vector;
- complete linear-layer generation;
- MPC setup plus affine region;
- MPC Frobenius region; and
- the complete MPC call.

Each selected implementation is checked against reference before timing.
`analyze_avx512_causes.py` then builds a keypair count model and a sign/verify
model with an explicit residual for SHAKE/tree, allocation, cache context, and
model error.

Run it on the locked AVX-512 Linux host:

```sh
cd x86
CORE=2 VERIFY=1 ./benchmarks/run_avx512_causes.sh
```

The generated report is placed under the ignored
`benchmarks/results/avx512-causes-*/` directory.  The runner measures seven
independent runs by default, alternating backend order and
reversing parameter order on even runs.  The analyzer uses the median of the
per-run medians.  The diagnostic C sources were
front-end checked for x86-64 reference and AVX-512 builds on the current Mac,
but no component cycle values are claimed here because the current machine is
Apple Silicon and cannot execute AVX-512.
