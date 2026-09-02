# AIMer v3 AVX2/AVX-512 및 liboqs 이식 계획

작성·갱신일: 2026-08-29  
작업 프로젝트: `AIMer_v3`  
비교 기준 artifact: `AIMer_v2`  
현재 현황: [`PORTING_STATUS_AVX512.md`](PORTING_STATUS_AVX512.md)

## 1. 목표

AIMer v3의 여섯 파라미터를 AIMer_v2와 독립된 최소 OQS-compatible
라이브러리로 통합하고, 같은 최적화 범위에 대해 reference, AVX2, AVX-512를
비교한다.

```text
AIM3 reference / AVX2 / AVX-512
                 ↓
        OQS_SIG runtime dispatch
                 ↓
              liboqs.a
                 ↓
       KAT / differential test / benchmark
```

공식 upstream liboqs 저장소에 패치로 넣는 작업은 이 독립 artifact가 안정된
뒤의 별도 단계다. 현재 빌드에는 AIMer_v2 경로나 빌드 설정을 사용하지 않는다.

## 2. 디렉터리와 namespace

```text
src/sig/aimer/
├── reference/
│   └── aimer-{128f,128s,192f,192s,256f,256s}
├── avx2/
│   └── aimer-{128f,128s,192f,192s,256f,256s}
└── avx512/
    └── aimer-{128f,128s,192f,192s,256f,256s}
```

각 구현은 `*_ref_*`, `*_avx2_*`, `*_opt_*` 내부 namespace를 사용한다.
AVX-512 폴더의 `_opt_` 이름은 기존 ABI를 보존하기 위한 내부 이름이며,
사용자에게 노출되는 backend 이름은 `avx512`다.

## 3. 구현 범위

| 구성요소 | AVX2 | AVX-512 |
|---|---|---|
| SHAKE x1 | AVX2 Keccak assembly | AVX-512VL assembly |
| SHAKE x4 | SIMD256 four-way Keccak | AVX-512VL four-way assembly |
| scalar GF | XMM PCLMUL | XMM PCLMUL |
| batch GF | XMM/YMM party packing | YMM/ZMM + VPCLMUL |
| matrix | YMM mask/AND/XOR | VPTERNLOGQ accumulation |
| MPC/sign | AIM3 party batching, commitment/tape x4 | 같은 범위의 wider batching |
| tree | 순차 traversal + x1 SHAKE | 순차 traversal + x1 SHAKE |

AVX2를 AVX-512 코드를 단순 축소해 복제하지 않고, 128/256-bit register 폭과
AVX2에서 가능한 PCLMUL·lane 조작에 맞게 별도 스케줄링한다. 비교의 공정성을
위해 알고리즘 수준의 최적화 경계는 두 SIMD backend에서 같게 둔다.

## 4. 절대 보존할 수학적 불변조건

- 128-bit set은 SHAKE128, 192/256-bit set은 SHAKE256을 사용한다.
- SHAKE rate, padding, domain separator, absorb/finalize/squeeze 호출 순서와
  x4 lane 순서를 바꾸지 않는다.
- field element는 little-endian 64-bit word 배열을 유지한다.
- GF(2^128), GF(2^192)는 `x^n + x^7 + x^2 + x + 1` (`0x87`),
  GF(2^256)는 `x^256 + x^10 + x^5 + x^2 + 1` (`0x425`)로 환원한다.
- AIMer v3 affine matrix, 일반 역원 `a^(2^n-2)`, MPC 식, 직렬화,
  random-byte 소비 순서와 zero-input 처리를 보존한다.
- AIM2 전용 상수, inverse-Mersenne 지수 chain과 MPC 식을 복사하지 않는다.
- SIMD lane에는 서로 독립인 party만 배치하며 결과 party 순서를 보존한다.

## 5. 단계와 게이트

### 단계 0 — 기준점

- [x] 공식 AIMer v3 소스와 KAT 보존
- [x] 여섯 reference 구현의 KAT 기준점 확인
- [x] AIMer_v2와 독립된 프로젝트 구조 확인

