// SPDX-License-Identifier: MIT
// Incremental SHAKE128/SHAKE256 adapters for AIM2's XKCP AVX2 permutation.

#include "avx2_shake.h"
#include "avx2_shake256.h"

#include <stdio.h>
#include <stdlib.h>

#define KECCAK_CTX_ALIGNMENT 32
#define KECCAK_CTX_BYTES 224
#define SHAKE128_RATE 168U
#define SHAKE256_RATE 136U

extern void KeccakP1600_Initialize_avx2(void *state);
extern void KeccakP1600_AddByte_avx2(void *state, unsigned char data,
                                     unsigned int offset);
extern void KeccakP1600_AddBytes_avx2(void *state, const unsigned char *data,
                                      unsigned int offset,
                                      unsigned int length);
extern void KeccakP1600_Permute_24rounds_avx2(void *state);
extern void KeccakP1600_ExtractBytes_avx2(const void *state,
                                          unsigned char *data,
                                          unsigned int offset,
                                          unsigned int length);
extern size_t KeccakF1600_FastLoop_Absorb_avx2(
    void *state, unsigned int lane_count, const unsigned char *data,
    size_t data_len);

static void keccak_init(void **context) {
	*context = aligned_alloc(KECCAK_CTX_ALIGNMENT, KECCAK_CTX_BYTES);
	if (*context == NULL) {
		fprintf(stderr, "AIMer v3 AVX2 SHAKE state allocation failed\n");
		abort();
	}
	KeccakP1600_Initialize_avx2(*context);
	((uint64_t *)*context)[25] = 0;
}

static void keccak_absorb(void *context, uint32_t rate,
                          const uint8_t *input, size_t input_len) {
	uint64_t *state = context;
	size_t remaining = rate - state[25];
	if (state[25] != 0 && input_len >= remaining) {
		KeccakP1600_AddBytes_avx2(state, input, (unsigned int)state[25],
		                              (unsigned int)remaining);
		KeccakP1600_Permute_24rounds_avx2(state);
		input += remaining;
		input_len -= remaining;
		state[25] = 0;
	}
	if (input_len >= rate) {
		const size_t consumed = KeccakF1600_FastLoop_Absorb_avx2(
		    state, rate / 8, input, input_len);
		input += consumed;
		input_len -= consumed;
	}
	KeccakP1600_AddBytes_avx2(state, input, (unsigned int)state[25],
	                              (unsigned int)input_len);
	state[25] += input_len;
}

static void keccak_finalize(void *context, uint32_t rate) {
	uint64_t *state = context;
	KeccakP1600_AddByte_avx2(state, 0x1f, (unsigned int)state[25]);
	KeccakP1600_AddByte_avx2(state, 0x80, rate - 1);
	state[25] = 0;
}

static void keccak_squeeze(uint8_t *output, size_t output_len,
                           void *context, uint32_t rate) {
	uint64_t *state = context;
	while (output_len > state[25]) {
		KeccakP1600_ExtractBytes_avx2(
		    state, output, (unsigned int)(rate - state[25]),
		    (unsigned int)state[25]);
		KeccakP1600_Permute_24rounds_avx2(state);
		output += state[25];
		output_len -= state[25];
		state[25] = rate;
	}
	KeccakP1600_ExtractBytes_avx2(
	    state, output, (unsigned int)(rate - state[25]),
	    (unsigned int)output_len);
	state[25] -= output_len;
}

static void keccak_release(void **context) {
	if (*context != NULL) {
		KeccakP1600_Initialize_avx2(*context);
		free(*context);
		*context = NULL;
	}
}

#define DEFINE_SHAKE(bits, rate_value)                                       \
	void aimer_v3_avx2_shake##bits##_inc_init(                                  \
	    aimer_v3_avx2_shake##bits##incctx *state) {                              \
		keccak_init(&state->ctx);                                                   \
	}                                                                            \
	void aimer_v3_avx2_shake##bits##_inc_absorb(                                \
	    aimer_v3_avx2_shake##bits##incctx *state, const uint8_t *input,          \
	    size_t input_len) {                                                       \
		keccak_absorb(state->ctx, rate_value, input, input_len);                    \
	}                                                                            \
	void aimer_v3_avx2_shake##bits##_inc_finalize(                              \
	    aimer_v3_avx2_shake##bits##incctx *state) {                              \
		keccak_finalize(state->ctx, rate_value);                                    \
	}                                                                            \
	void aimer_v3_avx2_shake##bits##_inc_squeeze(                               \
	    uint8_t *output, size_t output_len,                                      \
	    aimer_v3_avx2_shake##bits##incctx *state) {                              \
		keccak_squeeze(output, output_len, state->ctx, rate_value);                 \
	}                                                                            \
	void aimer_v3_avx2_shake##bits##_inc_ctx_release(                           \
	    aimer_v3_avx2_shake##bits##incctx *state) {                              \
		keccak_release(&state->ctx);                                                \
	}

DEFINE_SHAKE(128, SHAKE128_RATE)
DEFINE_SHAKE(256, SHAKE256_RATE)

#undef DEFINE_SHAKE
