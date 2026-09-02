// SPDX-License-Identifier: MIT
// AIM3 SHAKE256 adapter for the Intel AVX-512VL backend used by AIMer v2.

#include "avx512_shake256.h"

#include <stdio.h>
#include <stdlib.h>

#define KECCAK_CTX_ALIGNMENT 32
#define KECCAK_CTX_BYTES 224

extern void SHA3_shake256_inc_ctx_reset_avx512vl(
    aimer_v3_avx512_shake256incctx *state);
extern void SHA3_shake256_inc_absorb_avx512vl(
    aimer_v3_avx512_shake256incctx *state,
    const uint8_t *input, size_t input_len);
extern void SHA3_shake256_inc_finalize_avx512vl(
    aimer_v3_avx512_shake256incctx *state);
extern void SHA3_shake256_inc_squeeze_avx512vl(
    uint8_t *output, size_t output_len,
    aimer_v3_avx512_shake256incctx *state);

void aimer_v3_avx512_shake256_inc_init(
    aimer_v3_avx512_shake256incctx *state) {
	state->ctx = aligned_alloc(KECCAK_CTX_ALIGNMENT, KECCAK_CTX_BYTES);
	if (state->ctx == NULL) {
		fprintf(stderr, "AIMer v3 AVX-512 SHAKE256 allocation failed\n");
		abort();
	}
	SHA3_shake256_inc_ctx_reset_avx512vl(state);
}

void aimer_v3_avx512_shake256_inc_absorb(
    aimer_v3_avx512_shake256incctx *state,
    const uint8_t *input, size_t input_len) {
	SHA3_shake256_inc_absorb_avx512vl(state, input, input_len);
}

void aimer_v3_avx512_shake256_inc_finalize(
    aimer_v3_avx512_shake256incctx *state) {
	SHA3_shake256_inc_finalize_avx512vl(state);
}

void aimer_v3_avx512_shake256_inc_squeeze(
    uint8_t *output, size_t output_len,
    aimer_v3_avx512_shake256incctx *state) {
	SHA3_shake256_inc_squeeze_avx512vl(output, output_len, state);
}

void aimer_v3_avx512_shake256_inc_ctx_release(
    aimer_v3_avx512_shake256incctx *state) {
	if (state->ctx != NULL) {
		SHA3_shake256_inc_ctx_reset_avx512vl(state);
		free(state->ctx);
		state->ctx = NULL;
	}
}
