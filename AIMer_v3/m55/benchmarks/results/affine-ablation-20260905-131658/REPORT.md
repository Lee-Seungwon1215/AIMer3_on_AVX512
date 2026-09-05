# Cortex-M55 AIMer v3 affine MVE controlled ablation

## 실제 변경 범위

D/MVE-AFFINE는 B/C의 MVE GF 곱셈·제곱·reduction·packed Frobenius를 그대로 사용하고, 단일 affine와 4-party affine만 MVE VAND/VEOR 누산으로 교체한다. 4-party Q register의 32-bit lane 네 개는 서로 다른 party이며, 두 output word accumulator가 같은 mask와 matrix word를 공유한다.

AIMer_v2, AIMer_v3/src, AVX2/AVX-512, GF/Frobenius, Keccak/SHAKE, low-memory schedule, linker layout과 직렬화는 변경하지 않았다. packed affine→Frobenius fusion도 하지 않았다.

## 구성과 재현 명령

- A/REF: `BACKEND=ref MATVEC=reference`
- B/MVE-REFMAT: `BACKEND=mve MATVEC=reference`
- C/MVE-COMPACT: `BACKEND=mve MATVEC=compact`
- D/MVE-AFFINE: `BACKEND=mve MATVEC=mve`

```sh
make -B PARAM=128f BACKEND=mve MATVEC=mve TEST=benchmark \
  MEMORY=full SIGN_SCHEDULE=lowmem run-board
./benchmarks/run_affine_ablation.sh
```

## Correctness gates

- 공식 reference matrix 함수 128/192/256-bit 소스 일치
- host low-memory byte exact 6/6
- A/B/C/D host field differential 24/24
- D host affine edge/alias/active-lane differential 6/6
- A/B/C/D host 공식 KAT 2,400/2,400
- D actual-board MVE affine differential 6/6 (MVE=2)
- A/B/C/D board keypair/sign/verify/tamper 24/24 및 checksum 일치
- D board 공식 KAT 600/600
- D VAND/VEOR 및 MPC affine 호출 연결 disassembly 6/6

Host scalar emulation은 API·edge·alias 검증이며 실제 MVE 실행 증거가 아니다. 실제 보드 differential, MVE=2 출력과 disassembly VAND/VEOR를 별도 gate로 사용했다. 이는 functional/구현 증거이며 constant-time의 완전한 증명은 아니다.

## 측정 조건과 통계

- STM32N657 NUCLEO Cortex-M55, 600 MHz, I-cache/D-cache 활성화
- 동일 AXI SRAM layout, stack/heap, portable Keccak/SHAKE, low-memory sign/verify schedule
- `arm-none-eabi-gcc (xPack GNU Arm Embedded GCC arm64) 15.2.1 20251203`, `-O3 -fno-tree-vectorize -fno-tree-slp-vectorize`
- 구성·파라미터·연산당 7 independent run × 7 measured sample = 49; 각 run에 warm-up 1회
- 표준편차는 49개 raw cycle 표본의 population SD(ddof=0); profile도 31표본의 population SD
- speedup은 baseline cycles / target cycles. 1보다 크면 개선, 1보다 작으면 성능 저하
- paired speedup은 동일 run의 7표본 중앙값끼리 계산한 7개 ratio의 중앙값; 95% CI는 그 7개 paired ratio를 10,000회 bootstrap
- PMU/SysTick 교차검사 168/168 통과, 최대 오차 0.820%

## E2E cycle 분포

