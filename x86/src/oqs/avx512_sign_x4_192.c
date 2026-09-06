// SPDX-License-Identifier: MIT
// The AIM3 signing schedule is parameterized by params.h. For security levels
// above 128 bits it uses SHAKE256, so map the common x4 signing source to the
// SHAKE256 adapter before compiling it in the 192-bit namespace.

#include "avx512_shake256_x4.h"

#define aimer_v3_avx512_shake128x4incctx \
        aimer_v3_avx512_shake256x4incctx
#define aimer_v3_avx512_shake128_x4_inc_init \
        aimer_v3_avx512_shake256_x4_inc_init
#define aimer_v3_avx512_shake128_x4_inc_absorb \
        aimer_v3_avx512_shake256_x4_inc_absorb
#define aimer_v3_avx512_shake128_x4_inc_finalize \
        aimer_v3_avx512_shake256_x4_inc_finalize
#define aimer_v3_avx512_shake128_x4_inc_squeeze \
        aimer_v3_avx512_shake256_x4_inc_squeeze
#define aimer_v3_avx512_shake128_x4_inc_ctx_release \
        aimer_v3_avx512_shake256_x4_inc_ctx_release

/* Prevent the common source from redeclaring the SHAKE128 adapter types. */
#define AIMER_V3_AVX512_SHAKE_X4_H
#include "avx512_sign_x4_128f.c"
