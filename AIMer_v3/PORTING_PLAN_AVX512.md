# AIMer v3 AVX-512 및 liboqs 이식 계획

작성일: 2026-08-29  
프로젝트 루트: `AIMer_M55`  
목표: 기존 AIM2 liboqs/AVX-512 구현의 범용 최적화 기법을 최신 AIM3에 이식한다.

## 1. 최종 목표

현재 저장소에는 AIM2 기반 구현이 다음 형태로 통합되어 있다.

```text
AIM2 ref / AVX2 / AVX-512
        ↓
OQS_SIG API 및 CPU 런타임 디스패치
        ↓
liboqs.a
        ↓
KAT 및 benchmark
```

이를 AIM3에서도 다음과 같이 구현한다.

```text
AIM3 ref / AVX-512
        ↓
OQS_SIG API 및 안전한 CPU 런타임 디스패치
        ↓
liboqs.a
        ↓
공식 AIM3 KAT 및 benchmark
```

초기 목표는 `ref`와 `avx512` 두 구현이다. AIM3 AVX2 구현은 필수 범위가
아니며, 필요하면 AVX-512 구현이 안정화된 뒤 추가한다.

## 2. 반드시 지킬 설계 결정

- 기존 AIM2 코드는 삭제하거나 AIM3로 덮어쓰지 않는다.
- AIM3는 `AIMER-v3-128f`처럼 AIM2와 구분되는 OQS 알고리즘 이름을 사용한다.
- AIM3 소스는 루트의 `src/sig/aimer_v3/`에 별도로 통합한다.
- `AIMer_v3/Reference_Implementation`은 공식 원본이므로 수정하지 않는다.
- `AIMer_v3/src/.../*_ref`는 AIM3 정확성 기준으로 유지한다.
- 최적화는 `*_opt` 또는 새 liboqs용 `*_avx512`에서만 수행한다.
- 각 단계가 끝날 때 공식 AIM3 KAT와 바이트 단위로 일치해야 한다.
- AIM2 전용 상수, AIM2 MPC 수식, inverse-Mersenne 전용 지수 체인은
  AIM3로 복사하지 않는다.

## 3. 현재 완료된 상태

### 기존 루트 프로젝트

- AIM2의 6개 파라미터가 liboqs 형식으로 통합되어 있다.
- 각 파라미터에 `ref`, `avx2`, `avx512` 구현이 있다.
- GF AVX-512는 VPCLMULQDQ와 VPTERNLOGQ를 사용한다.
- Keccak/SHAKE는 x1/x4 AVX-512VL 백엔드를 사용한다.
- `AIMER_IMPL` 및 `AIMER_KECCAK`으로 구현을 선택할 수 있다.
- `kat_sig`, `bench_sig`, `bench_full` 테스트 프로그램이 있다.

### 새 `AIMer_v3` 프로젝트

- 공식 AIM3 원본과 공식 KAT를 보존했다.
- 6개 파라미터 각각을 `_ref`와 `_opt`로 분리했다.
- 총 12개 구현이 독립적으로 빌드된다.
- `_opt`는 아직 AVX-512가 적용되지 않은 AIM3 베이스라인이다.
- ref/opt 심볼 namespace는 분리되어 있다.
- 12개 sign/verify 스모크 테스트를 모두 통과했다.
- 12개 공식 AIM3 KAT를 모두 바이트 단위로 통과했다.
- 복사된 ref 및 opt 핵심 소스가 공식 AIM3 원본과 일치함을 확인했다.

현재 검증 명령:

```bash
make -C AIMer_v3 all
make -C AIMer_v3 test
make -C AIMer_v3 check
```

## 4. AVX-512 머신으로 옮기기 전에 확인할 사항

2026-08-29 현재 상위 Git 저장소에서 `AIMer_v3/`는 untracked 상태였다.
다른 머신에서 `git clone`만 하면 이 폴더가 전달되지 않을 수 있다.

```bash
git status --short
git add AIMer_v3
git commit -m "Add AIMer v3 reference and optimization baseline"
git push
```

커밋 여부는 작업자가 결정하되, 원격 AVX-512 머신에 다음 항목이 모두
전달됐는지 반드시 확인한다.

