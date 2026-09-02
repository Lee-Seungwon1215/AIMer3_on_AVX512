// SPDX-License-Identifier: MIT
// GF(2^192) batch kernels adapted from AIMer v2. Four independent parties are
// evaluated by VPCLMULQDQ per ZMM; matrix accumulation handles eight parties
// per AVX-512VL block without over-reading the 24-byte field representation.

#include "avx512_field128_batch.h"

#include <immintrin.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(AIM3_NUM_WORDS_FIELD == 3, "GF(2^192) requires three words");
_Static_assert(AIMER_N % 8 == 0, "AIMER_N must be a multiple of eight");

#define CL512(a, b, i) _mm512_clmulepi64_epi128((a), (b), (i))
#define XOR512(a, b) _mm512_xor_si512((a), (b))
#define SLL512(a) _mm512_bslli_epi128((a), 8)
#define SRL512(a) _mm512_bsrli_epi128((a), 8)

static inline void reduce192(__m512i *z0, __m512i *z1, __m512i z2) {
	const __m512i modulus =
	    _mm512_broadcast_i32x4(_mm_set_epi64x(0, 0x87));
	__m512i t0 = CL512(z2, modulus, 0x00);
	const __m512i t1 = CL512(z2, modulus, 0x01);
	*z0 = XOR512(*z0, SLL512(t0));
	*z1 = XOR512(*z1, SRL512(t0));
	*z1 = XOR512(*z1, t1);
	t0 = CL512(*z1, modulus, 0x01);
	*z0 = XOR512(*z0, t0);
}

void gf_sqr_N(gf out[AIMER_N], const gf in[AIMER_N]) {
	uint64_t x0p[AIMER_N][2], x1p[AIMER_N][2];
	uint64_t z0p[AIMER_N][2], z1p[AIMER_N][2];
	for (size_t i = 0; i < AIMER_N; i++) {
		x0p[i][0] = in[i][0];
		x0p[i][1] = in[i][1];
		x1p[i][0] = in[i][2];
		x1p[i][1] = 0;
	}
	for (size_t party = 0; party < AIMER_N; party += 4) {
		const __m512i x0 = _mm512_loadu_si512((const void *)x0p[party]);
		const __m512i x1 = _mm512_loadu_si512((const void *)x1p[party]);
		__m512i z0 = CL512(x0, x0, 0x00);
		__m512i z1 = CL512(x0, x0, 0x11);
		const __m512i z2 = CL512(x1, x1, 0x00);
		reduce192(&z0, &z1, z2);
		_mm512_storeu_si512((void *)z0p[party], z0);
		_mm512_storeu_si512((void *)z1p[party], z1);
	}
	for (size_t i = 0; i < AIMER_N; i++) {
		out[i][0] = z0p[i][0];
		out[i][1] = z0p[i][1];
		out[i][2] = z1p[i][0];
	}
}

void gf_mul_add_N(gf accum[AIMER_N], const gf in[AIMER_N],
                  const gf multiplier) {
	const __m512i y0 = _mm512_broadcast_i32x4(
	    _mm_loadu_si128((const __m128i *)&multiplier[0]));
	const __m512i y1 = _mm512_broadcast_i32x4(
	    _mm_set_epi64x(0, (long long)multiplier[2]));
	uint64_t x0p[AIMER_N][2], x1p[AIMER_N][2];
	uint64_t z0p[AIMER_N][2], z1p[AIMER_N][2];
	for (size_t i = 0; i < AIMER_N; i++) {
		x0p[i][0] = in[i][0];
		x0p[i][1] = in[i][1];
		x1p[i][0] = in[i][2];
		x1p[i][1] = 0;
	}
	for (size_t party = 0; party < AIMER_N; party += 4) {
		const __m512i x0 = _mm512_loadu_si512((const void *)x0p[party]);
		const __m512i x1 = _mm512_loadu_si512((const void *)x1p[party]);
		__m512i t0 = XOR512(CL512(x0, y0, 0x01),
		                       CL512(x0, y0, 0x10));
		const __m512i t2 = XOR512(CL512(x1, y0, 0x00),
		                             CL512(x0, y1, 0x00));
		const __m512i t4 = XOR512(CL512(x1, y0, 0x10),
		                             CL512(x0, y1, 0x01));
		__m512i z0 = CL512(x0, y0, 0x00);
		__m512i z1 = CL512(x0, y0, 0x11);
		__m512i z2 = CL512(x1, y1, 0x00);
		z0 = XOR512(z0, SLL512(t0));
		z1 = XOR512(z1, SRL512(t0));
		z1 = XOR512(z1, t2);
		z1 = XOR512(z1, SLL512(t4));
		z2 = XOR512(z2, SRL512(t4));
		reduce192(&z0, &z1, z2);
		_mm512_storeu_si512((void *)z0p[party], z0);
		_mm512_storeu_si512((void *)z1p[party], z1);
	}
	for (size_t i = 0; i < AIMER_N; i++) {
		accum[i][0] ^= z0p[i][0];
		accum[i][1] ^= z0p[i][1];
		accum[i][2] ^= z1p[i][0];
	}
}

