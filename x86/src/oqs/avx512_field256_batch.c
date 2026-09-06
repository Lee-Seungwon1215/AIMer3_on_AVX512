// SPDX-License-Identifier: MIT
// GF(2^256) batch kernels adapted from AIMer v2. Four independent parties are
// evaluated by VPCLMULQDQ per ZMM; matrix accumulation handles eight parties
// per AVX-512VL block.

#include "avx512_field128_batch.h"

#include <immintrin.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(AIM3_NUM_WORDS_FIELD == 4, "GF(2^256) requires four words");
_Static_assert(AIMER_N % 8 == 0, "AIMER_N must be a multiple of eight");

#define CL512(a, b, i) _mm512_clmulepi64_epi128((a), (b), (i))
#define XOR512(a, b) _mm512_xor_si512((a), (b))
#define SLL512(a) _mm512_bslli_epi128((a), 8)
#define SRL512(a) _mm512_bsrli_epi128((a), 8)

static inline void reduce256(__m512i *z0, __m512i *z1,
                             __m512i z2, __m512i z3) {
	const __m512i modulus =
	    _mm512_broadcast_i32x4(_mm_set_epi64x(0, 0x425));
	__m512i t0 = CL512(z2, modulus, 0x01);
	const __m512i t1 = CL512(z3, modulus, 0x00);
	const __m512i t2 = CL512(z3, modulus, 0x01);
	*z0 = XOR512(*z0, SLL512(t0));
	*z1 = XOR512(*z1, SRL512(t0));
	*z1 = XOR512(*z1, t1);
	*z1 = XOR512(*z1, SLL512(t2));
	z2 = XOR512(z2, SRL512(t2));
	t0 = CL512(z2, modulus, 0x00);
	*z0 = XOR512(*z0, t0);
}

static inline void multiply256(__m512i *z0, __m512i *z1,
                               __m512i x0, __m512i x1,
                               __m512i y0, __m512i y1) {
	__m512i t0 = CL512(x0, y0, 0x10);
	__m512i t1 = CL512(x0, y0, 0x01);
	*z0 = CL512(x0, y0, 0x00);
	*z1 = CL512(x0, y0, 0x11);
	t0 = XOR512(t0, t1);
	t1 = SRL512(t0);
	t0 = SLL512(t0);
	*z0 = XOR512(*z0, t0);
	*z1 = XOR512(*z1, t1);

	__m512i t2 = CL512(x1, y1, 0x10);
	__m512i t3 = CL512(x1, y1, 0x01);
	__m512i z2 = CL512(x1, y1, 0x00);
	__m512i z3 = CL512(x1, y1, 0x11);
	t2 = XOR512(t2, t3);
	t3 = SRL512(t2);
	t2 = SLL512(t2);
	z2 = XOR512(z2, t2);
	z3 = XOR512(z3, t3);

	x0 = XOR512(x0, x1);
	y0 = XOR512(y0, y1);
	t0 = CL512(x0, y0, 0x00);
	t1 = CL512(x0, y0, 0x11);
	t2 = CL512(x0, y0, 0x01);
	t3 = CL512(x0, y0, 0x10);
	t2 = XOR512(t2, t3);
	t3 = SRL512(t2);
	t2 = SLL512(t2);
	t0 = XOR512(t0, *z0);
	t1 = XOR512(t1, *z1);
	t2 = XOR512(t2, z2);
	t3 = XOR512(t3, z3);
	t0 = XOR512(t0, t2);
	t1 = XOR512(t1, t3);
	*z1 = XOR512(*z1, t0);
	z2 = XOR512(z2, t1);
	reduce256(z0, z1, z2, z3);
}

void gf_sqr_N(gf out[AIMER_N], const gf in[AIMER_N]) {
	uint64_t x0p[AIMER_N][2], x1p[AIMER_N][2];
	uint64_t z0p[AIMER_N][2], z1p[AIMER_N][2];
	for (size_t i = 0; i < AIMER_N; i++) {
		x0p[i][0] = in[i][0]; x0p[i][1] = in[i][1];
		x1p[i][0] = in[i][2]; x1p[i][1] = in[i][3];
	}
	for (size_t party = 0; party < AIMER_N; party += 4) {
		const __m512i x0 = _mm512_loadu_si512((const void *)x0p[party]);
		const __m512i x1 = _mm512_loadu_si512((const void *)x1p[party]);
		__m512i z0 = CL512(x0, x0, 0x00);
		__m512i z1 = CL512(x0, x0, 0x11);
		const __m512i z2 = CL512(x1, x1, 0x00);
		const __m512i z3 = CL512(x1, x1, 0x11);
		reduce256(&z0, &z1, z2, z3);
		_mm512_storeu_si512((void *)z0p[party], z0);
		_mm512_storeu_si512((void *)z1p[party], z1);
	}
	for (size_t i = 0; i < AIMER_N; i++) {
		out[i][0] = z0p[i][0]; out[i][1] = z0p[i][1];
		out[i][2] = z1p[i][0]; out[i][3] = z1p[i][1];
	}
}

