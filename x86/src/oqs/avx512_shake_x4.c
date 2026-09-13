// SPDX-License-Identifier: MIT
// Four-lane AIM3 adapter for the Intel AVX-512VL SHAKE backend from AIMer v2.

#include "avx512_shake_x4.h"

#include <stdio.h>
#include <stdlib.h>

#define KECCAK_X4_CTX_ALIGNMENT 32
#define KECCAK_X4_CTX_BYTES 832

extern void SHA3_shake128_x4_inc_ctx_reset_avx512vl(
    aimer_v3_avx512_shake128x4incctx *state);
extern void SHA3_shake128_x4_inc_absorb_avx512vl(
    aimer_v3_avx512_shake128x4incctx *state,
    const uint8_t *input0, const uint8_t *input1,
    const uint8_t *input2, const uint8_t *input3,
    size_t input_len);
extern void SHA3_shake128_x4_inc_finalize_avx512vl(
    aimer_v3_avx512_shake128x4incctx *state);
extern void SHA3_shake128_x4_inc_squeeze_avx512vl(
    uint8_t *output0, uint8_t *output1,
    uint8_t *output2, uint8_t *output3,
    size_t output_len, aimer_v3_avx512_shake128x4incctx *state);

void aimer_v3_avx512_shake128_x4_inc_init(
    aimer_v3_avx512_shake128x4incctx *state) {
	state->ctx = aligned_alloc(KECCAK_X4_CTX_ALIGNMENT, KECCAK_X4_CTX_BYTES);
	if (state->ctx == NULL) {
		fprintf(stderr, "AIMer v3 AVX-512 SHAKE x4 allocation failed\n");
		abort();
	}
	SHA3_shake128_x4_inc_ctx_reset_avx512vl(state);
}

void aimer_v3_avx512_shake128_x4_inc_absorb(
    aimer_v3_avx512_shake128x4incctx *state,
    const uint8_t *input0, const uint8_t *input1,
    const uint8_t *input2, const uint8_t *input3,
    size_t input_len) {
	SHA3_shake128_x4_inc_absorb_avx512vl(
	    state, input0, input1, input2, input3, input_len);
}

void aimer_v3_avx512_shake128_x4_inc_finalize(
    aimer_v3_avx512_shake128x4incctx *state) {
	SHA3_shake128_x4_inc_finalize_avx512vl(state);
}

void aimer_v3_avx512_shake128_x4_inc_squeeze(
    uint8_t *output0, uint8_t *output1,
    uint8_t *output2, uint8_t *output3,
    size_t output_len, aimer_v3_avx512_shake128x4incctx *state) {
	SHA3_shake128_x4_inc_squeeze_avx512vl(
	    output0, output1, output2, output3, output_len, state);
}

void aimer_v3_avx512_shake128_x4_inc_ctx_release(
    aimer_v3_avx512_shake128x4incctx *state) {
	if (state->ctx != NULL) {
		SHA3_shake128_x4_inc_ctx_reset_avx512vl(state);
		free(state->ctx);
		state->ctx = NULL;
	}
}
