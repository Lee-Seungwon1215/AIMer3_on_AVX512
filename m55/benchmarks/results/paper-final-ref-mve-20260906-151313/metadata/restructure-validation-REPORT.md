# M55 Restructure Validation

- 검증 커밋: `352b33156210dd4f373a799330541b9a3b82e5fc`
- 검증 브랜치: `validation/m55-restructure-20260906`
- 보드: STMicroelectronics NUCLEO-N657X0-Q, Cortex-M55 r1p1, 600 MHz
- ST-LINK serial: `003C00223335510735383531`
- 툴체인: xPack GNU Arm Embedded GCC 15.2.1 (20251203)
- OpenOCD: xPack 0.12.0+dev-02228-ge5888bda3-dirty
- 최종 설정: `BACKEND=mve MATVEC=mve MEMORY=full SIGN_SCHEDULE=lowmem`

| 파라미터 | sign build | MVE config | poly disassembly | affine/MPC disassembly | board sign | board KAT |
|---|---:|---:|---:|---:|---:|---:|
| 128f | PASS | PASS | PASS | PASS | PASS | 100/100 |
| 128s | PASS | PASS | PASS | PASS | PASS | 100/100 |
| 192f | PASS | PASS | PASS | PASS | PASS | 100/100 |
| 192s | PASS | PASS | PASS | PASS | PASS | 100/100 |
| 256f | PASS | PASS | PASS | PASS | PASS | 100/100 |
| 256s | PASS | PASS | PASS | PASS | PASS | 100/100 |

## 결과

- Host GF differential: 6/6
- Host affine differential: 6/6
- Host MVE KAT: 600/600
- Board KAT: 600/600 (`KAT_FAIL` 0개)
- Board keypair/sign/verify/tamper: 6/6 (`SIGN_TEST_PASS` 6개)
- `__ARM_FEATURE_MVE=3`: 6/6
- `VMULLB.P16` 및 `VMULLT.P16`: 6/6
- affine 커널 `VAND` 및 `VEOR`: 6/6
- MPC batch 경로의 MVE affine 커널 호출 연결: 6/6
- `common/`, `x86/`, `m55/src/`, `m55/include/`, `m55/platform/`, `m55/tests/`, Makefile 범위의 검증 전후 473개 source manifest: 동일
- M55 실제 빌드 소스의 `x86/` 의존성: 없음

이번 검증은 `common/x86/m55` 폴더 구조 변경 후 경로, 빌드, MVE 명령 생성, NUCLEO-N657X0-Q 실보드 동작을 확인한 것이다. 소스 파일은 변경하지 않았다. M55 성능 튜닝과 A/B/C/D 전체 성능 재측정은 수행하지 않았고, 기존 성능 결과도 수정하지 않았다. 검증 후 새 worktree의 `m55/build`만 정리했으며 이 결과 디렉터리는 보존했다.