### 단계 1 — OQS 통합과 구조 분리

- [x] 여섯 `OQS_SIG` 알고리즘과 metadata 등록
- [x] `reference/avx2/avx512` 폴더 분리
- [x] 세 backend namespace와 runtime dispatch 연결
- [x] 자동 선택 순서 AVX-512 > AVX2 > reference 구현

### 단계 2 — SHAKE/Keccak

- [x] AVX2 SHAKE128/SHAKE256 x1 adapter
- [x] AVX2 SHAKE128/SHAKE256 x4 adapter
- [x] AVX-512 SHAKE128/SHAKE256 x1/x4 adapter
- [x] 호출 순서와 lane 순서를 KAT로 확인

### 단계 3 — GF

- [x] 128/192/256-bit scalar PCLMUL 곱셈·제곱·환원
- [x] AVX2 XMM/YMM N-party batching
- [x] AVX-512 YMM/ZMM/VPCLMUL N-party batching
- [x] scalar 및 batch differential test

### 단계 4 — MPC와 sign/verify

- [x] AIMer v3 MPC 식을 유지한 party batching
- [x] x4 commitment와 tape 처리
- [x] 여섯 파라미터 sign/verify와 tamper rejection

### 단계 5 — KAT 중단 게이트

- [x] reference 6 x 100 responses
- [x] AVX2 6 x 100 responses
- [x] AVX-512 6 x 100 responses
- [x] 합계 1,800 responses가 공식 파일과 byte-exact 일치

어느 단계에서든 하나라도 KAT가 달라지면 해당 변경 이후 작업을 중단하고,
처음 달라진 vector에서 RNG 소비, SHAKE lane/padding, GF reduction, MPC party
순서, 직렬화를 순서대로 추적한다.

### 단계 6 — 성능과 논문화

- [x] 동일 binary와 CPU에서 reference/AVX2/AVX-512 benchmark harness 구현
- [x] CPU governor, Turbo, affinity, compiler, revision, warm-up/sample metadata 기록
- [x] keygen/sign/verify cycles, 시간, operations/second CSV 측정
- [ ] performance governor에서 독립 반복 측정하고 논문용 결과 확정
- [x] SHAKE-only, GF-only, MPC batching ablation
- [x] 평균·median·분산 및 paired bootstrap 신뢰구간 보고
- [ ] 동등성 논증과 재현 패키지 작성

## 6. 검증 명령

```bash
make -C AIMer_v3
make -C AIMer_v3 oqs-test
make -C AIMer_v3 oqs-kat
make -C AIMer_v3 check
```

단일 파라미터:

```bash
make -C AIMer_v3 kat PARAM=128f
```

지원 CPU에서 backend 강제 선택:

```bash
AIMER_V3_IMPL=ref    AIMer_v3/build/liboqs/tests/test_oqs_sig_registry
AIMER_V3_IMPL=avx2   AIMer_v3/build/liboqs/tests/test_oqs_sig_registry
AIMER_V3_IMPL=avx512 AIMer_v3/build/liboqs/tests/test_oqs_sig_registry
```

## 7. 수학적 판단과 논문 주장 경계

현재 포팅은 field와 hash의 함수가 reference와 동일한 bit string을 만들고,
MPC party별 독립 계산을 SIMD로 병렬화한 구현이다. 따라서 위 불변조건이
유지되는 한 알고리즘의 수학적 정의는 바뀌지 않는다. 1,800 KAT와 직접 GF
differential test는 이를 강하게 뒷받침한다.

논문에서는 “새로운 스킴의 안전성 증명”이 아니라 다음 범위로 주장한다.

- AIMer v3와 byte-compatible한 AVX2/AVX-512 구현
- ISA 특성에 맞춘 party batching과 GF/SHAKE 최적화
- 동일 최적화 범위에서의 reference/AVX2/AVX-512 성능 비교
- KAT와 differential test 기반의 재현 가능한 구현 검증

형식 검증, side-channel 분석, upstream liboqs 품질 기준 충족은 별도 평가
항목으로 명시한다.
