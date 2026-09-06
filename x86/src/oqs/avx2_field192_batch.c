// SPDX-License-Identifier: MIT
// AVX2 GF(2^192) party batching. PCLMUL remains an XMM operation, so two
// independent parties are interleaved; YMM registers hold each 192-bit row.

#include "avx2_field128_batch.h"

#include <immintrin.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(AIM3_NUM_WORDS_FIELD == 3, "GF(2^192) requires three words");
_Static_assert(AIMER_N % 4 == 0, "AIMER_N must be a multiple of four");

static inline void reduce192(gf out, __m128i z0, __m128i z1, __m128i z2) {
	const __m128i modulus = _mm_set_epi64x(0, 0x87);
	__m128i t0 = _mm_clmulepi64_si128(z2, modulus, 0x00);
	const __m128i t1 = _mm_clmulepi64_si128(z2, modulus, 0x01);
	z0 = _mm_xor_si128(z0, _mm_slli_si128(t0, 8));
	z1 = _mm_xor_si128(z1, _mm_srli_si128(t0, 8));
	z1 = _mm_xor_si128(z1, t1);
	t0 = _mm_clmulepi64_si128(z1, modulus, 0x01);
	z0 = _mm_xor_si128(z0, t0);
	_mm_storeu_si128((__m128i *)&out[0], z0);
	_mm_storel_epi64((__m128i *)&out[2], z1);
}

static inline void square192(gf out, const gf in) {
	const __m128i x0 = _mm_loadu_si128((const __m128i *)&in[0]);
	const __m128i x1 = _mm_loadl_epi64((const __m128i *)&in[2]);
	const __m128i z0 = _mm_clmulepi64_si128(x0, x0, 0x00);
	const __m128i z1 = _mm_clmulepi64_si128(x0, x0, 0x11);
	const __m128i z2 = _mm_clmulepi64_si128(x1, x1, 0x00);
	reduce192(out, z0, z1, z2);
}

static inline void multiply192(gf out, const gf a, const gf b) {
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
	reduce192(out, z0, z1, z2);
}

void gf_sqr_N(gf out[AIMER_N], const gf in[AIMER_N]) {
	for (size_t party = 0; party < AIMER_N; party += 2) {
		square192(out[party], in[party]);
		square192(out[party + 1], in[party + 1]);
	}
}

void gf_mul_add_N(gf accum[AIMER_N], const gf in[AIMER_N],
                  const gf multiplier) {
	for (size_t party = 0; party < AIMER_N; party += 2) {
		gf product0;
		gf product1;
		multiply192(product0, in[party], multiplier);
		multiply192(product1, in[party + 1], multiplier);
		for (size_t word = 0; word < 3; word++) {
			accum[party][word] ^= product0[word];
			accum[party + 1][word] ^= product1[word];
		}
	}
}

static inline __m256i load192(const gf value) {
	return _mm256_set_epi64x(0, (long long)value[2],
	                         (long long)value[1], (long long)value[0]);
}

static inline void store192(gf value, __m256i vector) {
	_mm_storeu_si128((__m128i *)&value[0], _mm256_castsi256_si128(vector));
	value[2] = (uint64_t)_mm256_extract_epi64(vector, 2);
}

void gf_mat_vec_mul_N(gf out[AIMER_N], const gf in[AIMER_N],
                      const gf matrix[AIM3_NUM_BITS_FIELD]) {
	for (size_t party = 0; party < AIMER_N; party++) {
		out[party][0] = out[party][1] = out[party][2] = 0;
	}
	gf_mat_vec_mul_add_N(out, in, matrix);
}

void gf_mat_vec_mul_add_N(gf accum[AIMER_N], const gf in[AIMER_N],
                          const gf matrix[AIM3_NUM_BITS_FIELD]) {
	const __m256i zero = _mm256_setzero_si256();
	for (size_t party = 0; party < AIMER_N; party += 4) {
		__m256i out[4] = {
		    load192(accum[party]), load192(accum[party + 1]),
		    load192(accum[party + 2]), load192(accum[party + 3]),
		};
		for (size_t word = 0; word < 3; word++) {
			__m256i bits[4] = {
			    _mm256_set1_epi64x((long long)in[party][word]),
			    _mm256_set1_epi64x((long long)in[party + 1][word]),
			    _mm256_set1_epi64x((long long)in[party + 2][word]),
			    _mm256_set1_epi64x((long long)in[party + 3][word]),
			};
			const gf *rows = &matrix[64 * word];
			for (int bit = 63; bit >= 0; bit--) {
				const __m256i row = load192(rows[bit]);
				for (size_t lane = 0; lane < 4; lane++) {
					const __m256i mask =
					    _mm256_cmpgt_epi64(zero, bits[lane]);
					out[lane] = _mm256_xor_si256(
					    out[lane], _mm256_and_si256(mask, row));
					bits[lane] = _mm256_slli_epi64(bits[lane], 1);
				}
			}
		}
		store192(accum[party], out[0]);
		store192(accum[party + 1], out[1]);
		store192(accum[party + 2], out[2]);
		store192(accum[party + 3], out[3]);
	}
}
