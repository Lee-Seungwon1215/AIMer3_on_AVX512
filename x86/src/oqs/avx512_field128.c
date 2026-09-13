// SPDX-License-Identifier: MIT
// AIM3 GF(2^128) adapter using the PCLMUL/AVX2 kernels proven in AIMer v2.
// The AIM3 modulus is x^128 + x^7 + x^2 + x + 1 (low word 0x87),
// identical to the field representation used by the reusable kernel.

#include "field.h"

#include <immintrin.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void gf_to_bytes(uint8_t *out, const gf in) {
	for (size_t i = 0; i < AIM3_NUM_WORDS_FIELD; i++) {
		uint64_t word = in[i];
		for (size_t j = 0; j < 8; j++) {
			out[8 * i + j] = (uint8_t)word;
			word >>= 8;
		}
	}
}

void gf_from_bytes(gf out, const uint8_t *in) {
	for (size_t i = 0; i < AIM3_NUM_WORDS_FIELD; i++) {
		uint64_t word = 0;
		for (int j = 7; j >= 0; j--) {
			word = (word << 8) | in[8 * i + (size_t)j];
		}
		out[i] = word;
	}
}

void gf_set0(gf a) {
	a[0] = 0;
	a[1] = 0;
}

bool gf_is0(const gf a) {
	return (a[0] | a[1]) == 0;
}

void gf_copy(gf out, const gf in) {
	out[0] = in[0];
	out[1] = in[1];
}

void gf_add(gf c, const gf a, const gf b) {
	c[0] = a[0] ^ b[0];
	c[1] = a[1] ^ b[1];
}

static inline __m128i gf128_reduce(__m128i low, __m128i high) {
	const __m128i modulus = _mm_set_epi64x(0, 0x87);
	__m128i folded = _mm_clmulepi64_si128(high, modulus, 0x01);
	low = _mm_xor_si128(low, _mm_slli_si128(folded, 8));
	high = _mm_xor_si128(high, _mm_srli_si128(folded, 8));
	low = _mm_xor_si128(low,
	                      _mm_clmulepi64_si128(high, modulus, 0x00));
	return low;
}

void gf_mul(gf c, const gf a, const gf b) {
	const __m128i x = _mm_loadu_si128((const __m128i *)a);
	const __m128i y = _mm_loadu_si128((const __m128i *)b);

	__m128i low = _mm_clmulepi64_si128(x, y, 0x00);
	__m128i high = _mm_clmulepi64_si128(x, y, 0x11);
	const __m128i cross = _mm_xor_si128(
	    _mm_clmulepi64_si128(x, y, 0x01),
	    _mm_clmulepi64_si128(x, y, 0x10));
	low = _mm_xor_si128(low, _mm_slli_si128(cross, 8));
	high = _mm_xor_si128(high, _mm_srli_si128(cross, 8));

	_mm_storeu_si128((__m128i *)c, gf128_reduce(low, high));
}

void gf_mul_add(gf c, const gf a, const gf b) {
	gf product;
	gf_mul(product, a, b);
	c[0] ^= product[0];
	c[1] ^= product[1];
}

void gf_sqr(gf c, const gf a) {
	const __m128i x = _mm_loadu_si128((const __m128i *)a);
	const __m128i low = _mm_clmulepi64_si128(x, x, 0x00);
	const __m128i high = _mm_clmulepi64_si128(x, x, 0x11);
	_mm_storeu_si128((__m128i *)c, gf128_reduce(low, high));
}

/* Compute x^(2^s) * y.  Local temporaries make the helper safe when out
 * aliases either input, as required by the fixed inversion chain below. */
static inline void gf_sqr_n_mul(gf out, const gf x, size_t s, const gf y) {
	gf squared;
	gf product;
	gf_copy(squared, x);
	for (size_t i = 0; i < s; i++) {
		gf_sqr(squared, squared);
	}
	gf_mul(product, squared, y);
	gf_copy(out, product);
}

