// SPDX-License-Identifier: MIT
// AIM3-only replacement for the incremental SHAKE API using XKCP AVX2.

#ifndef FIPS202_H
#define FIPS202_H

#include "oqs/avx2_shake.h"
#include "oqs/avx2_shake256.h"

typedef aimer_v3_avx2_shake128incctx shake128incctx;
typedef aimer_v3_avx2_shake256incctx shake256incctx;

#define shake128_inc_init aimer_v3_avx2_shake128_inc_init
#define shake128_inc_absorb aimer_v3_avx2_shake128_inc_absorb
#define shake128_inc_finalize aimer_v3_avx2_shake128_inc_finalize
#define shake128_inc_squeeze aimer_v3_avx2_shake128_inc_squeeze
#define shake128_inc_ctx_release aimer_v3_avx2_shake128_inc_ctx_release

#define shake256_inc_init aimer_v3_avx2_shake256_inc_init
#define shake256_inc_absorb aimer_v3_avx2_shake256_inc_absorb
#define shake256_inc_finalize aimer_v3_avx2_shake256_inc_finalize
#define shake256_inc_squeeze aimer_v3_avx2_shake256_inc_squeeze
#define shake256_inc_ctx_release aimer_v3_avx2_shake256_inc_ctx_release

#endif // FIPS202_H

