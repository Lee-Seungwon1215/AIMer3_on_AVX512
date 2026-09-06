// SPDX-License-Identifier: MIT
// AVX2 GF(2^128) party batching: independent XMM PCLMUL streams and
// two 128-bit party lanes per YMM for matrix accumulation.

#include "avx2_field128_batch.h"

#include <immintrin.h>
#include <stddef.h>

_Static_assert(AIMER_N % 8 == 0, "AIMER_N must be a multiple of eight");

static inline __m128i reduce128(__m128i low, __m128i high) {
	const __m128i modulus = _mm_set_epi64x(0, 0x87);
	__m128i folded = _mm_clmulepi64_si128(high, modulus, 0x01);
	low = _mm_xor_si128(low, _mm_slli_si128(folded, 8));
	high = _mm_xor_si128(high, _mm_srli_si128(folded, 8));
	return _mm_xor_si128(
	    low, _mm_clmulepi64_si128(high, modulus, 0x00));
}

void gf_sqr_N(gf out[AIMER_N], const gf in[AIMER_N]) {
	for (size_t party = 0; party < AIMER_N; party += 4) {
		const __m128i x0 = _mm_loadu_si128((const __m128i *)in[party]);
		const __m128i x1 = _mm_loadu_si128((const __m128i *)in[party + 1]);
		const __m128i x2 = _mm_loadu_si128((const __m128i *)in[party + 2]);
		const __m128i x3 = _mm_loadu_si128((const __m128i *)in[party + 3]);
		const __m128i low0 = _mm_clmulepi64_si128(x0, x0, 0x00);
		const __m128i low1 = _mm_clmulepi64_si128(x1, x1, 0x00);
		const __m128i low2 = _mm_clmulepi64_si128(x2, x2, 0x00);
		const __m128i low3 = _mm_clmulepi64_si128(x3, x3, 0x00);
		const __m128i high0 = _mm_clmulepi64_si128(x0, x0, 0x11);
		const __m128i high1 = _mm_clmulepi64_si128(x1, x1, 0x11);
		const __m128i high2 = _mm_clmulepi64_si128(x2, x2, 0x11);
		const __m128i high3 = _mm_clmulepi64_si128(x3, x3, 0x11);
		_mm_storeu_si128((__m128i *)out[party], reduce128(low0, high0));
		_mm_storeu_si128((__m128i *)out[party + 1], reduce128(low1, high1));
		_mm_storeu_si128((__m128i *)out[party + 2], reduce128(low2, high2));
		_mm_storeu_si128((__m128i *)out[party + 3], reduce128(low3, high3));
	}
}

static inline __m128i multiply128(__m128i x, __m128i y) {
	__m128i low = _mm_clmulepi64_si128(x, y, 0x00);
	__m128i high = _mm_clmulepi64_si128(x, y, 0x11);
	const __m128i cross = _mm_xor_si128(
	    _mm_clmulepi64_si128(x, y, 0x01),
	    _mm_clmulepi64_si128(x, y, 0x10));
	low = _mm_xor_si128(low, _mm_slli_si128(cross, 8));
	high = _mm_xor_si128(high, _mm_srli_si128(cross, 8));
	return reduce128(low, high);
}