| param | operation | config | median | mean | population SD | n |
|---|---|---|---|---|---|---|
| 128f | keypair | A/REF | 5,112,207 | 5,112,133 | 528 | 49 |
| 128f | keypair | B/MVE-REFMAT | 3,596,158 | 3,596,105 | 464 | 49 |
| 128f | keypair | C/MVE-COMPACT | 3,974,768 | 3,974,658 | 414 | 49 |
| 128f | keypair | D/MVE-AFFINE | 3,395,099 | 3,395,004 | 511 | 49 |
| 128f | sign | A/REF | 109,301,702 | 109,296,551 | 72,644 | 49 |
| 128f | sign | B/MVE-REFMAT | 91,596,332 | 91,595,797 | 35,149 | 49 |
| 128f | sign | C/MVE-COMPACT | 95,295,102 | 95,308,531 | 24,784 | 49 |
| 128f | sign | D/MVE-AFFINE | 84,463,334 | 84,485,755 | 58,281 | 49 |
| 128f | verify | A/REF | 62,729,672 | 62,726,986 | 28,309 | 49 |
| 128f | verify | B/MVE-REFMAT | 49,138,608 | 49,135,303 | 11,873 | 49 |
| 128f | verify | C/MVE-COMPACT | 51,058,557 | 51,064,870 | 12,384 | 49 |
| 128f | verify | D/MVE-AFFINE | 45,373,567 | 45,365,446 | 21,151 | 49 |
| 128s | keypair | A/REF | 5,114,904 | 5,114,782 | 833 | 49 |
| 128s | keypair | B/MVE-REFMAT | 3,599,045 | 3,598,834 | 743 | 49 |
| 128s | keypair | C/MVE-COMPACT | 3,978,106 | 3,977,937 | 906 | 49 |
| 128s | keypair | D/MVE-AFFINE | 3,398,166 | 3,397,962 | 699 | 49 |
| 128s | sign | A/REF | 866,696,436 | 866,778,878 | 462,823 | 49 |
| 128s | sign | B/MVE-REFMAT | 736,397,062 | 736,271,998 | 456,677 | 49 |
| 128s | sign | C/MVE-COMPACT | 763,159,243 | 763,294,897 | 456,628 | 49 |
| 128s | sign | D/MVE-AFFINE | 674,379,681 | 674,386,162 | 195,088 | 49 |
| 128s | verify | A/REF | 511,768,203 | 511,762,843 | 57,618 | 49 |
| 128s | verify | B/MVE-REFMAT | 400,147,179 | 400,146,223 | 63,140 | 49 |
| 128s | verify | C/MVE-COMPACT | 413,091,129 | 413,080,140 | 61,489 | 49 |
| 128s | verify | D/MVE-AFFINE | 370,305,156 | 370,303,591 | 40,327 | 49 |
| 192f | keypair | A/REF | 14,733,688 | 14,733,752 | 1,676 | 49 |
| 192f | keypair | B/MVE-REFMAT | 10,130,443 | 10,130,350 | 1,422 | 49 |
| 192f | keypair | C/MVE-COMPACT | 10,400,262 | 10,400,336 | 1,239 | 49 |
| 192f | keypair | D/MVE-AFFINE | 9,387,148 | 9,387,072 | 1,159 | 49 |
| 192f | sign | A/REF | 332,582,376 | 332,293,229 | 1,563,571 | 49 |
| 192f | sign | B/MVE-REFMAT | 257,646,268 | 257,896,352 | 1,465,356 | 49 |
| 192f | sign | C/MVE-COMPACT | 260,778,348 | 260,905,584 | 1,428,764 | 49 |
| 192f | sign | D/MVE-AFFINE | 218,556,572 | 218,539,236 | 410,368 | 49 |
| 192f | verify | A/REF | 196,053,714 | 196,116,847 | 1,420,880 | 49 |
| 192f | verify | B/MVE-REFMAT | 144,827,595 | 144,872,133 | 1,052,631 | 49 |
| 192f | verify | C/MVE-COMPACT | 146,754,087 | 146,825,887 | 1,028,674 | 49 |
| 192f | verify | D/MVE-AFFINE | 125,444,162 | 125,458,884 | 206,104 | 49 |
| 192s | keypair | A/REF | 14,740,398 | 14,740,087 | 2,217 | 49 |
| 192s | keypair | B/MVE-REFMAT | 10,138,399 | 10,138,337 | 2,029 | 49 |
| 192s | keypair | C/MVE-COMPACT | 10,409,852 | 10,409,936 | 2,099 | 49 |
| 192s | keypair | D/MVE-AFFINE | 9,400,073 | 9,399,994 | 1,732 | 49 |
| 192s | sign | A/REF | 2,476,392,172 | 2,479,879,263 | 11,725,761 | 49 |
| 192s | sign | B/MVE-REFMAT | 1,907,555,637 | 1,908,026,568 | 10,757,051 | 49 |
| 192s | sign | C/MVE-COMPACT | 1,928,363,793 | 1,925,981,647 | 14,230,598 | 49 |
| 192s | sign | D/MVE-AFFINE | 1,675,655,373 | 1,676,047,613 | 2,375,143 | 49 |
| 192s | verify | A/REF | 1,497,458,618 | 1,497,421,478 | 1,218,519 | 49 |
| 192s | verify | B/MVE-REFMAT | 1,081,618,764 | 1,081,551,670 | 1,266,094 | 49 |
| 192s | verify | C/MVE-COMPACT | 1,081,851,537 | 1,081,793,671 | 920,450 | 49 |
| 192s | verify | D/MVE-AFFINE | 986,872,037 | 986,900,659 | 158,028 | 49 |
| 256f | keypair | A/REF | 44,511,976 | 44,513,257 | 7,952 | 49 |
| 256f | keypair | B/MVE-REFMAT | 32,443,854 | 32,444,225 | 8,758 | 49 |
| 256f | keypair | C/MVE-COMPACT | 35,382,001 | 35,385,301 | 13,987 | 49 |
| 256f | keypair | D/MVE-AFFINE | 24,073,823 | 24,074,185 | 7,113 | 49 |
| 256f | sign | A/REF | 925,821,693 | 927,279,646 | 8,774,137 | 49 |
| 256f | sign | B/MVE-REFMAT | 817,013,403 | 816,503,349 | 8,935,265 | 49 |
| 256f | sign | C/MVE-COMPACT | 675,551,693 | 674,820,563 | 7,563,788 | 49 |
| 256f | sign | D/MVE-AFFINE | 508,930,046 | 509,063,828 | 1,018,910 | 49 |
| 256f | verify | A/REF | 551,564,370 | 551,162,033 | 3,670,176 | 49 |
| 256f | verify | B/MVE-REFMAT | 471,876,849 | 470,798,133 | 4,858,149 | 49 |
| 256f | verify | C/MVE-COMPACT | 400,761,285 | 400,358,257 | 2,915,273 | 49 |
| 256f | verify | D/MVE-AFFINE | 317,528,225 | 317,556,158 | 401,483 | 49 |
| 256s | keypair | A/REF | 44,517,706 | 44,522,299 | 11,404 | 49 |
| 256s | keypair | B/MVE-REFMAT | 32,428,887 | 32,429,653 | 8,709 | 49 |
| 256s | keypair | C/MVE-COMPACT | 35,432,397 | 35,433,234 | 12,094 | 49 |
| 256s | keypair | D/MVE-AFFINE | 24,069,592 | 24,070,644 | 11,164 | 49 |
| 256s | sign | A/REF | 7,371,305,325 | 7,376,045,112 | 67,521,550 | 49 |
| 256s | sign | B/MVE-REFMAT | 6,538,534,000 | 6,532,520,951 | 60,372,155 | 49 |
| 256s | sign | C/MVE-COMPACT | 4,727,058,456 | 4,730,575,558 | 48,313,611 | 49 |
| 256s | sign | D/MVE-AFFINE | 3,814,639,206 | 3,814,374,827 | 6,283,918 | 49 |
| 256s | verify | A/REF | 4,546,871,400 | 4,546,058,347 | 2,657,811 | 49 |
| 256s | verify | B/MVE-REFMAT | 3,897,461,646 | 3,896,570,218 | 3,108,219 | 49 |
| 256s | verify | C/MVE-COMPACT | 2,822,890,543 | 2,822,968,945 | 973,490 | 49 |
| 256s | verify | D/MVE-AFFINE | 2,420,537,765 | 2,420,522,573 | 240,635 | 49 |