- `AIMer_v3/Reference_Implementation/`
- `AIMer_v3/KAT/`
- `AIMer_v3/src/`
- `AIMer_v3/tests/`
- `AIMer_v3/Makefile`
- 이 계획 문서

`AIMer_v3/build/`는 생성물이라 전달하거나 커밋할 필요가 없다.

## 5. AVX-512 머신 첫 점검

### 5.1 환경 기록

```bash
git rev-parse HEAD
uname -a
lscpu
gcc --version
make --version
```

### 5.2 CPU 기능 확인

```bash
grep -m1 '^flags' /proc/cpuinfo | tr ' ' '\n' | \
  grep -E '^(aes|avx2|bmi2|pclmulqdq|popcnt|avx512f|avx512vl|avx512bw|avx512dq|vpclmulqdq)$'
```

다음 열 개 플래그가 모두 보여야 한다.

```text
aes avx2 bmi2 pclmulqdq popcnt
avx512f avx512vl avx512bw avx512dq vpclmulqdq
```

### 5.3 기존 AIM2 AVX-512 기준점 확인

```bash
make distclean
make CC=gcc AR=ar -j"$(nproc)"

AIMER_IMPL=avx512 AIMER_KECCAK=avx512 \
  ./build/tests/kat_sig AIMER-128f
```

이 명령이 실패하면 AIM3 이식 전에 컴파일러, assembler, CPU 기능 또는
기존 AVX-512 백엔드 문제를 먼저 해결한다.

### 5.4 AIM3 독립 기준점 재확인

```bash
make -C AIMer_v3 clean
make -C AIMer_v3 all -j"$(nproc)"
make -C AIMer_v3 check
```

이 단계에서는 아직 AVX-512를 사용하지 않는다. Mac에서 확인한 12개 KAT가
새 Linux 머신에서도 모두 통과해야 다음 단계로 간다.

## 6. 디렉터리 통합안

기존 AIM2를 보존하기 위해 AIM3용 디렉터리를 별도로 추가한다.

```text
src/sig/
├── aimer/                         기존 AIM2, 유지
└── aimer_v3/
    ├── aimer-128f_ref/
    ├── aimer-128f_avx512/
    ├── aimer-128s_ref/
    ├── aimer-128s_avx512/
    ├── aimer-192f_ref/
    ├── aimer-192f_avx512/
    ├── aimer-192s_ref/
    ├── aimer-192s_avx512/
    ├── aimer-256f_ref/
    ├── aimer-256f_avx512/
    ├── aimer-256s_ref/
    ├── aimer-256s_avx512/
    └── sig_aimer_v3_<variant>.c
```

추가할 OQS 공개 이름은 다음처럼 구분한다.

```text
OQS_SIG_alg_aimer_v3_128f → "AIMER-v3-128f"
OQS_SIG_alg_aimer_v3_128s → "AIMER-v3-128s"
OQS_SIG_alg_aimer_v3_192f → "AIMER-v3-192f"
OQS_SIG_alg_aimer_v3_192s → "AIMER-v3-192s"
OQS_SIG_alg_aimer_v3_256f → "AIMER-v3-256f"
OQS_SIG_alg_aimer_v3_256s → "AIMER-v3-256s"
```

`include/oqs/sig_aimer_v3.h`도 별도 생성한다. 기존
`include/oqs/sig_aimer.h`의 AIM2 크기를 AIM3 크기로 덮어쓰지 않는다.

## 7. AIM3 크기 상수

liboqs 헤더와 wrapper는 다음 공식 AIM3 크기를 사용해야 한다.

| 파라미터 | Public key | Secret key | Signature |
|---|---:|---:|---:|
| 128f | 32 | 48 | 6944 |
| 128s | 32 | 48 | 4704 |
| 192f | 48 | 72 | 15408 |
| 192s | 48 | 72 | 10320 |
| 256f | 64 | 96 | 31360 |
| 256s | 64 | 96 | 20224 |

## 8. 단계별 작업 계획

각 단계는 `128f` 하나로 먼저 구현하고 KAT를 통과한 뒤 나머지 파라미터로
확장한다.

### 단계 0: 기준점 고정

- [ ] `AIMer_v3/`가 Git 또는 동기화 수단으로 AVX-512 머신에 전달됐는지 확인
- [ ] 머신 정보와 Git revision 기록
- [ ] 기존 AIM2 AVX-512 KAT 통과
- [ ] `make -C AIMer_v3 check` 전체 통과
- [ ] 이 시점의 커밋 또는 태그 기록

