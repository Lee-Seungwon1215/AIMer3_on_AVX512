// SPDX-License-Identifier: MIT

#ifndef AIMER_V3_AVX512_SHAKE_H
#define AIMER_V3_AVX512_SHAKE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	void *ctx;
} aimer_v3_avx512_shake128incctx;

void aimer_v3_avx512_shake128_inc_init(
    aimer_v3_avx512_shake128incctx *state);
void aimer_v3_avx512_shake128_inc_absorb(
    aimer_v3_avx512_shake128incctx *state,
    const uint8_t *input, size_t input_len);
void aimer_v3_avx512_shake128_inc_finalize(
    aimer_v3_avx512_shake128incctx *state);
void aimer_v3_avx512_shake128_inc_squeeze(
    uint8_t *output, size_t output_len,
    aimer_v3_avx512_shake128incctx *state);
void aimer_v3_avx512_shake128_inc_ctx_release(
    aimer_v3_avx512_shake128incctx *state);

#endif // AIMER_V3_AVX512_SHAKE_H