## E2E paired speedup

### keypair

| param | A cycles | B cycles | C cycles | D cycles | A/B speedup [95% CI] | B/D speedup [95% CI] | C/D speedup [95% CI] | A/D speedup [95% CI] |
|---|---|---|---|---|---|---|---|---|
| 128f | 5,112,207 | 3,596,158 | 3,974,768 | 3,395,099 | 1.422× [1.421, 1.422] | 1.059× [1.059, 1.059] | 1.171× [1.171, 1.171] | 1.506× [1.506, 1.506] |
| 128s | 5,114,904 | 3,599,045 | 3,978,106 | 3,398,166 | 1.421× [1.421, 1.421] | 1.059× [1.059, 1.059] | 1.171× [1.171, 1.171] | 1.505× [1.505, 1.505] |
| 192f | 14,733,688 | 10,130,443 | 10,400,262 | 9,387,148 | 1.454× [1.454, 1.454] | 1.079× [1.079, 1.079] | 1.108× [1.108, 1.108] | 1.570× [1.569, 1.570] |
| 192s | 14,740,398 | 10,138,399 | 10,409,852 | 9,400,073 | 1.454× [1.454, 1.454] | 1.079× [1.078, 1.079] | 1.107× [1.107, 1.108] | 1.568× [1.568, 1.568] |
| 256f | 44,511,976 | 32,443,854 | 35,382,001 | 24,073,823 | 1.372× [1.372, 1.372] | 1.348× [1.348, 1.348] | 1.470× [1.470, 1.470] | 1.849× [1.849, 1.849] |
| 256s | 44,517,706 | 32,428,887 | 35,432,397 | 24,069,592 | 1.373× [1.373, 1.373] | 1.347× [1.347, 1.348] | 1.472× [1.472, 1.472] | 1.849× [1.849, 1.850] |

### sign

