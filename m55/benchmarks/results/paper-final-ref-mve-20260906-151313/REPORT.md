# Cortex-M55 paper-final Reference vs MVE measurement

## 범위

- Git 커밋: `d41b4a21cbf57e9db929a2b3fd80095ce9cd77bf`
- 보드: STM32N657 NUCLEO-N657X0-Q, Cortex-M55, single-core bare-metal, 600 MHz
- 툴체인: `arm-none-eabi-gcc (xPack GNU Arm Embedded GCC arm64) 15.2.1 20251203`
- Reference: `BACKEND=ref MATVEC=reference MEMORY=full SIGN_SCHEDULE=lowmem`
- MVE: `BACKEND=mve MATVEC=mve MEMORY=full SIGN_SCHEDULE=lowmem`
- 공통 옵션: `-O3 -fno-tree-vectorize -fno-tree-slp-vectorize`
- 두 구성 모두 동일한 AXI SRAM layout, stack/heap, portable Keccak/SHAKE를 사용했다.
- A/REF와 D/MVE만 재측정했으며 B/C ablation과 성능 튜닝은 수행하지 않았다.

## Correctness와 측정 gate

- 현재 소스 host GF differential 6/6, affine differential 6/6, MVE KAT 600/600
- 현재 소스 Reference host low-memory 및 KAT 600/600
- 실제 보드 Reference/MVE keypair/sign/verify/tamper 12/12
- 동일 소스에 연결된 restructure validation의 실제 보드 MVE KAT 600/600 (`KAT_FAIL` 0)
- `__ARM_FEATURE_MVE=3`, VMULLB/VMULLT, affine VAND/VEOR와 MPC batch 호출 6/6
- PMU/SysTick 교차검증: 84/84, 최대 오차 0.744%
- source manifest: PASS source manifest unchanged

## 통계 조건

- 7 independent runs, run당 50 measured samples, warm-up 10
- 홀수 run은 Reference→MVE, 짝수 run은 MVE→Reference 순서로 실행
- run별 50개 표본의 중앙값을 만들고, 같은 run의 Reference/MVE 중앙값 비율을 paired speedup으로 계산
- 7개 paired speedup의 중앙값과 10,000회 paired bootstrap percentile 95% CI 보고
- outlier 및 실행 결과 제거 없음
- run-median 최대 CV: 0.197%

## 결과

절대 cycle 수는 Cortex-M55 내부 비교만을 위해 제시한다.

| Parameter | Operation | Reference cycles | MVE cycles | Speedup | Bootstrap 95% CI |
|---|---|---:|---:|---:|---:|
| 128f | keypair | 5,110,916 | 3,398,254 | 1.504× | [1.504, 1.504] |
| 128f | sign | 109,600,709 | 84,541,917 | 1.297× | [1.296, 1.297] |
| 128f | verify | 62,704,676 | 45,421,108 | 1.381× | [1.380, 1.381] |
| 128s | keypair | 5,113,873 | 3,401,139 | 1.504× | [1.504, 1.504] |
| 128s | sign | 865,808,798 | 675,337,890 | 1.282× | [1.282, 1.282] |
| 128s | verify | 511,841,446 | 369,991,403 | 1.383× | [1.383, 1.383] |
| 192f | keypair | 14,733,692 | 9,393,928 | 1.568× | [1.568, 1.568] |
| 192f | sign | 332,600,309 | 218,565,970 | 1.522× | [1.520, 1.523] |
| 192f | verify | 195,721,780 | 125,417,246 | 1.561× | [1.560, 1.562] |
| 192s | keypair | 14,740,844 | 9,404,596 | 1.567× | [1.567, 1.567] |
| 192s | sign | 2,483,245,224 | 1,674,098,664 | 1.484× | [1.483, 1.484] |
| 192s | verify | 1,497,802,656 | 986,512,622 | 1.518× | [1.518, 1.519] |
| 256f | keypair | 44,617,426 | 24,076,474 | 1.853× | [1.853, 1.853] |
| 256f | sign | 919,163,554 | 510,380,166 | 1.801× | [1.798, 1.802] |
| 256f | verify | 550,742,866 | 318,722,738 | 1.728× | [1.728, 1.731] |
| 256s | keypair | 44,618,326 | 24,065,424 | 1.854× | [1.854, 1.854] |
| 256s | sign | 7,295,376,415 | 3,819,631,167 | 1.911× | [1.906, 1.913] |
| 256s | verify | 4,535,277,234 | 2,424,624,431 | 1.871× | [1.870, 1.874] |

## 해석 제한

이 결과는 Cortex-M55의 플랫폼-local Reference 대비 MVE 가속률이다. x86의 절대 cycle과 직접 비교하지 않는다. Cortex-M55에서는 Keccak/SHAKE를 portable 구현으로 유지했으므로, 공통 최적화 범위는 GF 연산, affine 연산과 party 병렬화이다.

측정 중 소스 수정, outlier 제거, 결과 선택 또는 추가 성능 튜닝을 하지 않았다.
