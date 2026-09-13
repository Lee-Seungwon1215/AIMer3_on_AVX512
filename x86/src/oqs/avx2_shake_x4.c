// SPDX-License-Identifier: MIT
// Four-lane SHAKE128/SHAKE256 adapters for AIM2's XKCP SIMD256 permutation.

#include "avx2_shake_x4.h"
#include "avx2_shake256_x4.h"

#include <stdio.h>
#include <stdlib.h>

#define KECCAK_X4_CTX_ALIGNMENT 32
#define KECCAK_X4_CTX_BYTES 832
#define SHAKE128_RATE 168U
#define SHAKE256_RATE 136U

extern void KeccakP1600times4_InitializeAll_avx2(void *states);
extern void KeccakP1600times4_AddByte_avx2(
    void *states, unsigned int instance, unsigned char byte,
    unsigned int offset);
extern void KeccakP1600times4_AddBytes_avx2(
    void *states, unsigned int instance, const unsigned char *data,
    unsigned int offset, unsigned int length);
extern void KeccakP1600times4_PermuteAll_24rounds_avx2(void *states);
extern void KeccakP1600times4_ExtractBytes_avx2(
    const void *states, unsigned int instance, unsigned char *data,
    unsigned int offset, unsigned int length);

static void keccak_x4_init(void **context) {
	*context = aligned_alloc(KECCAK_X4_CTX_ALIGNMENT, KECCAK_X4_CTX_BYTES);
	if (*context == NULL) {
		fprintf(stderr, "AIMer v3 AVX2 SHAKE x4 allocation failed\n");
		abort();
	}
	KeccakP1600times4_InitializeAll_avx2(*context);
	((uint64_t *)*context)[100] = 0;
}

static void add_bytes_all(void *state, const uint8_t *input[4],
                          unsigned int offset, unsigned int length) {
	for (unsigned int lane = 0; lane < 4; lane++) {
		KeccakP1600times4_AddBytes_avx2(
		    state, lane, input[lane], offset, length);
	}
}

static void keccak_x4_absorb(void *context, uint32_t rate,
                             const uint8_t *input0, const uint8_t *input1,
                             const uint8_t *input2, const uint8_t *input3,
                             size_t input_len) {
	uint64_t *state = context;
	const uint8_t *input[4] = {input0, input1, input2, input3};
	size_t remaining = rate - state[100];
	if (state[100] != 0 && input_len >= remaining) {
		add_bytes_all(state, input, (unsigned int)state[100],
		              (unsigned int)remaining);
		KeccakP1600times4_PermuteAll_24rounds_avx2(state);
		for (size_t lane = 0; lane < 4; lane++) {
			input[lane] += remaining;
		}
		input_len -= remaining;
		state[100] = 0;
	}
	while (input_len >= rate) {
		add_bytes_all(state, input, 0, rate);
		KeccakP1600times4_PermuteAll_24rounds_avx2(state);
		for (size_t lane = 0; lane < 4; lane++) {
			input[lane] += rate;
		}
		input_len -= rate;
	}
	add_bytes_all(state, input, (unsigned int)state[100],
	              (unsigned int)input_len);
	state[100] += input_len;
}

static void keccak_x4_finalize(void *context, uint32_t rate) {
	uint64_t *state = context;
	for (unsigned int lane = 0; lane < 4; lane++) {
		KeccakP1600times4_AddByte_avx2(
		    state, lane, 0x1f, (unsigned int)state[100]);
		KeccakP1600times4_AddByte_avx2(state, lane, 0x80, rate - 1);
	}
	state[100] = 0;
}

static void extract_all(const void *state, uint8_t *output[4],
                        unsigned int offset, unsigned int length) {
	for (unsigned int lane = 0; lane < 4; lane++) {
		KeccakP1600times4_ExtractBytes_avx2(
		    state, lane, output[lane], offset, length);
	}
}

static void keccak_x4_squeeze(uint8_t *output0, uint8_t *output1,
                              uint8_t *output2, uint8_t *output3,
                              size_t output_len, void *context,
                              uint32_t rate) {
	uint64_t *state = context;
	uint8_t *output[4] = {output0, output1, output2, output3};
	while (output_len > state[100]) {
		extract_all(state, output, (unsigned int)(rate - state[100]),
		            (unsigned int)state[100]);
		KeccakP1600times4_PermuteAll_24rounds_avx2(state);
		for (size_t lane = 0; lane < 4; lane++) {
			output[lane] += state[100];
		}
		output_len -= state[100];
		state[100] = rate;
	}
	extract_all(state, output, (unsigned int)(rate - state[100]),
	            (unsigned int)output_len);
	state[100] -= output_len;
}

static void keccak_x4_release(void **context) {
	if (*context != NULL) {
		KeccakP1600times4_InitializeAll_avx2(*context);
		free(*context);
		*context = NULL;
	}
}

#define DEFINE_SHAKE_X4(bits, rate_value)                                    \
	void aimer_v3_avx2_shake##bits##_x4_inc_init(                               \
	    aimer_v3_avx2_shake##bits##x4incctx *state) {                            \
		keccak_x4_init(&state->ctx);                                                \
	}                                                                            \
	void aimer_v3_avx2_shake##bits##_x4_inc_absorb(                             \
	    aimer_v3_avx2_shake##bits##x4incctx *state,                              \
	    const uint8_t *input0, const uint8_t *input1,                            \
	    const uint8_t *input2, const uint8_t *input3, size_t input_len) {         \
		keccak_x4_absorb(state->ctx, rate_value, input0, input1, input2, input3,     \
		                  input_len);                                                \
	}                                                                            \
	void aimer_v3_avx2_shake##bits##_x4_inc_finalize(                           \
	    aimer_v3_avx2_shake##bits##x4incctx *state) {                            \
		keccak_x4_finalize(state->ctx, rate_value);                                 \
	}                                                                            \
	void aimer_v3_avx2_shake##bits##_x4_inc_squeeze(                            \
	    uint8_t *output0, uint8_t *output1, uint8_t *output2,                    \
	    uint8_t *output3, size_t output_len,                                     \
	    aimer_v3_avx2_shake##bits##x4incctx *state) {                            \
		keccak_x4_squeeze(output0, output1, output2, output3, output_len,           \
		                  state->ctx, rate_value);                                   \
	}                                                                            \
	void aimer_v3_avx2_shake##bits##_x4_inc_ctx_release(                        \
	    aimer_v3_avx2_shake##bits##x4incctx *state) {                            \
		keccak_x4_release(&state->ctx);                                             \
	}

DEFINE_SHAKE_X4(128, SHAKE128_RATE)
DEFINE_SHAKE_X4(256, SHAKE256_RATE)

#undef DEFINE_SHAKE_X4