| param | A cycles | B cycles | C cycles | D cycles | A/B speedup [95% CI] | B/D speedup [95% CI] | C/D speedup [95% CI] | A/D speedup [95% CI] |
|---|---|---|---|---|---|---|---|---|
| 128f | 109,301,702 | 91,596,332 | 95,295,102 | 84,463,334 | 1.193× [1.193, 1.194] | 1.084× [1.083, 1.085] | 1.128× [1.127, 1.129] | 1.294× [1.293, 1.294] |
| 128s | 866,696,436 | 736,397,062 | 763,159,243 | 674,379,681 | 1.177× [1.177, 1.177] | 1.092× [1.092, 1.092] | 1.132× [1.132, 1.132] | 1.285× [1.285, 1.286] |
| 192f | 332,582,376 | 257,646,268 | 260,778,348 | 218,556,572 | 1.289× [1.288, 1.293] | 1.178× [1.178, 1.180] | 1.193× [1.192, 1.195] | 1.521× [1.518, 1.523] |
| 192s | 2,476,392,172 | 1,907,555,637 | 1,928,363,793 | 1,675,655,373 | 1.298× [1.296, 1.301] | 1.139× [1.136, 1.140] | 1.151× [1.149, 1.154] | 1.480× [1.475, 1.482] |
| 256f | 925,821,693 | 817,013,403 | 675,551,693 | 508,930,046 | 1.133× [1.129, 1.135] | 1.604× [1.601, 1.612] | 1.325× [1.319, 1.331] | 1.819× [1.809, 1.827] |
| 256s | 7,371,305,325 | 6,538,534,000 | 4,727,058,456 | 3,814,639,206 | 1.124× [1.122, 1.133] | 1.715× [1.712, 1.718] | 1.240× [1.235, 1.247] | 1.933× [1.924, 1.942] |

### verify

| param | A cycles | B cycles | C cycles | D cycles | A/B speedup [95% CI] | B/D speedup [95% CI] | C/D speedup [95% CI] | A/D speedup [95% CI] |
|---|---|---|---|---|---|---|---|---|
| 128f | 62,729,672 | 49,138,608 | 51,058,557 | 45,373,567 | 1.277× [1.277, 1.277] | 1.083× [1.083, 1.083] | 1.126× [1.125, 1.126] | 1.383× [1.382, 1.383] |
| 128s | 511,768,203 | 400,147,179 | 413,091,129 | 370,305,156 | 1.279× [1.279, 1.279] | 1.081× [1.081, 1.081] | 1.116× [1.115, 1.116] | 1.382× [1.382, 1.382] |
| 192f | 196,053,714 | 144,827,595 | 146,754,087 | 125,444,162 | 1.353× [1.347, 1.355] | 1.156× [1.153, 1.156] | 1.171× [1.169, 1.172] | 1.563× [1.558, 1.565] |
| 192s | 1,497,458,618 | 1,081,618,764 | 1,081,851,537 | 986,872,037 | 1.384× [1.384, 1.385] | 1.096× [1.096, 1.096] | 1.096× [1.096, 1.096] | 1.517× [1.517, 1.518] |
| 256f | 551,564,370 | 471,876,849 | 400,761,285 | 317,528,225 | 1.171× [1.163, 1.175] | 1.487× [1.477, 1.491] | 1.262× [1.254, 1.266] | 1.735× [1.733, 1.738] |
| 256s | 4,546,871,400 | 3,897,461,646 | 2,822,890,543 | 2,420,537,765 | 1.167× [1.166, 1.168] | 1.610× [1.608, 1.611] | 1.166× [1.166, 1.166] | 1.878× [1.878, 1.879] |

## Affine kernel과 MPC

4-party 행은 같은 입력 네 개를 처리한 scalar 기준(A/B/C fallback)과 D를 비교한다. call cycles에는 packing/unpacking이 포함되고 per-party는 call/4다. 제외 수치는 공식 개선율로 사용하지 않았다.

### gf_mat_vec_mul

| param | A call | B call | C call | D call | D per item | B/D | C/D | A/D |
|---|---|---|---|---|---|---|---|---|
| 128f | 3,262 | 3,264 | 3,997 | 2,872 | 2,872 | 1.136× | 1.392× | 1.136× |
| 128s | 3,262 | 3,264 | 3,997 | 2,872 | 2,872 | 1.136× | 1.392× | 1.136× |
| 192f | 6,983 | 6,983 | 7,334 | 6,040 | 6,040 | 1.156× | 1.214× | 1.156× |
| 192s | 6,983 | 6,983 | 7,334 | 6,040 | 6,040 | 1.156× | 1.214× | 1.156× |
| 256f | 13,295 | 13,431 | 15,220 | 7,813 | 7,813 | 1.719× | 1.948× | 1.702× |
| 256s | 13,363 | 13,366 | 15,220 | 7,882 | 7,882 | 1.696× | 1.931× | 1.695× |

### gf_mat_vec_mul_add

| param | A call | B call | C call | D call | D per item | B/D | C/D | A/D |
|---|---|---|---|---|---|---|---|---|
| 128f | 3,291 | 3,294 | 4,027 | 2,848 | 2,848 | 1.157× | 1.414× | 1.156× |
| 128s | 3,291 | 3,294 | 4,027 | 2,848 | 2,848 | 1.157× | 1.414× | 1.156× |
| 192f | 7,012 | 7,012 | 7,378 | 6,009 | 6,009 | 1.167× | 1.228× | 1.167× |
| 192s | 7,012 | 7,012 | 7,378 | 6,009 | 6,009 | 1.167× | 1.228× | 1.167× |
| 256f | 13,334 | 13,467 | 15,263 | 7,735 | 7,735 | 1.741× | 1.973× | 1.724× |
| 256s | 13,403 | 13,401 | 15,263 | 7,803 | 7,803 | 1.717× | 1.956× | 1.718× |