void gf_inv(gf c, const gf a) {
	gf a3;
	gf a7;
	gf t;

	/* Let A_k = a^(2^k - 1).  This fixed Itoh--Tsujii-style chain computes
	 * A_127 with ten general multiplications and 126 squarings. */
	gf_sqr_n_mul(t, a, 1, a);      /* A_2   = A_1^(2^1)   * A_1 */
	gf_sqr_n_mul(a3, t, 1, a);     /* A_3   = A_2^(2^1)   * A_1 */
	gf_sqr_n_mul(t, a3, 3, a3);    /* A_6   = A_3^(2^3)   * A_3 */
	gf_sqr_n_mul(a7, t, 1, a);     /* A_7   = A_6^(2^1)   * A_1 */
	gf_sqr_n_mul(t, a7, 7, a7);    /* A_14  = A_7^(2^7)   * A_7 */
	gf_sqr_n_mul(t, t, 1, a);      /* A_15  = A_14^(2^1)  * A_1 */
	gf_sqr_n_mul(t, t, 15, t);     /* A_30  = A_15^(2^15) * A_15 */
	gf_sqr_n_mul(t, t, 30, t);     /* A_60  = A_30^(2^30) * A_30 */
	gf_sqr_n_mul(t, t, 60, t);     /* A_120 = A_60^(2^60) * A_60 */
	gf_sqr_n_mul(t, t, 7, a7);     /* A_127 = A_120^(2^7) * A_7 */

	/* A_127^2 = a^(2^128 - 2). */
	gf_sqr(c, t);
}

void gf_mat_vec_mul(gf c, const gf a,
                    const gf matrix[AIM3_NUM_BITS_FIELD]) {
	const uint32_t *words = (const uint32_t *)a;
	const __m256i shifts = _mm256_set_epi32(0, 2, 4, 6, 1, 3, 5, 7);
	__m256i accum0 = _mm256_setzero_si256();
	__m256i accum1 = _mm256_setzero_si256();

	for (size_t i = 0; i < 4; i++) {
		__m256i index = _mm256_set1_epi32((int)words[i]);
		for (size_t j = 32 * (i + 1); j > 32 * i; j -= 8) {
			const __m256i mask = _mm256_sllv_epi32(index, shifts);
			const __m256i row0 =
			    _mm256_loadu_si256((const __m256i *)&matrix[j - 2]);
			const __m256i row1 =
			    _mm256_loadu_si256((const __m256i *)&matrix[j - 4]);
			const __m256i row2 =
			    _mm256_loadu_si256((const __m256i *)&matrix[j - 6]);
			const __m256i row3 =
			    _mm256_loadu_si256((const __m256i *)&matrix[j - 8]);

			accum0 = _mm256_xor_si256(
			    accum0,
			    _mm256_and_si256(
			        row0, _mm256_srai_epi32(_mm256_shuffle_epi32(mask, 0xff), 31)));
			accum1 = _mm256_xor_si256(
			    accum1,
			    _mm256_and_si256(
			        row1, _mm256_srai_epi32(_mm256_shuffle_epi32(mask, 0xaa), 31)));
			accum0 = _mm256_xor_si256(
			    accum0,
			    _mm256_and_si256(
			        row2, _mm256_srai_epi32(_mm256_shuffle_epi32(mask, 0x55), 31)));
			accum1 = _mm256_xor_si256(
			    accum1,
			    _mm256_and_si256(
			        row3, _mm256_srai_epi32(_mm256_shuffle_epi32(mask, 0x00), 31)));
			index = _mm256_slli_epi32(index, 8);
		}
	}

	const __m256i accum = _mm256_xor_si256(accum0, accum1);
	_mm_storeu_si128((__m128i *)c,
	                 _mm_xor_si128(_mm256_extracti128_si256(accum, 0),
	                               _mm256_extracti128_si256(accum, 1)));
}

void gf_mat_vec_mul_add(gf c, const gf a,
                        const gf matrix[AIM3_NUM_BITS_FIELD]) {
	gf product;
	gf_mat_vec_mul(product, a, matrix);
	c[0] ^= product[0];
	c[1] ^= product[1];
}