통과 조건: 기존 AIM2와 AIM3 standalone 기준 구현이 모두 정상이다.

### 단계 1: AIM3 reference를 liboqs에 먼저 통합

AVX-512를 넣기 전에 AIM3 reference가 OQS API를 통해 공식 KAT를 통과해야
한다. 이것이 이후 최적화의 liboqs 기준점이다.

- [ ] `src/sig/aimer_v3/aimer-128f_ref/` 추가
- [ ] `src/sig/aimer_v3/sig_aimer_v3_128f.c` wrapper 추가
- [ ] `include/oqs/sig_aimer_v3.h` 추가
- [ ] `include/oqs/sig.h`에 AIM3 알고리즘 이름과 개수 추가
- [ ] `src/sig/sig.c`에 enable/new dispatch 추가
- [ ] 루트 `Makefile`에 AIM3 ref object 추가
- [ ] AIM3의 SHAKE API를 liboqs SHA3 API에 연결
- [ ] AIM3의 runtime RNG를 OQS random API에 연결
- [ ] 공식 AIM3 KAT를 `tests/KAT/aimer-v3-128f/`로 분리
- [ ] `kat_sig`가 `AIMER-v3-128f`를 인식하도록 확장

주의: KAT용 AES-CTR-DRBG와 실제 runtime random source를 혼동하지 않는다.
KAT 실행 시 기존 `rand_nist`가 생성하는 바이트 순서와 호출 횟수가 공식
AIM3 KAT와 같아야 한다.

예상 검증 명령:

```bash
AIMER_V3_IMPL=ref AIMER_KECCAK=ref \
  ./build/tests/kat_sig AIMER-v3-128f
```

통과 조건: liboqs의 `OQS_SIG` API를 통해 AIM3-128f 공식 KAT 100개가 모두
일치한다.

### 단계 2: AIM3에 기존 Keccak/SHAKE 백엔드 연결

이 단계에서는 GF 코드를 바꾸지 않는다. SHAKE 백엔드의 영향만 분리한다.

재사용 대상:

- `src/common/sha3/avx512vl_sha3.c`
- `src/common/sha3/avx512vl_sha3x4.c`
- `src/common/sha3/avx512vl_low/`
- `tests/aimer_keccak_select.c`

작업:

- [ ] AIM3 scalar hash wrapper를 `OQS_SHA3` x1 API로 교체
- [ ] 네 개의 독립적인 SHAKE 작업을 위한 x4 wrapper 추가
- [ ] seed expansion, commitment, tree 작업을 x4 단위로 묶기
- [ ] 남는 1~3개 작업은 scalar 또는 안전한 remainder 처리
- [ ] 입력 길이, absorb 순서, padding, domain separator 보존
- [ ] x4 lane 순서가 party 순서와 일치하는지 확인
- [ ] scalar/ref Keccak과 AVX-512 Keccak 각각 KAT 실행

검증 예시:

```bash
AIMER_V3_IMPL=ref AIMER_KECCAK=ref \
  ./build/tests/kat_sig AIMER-v3-128f

AIMER_V3_IMPL=ref AIMER_KECCAK=avx512 \
  ./build/tests/kat_sig AIMER-v3-128f
```

통과 조건: field 구현은 동일하게 두고 Keccak만 바꿔도 공식 KAT가
일치한다.

### 단계 3: AIM3 GF AVX-512 커널 이식

기존 AIM2 `field.c`의 저수준 GF 커널을 AIM3 API에 맞게 연결한다.

우선 재사용할 커널:

- `GF_mul`, `GF_sqr`, field reduction
- `GF_mul_N`, `GF_sqr_N`, `GF_mul_add_N`
- `GF_transposed_matmul_add_N`
- `POLY_mul_add_N`, `POLY_red_N`
- VPCLMULQDQ를 이용한 4-party 병렬 처리
- VPTERNLOGQ를 이용한 행렬-벡터 누산

새로 작성하거나 검증할 부분:

