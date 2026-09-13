// SPDX-License-Identifier: MIT

#ifndef AIMER_V3_AVX512_SHAKE256_H
#define AIMER_V3_AVX512_SHAKE256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	void *ctx;
} aimer_v3_avx512_shake256incctx;

void aimer_v3_avx512_shake256_inc_init(
    aimer_v3_avx512_shake256incctx *state);
void aimer_v3_avx512_shake256_inc_absorb(
    aimer_v3_avx512_shake256incctx *state,
    const uint8_t *input, size_t input_len);
void aimer_v3_avx512_shake256_inc_finalize(
    aimer_v3_avx512_shake256incctx *state);
void aimer_v3_avx512_shake256_inc_squeeze(
    uint8_t *output, size_t output_len,
    aimer_v3_avx512_shake256incctx *state);
void aimer_v3_avx512_shake256_inc_ctx_release(
    aimer_v3_avx512_shake256incctx *state);

#endif // AIMER_V3_AVX512_SHAKE256_H
