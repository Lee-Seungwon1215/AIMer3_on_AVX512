# AIMer-AVX512

AVX-512 optimized implementations of all six AIMer post-quantum signature
variants, together with portable reference and AVX2 baselines.

This repository is the software artifact for the WISA 2026 paper
"Accelerating the AIMer Post-Quantum Signature with AVX-512: A
Field--Keccak Speedup Analysis."

The artifact allows a third party to:

1. build all three implementation backends from source;
2. validate every implementation/parameter-set combination with the supplied
   known-answer tests (KATs);
3. reproduce the Reference/AVX2/AVX-512 performance comparison; and
4. measure the field-only and Keccak contributions separately.

## Implementations

Each AIMer variant is provided in three forms:

| `AIMER_IMPL` | Binary-field arithmetic | SHA-3/SHAKE in a matched run |
|---|---|---|
| `ref` | Portable scalar C | Scalar Keccak |
| `avx2` | AVX2/PCLMULQDQ | AVX2 XKCP |
| `avx512` | VPCLMULQDQ and VPTERNLOGQ | Intel AVX-512VL Keccak |

The supported variants are `AIMER-128f`, `AIMER-128s`, `AIMER-192f`,
`AIMER-192s`, `AIMER-256f`, and `AIMER-256s`.

The AVX-512 implementation processes four MPC parties in parallel with
VPCLMULQDQ, uses VPTERNLOGQ in the linear layer, and uses the AVX-512VL
Keccak backend. When no implementation is forced, OQS runtime dispatch
selects a backend supported by the processor.

## Tested environment

The reported measurements were collected in this environment:

| Component | Version or setting |
|---|---|
| CPU | Intel Core i7-1165G7 (Tiger Lake), 4 cores/8 threads, 2.80 GHz base |
| OS | Ubuntu 24.04.4 LTS, x86-64 |
| Compiler / Binutils | GCC 13.3.0 / GNU Binutils 2.42 |
| Make / Python | GNU Make 4.3 / Python 3.12.3 |
| Core and power | Logical CPU 2, `performance` governor, Turbo Boost disabled |
| Sampling | 50 measurements after 5 discarded warm-up iterations |

Other recent Linux and GCC/Clang versions may work, but exact cycle counts
can vary across machines and toolchains.

## Requirements

- Linux on x86-64
- GCC or Clang with GNU assembler support
- GNU Make and GNU Binutils
- Python 3 and `taskset` from `util-linux`
- AVX2, PCLMULQDQ, AES-NI, BMI2, and POPCNT for the AVX2 backend
- AVX-512F/VL/BW/DQ and VPCLMULQDQ, in addition to the AVX2 requirements,
  for the AVX-512 backend

No third-party Python package is required. On Ubuntu or Debian:

```bash
sudo apt-get update
sudo apt-get install build-essential python3 util-linux
```

Check processor features before forcing an optimized backend:

```bash
grep -m1 '^flags' /proc/cpuinfo | tr ' ' '\n' | \
  grep -E '^(aes|avx2|bmi2|pclmulqdq|popcnt|avx512f|avx512vl|avx512bw|avx512dq|vpclmulqdq)$'
```

The AVX-512 experiment requires all ten flags. Forcing an unsupported
backend can cause an illegal-instruction error.

## Build

Run these commands from the repository root:

```bash
make distclean
make CC=gcc AR=ar -j"$(nproc)"
```

The build creates:

- `build/lib/liboqs.a`
- `build/lib/liboqs-internal.a`
- `build/tests/kat_sig`
- `build/tests/bench_sig`
- `build/tests/bench_full`

The Makefile defaults to `/usr/bin/cc` and `/usr/bin/ar`. Common code uses
`-O3`. AVX2 kernels additionally use `-mavx2 -mpclmul -mbmi2 -mpopcnt
-maes`. AVX-512 kernels additionally use `-mavx512f -mavx512vl -mavx512bw
-mavx512dq -mvpclmulqdq`. See [`Makefile`](Makefile) for the exact rules.