- [ ] AIM3 field type과 기존 SIMD field type의 byte/word layout 비교
- [ ] AIM3 API 이름 및 인자 순서에 맞는 wrapper 작성
- [ ] AIM3에 필요한 일반 `GF_inv` 구현 또는 최적화
- [ ] AIM3 Frobenius 연산 `x^(2^e)` 처리
- [ ] AIM3 IV 기반 affine matrix에 generic matrix kernel 적용
- [ ] 정렬 요구사항과 unaligned 입력 처리 확인
- [ ] `N=16`과 `N=256`의 remainder 없이 4-party batching 확인

AIM2에서 복사하면 안 되는 부분:

- `aim2_constant.h`의 AIM2 상수와 행렬값
- AIM2 inverse-Mersenne 전용 함수와 고정 exponent chain
- AIM2 MPC equation과 `aim2.c`의 상위 알고리즘

검증 순서:

```text
GF 단위 테스트
  → AIM3-128f standalone KAT
  → AIM3-128f liboqs ref-Keccak KAT
  → AIM3-128f liboqs AVX512-Keccak KAT
```

통과 조건: GF와 Keccak을 모두 AVX-512로 강제해도 공식 AIM3 KAT가
일치한다.

### 단계 4: AIM3 MPC party 병렬화

GF 함수만 SIMD로 바꾸는 것에서 끝내지 않고 AIM3 `aim3_mpc()`와 signing
phase가 여러 party를 batch 함수로 호출하도록 변경한다.

- [ ] AIM3 전용 `aim3_mpc_N` 설계
- [ ] `pt_share`, `y_shares`, `a_shares`, `c_share` 배치 구조 검증
- [ ] input/output affine layer를 `GF_transposed_matmul_add_N`에 연결
- [ ] 반복 squaring을 `GF_sqr_N` 또는 Frobenius matrix로 처리
- [ ] multiplication check에서 deferred reduction 적용
- [ ] zero-input 검사와 key-generation 재시도 보존
- [ ] proof serialization과 공개 signature layout 보존
- [ ] sign과 verify 양쪽에서 같은 batch 순서 사용

통과 조건: AIM3-128f의 keygen/sign/verify/KAT가 모두 통과하며 scalar
reference보다 성능이 개선된다.

### 단계 5: 나머지 다섯 파라미터로 확장

권장 확장 순서:

```text
128f → 128s → 192f → 192s → 256f → 256s
```

192-bit와 256-bit field는 word 수와 VPCLMUL reduction 과정이 다르므로
단순 매크로 변경만으로 완료됐다고 판단하지 않는다.

- [ ] 128f ref/avx512 KAT
- [ ] 128s ref/avx512 KAT
- [ ] 192f ref/avx512 KAT
- [ ] 192s ref/avx512 KAT
- [ ] 256f ref/avx512 KAT
- [ ] 256s ref/avx512 KAT

### 단계 6: 안전한 runtime dispatch

초기 AIM3 dispatcher는 `ref`와 `avx512`만 선택한다.

```text
AIMER_V3_IMPL=ref       → 강제로 reference
AIMER_V3_IMPL=avx512    → 강제로 AVX-512, 테스트/벤치마크용
환경변수 없음           → CPU 기능 확인 후 avx512 또는 ref
```

중요: 현재 AIM2 wrapper는 `OQS_CPU_EXT_AVX512`만 확인하고 AVX-512 구현을
선택한다. 하지만 GF는 VPCLMULQDQ가 필요하고 Keccak은 AVX512VL이 필요하다.
AIM3 dispatcher에서는 적어도 다음을 모두 확인해야 한다.

- `OQS_CPU_EXT_AVX512`
- `OQS_CPU_EXT_VPCLMULQDQ`
- AVX512VL 지원
- AVX2, PCLMULQDQ, AES, BMI2, POPCNT 등 실제 커널 요구사항

현재 `src/common/common.c`의 `OQS_CPU_EXT_AVX512` 검사는 F/BW/DQ를 확인하지만
VL을 포함하지 않는다. AVX512VL CPUID 검사를 추가하거나 별도 extension으로
관리해야 한다. 지원하지 않는 CPU에서 opt를 자동 선택하면 안 된다.

## 9. 공식 KAT 검증 원칙

최적화 단계마다 다음 네 가지를 구분해 실행한다.

| Field/MPC | Keccak | 목적 |
|---|---|---|
| ref | ref | AIM3/liboqs 기준점 |
| ref | avx512 | Keccak 이식만 검증 |
| avx512 | ref | GF/MPC 이식만 검증 |
| avx512 | avx512 | 최종 구현 검증 |

