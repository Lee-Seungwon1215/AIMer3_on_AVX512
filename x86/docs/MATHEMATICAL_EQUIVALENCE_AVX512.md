# AIMer v3 AVX2/AVX-512 수학적 동등성 검토

기준일: 2026-08-29

## 결론

현재 포팅은 AIMer v3 알고리즘을 변경하지 않고 동일한 비트 연산과 독립
party 계산을 SIMD lane에 재배치한 구현이다. 아래 불변조건과 코드 구조에
따르면 reference, AVX2, AVX-512는 같은 함수 값을 계산한다. 공식 KAT
1,800 responses와 직접 GF differential test의 완전 일치가 구현 수준의
비트 동등성을 강하게 뒷받침한다.

이 결론은 AIM2 알고리즘을 AIM3에 섞어도 된다는 뜻이 아니다. 재사용한 것은
Keccak permutation과 유한체 저수준 구현 기법이며, AIMer v3 상수, affine
layer, MPC 관계식, challenge와 proof 형식은 그대로 유지했다.

## 보존한 수학적 대상

| 파라미터 | Hash/XOF | Field 표현 | 기약다항식 |
|---|---|---|---|
| 128f, 128s | SHAKE128 | little-endian 2 x 64-bit | `x^128 + x^7 + x^2 + x + 1` |
| 192f, 192s | SHAKE256 | little-endian 3 x 64-bit | `x^192 + x^7 + x^2 + x + 1` |
| 256f, 256s | SHAKE256 | little-endian 4 x 64-bit | `x^256 + x^10 + x^5 + x^2 + 1` |

공개키, 비밀키, 서명 크기와 NIST level도 공식 파라미터 정의를 그대로
사용한다.

## 동등성 논증

### 1. SHAKE x1/x4

AVX2와 AVX-512VL 경로는 scalar와 같은 Keccak-f[1600] permutation, rate,
padding과 domain separator를 사용한다. x4 경로는 네 독립 state를 한 SIMD
호출에 넣으며 lane 사이에 데이터 의존성이 없다. 따라서 각 lane 출력은
해당 scalar SHAKE 출력과 같다. absorb/finalize/squeeze 순서와 remainder
처리도 보존한다.

### 2. 유한체 곱셈과 환원

PCLMULQDQ는 64-bit 다항식의 carry-less product를 정확히 계산한다. 부분곱을
XOR로 합친 2n-bit 다항식을 위 표의 기약다항식으로 환원하므로 결과는
reference의 GF(2^n) 곱셈과 같다. AVX-512의 VPCLMULQDQ는 이 연산을
여러 독립 lane에서 동시에 수행할 뿐이다.

제곱은 같은 carry-less product와 환원을 사용한다. 역원은 다음 일반 연쇄를
유지한다.

```text
power = a
result = 1
for i = 1 .. n-1:
    power = power^2
    result = result * power
```

최종 지수는 `2 + 4 + ... + 2^(n-1) = 2^n - 2`이므로 0이 아닌
`a`에 대해 `a^(-1)`이다. AIM2 전용 exponent chain은 사용하지 않았다.

### 3. party batch와 행렬-벡터 곱

AVX2는 XMM/YMM에, AVX-512는 YMM/ZMM에 서로 다른 party를 pack한다.
packing/unpacking이 party index와 word 순서를 보존하므로 batch 함수는 scalar
함수를 party별로 map한 것과 같다.

행렬 누산은 각 bit에 대해 다음 식을 구현한다.

```text
accumulator = accumulator XOR (bit_mask AND matrix_row)
```

AVX2는 compare/mask, AND, XOR로, AVX-512는 VPTERNLOGQ로 이 식을 계산한다.
이는 모두 GF(2) 행렬-벡터 곱의 scalar 조건부 XOR와 동일하다. 192-bit의
3-word field에서도 24-byte 유효 범위만 load/store한다.

### 4. AIMer v3 MPC와 직렬화

상위 `aim3.c`, `hash.c`, `tree.c`의 AIMer v3 흐름을 유지하고 sign/verify의
독립 party 연산만 batch 함수로 치환했다. affine matrix, challenge, zero-input
처리, random-byte 소비, proof 순서와 signature layout은 변경하지 않았다.
그러므로 SIMD 변환은 MPC 관계식 자체를 변경하지 않는다.

## 검증 증거

| 검사 | 결과 |
|---|---:|
| OQS 6 parameter x reference/AVX2/AVX-512 x 100 KAT | 1,800 통과 |
| GF scalar 및 batch 3경로 differential test | 6종 모두 통과 |
| 강제 reference/AVX2/AVX-512 및 자동 dispatch API | 모두 통과 |
| context sign/verify와 tamper rejection | 모두 통과 |

KAT는 key generation과 signature의 최종 바이트열을 고정하므로 SHAKE 순서,
RNG 소비, field 결과, MPC proof와 직렬화 중 하나라도 달라지면 일치하기
어렵다. GF differential test는 KAT에 더해 각 저수준 연산을 직접 비교한다.

## 논문에서 구분해야 할 주장

1. 구현 동등성: 보존 조건, 코드 대응, KAT와 differential test로 뒷받침한다.
2. 성능 개선: 고정 CPU/compiler/governor/affinity에서 충분히 반복 측정하고
   SHAKE-only, GF-only, MPC-batch ablation으로 뒷받침한다.

KAT 통과만으로 constant-time, side-channel 저항성, 형식 검증 또는 AIMer v3
스킴의 암호학적 안전성을 새로 증명했다고 주장해서는 안 된다. 논문화 전에는
비-AVX-512 실제 장비 fallback, sanitizer, 통계적 benchmark, disassembly와
별도 side-channel 검토가 남아 있다.