### gf_mat_vec_mul_batch4

| param | A call | B call | C call | D call | D per item | B/D | C/D | A/D |
|---|---|---|---|---|---|---|---|---|
| 128f | 13,073 | 13,085 | 16,019 | 6,213 | 1,553 | 2.106× | 2.578× | 2.104× |
| 128s | 13,073 | 13,085 | 16,019 | 6,213 | 1,553 | 2.106× | 2.578× | 2.104× |
| 192f | 27,658 | 27,658 | 29,358 | 14,177 | 3,544 | 1.951× | 2.071× | 1.951× |
| 192s | 27,658 | 27,658 | 29,358 | 14,177 | 3,544 | 1.951× | 2.071× | 1.951× |
| 256f | 52,996 | 53,206 | 60,706 | 24,106 | 6,026 | 2.207× | 2.518× | 2.198× |
| 256s | 53,206 | 53,273 | 60,638 | 24,107 | 6,027 | 2.210× | 2.515× | 2.207× |

### gf_mat_vec_mul_add_batch4

| param | A call | B call | C call | D call | D per item | B/D | C/D | A/D |
|---|---|---|---|---|---|---|---|---|
| 128f | 13,189 | 13,201 | 16,133 | 6,112 | 1,528 | 2.160× | 2.640× | 2.158× |
| 128s | 13,189 | 13,201 | 16,133 | 6,112 | 1,528 | 2.160× | 2.640× | 2.158× |
| 192f | 27,935 | 27,935 | 29,521 | 14,051 | 3,513 | 1.988× | 2.101× | 1.988× |
| 192s | 27,935 | 27,935 | 29,521 | 14,051 | 3,513 | 1.988× | 2.101× | 1.988× |
| 256f | 53,224 | 53,494 | 60,943 | 23,952 | 5,988 | 2.233× | 2.544× | 2.222× |
| 256s | 53,428 | 53,426 | 60,810 | 23,954 | 5,988 | 2.230× | 2.539× | 2.230× |

### aim3_mpc_batch4_affine

| param | A call | B call | C call | D call | D per item | B/D | C/D | A/D |
|---|---|---|---|---|---|---|---|---|
| 128f | 53,376 | 53,513 | 65,148 | 26,076 | 6,519 | 2.052× | 2.498× | 2.047× |
| 128s | 53,381 | 53,488 | 65,124 | 26,076 | 6,519 | 2.051× | 2.498× | 2.047× |
| 192f | 112,645 | 112,681 | 119,098 | 58,425 | 14,606 | 1.929× | 2.038× | 1.928× |
| 192s | 112,645 | 112,762 | 119,140 | 58,424 | 14,606 | 1.930× | 2.039× | 1.928× |
| 256f | 423,597 | 410,444 | 451,065 | 171,688 | 42,922 | 2.391× | 2.627× | 2.467× |
| 256s | 422,056 | 410,397 | 450,669 | 168,017 | 42,004 | 2.443× | 2.682× | 2.512× |

### aim3_mpc_batch4_frobenius

| param | A call | B call | C call | D call | D per item | B/D | C/D | A/D |
|---|---|---|---|---|---|---|---|---|
| 128f | 18,918 | 5,223 | 5,223 | 5,243 | 1,311 | 0.996× | 0.996× | 3.608× |
| 128s | 18,918 | 5,199 | 5,199 | 5,243 | 1,311 | 0.992× | 0.992× | 3.608× |
| 192f | 120,684 | 23,778 | 23,810 | 23,860 | 5,965 | 0.997× | 0.998× | 5.058× |
| 192s | 120,684 | 23,810 | 23,810 | 23,862 | 5,966 | 0.998× | 0.998× | 5.058× |
| 256f | 92,364 | 20,507 | 20,539 | 20,507 | 5,127 | 1.000× | 1.002× | 4.504× |
| 256s | 92,364 | 20,506 | 20,540 | 20,522 | 5,131 | 0.999× | 1.001× | 4.501× |

### aim3_mpc_batch4

| param | A call | B call | C call | D call | D per item | B/D | C/D | A/D |
|---|---|---|---|---|---|---|---|---|
| 128f | 72,359 | 58,802 | 70,437 | 31,386 | 7,846 | 1.874× | 2.244× | 2.305× |
| 128s | 72,364 | 58,753 | 70,389 | 31,386 | 7,846 | 1.872× | 2.243× | 2.306× |
| 192f | 233,397 | 136,561 | 143,010 | 82,390 | 20,598 | 1.657× | 1.736× | 2.833× |
| 192s | 233,397 | 136,708 | 143,086 | 82,390 | 20,598 | 1.659× | 1.737× | 2.833× |
| 256f | 516,126 | 431,086 | 471,739 | 192,359 | 48,090 | 2.241× | 2.452× | 2.683× |
| 256s | 514,585 | 431,039 | 471,345 | 188,708 | 47,177 | 2.284× | 2.498× | 2.727× |

