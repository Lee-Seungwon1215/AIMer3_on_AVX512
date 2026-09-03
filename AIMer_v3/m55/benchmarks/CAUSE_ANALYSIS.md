# Why the M55 results have this shape

This note explains the post-MPC-squaring measurements in
[`RESULTS_MPC_SQUARING.md`](RESULTS_MPC_SQUARING.md).  It is a causal profile,
not a replacement for the seven-sample end-to-end benchmark.  Diagnostic
builds add counters and can perturb code layout; the official cycle values
remain in `results_mpc_squaring.csv`.

## Main conclusions

1. `192f` is not the fastest keypair.  Its MVE keypair takes 10,387,828
   cycles, between `128f` (3,977,230) and `256f` (35,428,625).  What is largest
   at 192 bits is the reference/MVE speedup, 1.415x.
2. The 192-bit zero padding is real for general `gf_mul`: twelve 16-bit limbs
   occupy two eight-limb vector blocks, leaving four of sixteen block slots
   unused.  It does not apply to the four-party Frobenius path.  That path
   maps one party to each 32-bit MVE lane and uses six Q registers for a
   192-bit field value.
3. The keypair ratios are almost completely predicted by inversion savings
   minus the cost of the MVE backend's generic matrix-vector routine.  The
   model accounts for 98.9% to 100.1% of the observed cycle differences.
4. The 192-bit MPC uses Frobenius exponents `{11, 23, 47}`, whose sum is 81.
   This is much longer than 128-bit `{3, 7, 11}` (sum 21) and 256-bit
   `{3, 7, 11, 19}` (sum 40).  Keeping four parties packed through each whole
   chain therefore helps 192-bit MPC disproportionately.
5. The large 256-bit end-to-end gain is not pure squaring speedup.  In the
   full benchmark layout, about 71.7M of the 90.4M-cycle MPC improvement comes
   from the affine/matrix portion and about 18.6M from Frobenius.  The MVE
   field backend uses a compact generic `gf_mat_vec_mul`; the reference
   256-bit four-bit-unrolled routine is faster in a hot microbenchmark but
   develops a 156-byte stack frame with heavy spilling and performs poorly
   while repeatedly scanning the 49,280-byte linear layer.

## Parameters and where padding occurs

| Width | L | Frobenius exponents | Sum | 16-bit limbs | Padded limbs | Linear data |
|---:|---:|---|---:|---:|---:|---:|
| 128 | 2 | 3, 7, 11 | 21 | 8 | 8 | 8,240 B |
| 192 | 2 | 11, 23, 47 | 81 | 12 | 16 | 18,504 B |
| 256 | 3 | 3, 7, 11, 19 | 40 | 16 | 16 | 49,280 B |

The padding column describes only the general MVE multiplier in
`src/mve/field_mve.c`.  The batched Frobenius implementation in
`src/mve/field_batch.c` is a different layout: four independent parties are
the vector lanes, and field words run across Q registers.  It has no
192-to-256-bit zero extension.

The `f` sets use `N=16` and `T={33,49,65}`.  The `s` sets use `N=256` and
`T={17,25,33}`.  Consequently, the party work `N*T` in an `s` set is about
8.1--8.2 times its same-width `f` set even though `T` is smaller.

## Keypair: an almost exact cost model

The keypair executes `L+1` inversions.  Including the surrounding AIM3 work,
the counted field operations and matrix-vector calls are:

| Width | Inversions | Multiplications | Squarings | Matrix-vector calls |
|---:|---:|---:|---:|---:|
| 128 | 3 | 384 | 402 | 516 |
| 192 | 3 | 576 | 654 | 772 |
| 256 | 4 | 1,024 | 1,060 | 1,542 |

The following uses mean kernel cycles from the diagnostic profile and median
end-to-end cycles from the official benchmark.  `Predicted saving` is

```
(gf_inv_ref - gf_inv_mve) * inversion_count
- (matvec_mve - matvec_ref) * matvec_count.
```

| Width | Keypair ref | MVE | Ref/MVE | Inversion saving | Matvec penalty | Predicted saving | Observed saving |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 128 | 5,112,496 | 3,977,230 | 1.285x | 1,501,619 | 378,563 | 1,123,055 | 1,135,266 |
| 192 | 14,698,509 | 10,387,828 | 1.415x | 4,584,575 | 270,069 | 4,314,506 | 4,310,681 |
| 256 | 44,619,048 | 35,428,625 | 1.259x | 12,172,367 | 2,975,983 | 9,196,384 | 9,190,423 |

The model residuals are only +12,211, -3,825, and -5,961 cycles.  Thus the
192-bit ratio is not a measurement anomaly.  It has the best inversion
speedup (5.39x) and the smallest relative matvec penalty (5.0%).  For
comparison, the 128- and 256-bit matvec penalties are 22.5% and 14.5%.

