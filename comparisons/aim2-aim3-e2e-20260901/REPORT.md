# AIMer v2–v3 end-to-end comparison

## Measurement validity

| Item | AIMer v2 | AIMer v3 |
|---|---:|---:|
| CPU / core | i7-1165G7 / CPU 2 | i7-1165G7 / CPU 2 |
| Governor / EPP | performance / performance | performance / performance |
| Frequency / turbo | 2.8 GHz fixed / off | 2.8 GHz fixed / off |
| Independent runs × samples | 7 × 50 | 7 × 50 |
| Warmup | 10 | 10 |
| Official KAT | 18/18 combinations passed | full OQS validation passed |
| Maximum run-median CV | 1.36% | 3.65% |
| Session loadavg start → end | 7.22 → 8.99 | 0.89 → 1.74 |

> 각 버전 내부 speedup과 95% CI는 같은 run 번호의 paired 비교입니다. AIM2와 AIM3는 서로 다른 날짜에 측정했으므로 세대 간 `AIM3/AIM2`는 비대응 median 비율이며 CI를 부여하지 않습니다.

## Absolute median cycles

`AIM3/AIM2 < 1`이면 AIM3가 더 빠르고, `> 1`이면 AIM3가 더 많은 cycle을 사용합니다.

| Param | Op | AIM2 Ref | AIM3 Ref | v3/v2 | AIM2 AVX2 | AIM3 AVX2 | v3/v2 | AIM2 AVX-512 | AIM3 AVX-512 | v3/v2 |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 128f | keypair | 230.1k | 534.7k | 2.32× | 133.0k | 216.9k | 1.63× | 126.1k | 198.6k | 1.57× |
| 128f | sign | 8.887M | 6.661M | 0.75× | 1.334M | 2.070M | 1.55× | 780.6k | 1.437M | 1.84× |
| 128f | verify | 8.266M | 6.203M | 0.75× | 1.321M | 1.973M | 1.49× | 793.3k | 1.350M | 1.70× |
| 128s | keypair | 240.3k | 561.1k | 2.34× | 136.4k | 234.7k | 1.72× | 129.0k | 209.8k | 1.63× |
| 128s | sign | 72.587M | 51.178M | 0.71× | 9.988M | 15.950M | 1.60× | 5.414M | 10.857M | 2.01× |
| 128s | verify | 71.551M | 51.550M | 0.72× | 9.824M | 15.854M | 1.61× | 5.307M | 10.863M | 2.05× |
| 192f | keypair | 498.3k | 1.381M | 2.77× | 201.1k | 627.8k | 3.12× | 178.7k | 430.7k | 2.41× |
| 192f | sign | 17.458M | 20.021M | 1.15× | 3.386M | 5.308M | 1.57× | 2.115M | 3.674M | 1.74× |
| 192f | verify | 16.082M | 18.541M | 1.15× | 3.393M | 5.174M | 1.52× | 2.142M | 3.561M | 1.66× |
| 192s | keypair | 514.0k | 1.430M | 2.78× | 210.4k | 648.3k | 3.08× | 185.8k | 446.3k | 2.40× |
| 192s | sign | 136.142M | 154.040M | 1.13× | 25.170M | 39.528M | 1.57× | 15.253M | 27.493M | 1.80× |
| 192s | verify | 135.731M | 154.063M | 1.14× | 24.927M | 39.322M | 1.58× | 14.949M | 27.462M | 1.84× |
| 256f | keypair | 1.102M | 3.740M | 3.39× | 375.2k | 1.712M | 4.56× | 308.1k | 1.170M | 3.80× |
| 256f | sign | 44.356M | 42.658M | 0.96× | 6.670M | 10.392M | 1.56× | 3.868M | 6.905M | 1.79× |
| 256f | verify | 40.842M | 39.109M | 0.96× | 6.657M | 10.113M | 1.52× | 3.882M | 6.634M | 1.71× |
| 256s | keypair | 1.125M | 3.827M | 3.40× | 389.1k | 1.756M | 4.51× | 317.4k | 1.205M | 3.80× |
| 256s | sign | 346.224M | 317.747M | 0.92× | 47.741M | 73.607M | 1.54× | 26.124M | 49.547M | 1.90× |
| 256s | verify | 343.611M | 318.911M | 0.93× | 47.533M | 72.277M | 1.52× | 25.906M | 48.687M | 1.88× |

