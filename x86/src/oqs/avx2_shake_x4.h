// SPDX-License-Identifier: MIT

#ifndef AIMER_V3_AVX2_SHAKE_X4_H
#define AIMER_V3_AVX2_SHAKE_X4_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	void *ctx;
} aimer_v3_avx2_shake128x4incctx;

void aimer_v3_avx2_shake128_x4_inc_init(
    aimer_v3_avx2_shake128x4incctx *state);
void aimer_v3_avx2_shake128_x4_inc_absorb(
    aimer_v3_avx2_shake128x4incctx *state,
    const uint8_t *input0, const uint8_t *input1,
    const uint8_t *input2, const uint8_t *input3, size_t input_len);
void aimer_v3_avx2_shake128_x4_inc_finalize(
    aimer_v3_avx2_shake128x4incctx *state);
void aimer_v3_avx2_shake128_x4_inc_squeeze(
    uint8_t *output0, uint8_t *output1, uint8_t *output2, uint8_t *output3,
    size_t output_len, aimer_v3_avx2_shake128x4incctx *state);
void aimer_v3_avx2_shake128_x4_inc_ctx_release(
    aimer_v3_avx2_shake128x4incctx *state);

#endif // AIMER_V3_AVX2_SHAKE_X4_H