Use `make clean` to remove object and dependency files, or `make distclean`
to remove the complete `build/` and `out/` directories.

## Selecting an implementation

The KAT and benchmark harnesses use two environment variables:

- `AIMER_IMPL={ref,avx2,avx512}` selects the AIMer field/signing code.
- `AIMER_KECCAK={ref,avx2,avx512}` selects the SHA-3/SHAKE backend.

`AIMER_KECCAK` is a measurement-only override. If omitted, it follows
`AIMER_IMPL`.

| Configuration | `AIMER_IMPL` | `AIMER_KECCAK` |
|---|---|---|
| Portable reference | `ref` | `ref` |
| AVX2 baseline | `avx2` | `avx2` |
| Field-only baseline: AVX2 field with AVX-512 Keccak | `avx2` | `avx512` |
| Complete AVX-512 | `avx512` | `avx512` |

Set both variables when reproducing a named configuration so that a field
implementation is not accidentally paired with another Keccak backend.

## Correctness reproduction

Run one 100-vector KAT set:

```bash
AIMER_IMPL=avx512 AIMER_KECCAK=avx512 \
  ./build/tests/kat_sig AIMER-128f
```

Run all variants with all three backends:

```bash
./tests/run_kat_all.sh
```

This executes 18 implementation/variant combinations. Each checks 100
vectors from `tests/KAT/`, for 1,800 checks in total. Success means that all
combinations complete and the script exits with status zero. Any mismatch
stops the script with a nonzero status.

This repository version was validated with all 1,800 checks passing.

## Performance reproduction

### 1. Record and stabilize the environment

Record the repository revision and machine configuration with every run:

```bash
git rev-parse HEAD
uname -a
lscpu
cc --version
cat /sys/devices/system/cpu/cpu2/cpufreq/scaling_governor
cat /sys/devices/system/cpu/intel_pstate/no_turbo
```

The final path is specific to Intel P-state and may not exist elsewhere.
For conditions matching the paper:

- use one otherwise-idle logical core;
- set the CPU-frequency governor to `performance`;
- disable Turbo Boost;
- stop competing workloads; and
- use the same core and sample count for all implementations.

Changing system power settings normally requires administrator access. Use
the mechanism provided by the host operating system, then verify the
effective state before measuring.

### 2. Run the matched comparison

Collect 50 samples for Reference, AVX2, and AVX-512:

```bash
CORE=2 ./bench/run_all.sh 50
```

`CORE` is the logical CPU used by `taskset`. Select an online CPU. The script
prints the core, detected governor, and sample count and warns if the
governor is not `performance`.

It writes these headerless files:

- `bench/results/ref.csv`
- `bench/results/avx2.csv`
- `bench/results/avx512.csv`

Each file has 18 rows: six variants times key generation, signing, and
verification. The exact schema is:

```text
build,variant,op,N,min,median,max,mean,std,cv,med_us,mean_us,ops
```

Print the comparison again without rerunning the measurements:

```bash
python3 bench/compare.py \
  bench/results/ref.csv \
  bench/results/avx2.csv \
  bench/results/avx512.csv
```

Speedup is `baseline median / candidate median`, so a value above one means
that the candidate is faster. A small sample count is suitable only for a
smoke test, not for comparison with the paper.

### 3. Reproduce the field/Keccak decomposition

First run `bench/run_all.sh` to create the matched AVX2 and AVX-512 files.
Then fix Keccak to AVX-512VL and run the AVX2 field implementation. Comparing
this mixed run with complete AVX-512 changes only the field implementation:

```bash
: > bench/results/avx2-keccak512.csv
for variant in AIMER-128f AIMER-128s AIMER-192f AIMER-192s AIMER-256f AIMER-256s; do
  AIMER_IMPL=avx2 AIMER_KECCAK=avx512 \
    taskset -c 2 ./build/tests/bench_full avx2-keccak512 "$variant" 50 \
    >> bench/results/avx2-keccak512.csv
done

python3 bench/compare.py \
  bench/results/avx2.csv \
  bench/results/avx2-keccak512.csv \
  bench/results/avx512.csv
```