void gf_mat_vec_mul_N(gf out[AIMER_N], const gf in[AIMER_N],
                      const gf matrix[AIM3_NUM_BITS_FIELD]) {
	for (size_t party = 0; party < AIMER_N; party++) {
		out[party][0] = 0;
		out[party][1] = 0;
		out[party][2] = 0;
	}
	gf_mat_vec_mul_add_N(out, in, matrix);
}

void gf_mat_vec_mul_add_N(gf accum[AIMER_N], const gf in[AIMER_N],
                          const gf matrix[AIM3_NUM_BITS_FIELD]) {
	const __mmask8 three_words = 0x07;
	for (size_t party = 0; party < AIMER_N; party += 8) {
		__m256i out0 = _mm256_maskz_loadu_epi64(three_words, accum[party]);
		__m256i out1 = _mm256_maskz_loadu_epi64(three_words, accum[party + 1]);
		__m256i out2 = _mm256_maskz_loadu_epi64(three_words, accum[party + 2]);
		__m256i out3 = _mm256_maskz_loadu_epi64(three_words, accum[party + 3]);
		__m256i out4 = _mm256_maskz_loadu_epi64(three_words, accum[party + 4]);
		__m256i out5 = _mm256_maskz_loadu_epi64(three_words, accum[party + 5]);
		__m256i out6 = _mm256_maskz_loadu_epi64(three_words, accum[party + 6]);
		__m256i out7 = _mm256_maskz_loadu_epi64(three_words, accum[party + 7]);

		for (size_t word = 0; word < 3; word++) {
			__m256i bits0 = _mm256_set1_epi64x((long long)in[party][word]);
			__m256i bits1 = _mm256_set1_epi64x((long long)in[party + 1][word]);
			__m256i bits2 = _mm256_set1_epi64x((long long)in[party + 2][word]);
			__m256i bits3 = _mm256_set1_epi64x((long long)in[party + 3][word]);
			__m256i bits4 = _mm256_set1_epi64x((long long)in[party + 4][word]);
			__m256i bits5 = _mm256_set1_epi64x((long long)in[party + 5][word]);
			__m256i bits6 = _mm256_set1_epi64x((long long)in[party + 6][word]);
			__m256i bits7 = _mm256_set1_epi64x((long long)in[party + 7][word]);
			const gf *rows = &matrix[64 * word];
			for (int bit = 63; bit >= 0; bit--) {
				const __m256i row =
				    _mm256_maskz_loadu_epi64(three_words, rows[bit]);
				out0 = _mm256_ternarylogic_epi64(
				    out0, _mm256_srai_epi64(bits0, 63), row, 0x78);
				out1 = _mm256_ternarylogic_epi64(
				    out1, _mm256_srai_epi64(bits1, 63), row, 0x78);
				out2 = _mm256_ternarylogic_epi64(
				    out2, _mm256_srai_epi64(bits2, 63), row, 0x78);
				out3 = _mm256_ternarylogic_epi64(
				    out3, _mm256_srai_epi64(bits3, 63), row, 0x78);
				out4 = _mm256_ternarylogic_epi64(
				    out4, _mm256_srai_epi64(bits4, 63), row, 0x78);
				out5 = _mm256_ternarylogic_epi64(
				    out5, _mm256_srai_epi64(bits5, 63), row, 0x78);
				out6 = _mm256_ternarylogic_epi64(
				    out6, _mm256_srai_epi64(bits6, 63), row, 0x78);
				out7 = _mm256_ternarylogic_epi64(
				    out7, _mm256_srai_epi64(bits7, 63), row, 0x78);
				bits0 = _mm256_slli_epi64(bits0, 1);
				bits1 = _mm256_slli_epi64(bits1, 1);
				bits2 = _mm256_slli_epi64(bits2, 1);
				bits3 = _mm256_slli_epi64(bits3, 1);
				bits4 = _mm256_slli_epi64(bits4, 1);
				bits5 = _mm256_slli_epi64(bits5, 1);
				bits6 = _mm256_slli_epi64(bits6, 1);
				bits7 = _mm256_slli_epi64(bits7, 1);
			}
		}
		_mm256_mask_storeu_epi64(accum[party], three_words, out0);
		_mm256_mask_storeu_epi64(accum[party + 1], three_words, out1);
		_mm256_mask_storeu_epi64(accum[party + 2], three_words, out2);
		_mm256_mask_storeu_epi64(accum[party + 3], three_words, out3);
		_mm256_mask_storeu_epi64(accum[party + 4], three_words, out4);
		_mm256_mask_storeu_epi64(accum[party + 5], three_words, out5);
		_mm256_mask_storeu_epi64(accum[party + 6], three_words, out6);
		_mm256_mask_storeu_epi64(accum[party + 7], three_words, out7);
	}
}

#undef CL512
#undef XOR512
#undef SLL512
#undef SRL512