## GF/Frobenius 회귀 관찰

| param | operation | A | B | C | D | D/B cycles ratio |
|---|---|---|---|---|---|---|
| 128f | gf_mul | 4,708 | 746 | 746 | 745 | 0.999 |
| 128f | gf_sqr | 217 | 250 | 250 | 249 | 0.996 |
| 128f | gf_sqr_batch4 | 934 | 470 | 470 | 470 | 1.000 |
| 128f | gf_mul_const_batch4 | 18,858 | 9,031 | 9,031 | 9,031 | 1.000 |
| 128s | gf_mul | 4,708 | 746 | 746 | 745 | 0.999 |
| 128s | gf_sqr | 217 | 250 | 250 | 249 | 0.996 |
| 128s | gf_sqr_batch4 | 934 | 470 | 470 | 466 | 0.991 |
| 128s | gf_mul_const_batch4 | 18,858 | 9,031 | 9,031 | 9,031 | 1.000 |
| 192f | gf_mul | 9,438 | 1,422 | 1,422 | 1,422 | 1.000 |
| 192f | gf_sqr | 367 | 404 | 404 | 404 | 1.000 |
| 192f | gf_sqr_batch4 | 1,542 | 658 | 658 | 660 | 1.002 |
| 192f | gf_mul_const_batch4 | 37,914 | 19,781 | 19,781 | 19,781 | 1.000 |
| 192s | gf_mul | 9,438 | 1,422 | 1,422 | 1,422 | 1.000 |
| 192s | gf_sqr | 367 | 404 | 404 | 404 | 1.000 |
| 192s | gf_sqr_batch4 | 1,542 | 654 | 654 | 661 | 1.011 |
| 192s | gf_mul_const_batch4 | 37,914 | 19,784 | 19,784 | 19,781 | 1.000 |
| 256f | gf_mul | 14,208 | 2,288 | 2,288 | 2,282 | 0.997 |
| 256f | gf_sqr | 570 | 566 | 566 | 564 | 0.997 |
| 256f | gf_sqr_batch4 | 2,372 | 1,516 | 1,516 | 1,507 | 0.994 |
| 256f | gf_mul_const_batch4 | 56,858 | 37,363 | 37,363 | 37,362 | 1.000 |
| 256s | gf_mul | 14,208 | 2,288 | 2,288 | 2,282 | 0.997 |
| 256s | gf_sqr | 570 | 566 | 566 | 564 | 0.997 |
| 256s | gf_sqr_batch4 | 2,372 | 1,512 | 1,512 | 1,507 | 0.997 |
| 256s | gf_mul_const_batch4 | 56,858 | 37,363 | 37,363 | 37,362 | 1.000 |

D와 B의 GF/Frobenius 소스는 동일하다. 위 차이는 binary layout·측정 잡음을 포함한 회귀 관찰이며 새 GF 알고리즘의 효과로 해석하지 않는다.

## 256-bit phase 진단

| param | kind | phase | A | B | C | D | B/D | C/D |
|---|---|---|---|---|---|---|---|---|
| 256f | sign | mpc_affine | 399,903,197 | 402,720,048 | 269,262,990 | 113,135,784 | 3.560× | 2.380× |
| 256f | sign | mpc_frobenius | 48,307,104 | 10,646,656 | 10,689,120 | 10,645,992 | 1.000× | 1.004× |
| 256f | sign | total_phases | 924,851,774 | 812,607,866 | 677,418,346 | 508,327,002 | 1.599× | 1.333× |
| 256f | verify | mpc_affine | 200,169,445 | 200,688,205 | 129,775,456 | 56,574,655 | 3.547× | 2.294× |
| 256f | verify | mpc_frobenius | 23,887,212 | 5,407,253 | 5,430,320 | 5,407,178 | 1.000× | 1.004× |
| 256f | verify | total | 549,849,805 | 470,711,635 | 402,738,962 | 318,015,401 | 1.480× | 1.266× |
| 256s | sign | mpc_affine | 3,617,865,862 | 3,533,899,263 | 1,824,399,925 | 872,827,955 | 4.049× | 2.090× |
| 256s | sign | mpc_frobenius | 392,376,258 | 86,319,612 | 86,834,202 | 86,455,339 | 0.998× | 1.004× |
| 256s | sign | total_phases | 7,395,391,061 | 6,492,927,666 | 4,781,863,876 | 3,815,061,000 | 1.702× | 1.253× |
| 256s | verify | mpc_affine | 1,874,634,840 | 1,866,857,674 | 798,580,207 | 411,715,513 | 4.534× | 1.940× |
| 256s | verify | mpc_frobenius | 194,032,333 | 43,856,594 | 44,142,938 | 43,915,125 | 0.999× | 1.005× |
| 256s | verify | total | 4,543,446,482 | 3,888,304,129 | 2,823,409,601 | 2,423,753,234 | 1.604× | 1.165× |