For median signing cycles `T_avx2`, `T_mixed`, and `T_full`, first
calculate the two speedups:

```text
S_overall = T_avx2 / T_full
S_field   = T_mixed / T_full
```

The contribution shares reported in the paper are based on the increase over
a speedup of one:

```text
field share  = (S_field - 1) / (S_overall - 1)
Keccak share = (S_overall - S_field) / (S_overall - 1)
```

For example, `S_overall = 1.74` and `S_field = 1.11` give approximately
14.9% for field arithmetic and 85.1% for Keccak. These shares describe only
the incremental speedup from the selected AVX2 baseline to complete AVX-512.
They are not an absolute execution-time breakdown relative to the scalar
reference implementation.

### 4. Interpret the measurements

For `N=50`, `bench_full` discards five warm-up iterations and measures key
generation, signing, and verification independently. It reports the median,
range, mean, standard deviation, coefficient of variation (CV), estimated
time, and throughput.

Cycles come from the invariant TSC and are reference-clock cycles, not
hardware performance-counter core cycles. Use median cycles and speedup
ratios collected under identical conditions. Absolute values can change
with the CPU, compiler, kernel, temperature, and background activity.

The camera-ready Table 3 values and the complete portable-reference raw
output are in [`results/`](results/README.md). The expected AVX2-to-AVX-512
signing speedup in the reported environment is `1.60`--`1.84x`.
Correctness must match exactly; timing should match in trend rather than
bit-for-bit.

## Repository layout

- `src/sig/aimer/aimer-<variant>_ref/`: portable reference kernels
- `src/sig/aimer/aimer-<variant>_avx2/`: AVX2/PCLMULQDQ kernels
- `src/sig/aimer/aimer-<variant>_avx512/`: AVX-512 kernels
- `src/sig/aimer/sig_aimer_<variant>.c`: OQS API glue and runtime dispatch
- `src/common/sha3/`: scalar, AVX2, and AVX-512VL SHA-3/SHAKE backends
- `include/oqs/`: OQS-compatible public headers
- `tests/aimer_keccak_select.c`: measurement-only Keccak selector
- `tests/kat_sig.c` and `tests/KAT/`: KAT runner and expected vectors
- `bench/bench_full.c`: benchmark and CSV producer
- `bench/run_all.sh`: matched three-backend benchmark driver
- `bench/compare.py`: CSV comparison and speedup reporting
- `results/`: measurements distributed with the paper artifact

For example, `aimer-128f_ref`, `aimer-128f_avx2`, and
`aimer-128f_avx512` are the three implementations of `AIMER-128f`. The same
pattern is used for all parameter sets, allowing direct comparison of the
`field`, `aim2`, `hash`, `sign`, and `tree` components.

## Troubleshooting

- **Missing a binary under `build/tests/`**: run `make -j"$(nproc)"` first.
- **Affinity error from `taskset`**: select an online CPU with `CORE=<number>`.
- **Illegal instruction**: verify CPU flags or select the `ref` backend.
- **Governor warning**: the run is valid for testing, but not directly
  comparable to the controlled paper measurements.
- **Large timing variation**: stop background work, increase the sample count,
  keep the same core, and inspect the CV column.

For a reproduction problem, include the failed command, complete terminal
output, environment information, and generated CSV files.

## Upstream sources

- AIMer reference implementation:
  <https://github.com/samsungsds-opensource/AIMer>, base commit `e47c497`
- liboqs SHA-3/SHAKE infrastructure and optimized Keccak backends: liboqs
  0.15.0

Vendored files retain their original SPDX identifiers and copyright notices.

## License

The AIMer implementation is distributed under the MIT License; see
[`LICENSE`](LICENSE). Vendored components remain subject to the license
notices in their source files.

This code is a research artifact and has not been independently audited for
production use.