최종 KAT 스크립트는 6개 파라미터의 ref/avx512 조합을 검사해야 한다.
각 파라미터당 100개 공식 벡터라면 최소 1,200개 검사가 된다.

KAT 실패 시 다음 순서로 원인을 좁힌다.

1. 처음 실패하는 count 하나만 재실행한다.
2. pk와 sk가 다르면 RNG 소비 순서, AIM3 keygen, zero-input retry를 확인한다.
3. pk/sk는 같고 signature만 다르면 salt, tape, commitment, challenge 순서를 확인한다.
4. scalar Keccak은 맞고 AVX-512 Keccak만 틀리면 lane 순서와 padding을 확인한다.
5. Keccak 조합은 맞고 AVX-512 GF만 틀리면 field layout과 reduction을 확인한다.
6. signature 생성은 맞고 verify만 실패하면 proof parsing과 MPC reconstruction을 확인한다.

## 10. 성능 측정 계획

KAT 전체 통과 전에는 논문용 수치를 측정하지 않는다.

최소 비교 대상:

```text
AIM3 ref field + ref Keccak
AIM3 ref field + AVX-512 Keccak
AIM3 AVX-512 field/MPC + ref Keccak
AIM3 AVX-512 field/MPC + AVX-512 Keccak
```

측정 항목:

- key generation cycles
- signing cycles
- verification cycles
- operations per second
- signature size
- Keccak만의 기여도
- GF/MPC만의 기여도
- AIM2와 AIM3의 연산 구조 및 최적화 효과 차이

논문용 측정에서는 CPU governor, Turbo Boost, 코어 affinity, compiler,
Git revision, sample 수와 warm-up 수를 모두 기록한다.

## 11. 작업 중 절대 깨지면 안 되는 항목

- SHAKE absorb/finalize/squeeze 호출 순서
- domain separation prefix
- x4 lane과 party index의 대응
- field element의 little-endian byte layout
- AIM3 affine matrix 생성 순서
- random byte 소비 순서
- zero-input 검사와 재시도
- signature serialize/deserialize 순서와 크기
- 공개키 및 비밀키 형식

SIMD 구현은 수학적으로 같은 정수/비트 결과를 내야 하며, 근사 최적화는
허용되지 않는다.

## 12. 세션별 진행 기록 양식

AVX-512 머신에서 작업할 때 아래 블록을 복사하여 문서 하단에 누적한다.

```markdown
### YYYY-MM-DD 작업 기록

- Git revision:
- CPU:
- Compiler:
- 변경한 파일:
- 구현한 내용:
- 실행한 명령:
- 통과한 테스트/KAT:
- 실패 또는 남은 문제:
- 다음 작업:
```

## 13. 완료 조건

- [ ] AIM3 6개 파라미터가 별도 OQS 알고리즘으로 등록됨
- [ ] AIM2와 AIM3가 같은 liboqs 빌드에서 공존함
- [ ] AIM3 ref와 AVX-512 runtime dispatch가 동작함
- [ ] 미지원 CPU에서는 ref로 안전하게 fallback함
- [ ] AIM3 ref 6종 공식 KAT 통과
- [ ] AIM3 AVX-512 6종 공식 KAT 통과
- [ ] 총 1,200개 이상의 liboqs KAT 검사 통과
- [ ] standalone AIM3 KAT도 계속 통과
- [ ] keygen/sign/verify benchmark 자동화 완료
- [ ] ref 대비 AVX-512 성능 결과와 환경 정보 저장
- [ ] README에 빌드, KAT, CPU 요구사항, 재현 방법 문서화

## 14. AVX-512 머신에서 바로 시작할 첫 작업

첫 구현 목표는 다음 하나다.

> AIM3-128f reference를 `AIMER-v3-128f`라는 별도 OQS_SIG 알고리즘으로
> 등록하고, AVX-512를 전혀 사용하지 않은 상태에서 공식 AIM3 KAT 100개를
> liboqs 테스트 프로그램으로 통과시킨다.

이 기준점이 만들어지기 전에는 SHAKE x4나 GF AVX-512 이식을 시작하지
않는다. 기준점 이후에는 Keccak/SHAKE, GF, AIM3 MPC 순서로 진행한다.