phasebench는 printf와 계측으로 공식 benchmark ELF의 배치를 바꾸므로 원인 위치 진단용이다. E2E speedup 주장에는 사용하지 않는다.

## Code/RAM/stack/heap

| param | config | text | data | bss | static RAM | stack peak | heap peak |
|---|---|---|---|---|---|---|---|
| 128f | A/REF | 73,480 | 2,192 | 143,192 | 10,216 | 7,992 | 16,384 |
| 128f | B/MVE-REFMAT | 71,760 | 2,192 | 143,192 | 10,216 | 7,992 | 16,384 |
| 128f | C/MVE-COMPACT | 71,256 | 2,192 | 143,192 | 10,216 | 7,992 | 16,384 |
| 128f | D/MVE-AFFINE | 72,080 | 2,192 | 143,192 | 10,216 | 7,984 | 16,384 |
| 128s | A/REF | 73,264 | 2,192 | 140,952 | 7,976 | 7,852 | 61,440 |
| 128s | B/MVE-REFMAT | 71,544 | 2,192 | 140,952 | 7,976 | 7,744 | 61,440 |
| 128s | C/MVE-COMPACT | 71,040 | 2,192 | 140,952 | 7,976 | 7,744 | 61,440 |
| 128s | D/MVE-AFFINE | 71,864 | 2,192 | 140,952 | 7,976 | 7,844 | 61,440 |
| 192f | A/REF | 75,896 | 2,192 | 151,696 | 18,720 | 14,796 | 28,672 |
| 192f | B/MVE-REFMAT | 74,856 | 2,192 | 151,696 | 18,720 | 14,688 | 28,672 |
| 192f | C/MVE-COMPACT | 74,536 | 2,192 | 151,696 | 18,720 | 14,796 | 28,672 |
| 192f | D/MVE-AFFINE | 75,360 | 2,192 | 151,696 | 18,720 | 14,788 | 28,672 |
| 192s | A/REF | 75,800 | 2,192 | 146,608 | 13,632 | 14,212 | 94,208 |
| 192s | B/MVE-REFMAT | 74,752 | 2,192 | 146,608 | 13,632 | 14,104 | 94,208 |
| 192s | C/MVE-COMPACT | 74,440 | 2,192 | 146,608 | 13,632 | 14,212 | 94,208 |
| 192s | D/MVE-AFFINE | 75,256 | 2,192 | 146,608 | 13,632 | 14,204 | 94,208 |
| 256f | A/REF | 78,552 | 2,192 | 167,688 | 34,712 | 24,716 | 61,440 |
| 256f | B/MVE-REFMAT | 77,904 | 2,192 | 167,688 | 34,712 | 24,716 | 61,440 |
| 256f | C/MVE-COMPACT | 77,472 | 2,192 | 167,688 | 34,712 | 24,716 | 61,440 |
| 256f | D/MVE-AFFINE | 78,256 | 2,192 | 167,688 | 34,712 | 24,708 | 61,440 |
| 256s | A/REF | 78,320 | 2,192 | 156,552 | 23,576 | 23,708 | 167,936 |
| 256s | B/MVE-REFMAT | 77,672 | 2,192 | 156,552 | 23,576 | 23,708 | 167,936 |
| 256s | C/MVE-COMPACT | 77,240 | 2,192 | 156,552 | 23,576 | 23,708 | 167,936 |
| 256s | D/MVE-AFFINE | 78,024 | 2,192 | 156,552 | 23,576 | 23,700 | 167,936 |

### Compiler stack-usage evidence

