// SPDX-License-Identifier: MIT
// AIM3 GF(2^192) adapter using the PCLMUL reduction strategy from AIMer v2.
// Both implementations use little-endian 3x64-bit elements and the modulus
// x^192 + x^7 + x^2 + x + 1 (low word 0x87).

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
	a[2] = 0;
}

bool gf_is0(const gf a) {
	return (a[0] | a[1] | a[2]) == 0;
}

void gf_copy(gf out, const gf in) {
	out[0] = in[0];
	out[1] = in[1];
	out[2] = in[2];
}

void gf_add(gf c, const gf a, const gf b) {
	c[0] = a[0] ^ b[0];
	c[1] = a[1] ^ b[1];
	c[2] = a[2] ^ b[2];
}

static inline void gf192_reduce_store(gf c, __m128i z0, __m128i z1,
                                      __m128i z2) {
	const __m128i modulus = _mm_set_epi64x(0, 0x87);
	__m128i t0 = _mm_clmulepi64_si128(z2, modulus, 0x00);
	const __m128i t1 = _mm_clmulepi64_si128(z2, modulus, 0x01);
	z0 = _mm_xor_si128(z0, _mm_slli_si128(t0, 8));
	z1 = _mm_xor_si128(z1, _mm_srli_si128(t0, 8));
	z1 = _mm_xor_si128(z1, t1);
	t0 = _mm_clmulepi64_si128(z1, modulus, 0x01);
	z0 = _mm_xor_si128(z0, t0);
	_mm_storeu_si128((__m128i *)&c[0], z0);
	_mm_storel_epi64((__m128i *)&c[2], z1);
}

void gf_mul(gf c, const gf a, const gf b) {
	const __m128i x0 = _mm_loadu_si128((const __m128i *)&a[0]);
	const __m128i x1 = _mm_loadl_epi64((const __m128i *)&a[2]);
	const __m128i y0 = _mm_loadu_si128((const __m128i *)&b[0]);
	const __m128i y1 = _mm_loadl_epi64((const __m128i *)&b[2]);

	__m128i t0 = _mm_xor_si128(
	    _mm_clmulepi64_si128(x0, y0, 0x01),
	    _mm_clmulepi64_si128(x0, y0, 0x10));
	const __m128i t2 = _mm_xor_si128(
	    _mm_clmulepi64_si128(x1, y0, 0x00),
	    _mm_clmulepi64_si128(x0, y1, 0x00));
	__m128i t4 = _mm_xor_si128(
	    _mm_clmulepi64_si128(x1, y0, 0x10),
	    _mm_clmulepi64_si128(x0, y1, 0x01));
	__m128i z0 = _mm_clmulepi64_si128(x0, y0, 0x00);
	__m128i z1 = _mm_clmulepi64_si128(x0, y0, 0x11);
	__m128i z2 = _mm_clmulepi64_si128(x1, y1, 0x00);

	z0 = _mm_xor_si128(z0, _mm_slli_si128(t0, 8));
	z1 = _mm_xor_si128(z1, _mm_srli_si128(t0, 8));
	z1 = _mm_xor_si128(z1, t2);
	z1 = _mm_xor_si128(z1, _mm_slli_si128(t4, 8));
	z2 = _mm_xor_si128(z2, _mm_srli_si128(t4, 8));
	gf192_reduce_store(c, z0, z1, z2);
}

void gf_mul_add(gf c, const gf a, const gf b) {
	gf product;
	gf_mul(product, a, b);
	c[0] ^= product[0];
	c[1] ^= product[1];
	c[2] ^= product[2];
}

void gf_sqr(gf c, const gf a) {
	const __m128i x0 = _mm_loadu_si128((const __m128i *)&a[0]);
	const __m128i x1 = _mm_loadl_epi64((const __m128i *)&a[2]);
	const __m128i z0 = _mm_clmulepi64_si128(x0, x0, 0x00);
	const __m128i z1 = _mm_clmulepi64_si128(x0, x0, 0x11);
	const __m128i z2 = _mm_clmulepi64_si128(x1, x1, 0x00);
	gf192_reduce_store(c, z0, z1, z2);
}

/* Compute x^(2^s) * y.  Local temporaries keep aliasing behavior independent
 * of the multiplication and squaring kernels used by this backend. */
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
	gf a2;
	gf a3;
	gf t;

	/* Let A_k = a^(2^k - 1).  The final square maps A_191 to the
	 * required a^(2^192 - 2). */
	gf_sqr_n_mul(a2, a, 1, a);     /* A_2   = A_1^(2^1)   * A_1 */
	gf_sqr_n_mul(a3, a2, 1, a);    /* A_3   = A_2^(2^1)   * A_1 */
	gf_sqr_n_mul(t, a3, 2, a2);    /* A_5   = A_3^(2^2)   * A_2 */
	gf_sqr_n_mul(t, t, 5, t);      /* A_10  = A_5^(2^5)   * A_5 */
	gf_sqr_n_mul(t, t, 10, t);     /* A_20  = A_10^(2^10) * A_10 */
	gf_sqr_n_mul(t, t, 3, a3);     /* A_23  = A_20^(2^3)  * A_3 */
	gf_sqr_n_mul(t, t, 23, t);     /* A_46  = A_23^(2^23) * A_23 */
	gf_sqr_n_mul(t, t, 1, a);      /* A_47  = A_46^(2^1)  * A_1 */
	gf_sqr_n_mul(t, t, 47, t);     /* A_94  = A_47^(2^47) * A_47 */
	gf_sqr_n_mul(t, t, 94, t);     /* A_188 = A_94^(2^94) * A_94 */
	gf_sqr_n_mul(t, t, 3, a3);     /* A_191 = A_188^(2^3) * A_3 */

	gf_sqr(c, t);
}

void gf_mat_vec_mul(gf c, const gf a,
                    const gf matrix[AIM3_NUM_BITS_FIELD]) {
	uint64_t out0 = 0;
	uint64_t out1 = 0;
	uint64_t out2 = 0;
	for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; word++) {
		uint64_t bits = a[word];
		for (size_t bit = 0; bit < 64; bit++, bits >>= 1) {
			const uint64_t mask = 0 - (bits & 1);
			const gf *row = &matrix[64 * word + bit];
			out0 ^= (*row)[0] & mask;
			out1 ^= (*row)[1] & mask;
			out2 ^= (*row)[2] & mask;
		}
	}
	c[0] = out0;
	c[1] = out1;
	c[2] = out2;
}

void gf_mat_vec_mul_add(gf c, const gf a,
                        const gf matrix[AIM3_NUM_BITS_FIELD]) {
	gf product;
	gf_mat_vec_mul(product, a, matrix);
	c[0] ^= product[0];
	c[1] ^= product[1];
	c[2] ^= product[2];
}
