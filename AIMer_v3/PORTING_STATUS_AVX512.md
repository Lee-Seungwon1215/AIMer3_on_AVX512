# AIMer v3 SIMD/liboqs 포팅 현황

기준일: 2026-08-29  
작업 프로젝트: `AIMer_v3`  
비교 기준 artifact: `AIMer_v2`

## 프로젝트 범위

`AIMer_v3`는 AIMer_v2의 소스나 빌드 설정 없이 자체적으로 최소
OQS-compatible `liboqs.a`를 빌드한다. 필요한 Keccak 최적화 소스는
`AIMer_v3/src/oqs` 아래에 vendoring했다. 공식 upstream liboqs 전체 트리에
대한 패치와 AIMer v2/v3의 단일 라이브러리 공존은 별도 후속 작업이다.

구현 소스는 다음처럼 분리했다.

```text
src/sig/aimer/
├── reference/aimer-{128f,128s,192f,192s,256f,256s}
├── avx2/aimer-{128f,128s,192f,192s,256f,256s}
└── avx512/aimer-{128f,128s,192f,192s,256f,256s}
```

## 현재 결론

여섯 파라미터를 각각 `OQS_SIG` 알고리즘으로 등록했고 reference, AVX2,
AVX-512 경로를 모두 연결했다. 자동 dispatch 우선순위는 AVX-512 > AVX2
> reference이며 `AIMER_V3_IMPL=ref|avx2|avx512`로 시험 경로를 고정할
수 있다.

세 경로는 각 파라미터의 공식 AIMer v3 KAT 100개와 바이트 단위로
일치한다. 총 검증량은 6 parameter sets x 3 backends x 100 vectors =
1,800 responses다. 포팅 중 KAT가 달라지면 중단한다는 규칙을 적용했으며,
현재까지 차이는 없었다.

## 구현 범위

- SHAKE128(128-bit set)과 SHAKE256(192/256-bit set)의 x1/x4 adapter
- AVX2 x1 Keccak assembly와 SIMD256 four-way Keccak
- AVX-512VL x1/x4 Keccak assembly
- PCLMUL 기반 GF(2^128), GF(2^192), GF(2^256) 스칼라 곱셈·제곱·환원
- AVX2 XMM/YMM 기반 N-party GF batching과 matrix-vector 연산
- AVX-512 VPCLMUL/ZMM batching과 VPTERNLOGQ matrix 누산
- AIMer v3 식을 유지한 MPC party batching과 four-way commitment/tape 처리
- NIST level 1/3/5, context API, 키·서명 크기 및 runtime CPU dispatch

두 SIMD 경로의 tree traversal 자체는 순차적이며, 각 노드의 hash는 선택된
x1 SHAKE를 사용한다. 즉 AVX2 구현 범위는 현재 AVX-512 구현 범위와 같다.

## 수학적 동등성 경계

- SHAKE rate, padding, domain separator, absorb/finalize/squeeze 순서와 x4
  lane 순서를 보존한다.
- field element의 little-endian 64-bit word 표현을 보존한다.
- GF(2^128), GF(2^192)는 환원 상수 `0x87`, GF(2^256)는 `0x425`를
  사용한다.
- AIMer v3 affine matrix, 일반 역원, MPC 식, 직렬화와 random-byte 소비
  순서를 보존한다.
- SIMD lane은 독립적인 party를 병렬 배치할 뿐 수학적 식은 바꾸지 않는다.

이 조건과 byte-exact KAT, GF differential 결과는 구현 동등성의 강한 실험적
근거다. 다만 암호 스킴 자체의 새로운 안전성 증명이나 형식 검증을 대신하지
않는다.

## 검증 결과

- 공식 KAT: reference/AVX2/AVX-512 전체 1,800 responses 일치
- GF scalar: mul, sqr, inv, matrix를 세 경로에서 직접 비교
- GF batch: sqr_N, mul_add_N, matrix_N, matrix_add_N을 직접 비교
- 세 강제 경로와 자동 dispatch에서 여섯 알고리즘의 keygen/sign/verify 통과
- context 서명/검증, 변조 거부, 크기와 NIST level metadata 통과

재현 명령:

```bash
make -C AIMer_v3
make -C AIMer_v3 oqs-kat
make -C AIMer_v3 oqs-test
make -C AIMer_v3 check
```

## 남은 후속 작업

1. performance governor와 고정 주파수·코어에서 충분한 독립 반복 측정을
   수행하고 reference/AVX2/AVX-512 논문용 결과를 확정한다.
2. AVX-512가 없는 실제 CPU에서 AVX2/reference fallback을 추가 검증한다.
3. performance 환경 재측정 후 논문용 결과 표와 재현 패키지를 확정한다.
4. 논문용 동등성 논증과 구현 범위 서술을 최종 교정한다.

공식 upstream liboqs 패치화는 이 독립 artifact의 완료 조건이 아니며, 성능
수치는 측정 환경을 고정하기 전에는 논문 결과로 사용하지 않는다.