| param | config | function | bytes | kind |
|---|---|---|---|---|
| 128f | D/MVE-AFFINE | m55_gf_mat_vec_mul_add | 32 | static |
| 128f | D/MVE-AFFINE | m55_gf_mat_vec_mul_add_batch4 | 224 | static |
| 128f | D/MVE-AFFINE | m55_aim3_mpc_batch4 | 192 | static |
| 128s | D/MVE-AFFINE | m55_gf_mat_vec_mul_add | 32 | static |
| 128s | D/MVE-AFFINE | m55_gf_mat_vec_mul_add_batch4 | 224 | static |
| 128s | D/MVE-AFFINE | m55_aim3_mpc_batch4 | 192 | static |
| 192f | D/MVE-AFFINE | m55_gf_mat_vec_mul_add | 104 | static |
| 192f | D/MVE-AFFINE | m55_gf_mat_vec_mul_add_batch4 | 256 | static |
| 192f | D/MVE-AFFINE | m55_aim3_mpc_batch4 | 256 | static |
| 192s | D/MVE-AFFINE | m55_gf_mat_vec_mul_add | 104 | static |
| 192s | D/MVE-AFFINE | m55_gf_mat_vec_mul_add_batch4 | 256 | static |
| 192s | D/MVE-AFFINE | m55_aim3_mpc_batch4 | 256 | static |
| 256f | D/MVE-AFFINE | m55_gf_mat_vec_mul_add | 80 | static |
| 256f | D/MVE-AFFINE | m55_gf_mat_vec_mul_add_batch4 | 288 | static |
| 256f | D/MVE-AFFINE | m55_aim3_mpc_batch4 | 328 | static |
| 256s | D/MVE-AFFINE | m55_gf_mat_vec_mul_add | 80 | static |
| 256s | D/MVE-AFFINE | m55_gf_mat_vec_mul_add_batch4 | 288 | static |
| 256s | D/MVE-AFFINE | m55_aim3_mpc_batch4 | 328 | static |
| 128f | C/MVE-COMPACT | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 128f | C/MVE-COMPACT | m55_aim3_mpc_batch4 | 184 | static |
| 128s | C/MVE-COMPACT | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 128s | C/MVE-COMPACT | m55_aim3_mpc_batch4 | 192 | static |
| 192f | C/MVE-COMPACT | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 192f | C/MVE-COMPACT | m55_aim3_mpc_batch4 | 256 | static |
| 192s | C/MVE-COMPACT | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 192s | C/MVE-COMPACT | m55_aim3_mpc_batch4 | 256 | static |
| 256f | C/MVE-COMPACT | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 256f | C/MVE-COMPACT | m55_aim3_mpc_batch4 | 312 | static |
| 256s | C/MVE-COMPACT | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 256s | C/MVE-COMPACT | m55_aim3_mpc_batch4 | 312 | static |
| 128f | B/MVE-REFMAT | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 128f | B/MVE-REFMAT | m55_aim3_mpc_batch4 | 184 | static |
| 128s | B/MVE-REFMAT | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 128s | B/MVE-REFMAT | m55_aim3_mpc_batch4 | 192 | static |
| 192f | B/MVE-REFMAT | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 192f | B/MVE-REFMAT | m55_aim3_mpc_batch4 | 256 | static |
| 192s | B/MVE-REFMAT | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 192s | B/MVE-REFMAT | m55_aim3_mpc_batch4 | 256 | static |
| 256f | B/MVE-REFMAT | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 256f | B/MVE-REFMAT | m55_aim3_mpc_batch4 | 312 | static |
| 256s | B/MVE-REFMAT | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 256s | B/MVE-REFMAT | m55_aim3_mpc_batch4 | 312 | static |
| 128f | A/REF | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 128f | A/REF | m55_aim3_mpc_batch4 | 184 | static |
| 128s | A/REF | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 128s | A/REF | m55_aim3_mpc_batch4 | 192 | static |
| 192f | A/REF | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 192f | A/REF | m55_aim3_mpc_batch4 | 256 | static |
| 192s | A/REF | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 192s | A/REF | m55_aim3_mpc_batch4 | 256 | static |
| 256f | A/REF | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 256f | A/REF | m55_aim3_mpc_batch4 | 312 | static |
| 256s | A/REF | m55_gf_mat_vec_mul_add_batch4 | 24 | static |
| 256s | A/REF | m55_aim3_mpc_batch4 | 312 | static |

D disassembly는 VAND/VEOR와 `m55_aim3_mpc_batch4` 호출 연결을 확인했다. 저장된 함수별 disassembly와 `.su` 파일이 register save/restore, spill/reload, code size 검토의 근거다. intrinsic 사용만으로 레지스터 유지나 개선을 단정하지 않는다.

## 논문에서 주장 가능한 내용과 한계

- 주장 가능: 동일 공식 행렬과 AIMer 파라미터를 유지하면서 Cortex-M55 MVE에 맞춘 masked AND/XOR, 4-party data reuse와 작은 accumulator block을 독립 D backend로 구현했고 byte-exact/공식 KAT를 통과했다.
- 성능 주장은 위 paired A/B, B/D, C/D, A/D 결과와 CI가 직접 지지하는 파라미터·연산에 한정한다.
- B→D는 SIMD 논리 연산, matrix reuse, scheduling, packing/unpacking의 결합효과다. VAND 또는 VEOR 한 명령의 독립 기여율로 분해하지 않는다.
- KAT와 fixed test는 functional correctness 증거이지 constant-time 완전 증명이 아니다.
- D가 모든 항목에서 최선이라고 가정하지 않으며 parameter별 최솟값을 합성 backend처럼 제시하지 않는다.
- packed affine→Frobenius fusion, GF/Frobenius·Keccak 변경과 혼합 dispatch는 이번 범위 밖이다.

전체 raw samples, population 통계, 7-run paired ratio, ELF/MAP/disassembly, stack usage와 checksum은 이 결과 디렉터리에 보존되어 있다.

결과 디렉터리: [현재 결과 디렉터리](.)
