// SPDX-License-Identifier: MIT
// AIM3 GF(2^256) adapter using the PCLMUL reduction strategy from AIMer v2.
// Both implementations use little-endian 4x64-bit elements and the modulus
// x^256 + x^10 + x^5 + x^2 + 1 (low word 0x425).

#include "field.h"

#include <immintrin.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(AIM3_NUM_WORDS_FIELD == 4, "GF(2^256) requires four words");

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
	a[0] = a[1] = a[2] = a[3] = 0;
}

bool gf_is0(const gf a) {
	return (a[0] | a[1] | a[2] | a[3]) == 0;
}

void gf_copy(gf out, const gf in) {
	out[0] = in[0];
	out[1] = in[1];
	out[2] = in[2];
	out[3] = in[3];
}

void gf_add(gf c, const gf a, const gf b) {
	c[0] = a[0] ^ b[0];
	c[1] = a[1] ^ b[1];
	c[2] = a[2] ^ b[2];
	c[3] = a[3] ^ b[3];
}

static inline void gf256_reduce_store(gf c, __m128i z0, __m128i z1,
                                      __m128i z2, __m128i z3) {
	const __m128i modulus = _mm_set_epi64x(0, 0x425);
	__m128i t0 = _mm_clmulepi64_si128(z2, modulus, 0x01);
	const __m128i t1 = _mm_clmulepi64_si128(z3, modulus, 0x00);
	const __m128i t2 = _mm_clmulepi64_si128(z3, modulus, 0x01);
	z0 = _mm_xor_si128(z0, _mm_slli_si128(t0, 8));
	z1 = _mm_xor_si128(z1, _mm_srli_si128(t0, 8));
	z1 = _mm_xor_si128(z1, t1);
	z1 = _mm_xor_si128(z1, _mm_slli_si128(t2, 8));
	z2 = _mm_xor_si128(z2, _mm_srli_si128(t2, 8));
	t0 = _mm_clmulepi64_si128(z2, modulus, 0x00);
	z0 = _mm_xor_si128(z0, t0);
	_mm_storeu_si128((__m128i *)&c[0], z0);
	_mm_storeu_si128((__m128i *)&c[2], z1);
}

void gf_mul(gf c, const gf a, const gf b) {
	__m128i x0 = _mm_loadu_si128((const __m128i *)&a[0]);
	const __m128i x1 = _mm_loadu_si128((const __m128i *)&a[2]);
	__m128i y0 = _mm_loadu_si128((const __m128i *)&b[0]);
	const __m128i y1 = _mm_loadu_si128((const __m128i *)&b[2]);

	__m128i t0 = _mm_clmulepi64_si128(x0, y0, 0x10);
	__m128i t1 = _mm_clmulepi64_si128(x0, y0, 0x01);
	__m128i z0 = _mm_clmulepi64_si128(x0, y0, 0x00);
	__m128i z1 = _mm_clmulepi64_si128(x0, y0, 0x11);
	t0 = _mm_xor_si128(t0, t1);
	t1 = _mm_srli_si128(t0, 8);
	t0 = _mm_slli_si128(t0, 8);
	z0 = _mm_xor_si128(z0, t0);
	z1 = _mm_xor_si128(z1, t1);

	__m128i t2 = _mm_clmulepi64_si128(x1, y1, 0x10);
	__m128i t3 = _mm_clmulepi64_si128(x1, y1, 0x01);
	__m128i z2 = _mm_clmulepi64_si128(x1, y1, 0x00);
	__m128i z3 = _mm_clmulepi64_si128(x1, y1, 0x11);
	t2 = _mm_xor_si128(t2, t3);
	t3 = _mm_srli_si128(t2, 8);
	t2 = _mm_slli_si128(t2, 8);
	z2 = _mm_xor_si128(z2, t2);
	z3 = _mm_xor_si128(z3, t3);

	x0 = _mm_xor_si128(x0, x1);
	y0 = _mm_xor_si128(y0, y1);
	t0 = _mm_clmulepi64_si128(x0, y0, 0x00);
	t1 = _mm_clmulepi64_si128(x0, y0, 0x11);
	t2 = _mm_clmulepi64_si128(x0, y0, 0x01);
	t3 = _mm_clmulepi64_si128(x0, y0, 0x10);
	t2 = _mm_xor_si128(t2, t3);
	t3 = _mm_srli_si128(t2, 8);
	t2 = _mm_slli_si128(t2, 8);
	t0 = _mm_xor_si128(t0, z0);
	t1 = _mm_xor_si128(t1, z1);
	t2 = _mm_xor_si128(t2, z2);
	t3 = _mm_xor_si128(t3, z3);
	t0 = _mm_xor_si128(t0, t2);
	t1 = _mm_xor_si128(t1, t3);
	z1 = _mm_xor_si128(z1, t0);
	z2 = _mm_xor_si128(z2, t1);

	gf256_reduce_store(c, z0, z1, z2, z3);
}