## Reference-relative SIMD speedup

| Param | Op | AIM2 AVX2 | AIM3 AVX2 | AIM2 AVX-512 | AIM3 AVX-512 |
|---|---|---:|---:|---:|---:|
| 128f | keypair | 1.73× | 2.45× | 1.82× | 2.68× |
| 128f | sign | 6.66× | 3.22× | 11.39× | 4.63× |
| 128f | verify | 6.26× | 3.14× | 10.41× | 4.59× |
| 128s | keypair | 1.77× | 2.42× | 1.87× | 2.67× |
| 128s | sign | 7.27× | 3.21× | 13.41× | 4.72× |
| 128s | verify | 7.29× | 3.25× | 13.48× | 4.75× |
| 192f | keypair | 2.48× | 2.21× | 2.79× | 3.21× |
| 192f | sign | 5.16× | 3.77× | 8.25× | 5.45× |
| 192f | verify | 4.74× | 3.58× | 7.51× | 5.21× |
| 192s | keypair | 2.44× | 2.20× | 2.77× | 3.15× |
| 192s | sign | 5.41× | 3.88× | 8.92× | 5.61× |
| 192s | verify | 5.44× | 3.90× | 9.08× | 5.59× |
| 256f | keypair | 2.94× | 2.19× | 3.58× | 3.20× |
| 256f | sign | 6.65× | 4.10× | 11.46× | 6.18× |
| 256f | verify | 6.13× | 3.87× | 10.52× | 5.90× |
| 256s | keypair | 2.89× | 2.18× | 3.55× | 3.17× |
| 256s | sign | 7.26× | 4.32× | 13.25× | 6.41× |
| 256s | verify | 7.23× | 4.41× | 13.27× | 6.55× |

## Geometric-mean trend

| Scope | Backend | AIM2/AIM3 median-cycle ratio | Interpretation |
|---|---:|---:|---|
| keypair | ref | 0.357× | AIM2 uses fewer cycles |
| keypair | avx2 | 0.349× | AIM2 uses fewer cycles |
| keypair | avx512 | 0.409× | AIM2 uses fewer cycles |
| sign | ref | 1.087× | AIM3 uses fewer cycles |
| sign | avx2 | 0.639× | AIM2 uses fewer cycles |
| sign | avx512 | 0.543× | AIM2 uses fewer cycles |
| verify | ref | 1.080× | AIM3 uses fewer cycles |
| verify | avx2 | 0.649× | AIM2 uses fewer cycles |
| verify | avx512 | 0.555× | AIM2 uses fewer cycles |

For sign+verify, the geometric-mean reference-relative speedups are:

- AVX2: AIM2 `6.228×`, AIM3 `3.697×`.
- AVX512: AIM2 `10.711×`, AIM3 `5.424×`.

## 192-bit observation

- AIM2의 192-bit sign SIMD speedup은 AVX2 `5.16×/5.41×`(f/s), AVX-512 `8.25×/8.92×`로 128-bit와 256-bit보다 일관되게 낮습니다.
- AIM3의 192-bit sign speedup은 AVX2 `3.77×/3.88×`, AVX-512 `5.45×/5.61×`로 128-bit와 256-bit 사이의 자연스러운 추세입니다.
- 따라서 AIM2에서 보이던 192-bit 상대 성능 저하는 AIM3 E2E 결과에서는 두드러지지 않습니다. 이것만으로 zero-padding 자체가 제거됐다고 결론 내릴 수는 없으며, AIM3의 변경된 연산 구성과 hotspot 비중이 그 비용을 가린 것으로 해석해야 합니다.

## Scope warning

- AIM2와 AIM3는 서로 다른 서명 알고리즘 버전이므로 절대 cycle 차이는 “포팅 품질”만의 효과가 아닙니다.
- AIM2 측정은 CV가 낮지만 AIM3 측정보다 시스템 load average가 높았습니다. 최종 논문의 강한 세대 간 성능 주장에는 두 바이너리를 같은 세션에서 번갈아 실행하는 joint interleaved 측정이 더 적합합니다.
- 기존 측정값은 삭제하거나 사후 튜닝하지 않았습니다.