General `gf_mul` reference/MVE speedups are 6.35x, 6.64x, and 6.21x at 128,
192, and 256 bits.  The 192-bit padding costs work, but it is not large enough
to erase the MVE multiplier's advantage over that width's reference
multiplier.  The inversion combines these multiplications with squarings,
giving measured inversion speedups of 4.99x, 5.39x, and 5.17x.

## Verify: where the end-to-end cycles move

The instrumented verifier separates linear generation, batched MPC, the two
`epsilon*x`/`epsilon*z` products, and the `alpha*b` product.  The table uses a
single representative phase run for 128f and 192f, and the median full
benchmark-layout phase run for 256f.  It compares the modeled saving with the
official end-to-end median saving.

| Width | Linear impact | MPC saving | epsilon products | alpha*b | Modeled net | Official net |
|---:|---:|---:|---:|---:|---:|---:|
| 128 | -374,949 | 331,019 | 7,820,454 | 3,904,832 | 11,681,356 | 11,536,251 |
| 192 | -245,381 | 16,451,111 | 21,269,812 | 10,628,694 | 48,104,236 | 49,702,225 |
| 256 | -2,925,507 | 90,382,832 | 41,044,958 | 20,466,831 | 148,969,114 | 147,939,321 |

The four categories explain 96.8% to 101.3% of the official differences.
The small over/under-shoot is consistent with a diagnostic build's added
counters and altered layout.

At 128 bits, almost all verification improvement is challenge multiplication;
the slower affine layer nearly cancels the packed Frobenius gain inside MPC.
At 192 bits, the exponent-sum-81 Frobenius chain makes MPC a material source
of saving as well.  At 256 bits, both the contextual affine behavior and
challenge products are major contributors.

The number of four-party challenge multiplication calls in both signing and
verification is 1,188, 1,764, and 3,120 for the `f` sets.  The MVE batch
constant-multiply call is 2.09x, 1.92x, and 1.52x faster at the three widths,
so even the smallest per-call ratio accumulates into a large end-to-end
reduction.

## MPC internals

These are total verifier cycles over all batched MPC calls.  The 128f and
192f rows are representative phase runs.  The 256f row is the median of seven
measurements in `phasebench`, which preserves the full benchmark program and
its code footprint.

| Width | Affine ref | Affine MVE | Affine impact | Frobenius ref | Frobenius MVE | Frobenius saving |
|---:|---:|---:|---:|---:|---:|---:|
| 128 | 7,129,016 | 8,622,259 | -1,493,243 | 2,524,277 | 704,421 | 1,819,856 |
| 192 | 30,655,314 | 32,891,297 | -2,235,983 | 23,473,703 | 4,762,074 | 18,711,629 |
| 256 | 199,545,360 | 127,808,191 | 71,737,169 | 24,020,711 | 5,375,862 | 18,644,849 |

This also explains why a tight-loop MPC microbenchmark underestimates the
256-bit end-to-end gain.  Repeating one MPC call over the same `linear` and
`tapes` objects reports only 502,878 versus 470,011 cycles per four parties.
In the full verifier, changing parties and tapes while traversing the large
linear layer makes the reference affine path much more expensive.  The
full-layout result, rather than the hot-cache loop, is the relevant number for
end-to-end behavior.

PMU experiments did not support a simple instruction-cache explanation.
Reference MPC produced only tens of L1 I-cache refills per verification.
Both backends produced roughly 1.3 million L1 D-cache refills in the 256-bit
MPC region.  The decisive observation is the direct affine/Frobenius timing:
the implementations respond differently to the same memory pressure, rather
than one backend merely avoiding all cache traffic.

## Scope and interpretation

The current MVE-versus-reference comparison is valid as a comparison of the
two complete backends, and all checksums and KATs agree.  It is not a pure
ablation of only `VMULL*.P16` squaring/reduction:

- `field_mve.c` also supplies a compact generic `gf_mat_vec_mul`.
- That routine is slower in isolated measurements at every width.
- In the 256-bit full MPC workload it is substantially faster than the
  reference four-bit-unrolled routine because the latter spills heavily and
  interacts poorly with the 49KB linear layer.

Therefore a paper that claims an isolated squaring/reduction contribution
should report a matvec-controlled ablation separately.  The current
end-to-end numbers can be reported as full-backend results, with the 256-bit
affine contribution disclosed.

## Reproduction

The diagnostic targets do not change normal `sign`, `kat`, or `benchmark`
builds.

```sh
# Kernel costs, real exponents, and static call counts
make -B PARAM=192f BACKEND=mve TEST=profile MEMORY=full run-board

# One sign/verify run with phase and PMU diagnostics
make -B PARAM=192f BACKEND=mve TEST=phase MEMORY=full run-board

# Phase instrumentation in the complete benchmark program/layout
make -B PARAM=256f BACKEND=mve TEST=phasebench MEMORY=full run-board
```

After adding the diagnostics, all six low-memory differential tests and all
600 host MVE KAT vectors passed again.  The diagnostic board runs also passed
sign, verify, tamper rejection, and reference/MVE profile checksums.