void gf_mul_add_N(gf accum[AIMER_N], const gf in[AIMER_N],
                  const gf multiplier) {
	const __m512i y0 = _mm512_broadcast_i32x4(
	    _mm_loadu_si128((const __m128i *)&multiplier[0]));
	const __m512i y1 = _mm512_broadcast_i32x4(
	    _mm_loadu_si128((const __m128i *)&multiplier[2]));
	uint64_t x0p[AIMER_N][2], x1p[AIMER_N][2];
	uint64_t z0p[AIMER_N][2], z1p[AIMER_N][2];
	for (size_t i = 0; i < AIMER_N; i++) {
		x0p[i][0] = in[i][0]; x0p[i][1] = in[i][1];
		x1p[i][0] = in[i][2]; x1p[i][1] = in[i][3];
	}
	for (size_t party = 0; party < AIMER_N; party += 4) {
		const __m512i x0 = _mm512_loadu_si512((const void *)x0p[party]);
		const __m512i x1 = _mm512_loadu_si512((const void *)x1p[party]);
		__m512i z0, z1;
		multiply256(&z0, &z1, x0, x1, y0, y1);
		_mm512_storeu_si512((void *)z0p[party], z0);
		_mm512_storeu_si512((void *)z1p[party], z1);
	}
	for (size_t i = 0; i < AIMER_N; i++) {
		accum[i][0] ^= z0p[i][0]; accum[i][1] ^= z0p[i][1];
		accum[i][2] ^= z1p[i][0]; accum[i][3] ^= z1p[i][1];
	}
}

void gf_mat_vec_mul_N(gf out[AIMER_N], const gf in[AIMER_N],
                      const gf matrix[AIM3_NUM_BITS_FIELD]) {
	for (size_t party = 0; party < AIMER_N; party++) {
		out[party][0] = out[party][1] = 0;
		out[party][2] = out[party][3] = 0;
	}
	gf_mat_vec_mul_add_N(out, in, matrix);
}

void gf_mat_vec_mul_add_N(gf accum[AIMER_N], const gf in[AIMER_N],
                                const gf matrix[AIM3_NUM_BITS_FIELD])
{
  // GF(2^256) = 2 lanes, so 2 parties pack into one __m512i; 8 parties (4 regs)
  // per iteration share one broadcast of b[row].
  const __m512i idx[4] = {
    _mm512_set_epi64(4, 4, 4, 4, 0, 0, 0, 0),
    _mm512_set_epi64(5, 5, 5, 5, 1, 1, 1, 1),
    _mm512_set_epi64(6, 6, 6, 6, 2, 2, 2, 2),
    _mm512_set_epi64(7, 7, 7, 7, 3, 3, 3, 3),
  };

  for (size_t party = 0; party < AIMER_N; party += 8)
  {
    __m512i v0 = _mm512_loadu_si512((const void *)in[party]);
    __m512i v1 = _mm512_loadu_si512((const void *)in[party + 2]);
    __m512i v2 = _mm512_loadu_si512((const void *)in[party + 4]);
    __m512i v3 = _mm512_loadu_si512((const void *)in[party + 6]);
    __m512i k0 = _mm512_loadu_si512((const void *)accum[party]);
    __m512i k1 = _mm512_loadu_si512((const void *)accum[party + 2]);
    __m512i k2 = _mm512_loadu_si512((const void *)accum[party + 4]);
    __m512i k3 = _mm512_loadu_si512((const void *)accum[party + 6]);

    for (int w = 0; w < 4; w++)
    {
      __m512i h0 = _mm512_permutexvar_epi64(idx[w], v0);
      __m512i h1 = _mm512_permutexvar_epi64(idx[w], v1);
      __m512i h2 = _mm512_permutexvar_epi64(idx[w], v2);
      __m512i h3 = _mm512_permutexvar_epi64(idx[w], v3);
      const gf *brow = &matrix[64 * w];
      for (int kk = 63; kk >= 0; kk--)
      {
        __m512i bc = _mm512_broadcast_i64x4(
                       _mm256_loadu_si256((const __m256i *)brow[kk]));
        k0 = _mm512_ternarylogic_epi64(k0, _mm512_srai_epi64(h0, 63), bc, 0x78);
        k1 = _mm512_ternarylogic_epi64(k1, _mm512_srai_epi64(h1, 63), bc, 0x78);
        k2 = _mm512_ternarylogic_epi64(k2, _mm512_srai_epi64(h2, 63), bc, 0x78);
        k3 = _mm512_ternarylogic_epi64(k3, _mm512_srai_epi64(h3, 63), bc, 0x78);
        h0 = _mm512_slli_epi64(h0, 1); h1 = _mm512_slli_epi64(h1, 1);
        h2 = _mm512_slli_epi64(h2, 1); h3 = _mm512_slli_epi64(h3, 1);
      }
    }
    _mm512_storeu_si512((void *)accum[party],     k0);
    _mm512_storeu_si512((void *)accum[party + 2], k1);
    _mm512_storeu_si512((void *)accum[party + 4], k2);
    _mm512_storeu_si512((void *)accum[party + 6], k3);
  }
}

#undef CL512
#undef XOR512
#undef SLL512
#undef SRL512
