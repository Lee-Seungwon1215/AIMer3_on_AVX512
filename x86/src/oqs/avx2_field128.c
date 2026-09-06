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

void gf_inv(gf c, const gf a) {
	gf power;
	gf_copy(power, a);
	gf_set0(c);
	c[0] = 1;

	/* AIM3 requires the general a^(2^128-2) inverse. This deliberately keeps
	 * the AIM3 exponent instead of importing AIM2's specialized chain. */
	for (size_t i = 1; i < AIM3_NUM_BITS_FIELD; i++) {
		gf_sqr(power, power);
		gf_mul(c, c, power);
	}
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