void gf_mul_add(gf c, const gf a, const gf b) {
	gf product;
	gf_mul(product, a, b);
	c[0] ^= product[0];
	c[1] ^= product[1];
	c[2] ^= product[2];
	c[3] ^= product[3];
}

void gf_sqr(gf c, const gf a) {
	const __m128i x0 = _mm_loadu_si128((const __m128i *)&a[0]);
	const __m128i x1 = _mm_loadu_si128((const __m128i *)&a[2]);
	const __m128i z0 = _mm_clmulepi64_si128(x0, x0, 0x00);
	const __m128i z1 = _mm_clmulepi64_si128(x0, x0, 0x11);
	const __m128i z2 = _mm_clmulepi64_si128(x1, x1, 0x00);
	const __m128i z3 = _mm_clmulepi64_si128(x1, x1, 0x11);
	gf256_reduce_store(c, z0, z1, z2, z3);
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

	/* Let A_k = a^(2^k - 1).  Preserve A_5 in a2 and A_15 in a3 for
	 * the two non-doubling links in the fixed chain. */
	gf_sqr_n_mul(a2, a, 1, a);     /* A_2   = A_1^(2^1)   * A_1 */
	gf_sqr_n_mul(a3, a2, 1, a);    /* A_3   = A_2^(2^1)   * A_1 */
	gf_sqr_n_mul(a2, a3, 2, a2);   /* A_5   = A_3^(2^2)   * A_2 */
	gf_sqr_n_mul(t, a2, 5, a2);    /* A_10  = A_5^(2^5)   * A_5 */
	gf_sqr_n_mul(a3, t, 5, a2);    /* A_15  = A_10^(2^5)  * A_5 */
	gf_sqr_n_mul(t, a3, 15, a3);   /* A_30  = A_15^(2^15) * A_15 */
	gf_sqr_n_mul(t, t, 30, t);     /* A_60  = A_30^(2^30) * A_30 */
	gf_sqr_n_mul(t, t, 60, t);     /* A_120 = A_60^(2^60) * A_60 */
	gf_sqr_n_mul(t, t, 120, t);    /* A_240 = A_120^(2^120) * A_120 */
	gf_sqr_n_mul(t, t, 15, a3);    /* A_255 = A_240^(2^15) * A_15 */

	/* A_255^2 = a^(2^256 - 2). */
	gf_sqr(c, t);
}

void gf_mat_vec_mul(gf c, const gf a,
                    const gf matrix[AIM3_NUM_BITS_FIELD]) {
	uint64_t out[4] = {0, 0, 0, 0};
	for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; word++) {
		uint64_t bits = a[word];
		for (size_t bit = 0; bit < 64; bit++, bits >>= 1) {
			const uint64_t mask = 0 - (bits & 1);
			const gf *row = &matrix[64 * word + bit];
			out[0] ^= (*row)[0] & mask;
			out[1] ^= (*row)[1] & mask;
			out[2] ^= (*row)[2] & mask;
			out[3] ^= (*row)[3] & mask;
		}
	}
	gf_copy(c, out);
}

void gf_mat_vec_mul_add(gf c, const gf a,
                        const gf matrix[AIM3_NUM_BITS_FIELD]) {
	gf product;
	gf_mat_vec_mul(product, a, matrix);
	c[0] ^= product[0];
	c[1] ^= product[1];
	c[2] ^= product[2];
	c[3] ^= product[3];
}