void gf_mul_add_N(gf accum[AIMER_N], const gf in[AIMER_N],
                  const gf multiplier) {
	const __m128i y = _mm_loadu_si128((const __m128i *)multiplier);
	for (size_t party = 0; party < AIMER_N; party += 2) {
		const __m128i x0 = _mm_loadu_si128((const __m128i *)in[party]);
		const __m128i x1 = _mm_loadu_si128((const __m128i *)in[party + 1]);
		const __m128i product0 = multiply128(x0, y);
		const __m128i product1 = multiply128(x1, y);
		const __m128i old0 =
		    _mm_loadu_si128((const __m128i *)accum[party]);
		const __m128i old1 =
		    _mm_loadu_si128((const __m128i *)accum[party + 1]);
		_mm_storeu_si128((__m128i *)accum[party],
		                 _mm_xor_si128(old0, product0));
		_mm_storeu_si128((__m128i *)accum[party + 1],
		                 _mm_xor_si128(old1, product1));
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
	const __m256i zero = _mm256_setzero_si256();
	for (size_t base = 0; base < AIMER_N; base += 8) {
		__m256i value0 =
		    _mm256_loadu_si256((const __m256i *)in[base]);
		__m256i value1 =
		    _mm256_loadu_si256((const __m256i *)in[base + 2]);
		__m256i value2 =
		    _mm256_loadu_si256((const __m256i *)in[base + 4]);
		__m256i value3 =
		    _mm256_loadu_si256((const __m256i *)in[base + 6]);
		__m256i out0 =
		    _mm256_loadu_si256((const __m256i *)accum[base]);
		__m256i out1 =
		    _mm256_loadu_si256((const __m256i *)accum[base + 2]);
		__m256i out2 =
		    _mm256_loadu_si256((const __m256i *)accum[base + 4]);
		__m256i out3 =
		    _mm256_loadu_si256((const __m256i *)accum[base + 6]);

		__m256i high0 = _mm256_shuffle_epi32(value0, 0xee);
		__m256i high1 = _mm256_shuffle_epi32(value1, 0xee);
		__m256i high2 = _mm256_shuffle_epi32(value2, 0xee);
		__m256i high3 = _mm256_shuffle_epi32(value3, 0xee);
		__m256i low0 = _mm256_shuffle_epi32(value0, 0x44);
		__m256i low1 = _mm256_shuffle_epi32(value1, 0x44);
		__m256i low2 = _mm256_shuffle_epi32(value2, 0x44);
		__m256i low3 = _mm256_shuffle_epi32(value3, 0x44);

		for (int bit = 63; bit >= 0; bit--) {
			const __m256i row = _mm256_broadcastsi128_si256(
			    _mm_loadu_si128((const __m128i *)matrix[64 + bit]));
			out0 = _mm256_xor_si256(
			    out0, _mm256_and_si256(_mm256_cmpgt_epi64(zero, high0), row));
			out1 = _mm256_xor_si256(
			    out1, _mm256_and_si256(_mm256_cmpgt_epi64(zero, high1), row));
			out2 = _mm256_xor_si256(
			    out2, _mm256_and_si256(_mm256_cmpgt_epi64(zero, high2), row));
			out3 = _mm256_xor_si256(
			    out3, _mm256_and_si256(_mm256_cmpgt_epi64(zero, high3), row));
			high0 = _mm256_slli_epi64(high0, 1);
			high1 = _mm256_slli_epi64(high1, 1);
			high2 = _mm256_slli_epi64(high2, 1);
			high3 = _mm256_slli_epi64(high3, 1);
		}
		for (int bit = 63; bit >= 0; bit--) {
			const __m256i row = _mm256_broadcastsi128_si256(
			    _mm_loadu_si128((const __m128i *)matrix[bit]));
			out0 = _mm256_xor_si256(
			    out0, _mm256_and_si256(_mm256_cmpgt_epi64(zero, low0), row));
			out1 = _mm256_xor_si256(
			    out1, _mm256_and_si256(_mm256_cmpgt_epi64(zero, low1), row));
			out2 = _mm256_xor_si256(
			    out2, _mm256_and_si256(_mm256_cmpgt_epi64(zero, low2), row));
			out3 = _mm256_xor_si256(
			    out3, _mm256_and_si256(_mm256_cmpgt_epi64(zero, low3), row));
			low0 = _mm256_slli_epi64(low0, 1);
			low1 = _mm256_slli_epi64(low1, 1);
			low2 = _mm256_slli_epi64(low2, 1);
			low3 = _mm256_slli_epi64(low3, 1);
		}

		_mm256_storeu_si256((__m256i *)accum[base], out0);
		_mm256_storeu_si256((__m256i *)accum[base + 2], out1);
		_mm256_storeu_si256((__m256i *)accum[base + 4], out2);
		_mm256_storeu_si256((__m256i *)accum[base + 6], out3);
	}
}
