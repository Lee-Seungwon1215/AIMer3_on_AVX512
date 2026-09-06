// SPDX-License-Identifier: MIT
// Four independent GF(2^128) elements occupy the four 128-bit lanes of a ZMM.
// Matrix multiplication processes 16 parties per block and therefore supports
// both AIM3-128f (N=16) and AIM3-128s (N=256).

#include "avx512_field128_batch.h"

#include <immintrin.h>
#include <stddef.h>

_Static_assert(AIMER_N % 16 == 0, "AIMER_N must be a multiple of 16");

static inline __m512i gf128_reduce_N(__m512i low, __m512i high) {
	const __m512i modulus =
	    _mm512_broadcast_i32x4(_mm_set_epi64x(0, 0x87));
	__m512i folded = _mm512_clmulepi64_epi128(high, modulus, 0x01);
	low = _mm512_xor_si512(low, _mm512_bslli_epi128(folded, 8));
	high = _mm512_xor_si512(high, _mm512_bsrli_epi128(folded, 8));
	low = _mm512_xor_si512(
	    low, _mm512_clmulepi64_epi128(high, modulus, 0x00));
	return low;
}

void gf_sqr_N(gf out[AIMER_N], const gf in[AIMER_N]) {
	for (size_t party = 0; party < AIMER_N; party += 4) {
		const __m512i x = _mm512_loadu_si512((const void *)in[party]);
		const __m512i low = _mm512_clmulepi64_epi128(x, x, 0x00);
		const __m512i high = _mm512_clmulepi64_epi128(x, x, 0x11);
		_mm512_storeu_si512((void *)out[party], gf128_reduce_N(low, high));
	}
}

void gf_mul_add_N(gf accum[AIMER_N], const gf in[AIMER_N],
                  const gf multiplier) {
	const __m512i y =
	    _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i *)multiplier));
	for (size_t party = 0; party < AIMER_N; party += 4) {
		const __m512i x = _mm512_loadu_si512((const void *)in[party]);
		__m512i low = _mm512_clmulepi64_epi128(x, y, 0x00);
		__m512i high = _mm512_clmulepi64_epi128(x, y, 0x11);
		const __m512i cross = _mm512_xor_si512(
		    _mm512_clmulepi64_epi128(x, y, 0x01),
		    _mm512_clmulepi64_epi128(x, y, 0x10));
		low = _mm512_xor_si512(low, _mm512_bslli_epi128(cross, 8));
		high = _mm512_xor_si512(high, _mm512_bsrli_epi128(cross, 8));
		const __m512i product = gf128_reduce_N(low, high);
		const __m512i old =
		    _mm512_loadu_si512((const void *)accum[party]);
		_mm512_storeu_si512((void *)accum[party],
		                     _mm512_xor_si512(old, product));
	}
}

void gf_mat_vec_mul_N(gf out[AIMER_N], const gf in[AIMER_N],
                      const gf matrix[AIM3_NUM_BITS_FIELD]) {
	for (size_t party = 0; party < AIMER_N; party++) {
		out[party][0] = 0;
		out[party][1] = 0;
	}
	gf_mat_vec_mul_add_N(out, in, matrix);
}

void gf_mat_vec_mul_add_N(gf accum[AIMER_N], const gf in[AIMER_N],
                          const gf matrix[AIM3_NUM_BITS_FIELD]) {
	for (size_t base = 0; base < AIMER_N; base += 16) {
		__m512i value0 = _mm512_loadu_si512((const void *)in[base]);
		__m512i value1 = _mm512_loadu_si512((const void *)in[base + 4]);
		__m512i value2 = _mm512_loadu_si512((const void *)in[base + 8]);
		__m512i value3 = _mm512_loadu_si512((const void *)in[base + 12]);
		__m512i out0 = _mm512_loadu_si512((const void *)accum[base]);
		__m512i out1 = _mm512_loadu_si512((const void *)accum[base + 4]);
		__m512i out2 = _mm512_loadu_si512((const void *)accum[base + 8]);
		__m512i out3 = _mm512_loadu_si512((const void *)accum[base + 12]);

		__m512i high0 = _mm512_shuffle_epi32(value0, (_MM_PERM_ENUM)0xee);
		__m512i high1 = _mm512_shuffle_epi32(value1, (_MM_PERM_ENUM)0xee);
		__m512i high2 = _mm512_shuffle_epi32(value2, (_MM_PERM_ENUM)0xee);
		__m512i high3 = _mm512_shuffle_epi32(value3, (_MM_PERM_ENUM)0xee);
		__m512i low0 = _mm512_shuffle_epi32(value0, (_MM_PERM_ENUM)0x44);
		__m512i low1 = _mm512_shuffle_epi32(value1, (_MM_PERM_ENUM)0x44);
		__m512i low2 = _mm512_shuffle_epi32(value2, (_MM_PERM_ENUM)0x44);
		__m512i low3 = _mm512_shuffle_epi32(value3, (_MM_PERM_ENUM)0x44);

		for (int bit = 63; bit >= 0; bit--) {
			const __m512i row = _mm512_broadcast_i32x4(
			    _mm_loadu_si128((const __m128i *)matrix[64 + bit]));
			out0 = _mm512_ternarylogic_epi64(
			    out0, _mm512_srai_epi64(high0, 63), row, 0x78);
			out1 = _mm512_ternarylogic_epi64(
			    out1, _mm512_srai_epi64(high1, 63), row, 0x78);
			out2 = _mm512_ternarylogic_epi64(
			    out2, _mm512_srai_epi64(high2, 63), row, 0x78);
			out3 = _mm512_ternarylogic_epi64(
			    out3, _mm512_srai_epi64(high3, 63), row, 0x78);
			high0 = _mm512_slli_epi64(high0, 1);
			high1 = _mm512_slli_epi64(high1, 1);
			high2 = _mm512_slli_epi64(high2, 1);
			high3 = _mm512_slli_epi64(high3, 1);
		}

		for (int bit = 63; bit >= 0; bit--) {
			const __m512i row = _mm512_broadcast_i32x4(
			    _mm_loadu_si128((const __m128i *)matrix[bit]));
			out0 = _mm512_ternarylogic_epi64(
			    out0, _mm512_srai_epi64(low0, 63), row, 0x78);
			out1 = _mm512_ternarylogic_epi64(
			    out1, _mm512_srai_epi64(low1, 63), row, 0x78);
			out2 = _mm512_ternarylogic_epi64(
			    out2, _mm512_srai_epi64(low2, 63), row, 0x78);
			out3 = _mm512_ternarylogic_epi64(
			    out3, _mm512_srai_epi64(low3, 63), row, 0x78);
			low0 = _mm512_slli_epi64(low0, 1);
			low1 = _mm512_slli_epi64(low1, 1);
			low2 = _mm512_slli_epi64(low2, 1);
			low3 = _mm512_slli_epi64(low3, 1);
		}

		_mm512_storeu_si512((void *)accum[base], out0);
		_mm512_storeu_si512((void *)accum[base + 4], out1);
		_mm512_storeu_si512((void *)accum[base + 8], out2);
		_mm512_storeu_si512((void *)accum[base + 12], out3);
	}
}
