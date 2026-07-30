// SPDX-License-Identifier: MIT

#include "field.h"
#include <stddef.h>
#include <stdint.h>
#include <immintrin.h>

#define GF_BATCH 4

#define GF_ZERoupper() _mm256_zeroupper()

#define GF_IRR512 \
  _mm512_set_epi64(0, 0x425, 0, 0x425, 0, 0x425, 0, 0x425)

#define PACK_LO(a, p)                                                          \
  _mm512_set_epi64((int64_t)(a)[(p) + 3][1], (int64_t)(a)[(p) + 3][0],       \
                   (int64_t)(a)[(p) + 2][1], (int64_t)(a)[(p) + 2][0],       \
                   (int64_t)(a)[(p) + 1][1], (int64_t)(a)[(p) + 1][0],       \
                   (int64_t)(a)[(p)][1], (int64_t)(a)[(p)][0])
#define PACK_HI(a, p)                                                          \
  _mm512_set_epi64((int64_t)(a)[(p) + 3][3], (int64_t)(a)[(p) + 3][2],       \
                   (int64_t)(a)[(p) + 2][3], (int64_t)(a)[(p) + 2][2],       \
                   (int64_t)(a)[(p) + 1][3], (int64_t)(a)[(p) + 1][2],       \
                   (int64_t)(a)[(p)][3], (int64_t)(a)[(p)][2])

static inline void gf_store_lo(GF c[AIMER_N], size_t p, __m512i v)
{
  _mm_storeu_si128((__m128i *)&c[p][0], _mm512_castsi512_si128(v));
  _mm_storeu_si128((__m128i *)&c[p + 1][0], _mm512_extracti32x4_epi32(v, 1));
  _mm_storeu_si128((__m128i *)&c[p + 2][0], _mm512_extracti32x4_epi32(v, 2));
  _mm_storeu_si128((__m128i *)&c[p + 3][0], _mm512_extracti32x4_epi32(v, 3));
}

static inline void gf_store_hi(GF c[AIMER_N], size_t p, __m512i v)
{
  _mm_storeu_si128((__m128i *)&c[p][2], _mm512_castsi512_si128(v));
  _mm_storeu_si128((__m128i *)&c[p + 1][2], _mm512_extracti32x4_epi32(v, 1));
  _mm_storeu_si128((__m128i *)&c[p + 2][2], _mm512_extracti32x4_epi32(v, 2));
  _mm_storeu_si128((__m128i *)&c[p + 3][2], _mm512_extracti32x4_epi32(v, 3));
}

static inline void gf_reduce(__m512i *z0, __m512i *z1, __m512i z2, __m512i z3,
                             __m512i irr)
{
  __m512i t0 = _mm512_clmulepi64_epi128(z2, irr, 0x01);
  __m512i t1 = _mm512_clmulepi64_epi128(z3, irr, 0x00);
  __m512i t2 = _mm512_clmulepi64_epi128(z3, irr, 0x01);
  *z0 = _mm512_xor_si512(*z0, _mm512_bslli_epi128(t0, 8));
  *z1 = _mm512_xor_si512(*z1, _mm512_bsrli_epi128(t0, 8));
  *z1 = _mm512_xor_si512(*z1, t1);
  *z1 = _mm512_xor_si512(*z1, _mm512_bslli_epi128(t2, 8));
  z2 = _mm512_xor_si512(z2, _mm512_bsrli_epi128(t2, 8));
  t0 = _mm512_clmulepi64_epi128(z2, irr, 0x00);
  *z0 = _mm512_xor_si512(*z0, t0);
}

static inline void gf_karat_mul(__m512i *z0, __m512i *z1, __m512i x0, __m512i x1,
                                __m512i y0, __m512i y1, __m512i y2, __m512i irr)
{
  __m512i z2, z3, t0, t1, t2, t3;

  t0 = _mm512_clmulepi64_epi128(x0, y0, 0x10);
  t1 = _mm512_clmulepi64_epi128(x0, y0, 0x01);
  *z0 = _mm512_clmulepi64_epi128(x0, y0, 0x00);
  *z1 = _mm512_clmulepi64_epi128(x0, y0, 0x11);
  t0 = _mm512_xor_si512(t0, t1);
  t1 = _mm512_bsrli_epi128(t0, 8);
  t0 = _mm512_bslli_epi128(t0, 8);
  *z0 = _mm512_xor_si512(*z0, t0);
  *z1 = _mm512_xor_si512(*z1, t1);

  t2 = _mm512_clmulepi64_epi128(x1, y1, 0x10);
  t3 = _mm512_clmulepi64_epi128(x1, y1, 0x01);
  z2 = _mm512_clmulepi64_epi128(x1, y1, 0x00);
  z3 = _mm512_clmulepi64_epi128(x1, y1, 0x11);
  t2 = _mm512_xor_si512(t2, t3);
  t3 = _mm512_bsrli_epi128(t2, 8);
  t2 = _mm512_bslli_epi128(t2, 8);
  z2 = _mm512_xor_si512(z2, t2);
  z3 = _mm512_xor_si512(z3, t3);

  x0 = _mm512_xor_si512(x0, x1);
  t0 = _mm512_clmulepi64_epi128(x0, y2, 0x00);
  t1 = _mm512_clmulepi64_epi128(x0, y2, 0x11);
  t2 = _mm512_clmulepi64_epi128(x0, y2, 0x01);
  t3 = _mm512_clmulepi64_epi128(x0, y2, 0x10);
  t2 = _mm512_xor_si512(t2, t3);
  t3 = _mm512_bsrli_epi128(t2, 8);
  t2 = _mm512_bslli_epi128(t2, 8);
  t0 = _mm512_xor_si512(t0, *z0);
  t1 = _mm512_xor_si512(t1, *z1);
  t2 = _mm512_xor_si512(z2, t2);
  t3 = _mm512_xor_si512(z3, t3);
  t0 = _mm512_xor_si512(t0, t2);
  t1 = _mm512_xor_si512(t1, t3);
  *z1 = _mm512_xor_si512(*z1, t0);
  z2 = _mm512_xor_si512(z2, t1);

  gf_reduce(z0, z1, z2, z3, irr);
}

static inline void gf_karat_mul_add(__m512i *z0, __m512i *z1, __m512i x0,
                                    __m512i x1, __m512i y0, __m512i y1,
                                    __m512i y2, __m512i irr)
{
  __m512i z2 = _mm512_setzero_si512();
  __m512i z3 = _mm512_setzero_si512();
  __m512i t0, t1, t2, t3;

  t0 = _mm512_clmulepi64_epi128(x0, y0, 0x10);
  t1 = _mm512_clmulepi64_epi128(x0, y0, 0x01);
  t2 = _mm512_clmulepi64_epi128(x0, y0, 0x00);
  t3 = _mm512_clmulepi64_epi128(x0, y0, 0x11);
  t0 = _mm512_xor_si512(t0, t1);
  t1 = _mm512_bsrli_epi128(t0, 8);
  t0 = _mm512_bslli_epi128(t0, 8);
  t2 = _mm512_xor_si512(t2, t0);
  t3 = _mm512_xor_si512(t3, t1);
  *z0 = _mm512_xor_si512(*z0, t2);
  *z1 = _mm512_xor_si512(*z1, t3);
  *z1 = _mm512_xor_si512(*z1, t2);
  z2 = _mm512_xor_si512(z2, t3);

  t0 = _mm512_clmulepi64_epi128(x1, y1, 0x10);
  t1 = _mm512_clmulepi64_epi128(x1, y1, 0x01);
  t2 = _mm512_clmulepi64_epi128(x1, y1, 0x00);
  t3 = _mm512_clmulepi64_epi128(x1, y1, 0x11);
  t0 = _mm512_xor_si512(t0, t1);
  t1 = _mm512_bsrli_epi128(t0, 8);
  t0 = _mm512_bslli_epi128(t0, 8);
  t2 = _mm512_xor_si512(t2, t0);
  t3 = _mm512_xor_si512(t3, t1);
  *z1 = _mm512_xor_si512(*z1, t2);
  z2 = _mm512_xor_si512(z2, t3);
  z2 = _mm512_xor_si512(z2, t2);
  z3 = _mm512_xor_si512(z3, t3);

  x0 = _mm512_xor_si512(x0, x1);
  t0 = _mm512_clmulepi64_epi128(x0, y2, 0x10);
  t1 = _mm512_clmulepi64_epi128(x0, y2, 0x01);
  t2 = _mm512_clmulepi64_epi128(x0, y2, 0x00);
  t3 = _mm512_clmulepi64_epi128(x0, y2, 0x11);
  t0 = _mm512_xor_si512(t0, t1);
  t1 = _mm512_bsrli_epi128(t0, 8);
  t0 = _mm512_bslli_epi128(t0, 8);
  t2 = _mm512_xor_si512(t2, t0);
  t3 = _mm512_xor_si512(t3, t1);
  *z1 = _mm512_xor_si512(*z1, t2);
  z2 = _mm512_xor_si512(z2, t3);

  gf_reduce(z0, z1, z2, z3, irr);
}

static inline void gf_sqr_x4(__m512i *z0, __m512i *z1, __m512i x0, __m512i x1,
                             __m512i irr)
{
  __m512i z2, z3;
  *z0 = _mm512_clmulepi64_epi128(x0, x0, 0x00);
  *z1 = _mm512_clmulepi64_epi128(x0, x0, 0x11);
  z2 = _mm512_clmulepi64_epi128(x1, x1, 0x00);
  z3 = _mm512_clmulepi64_epi128(x1, x1, 0x11);
  gf_reduce(z0, z1, z2, z3, irr);
}

static inline void gf_poly_mul_add(__m512i *lo0, __m512i *lo1, __m512i *hi0,
                                   __m512i *hi1, __m512i x0, __m512i x1,
                                   __m512i y0, __m512i y1, __m512i y2)
{
  __m512i t0, t1, t2, t3;

  t0 = _mm512_clmulepi64_epi128(x0, y0, 0x10);
  t1 = _mm512_clmulepi64_epi128(x0, y0, 0x01);
  t2 = _mm512_clmulepi64_epi128(x0, y0, 0x00);
  t3 = _mm512_clmulepi64_epi128(x0, y0, 0x11);
  t0 = _mm512_xor_si512(t0, t1);
  t1 = _mm512_bsrli_epi128(t0, 8);
  t0 = _mm512_bslli_epi128(t0, 8);
  t2 = _mm512_xor_si512(t2, t0);
  t3 = _mm512_xor_si512(t3, t1);
  *lo0 = _mm512_xor_si512(*lo0, t2);
  *lo1 = _mm512_xor_si512(*lo1, t3);
  *lo1 = _mm512_xor_si512(*lo1, t2);
  *hi0 = _mm512_xor_si512(*hi0, t3);

  t0 = _mm512_clmulepi64_epi128(x1, y1, 0x10);
  t1 = _mm512_clmulepi64_epi128(x1, y1, 0x01);
  t2 = _mm512_clmulepi64_epi128(x1, y1, 0x00);
  t3 = _mm512_clmulepi64_epi128(x1, y1, 0x11);
  t0 = _mm512_xor_si512(t0, t1);
  t1 = _mm512_bsrli_epi128(t0, 8);
  t0 = _mm512_bslli_epi128(t0, 8);
  t2 = _mm512_xor_si512(t2, t0);
  t3 = _mm512_xor_si512(t3, t1);
  *lo1 = _mm512_xor_si512(*lo1, t2);
  *hi0 = _mm512_xor_si512(*hi0, t3);
  *hi0 = _mm512_xor_si512(*hi0, t2);
  *hi1 = _mm512_xor_si512(*hi1, t3);

  x0 = _mm512_xor_si512(x0, x1);
  t0 = _mm512_clmulepi64_epi128(x0, y2, 0x10);
  t1 = _mm512_clmulepi64_epi128(x0, y2, 0x01);
  t2 = _mm512_clmulepi64_epi128(x0, y2, 0x00);
  t3 = _mm512_clmulepi64_epi128(x0, y2, 0x11);
  t0 = _mm512_xor_si512(t0, t1);
  t1 = _mm512_bsrli_epi128(t0, 8);
  t0 = _mm512_bslli_epi128(t0, 8);
  t2 = _mm512_xor_si512(t2, t0);
  t3 = _mm512_xor_si512(t3, t1);
  *lo1 = _mm512_xor_si512(*lo1, t2);
  *hi0 = _mm512_xor_si512(*hi0, t3);
}

void GF_to_bytes(uint8_t *out, const GF in)
{
  int i, j;
  for (i = 0; i < AIM2_NUM_WORDS_FIELD; i++)
  {
    uint64_t u = in[i];
    for (j = 0; j < 8; j++)
    {
      *out++ = u;
      u >>= 8;
    }
  }
}

void GF_from_bytes(GF out, const uint8_t *in)
{
  int i, j;
  for (i = 0; i < AIM2_NUM_WORDS_FIELD; i++)
  {
    uint64_t u = 0;
    for (j = 7; j >= 0; j--)
    {
      u = (u << 8) | in[8 * i + j];
    }
    out[i] = u;
  }
}

void GF_set0(GF a)
{
  a[0] = 0;
  a[1] = 0;
  a[2] = 0;
  a[3] = 0;
}

void GF_copy(GF out, const GF in)
{
  out[0] = in[0];
  out[1] = in[1];
  out[2] = in[2];
  out[3] = in[3];
}

void GF_add(GF c, const GF a, const GF b)
{
  c[0] = a[0] ^ b[0];
  c[1] = a[1] ^ b[1];
  c[2] = a[2] ^ b[2];
  c[3] = a[3] ^ b[3];
}

void GF_mul(GF c, const GF a, const GF b)
{
  __m128i x[2], y[2], z[4], t[4];
  __m128i irr = _mm_set_epi64x(0x0, 0x425);

  // polynomial multiplication
  x[0] = _mm_loadu_si128((const __m128i *)&a[0]); // a0 a1
  x[1] = _mm_loadu_si128((const __m128i *)&a[2]); // a2 a3
  y[0] = _mm_loadu_si128((const __m128i *)&b[0]); // b0 b1
  y[1] = _mm_loadu_si128((const __m128i *)&b[2]); // b2 b3

  t[0] = _mm_clmulepi64_si128(x[0], y[0], 0x10);
  t[1] = _mm_clmulepi64_si128(x[0], y[0], 0x01);
  z[0] = _mm_clmulepi64_si128(x[0], y[0], 0x00);
  z[1] = _mm_clmulepi64_si128(x[0], y[0], 0x11);

  t[0] = _mm_xor_si128(t[0], t[1]);
  t[1] = _mm_srli_si128(t[0], 8);
  t[0] = _mm_slli_si128(t[0], 8);
  z[0] = _mm_xor_si128(z[0], t[0]);
  z[1] = _mm_xor_si128(z[1], t[1]);

  t[2] = _mm_clmulepi64_si128(x[1], y[1], 0x10);
  t[3] = _mm_clmulepi64_si128(x[1], y[1], 0x01);
  z[2] = _mm_clmulepi64_si128(x[1], y[1], 0x00);
  z[3] = _mm_clmulepi64_si128(x[1], y[1], 0x11);

  t[2] = _mm_xor_si128(t[2], t[3]);
  t[3] = _mm_srli_si128(t[2], 8);
  t[2] = _mm_slli_si128(t[2], 8);
  z[2] = _mm_xor_si128(z[2], t[2]);
  z[3] = _mm_xor_si128(z[3], t[3]);

  // Start Karat
  x[0] = _mm_xor_si128(x[0], x[1]);
  y[0] = _mm_xor_si128(y[0], y[1]);

  t[0] = _mm_clmulepi64_si128(x[0], y[0], 0x00);
  t[1] = _mm_clmulepi64_si128(x[0], y[0], 0x11);
  t[2] = _mm_clmulepi64_si128(x[0], y[0], 0x01);
  t[3] = _mm_clmulepi64_si128(x[0], y[0], 0x10);

  t[2] = _mm_xor_si128(t[2], t[3]);

  t[3] = _mm_srli_si128(t[2], 8);
  t[2] = _mm_slli_si128(t[2], 8);

  t[0] = _mm_xor_si128(t[0], z[0]);
  t[1] = _mm_xor_si128(t[1], z[1]);
  t[2] = _mm_xor_si128(z[2], t[2]);
  t[3] = _mm_xor_si128(z[3], t[3]);

  t[0] = _mm_xor_si128(t[0], t[2]); // t[0] = z[0] + z[2] + t[2]
  t[1] = _mm_xor_si128(t[1], t[3]); // t[1] = z[0] + z[2] + t[3]

  z[1] = _mm_xor_si128(z[1], t[0]);
  z[2] = _mm_xor_si128(z[2], t[1]);

  // modular reduction
  t[0] = _mm_clmulepi64_si128(z[2], irr, 0x01); // 2 ^ 64
  t[1] = _mm_clmulepi64_si128(z[3], irr, 0x00); // 2 ^ 128
  t[2] = _mm_clmulepi64_si128(z[3], irr, 0x01); // 2 ^ 192

  z[0] = _mm_xor_si128(z[0], _mm_slli_si128(t[0], 8));
  z[1] = _mm_xor_si128(z[1], _mm_srli_si128(t[0], 8));
  z[1] = _mm_xor_si128(z[1], t[1]);
  z[1] = _mm_xor_si128(z[1], _mm_slli_si128(t[2], 8));
  z[2] = _mm_xor_si128(z[2], _mm_srli_si128(t[2], 8));

  t[0] = _mm_clmulepi64_si128(z[2], irr, 0x00); // 2 ^ 0
  z[0] = _mm_xor_si128(z[0], t[0]);

  _mm_storeu_si128((__m128i *)&c[0], z[0]);
  _mm_storeu_si128((__m128i *)&c[2], z[1]);
}

void GF_mul_N(GF c[AIMER_N], const GF a[AIMER_N], const GF b)
{
#if AIMER_N >= 64
  __m512i y0 = _mm512_broadcast_i32x4(_mm_loadu_si128((__m128i *)&b[0]));
  __m512i y1 = _mm512_broadcast_i32x4(_mm_loadu_si128((__m128i *)&b[2]));
  __m512i y2 = _mm512_broadcast_i32x4(
      _mm_xor_si128(_mm_loadu_si128((__m128i *)&b[0]),
                    _mm_loadu_si128((__m128i *)&b[2])));
  size_t party;

  for (party = 0; party + GF_BATCH <= AIMER_N; party += GF_BATCH)
  {
    __m512i z0, z1;
    gf_karat_mul(&z0, &z1, PACK_LO(a, party), PACK_HI(a, party), y0, y1, y2,
                 GF_IRR512);
    gf_store_lo(c, party, z0);
    gf_store_hi(c, party, z1);
  }
  for (; party < AIMER_N; party++)
  {
    GF_mul(c[party], a[party], b);
  }
  GF_ZERoupper();
#else
  __m128i x[2], y[3], z[4], t[4];
  __m128i irr = _mm_set_epi64x(0x0, 0x425);

  y[0] = _mm_loadu_si128((const __m128i *)&b[0]);
  y[1] = _mm_loadu_si128((const __m128i *)&b[2]);
  y[2] = _mm_xor_si128(y[0], y[1]);

  for (size_t party = 0; party < AIMER_N; party++)
  {
    x[0] = _mm_loadu_si128((const __m128i *)&a[party][0]);
    x[1] = _mm_loadu_si128((const __m128i *)&a[party][2]);

    t[0] = _mm_clmulepi64_si128(x[0], y[0], 0x10);
    t[1] = _mm_clmulepi64_si128(x[0], y[0], 0x01);
    z[0] = _mm_clmulepi64_si128(x[0], y[0], 0x00);
    z[1] = _mm_clmulepi64_si128(x[0], y[0], 0x11);
    t[0] = _mm_xor_si128(t[0], t[1]);
    t[1] = _mm_srli_si128(t[0], 8);
    t[0] = _mm_slli_si128(t[0], 8);
    z[0] = _mm_xor_si128(z[0], t[0]);
    z[1] = _mm_xor_si128(z[1], t[1]);

    t[2] = _mm_clmulepi64_si128(x[1], y[1], 0x10);
    t[3] = _mm_clmulepi64_si128(x[1], y[1], 0x01);
    z[2] = _mm_clmulepi64_si128(x[1], y[1], 0x00);
    z[3] = _mm_clmulepi64_si128(x[1], y[1], 0x11);
    t[2] = _mm_xor_si128(t[2], t[3]);
    t[3] = _mm_srli_si128(t[2], 8);
    t[2] = _mm_slli_si128(t[2], 8);
    z[2] = _mm_xor_si128(z[2], t[2]);
    z[3] = _mm_xor_si128(z[3], t[3]);

    x[0] = _mm_xor_si128(x[0], x[1]);
    t[0] = _mm_clmulepi64_si128(x[0], y[2], 0x00);
    t[1] = _mm_clmulepi64_si128(x[0], y[2], 0x11);
    t[2] = _mm_clmulepi64_si128(x[0], y[2], 0x01);
    t[3] = _mm_clmulepi64_si128(x[0], y[2], 0x10);
    t[2] = _mm_xor_si128(t[2], t[3]);
    t[3] = _mm_srli_si128(t[2], 8);
    t[2] = _mm_slli_si128(t[2], 8);
    t[0] = _mm_xor_si128(t[0], z[0]);
    t[1] = _mm_xor_si128(t[1], z[1]);
    t[2] = _mm_xor_si128(z[2], t[2]);
    t[3] = _mm_xor_si128(z[3], t[3]);
    t[0] = _mm_xor_si128(t[0], t[2]);
    t[1] = _mm_xor_si128(t[1], t[3]);
    z[1] = _mm_xor_si128(z[1], t[0]);
    z[2] = _mm_xor_si128(z[2], t[1]);

    t[0] = _mm_clmulepi64_si128(z[2], irr, 0x01);
    t[1] = _mm_clmulepi64_si128(z[3], irr, 0x00);
    t[2] = _mm_clmulepi64_si128(z[3], irr, 0x01);
    z[0] = _mm_xor_si128(z[0], _mm_slli_si128(t[0], 8));
    z[1] = _mm_xor_si128(z[1], _mm_srli_si128(t[0], 8));
    z[1] = _mm_xor_si128(z[1], t[1]);
    z[1] = _mm_xor_si128(z[1], _mm_slli_si128(t[2], 8));
    z[2] = _mm_xor_si128(z[2], _mm_srli_si128(t[2], 8));
    t[0] = _mm_clmulepi64_si128(z[2], irr, 0x00);
    z[0] = _mm_xor_si128(z[0], t[0]);

    _mm_storeu_si128((__m128i *)&c[party][0], z[0]);
    _mm_storeu_si128((__m128i *)&c[party][2], z[1]);
  }
#endif
}

void GF_mul_add(GF c, const GF a, const GF b)
{
  __m128i x[2], y[2], z[4], t[4];
  __m128i irr = _mm_set_epi64x(0x0, 0x425);

  // polynomial multiplication
  x[0] = _mm_loadu_si128((const __m128i *)&a[0]); // a0 a1
  x[1] = _mm_loadu_si128((const __m128i *)&a[2]); // a2 a3
  y[0] = _mm_loadu_si128((const __m128i *)&b[0]); // b0 b1
  y[1] = _mm_loadu_si128((const __m128i *)&b[2]); // b2 b3
  z[0] = _mm_loadu_si128((const __m128i *)&c[0]);
  z[1] = _mm_loadu_si128((const __m128i *)&c[2]);
  z[2] = _mm_setzero_si128();
  z[3] = _mm_setzero_si128();

  // [t2 t3] = x[0] * y[0]
  t[0] = _mm_clmulepi64_si128(x[0], y[0], 0x10);
  t[1] = _mm_clmulepi64_si128(x[0], y[0], 0x01);
  t[2] = _mm_clmulepi64_si128(x[0], y[0], 0x00);
  t[3] = _mm_clmulepi64_si128(x[0], y[0], 0x11);

  t[0] = _mm_xor_si128(t[0], t[1]);
  t[1] = _mm_srli_si128(t[0], 8);
  t[0] = _mm_slli_si128(t[0], 8);
  t[2] = _mm_xor_si128(t[2], t[0]);
  t[3] = _mm_xor_si128(t[3], t[1]);

  // [z0 z1 z2 z3] += [t2 t3 0 0] + [0 t2 t3 0]
  z[0] = _mm_xor_si128(z[0], t[2]);
  z[1] = _mm_xor_si128(z[1], t[3]);
  z[1] = _mm_xor_si128(z[1], t[2]);
  z[2] = _mm_xor_si128(z[2], t[3]);

  // [t2 t3] = x[1] * y[1]
  t[0] = _mm_clmulepi64_si128(x[1], y[1], 0x10);
  t[1] = _mm_clmulepi64_si128(x[1], y[1], 0x01);
  t[2] = _mm_clmulepi64_si128(x[1], y[1], 0x00);
  t[3] = _mm_clmulepi64_si128(x[1], y[1], 0x11);

  t[0] = _mm_xor_si128(t[0], t[1]);
  t[1] = _mm_srli_si128(t[0], 8);
  t[0] = _mm_slli_si128(t[0], 8);
  t[2] = _mm_xor_si128(t[2], t[0]);
  t[3] = _mm_xor_si128(t[3], t[1]);

  // [z0 z1 z2 z3] += [0 t2 t3 0] + [0 0 t2 t3]
  z[1] = _mm_xor_si128(z[1], t[2]);
  z[2] = _mm_xor_si128(z[2], t[3]);
  z[2] = _mm_xor_si128(z[2], t[2]);
  z[3] = _mm_xor_si128(z[3], t[3]);

  // [t2 t3] = (x[0] + x[1]) * (y[0] + y[1])
  x[0] = _mm_xor_si128(x[0], x[1]);
  y[0] = _mm_xor_si128(y[0], y[1]);

  t[0] = _mm_clmulepi64_si128(x[0], y[0], 0x10);
  t[1] = _mm_clmulepi64_si128(x[0], y[0], 0x01);
  t[2] = _mm_clmulepi64_si128(x[0], y[0], 0x00);
  t[3] = _mm_clmulepi64_si128(x[0], y[0], 0x11);

  t[0] = _mm_xor_si128(t[0], t[1]);
  t[1] = _mm_srli_si128(t[0], 8);
  t[0] = _mm_slli_si128(t[0], 8);
  t[2] = _mm_xor_si128(t[2], t[0]);
  t[3] = _mm_xor_si128(t[3], t[1]);

  // [z0 z1 z2 z3] += [0 t2 t3 0]
  z[1] = _mm_xor_si128(z[1], t[2]);
  z[2] = _mm_xor_si128(z[2], t[3]);

  // modular reduction
  t[0] = _mm_clmulepi64_si128(z[2], irr, 0x01); // 2 ^ 64
  t[1] = _mm_clmulepi64_si128(z[3], irr, 0x00); // 2 ^ 128
  t[2] = _mm_clmulepi64_si128(z[3], irr, 0x01); // 2 ^ 192

  z[0] = _mm_xor_si128(z[0], _mm_slli_si128(t[0], 8));
  z[1] = _mm_xor_si128(z[1], _mm_srli_si128(t[0], 8));
  z[1] = _mm_xor_si128(z[1], t[1]);
  z[1] = _mm_xor_si128(z[1], _mm_slli_si128(t[2], 8));
  z[2] = _mm_xor_si128(z[2], _mm_srli_si128(t[2], 8));

  t[0] = _mm_clmulepi64_si128(z[2], irr, 0x00); // 2 ^ 0
  z[0] = _mm_xor_si128(z[0], t[0]);

  _mm_storeu_si128((__m128i *)&c[0], z[0]);
  _mm_storeu_si128((__m128i *)&c[2], z[1]);
}

void GF_mul_add_N(GF c[AIMER_N], const GF a[AIMER_N], const GF b)
{
#if AIMER_N >= 64
  __m512i y0 = _mm512_broadcast_i32x4(_mm_loadu_si128((__m128i *)&b[0]));
  __m512i y1 = _mm512_broadcast_i32x4(_mm_loadu_si128((__m128i *)&b[2]));
  __m512i y2 = _mm512_broadcast_i32x4(
      _mm_xor_si128(_mm_loadu_si128((__m128i *)&b[0]),
                    _mm_loadu_si128((__m128i *)&b[2])));
  size_t party;

  for (party = 0; party + GF_BATCH <= AIMER_N; party += GF_BATCH)
  {
    __m512i z0 = PACK_LO(c, party);
    __m512i z1 = PACK_HI(c, party);
    gf_karat_mul_add(&z0, &z1, PACK_LO(a, party), PACK_HI(a, party), y0, y1,
                     y2, GF_IRR512);
    gf_store_lo(c, party, z0);
    gf_store_hi(c, party, z1);
  }
  for (; party < AIMER_N; party++)
  {
    GF_mul_add(c[party], a[party], b);
  }
  GF_ZERoupper();
#else
  __m128i x[2], y[3], z[4], t[4];
  __m128i irr = _mm_set_epi64x(0x0, 0x425);

  y[0] = _mm_loadu_si128((const __m128i *)&b[0]);
  y[1] = _mm_loadu_si128((const __m128i *)&b[2]);
  y[2] = _mm_xor_si128(y[0], y[1]);

  for (size_t party = 0; party < AIMER_N; party++)
  {
    x[0] = _mm_loadu_si128((const __m128i *)&a[party][0]);
    x[1] = _mm_loadu_si128((const __m128i *)&a[party][2]);
    z[0] = _mm_loadu_si128((const __m128i *)&c[party][0]);
    z[1] = _mm_loadu_si128((const __m128i *)&c[party][2]);
    z[2] = _mm_setzero_si128();
    z[3] = _mm_setzero_si128();

    t[0] = _mm_clmulepi64_si128(x[0], y[0], 0x10);
    t[1] = _mm_clmulepi64_si128(x[0], y[0], 0x01);
    t[2] = _mm_clmulepi64_si128(x[0], y[0], 0x00);
    t[3] = _mm_clmulepi64_si128(x[0], y[0], 0x11);
    t[0] = _mm_xor_si128(t[0], t[1]);
    t[1] = _mm_srli_si128(t[0], 8);
    t[0] = _mm_slli_si128(t[0], 8);
    t[2] = _mm_xor_si128(t[2], t[0]);
    t[3] = _mm_xor_si128(t[3], t[1]);
    z[0] = _mm_xor_si128(z[0], t[2]);
    z[1] = _mm_xor_si128(z[1], t[3]);
    z[1] = _mm_xor_si128(z[1], t[2]);
    z[2] = _mm_xor_si128(z[2], t[3]);

    t[0] = _mm_clmulepi64_si128(x[1], y[1], 0x10);
    t[1] = _mm_clmulepi64_si128(x[1], y[1], 0x01);
    t[2] = _mm_clmulepi64_si128(x[1], y[1], 0x00);
    t[3] = _mm_clmulepi64_si128(x[1], y[1], 0x11);
    t[0] = _mm_xor_si128(t[0], t[1]);
    t[1] = _mm_srli_si128(t[0], 8);
    t[0] = _mm_slli_si128(t[0], 8);
    t[2] = _mm_xor_si128(t[2], t[0]);
    t[3] = _mm_xor_si128(t[3], t[1]);
    z[1] = _mm_xor_si128(z[1], t[2]);
    z[2] = _mm_xor_si128(z[2], t[3]);
    z[2] = _mm_xor_si128(z[2], t[2]);
    z[3] = _mm_xor_si128(z[3], t[3]);

    x[0] = _mm_xor_si128(x[0], x[1]);
    t[0] = _mm_clmulepi64_si128(x[0], y[2], 0x10);
    t[1] = _mm_clmulepi64_si128(x[0], y[2], 0x01);
    t[2] = _mm_clmulepi64_si128(x[0], y[2], 0x00);
    t[3] = _mm_clmulepi64_si128(x[0], y[2], 0x11);
    t[0] = _mm_xor_si128(t[0], t[1]);
    t[1] = _mm_srli_si128(t[0], 8);
    t[0] = _mm_slli_si128(t[0], 8);
    t[2] = _mm_xor_si128(t[2], t[0]);
    t[3] = _mm_xor_si128(t[3], t[1]);
    z[1] = _mm_xor_si128(z[1], t[2]);
    z[2] = _mm_xor_si128(z[2], t[3]);

    t[0] = _mm_clmulepi64_si128(z[2], irr, 0x01);
    t[1] = _mm_clmulepi64_si128(z[3], irr, 0x00);
    t[2] = _mm_clmulepi64_si128(z[3], irr, 0x01);
    z[0] = _mm_xor_si128(z[0], _mm_slli_si128(t[0], 8));
    z[1] = _mm_xor_si128(z[1], _mm_srli_si128(t[0], 8));
    z[1] = _mm_xor_si128(z[1], t[1]);
    z[1] = _mm_xor_si128(z[1], _mm_slli_si128(t[2], 8));
    z[2] = _mm_xor_si128(z[2], _mm_srli_si128(t[2], 8));
    t[0] = _mm_clmulepi64_si128(z[2], irr, 0x00);
    z[0] = _mm_xor_si128(z[0], t[0]);

    _mm_storeu_si128((__m128i *)&c[party][0], z[0]);
    _mm_storeu_si128((__m128i *)&c[party][2], z[1]);
  }
#endif
}

void GF_sqr(GF c, const GF a)
{
  __m128i x[3], z[4];
  __m128i irr = _mm_set_epi64x(0x0, 0x425);

  // polynomial multiplication
  x[0] = _mm_loadu_si128((const __m128i *)&a[0]); // a0 a1
  x[1] = _mm_loadu_si128((const __m128i *)&a[2]); // a2 a3

  z[0] = _mm_clmulepi64_si128(x[0], x[0], 0x00);
  z[1] = _mm_clmulepi64_si128(x[0], x[0], 0x11);
  z[2] = _mm_clmulepi64_si128(x[1], x[1], 0x00);
  z[3] = _mm_clmulepi64_si128(x[1], x[1], 0x11);

  // modular reduction
  x[0] = _mm_clmulepi64_si128(z[2], irr, 0x01); // 2 ^ 64
  x[1] = _mm_clmulepi64_si128(z[3], irr, 0x00); // 2 ^ 128
  x[2] = _mm_clmulepi64_si128(z[3], irr, 0x01); // 2 ^ 192

  z[0] = _mm_xor_si128(z[0], _mm_slli_si128(x[0], 8));
  z[1] = _mm_xor_si128(z[1], _mm_srli_si128(x[0], 8));
  z[1] = _mm_xor_si128(z[1], x[1]);
  z[1] = _mm_xor_si128(z[1], _mm_slli_si128(x[2], 8));
  z[2] = _mm_xor_si128(z[2], _mm_srli_si128(x[2], 8));

  x[0] = _mm_clmulepi64_si128(z[2], irr, 0x00); // 2 ^ 0
  z[0] = _mm_xor_si128(z[0], x[0]);

  _mm_storeu_si128((__m128i *)&c[0], z[0]);
  _mm_storeu_si128((__m128i *)&c[2], z[1]);
}

void GF_sqr_N(GF c[AIMER_N], const GF a[AIMER_N])
{
#if AIMER_N >= 64
  size_t party;

  for (party = 0; party + GF_BATCH <= AIMER_N; party += GF_BATCH)
  {
    __m512i z0, z1;
    gf_sqr_x4(&z0, &z1, PACK_LO(a, party), PACK_HI(a, party), GF_IRR512);
    gf_store_lo(c, party, z0);
    gf_store_hi(c, party, z1);
  }
  for (; party < AIMER_N; party++)
  {
    GF_sqr(c[party], a[party]);
  }
  GF_ZERoupper();
#else
  __m128i x[6], z[8];
  __m128i irr = _mm_set_epi64x(0x0, 0x425);

  for (size_t party = 0; party < AIMER_N; party += 2)
  {
    x[0] = _mm_loadu_si128((const __m128i *)&a[party][0]);
    x[1] = _mm_loadu_si128((const __m128i *)&a[party][2]);
    x[2] = _mm_loadu_si128((const __m128i *)&a[party + 1][0]);
    x[3] = _mm_loadu_si128((const __m128i *)&a[party + 1][2]);

    z[0] = _mm_clmulepi64_si128(x[0], x[0], 0x00);
    z[1] = _mm_clmulepi64_si128(x[0], x[0], 0x11);
    z[2] = _mm_clmulepi64_si128(x[1], x[1], 0x00);
    z[3] = _mm_clmulepi64_si128(x[1], x[1], 0x11);
    z[4] = _mm_clmulepi64_si128(x[2], x[2], 0x00);
    z[5] = _mm_clmulepi64_si128(x[2], x[2], 0x11);
    z[6] = _mm_clmulepi64_si128(x[3], x[3], 0x00);
    z[7] = _mm_clmulepi64_si128(x[3], x[3], 0x11);

    x[0] = _mm_clmulepi64_si128(z[2], irr, 0x01);
    x[1] = _mm_clmulepi64_si128(z[3], irr, 0x00);
    x[2] = _mm_clmulepi64_si128(z[3], irr, 0x01);
    x[3] = _mm_clmulepi64_si128(z[6], irr, 0x01);
    x[4] = _mm_clmulepi64_si128(z[7], irr, 0x00);
    x[5] = _mm_clmulepi64_si128(z[7], irr, 0x01);

    z[0] = _mm_xor_si128(z[0], _mm_slli_si128(x[0], 8));
    z[1] = _mm_xor_si128(z[1], _mm_srli_si128(x[0], 8));
    z[1] = _mm_xor_si128(z[1], x[1]);
    z[1] = _mm_xor_si128(z[1], _mm_slli_si128(x[2], 8));
    z[2] = _mm_xor_si128(z[2], _mm_srli_si128(x[2], 8));
    z[4] = _mm_xor_si128(z[4], _mm_slli_si128(x[3], 8));
    z[5] = _mm_xor_si128(z[5], _mm_srli_si128(x[3], 8));
    z[5] = _mm_xor_si128(z[5], x[4]);
    z[5] = _mm_xor_si128(z[5], _mm_slli_si128(x[5], 8));
    z[6] = _mm_xor_si128(z[6], _mm_srli_si128(x[5], 8));

    x[0] = _mm_clmulepi64_si128(z[2], irr, 0x00);
    x[1] = _mm_clmulepi64_si128(z[6], irr, 0x00);
    z[0] = _mm_xor_si128(z[0], x[0]);
    z[4] = _mm_xor_si128(z[4], x[1]);

    _mm_storeu_si128((__m128i *)&c[party][0], z[0]);
    _mm_storeu_si128((__m128i *)&c[party][2], z[1]);
    _mm_storeu_si128((__m128i *)&c[party + 1][0], z[4]);
    _mm_storeu_si128((__m128i *)&c[party + 1][2], z[5]);
  }
#endif
}

void GF_transposed_matmul(GF c, const GF a, const GF b[AIM2_NUM_BITS_FIELD])
{
  const __m256i shift = _mm256_set_epi64x(0, 1, 2, 3);
  const __m256i zero = _mm256_setzero_si256();

  __m256i c0 = _mm256_setzero_si256();
  __m256i c1 = _mm256_setzero_si256();
  __m256i c2 = _mm256_setzero_si256();
  __m256i c3 = _mm256_setzero_si256();
  __m256i m0, m1, m2, m3, a0, a1, a2, a3;
  __m256i mask;

  for (int i = 3; i >= 0; i--)
  {
    mask = _mm256_set1_epi64x(a[i]);
    mask = _mm256_sllv_epi64(mask, shift);
    for (int row = 64 * (i + 1); row > 64 * i; row -= 4)
    {
      m0 = _mm256_loadu_si256((const __m256i *)b[row - 4]);
      m1 = _mm256_loadu_si256((const __m256i *)b[row - 3]);
      m2 = _mm256_loadu_si256((const __m256i *)b[row - 2]);
      m3 = _mm256_loadu_si256((const __m256i *)b[row - 1]);

      a0 = _mm256_permute4x64_epi64(mask, 0x00);
      a1 = _mm256_permute4x64_epi64(mask, 0x55);
      a2 = _mm256_permute4x64_epi64(mask, 0xaa);
      a3 = _mm256_permute4x64_epi64(mask, 0xff);

      a0 = _mm256_cmpgt_epi64(zero, a0);
      a1 = _mm256_cmpgt_epi64(zero, a1);
      a2 = _mm256_cmpgt_epi64(zero, a2);
      a3 = _mm256_cmpgt_epi64(zero, a3);

      c0 = _mm256_xor_si256(c0, _mm256_and_si256(m0, a0));
      c1 = _mm256_xor_si256(c1, _mm256_and_si256(m1, a1));
      c2 = _mm256_xor_si256(c2, _mm256_and_si256(m2, a2));
      c3 = _mm256_xor_si256(c3, _mm256_and_si256(m3, a3));

      mask = _mm256_slli_epi64(mask, 4);
    }
  }
  c0 = _mm256_xor_si256(c0, c1);
  c2 = _mm256_xor_si256(c2, c3);
  c0 = _mm256_xor_si256(c0, c2);
  _mm256_storeu_si256((__m256i *)c, c0);
}

void GF_transposed_matmul_add_N(GF c[AIMER_N], const GF a[AIMER_N],
                                const GF b[AIM2_NUM_BITS_FIELD])
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
    __m512i v0 = _mm512_loadu_si512((const void *)a[party]);
    __m512i v1 = _mm512_loadu_si512((const void *)a[party + 2]);
    __m512i v2 = _mm512_loadu_si512((const void *)a[party + 4]);
    __m512i v3 = _mm512_loadu_si512((const void *)a[party + 6]);
    __m512i k0 = _mm512_loadu_si512((const void *)c[party]);
    __m512i k1 = _mm512_loadu_si512((const void *)c[party + 2]);
    __m512i k2 = _mm512_loadu_si512((const void *)c[party + 4]);
    __m512i k3 = _mm512_loadu_si512((const void *)c[party + 6]);

    for (int w = 0; w < 4; w++)
    {
      __m512i h0 = _mm512_permutexvar_epi64(idx[w], v0);
      __m512i h1 = _mm512_permutexvar_epi64(idx[w], v1);
      __m512i h2 = _mm512_permutexvar_epi64(idx[w], v2);
      __m512i h3 = _mm512_permutexvar_epi64(idx[w], v3);
      const GF *brow = &b[64 * w];
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
    _mm512_storeu_si512((void *)c[party],     k0);
    _mm512_storeu_si512((void *)c[party + 2], k1);
    _mm512_storeu_si512((void *)c[party + 4], k2);
    _mm512_storeu_si512((void *)c[party + 6], k3);
  }
}

void POLY_mul_add_N(GF lo[AIMER_N], GF hi[AIMER_N],
                    const GF a[AIMER_N], const GF b)
{
#if AIMER_N >= 64
  __m512i y0 = _mm512_broadcast_i32x4(_mm_loadu_si128((__m128i *)&b[0]));
  __m512i y1 = _mm512_broadcast_i32x4(_mm_loadu_si128((__m128i *)&b[2]));
  __m512i y2 = _mm512_broadcast_i32x4(
      _mm_xor_si128(_mm_loadu_si128((__m128i *)&b[0]),
                    _mm_loadu_si128((__m128i *)&b[2])));
  size_t party;

  for (party = 0; party + GF_BATCH <= AIMER_N; party += GF_BATCH)
  {
    __m512i lo0 = PACK_LO(lo, party);
    __m512i lo1 = PACK_HI(lo, party);
    __m512i hi0 = PACK_LO(hi, party);
    __m512i hi1 = PACK_HI(hi, party);
    gf_poly_mul_add(&lo0, &lo1, &hi0, &hi1, PACK_LO(a, party), PACK_HI(a, party),
                    y0, y1, y2);
    gf_store_lo(lo, party, lo0);
    gf_store_hi(lo, party, lo1);
    gf_store_lo(hi, party, hi0);
    gf_store_hi(hi, party, hi1);
  }
  __m128i y0s = _mm_loadu_si128((__m128i *)&b[0]);
  __m128i y1s = _mm_loadu_si128((__m128i *)&b[2]);
  __m128i y2s = _mm_xor_si128(y0s, y1s);
  for (; party < AIMER_N; party++)
  {
    __m128i x[2], z[4], t[4];
    x[0] = _mm_loadu_si128((__m128i *)&a[party][0]);
    x[1] = _mm_loadu_si128((__m128i *)&a[party][2]);
    z[0] = _mm_loadu_si128((__m128i *)&lo[party][0]);
    z[1] = _mm_loadu_si128((__m128i *)&lo[party][2]);
    z[2] = _mm_loadu_si128((__m128i *)&hi[party][0]);
    z[3] = _mm_loadu_si128((__m128i *)&hi[party][2]);

    t[0] = _mm_clmulepi64_si128(x[0], y0s, 0x10);
    t[1] = _mm_clmulepi64_si128(x[0], y0s, 0x01);
    t[2] = _mm_clmulepi64_si128(x[0], y0s, 0x00);
    t[3] = _mm_clmulepi64_si128(x[0], y0s, 0x11);
    t[0] = _mm_xor_si128(t[0], t[1]);
    t[1] = _mm_srli_si128(t[0], 8);
    t[0] = _mm_slli_si128(t[0], 8);
    t[2] = _mm_xor_si128(t[2], t[0]);
    t[3] = _mm_xor_si128(t[3], t[1]);
    z[0] = _mm_xor_si128(z[0], t[2]);
    z[1] = _mm_xor_si128(z[1], t[3]);
    z[1] = _mm_xor_si128(z[1], t[2]);
    z[2] = _mm_xor_si128(z[2], t[3]);

    t[0] = _mm_clmulepi64_si128(x[1], y1s, 0x10);
    t[1] = _mm_clmulepi64_si128(x[1], y1s, 0x01);
    t[2] = _mm_clmulepi64_si128(x[1], y1s, 0x00);
    t[3] = _mm_clmulepi64_si128(x[1], y1s, 0x11);
    t[0] = _mm_xor_si128(t[0], t[1]);
    t[1] = _mm_srli_si128(t[0], 8);
    t[0] = _mm_slli_si128(t[0], 8);
    t[2] = _mm_xor_si128(t[2], t[0]);
    t[3] = _mm_xor_si128(t[3], t[1]);
    z[1] = _mm_xor_si128(z[1], t[2]);
    z[2] = _mm_xor_si128(z[2], t[3]);
    z[2] = _mm_xor_si128(z[2], t[2]);
    z[3] = _mm_xor_si128(z[3], t[3]);

    x[0] = _mm_xor_si128(x[0], x[1]);
    t[0] = _mm_clmulepi64_si128(x[0], y2s, 0x10);
    t[1] = _mm_clmulepi64_si128(x[0], y2s, 0x01);
    t[2] = _mm_clmulepi64_si128(x[0], y2s, 0x00);
    t[3] = _mm_clmulepi64_si128(x[0], y2s, 0x11);
    t[0] = _mm_xor_si128(t[0], t[1]);
    t[1] = _mm_srli_si128(t[0], 8);
    t[0] = _mm_slli_si128(t[0], 8);
    t[2] = _mm_xor_si128(t[2], t[0]);
    t[3] = _mm_xor_si128(t[3], t[1]);
    z[1] = _mm_xor_si128(z[1], t[2]);
    z[2] = _mm_xor_si128(z[2], t[3]);

    _mm_storeu_si128((__m128i *)&lo[party][0], z[0]);
    _mm_storeu_si128((__m128i *)&lo[party][2], z[1]);
    _mm_storeu_si128((__m128i *)&hi[party][0], z[2]);
    _mm_storeu_si128((__m128i *)&hi[party][2], z[3]);
  }
  GF_ZERoupper();
#else
  __m128i x[2], y[3], z[4], t[4];

  y[0] = _mm_loadu_si128((const __m128i *)&b[0]);
  y[1] = _mm_loadu_si128((const __m128i *)&b[2]);
  y[2] = _mm_xor_si128(y[0], y[1]);

  for (size_t party = 0; party < AIMER_N; party++)
  {
    x[0] = _mm_loadu_si128((const __m128i *)&a[party][0]);
    x[1] = _mm_loadu_si128((const __m128i *)&a[party][2]);
    z[0] = _mm_loadu_si128((const __m128i *)&lo[party][0]);
    z[1] = _mm_loadu_si128((const __m128i *)&lo[party][2]);
    z[2] = _mm_loadu_si128((const __m128i *)&hi[party][0]);
    z[3] = _mm_loadu_si128((const __m128i *)&hi[party][2]);

    t[0] = _mm_clmulepi64_si128(x[0], y[0], 0x10);
    t[1] = _mm_clmulepi64_si128(x[0], y[0], 0x01);
    t[2] = _mm_clmulepi64_si128(x[0], y[0], 0x00);
    t[3] = _mm_clmulepi64_si128(x[0], y[0], 0x11);
    t[0] = _mm_xor_si128(t[0], t[1]);
    t[1] = _mm_srli_si128(t[0], 8);
    t[0] = _mm_slli_si128(t[0], 8);
    t[2] = _mm_xor_si128(t[2], t[0]);
    t[3] = _mm_xor_si128(t[3], t[1]);
    z[0] = _mm_xor_si128(z[0], t[2]);
    z[1] = _mm_xor_si128(z[1], t[3]);
    z[1] = _mm_xor_si128(z[1], t[2]);
    z[2] = _mm_xor_si128(z[2], t[3]);

    t[0] = _mm_clmulepi64_si128(x[1], y[1], 0x10);
    t[1] = _mm_clmulepi64_si128(x[1], y[1], 0x01);
    t[2] = _mm_clmulepi64_si128(x[1], y[1], 0x00);
    t[3] = _mm_clmulepi64_si128(x[1], y[1], 0x11);
    t[0] = _mm_xor_si128(t[0], t[1]);
    t[1] = _mm_srli_si128(t[0], 8);
    t[0] = _mm_slli_si128(t[0], 8);
    t[2] = _mm_xor_si128(t[2], t[0]);
    t[3] = _mm_xor_si128(t[3], t[1]);
    z[1] = _mm_xor_si128(z[1], t[2]);
    z[2] = _mm_xor_si128(z[2], t[3]);
    z[2] = _mm_xor_si128(z[2], t[2]);
    z[3] = _mm_xor_si128(z[3], t[3]);

    x[0] = _mm_xor_si128(x[0], x[1]);
    t[0] = _mm_clmulepi64_si128(x[0], y[2], 0x10);
    t[1] = _mm_clmulepi64_si128(x[0], y[2], 0x01);
    t[2] = _mm_clmulepi64_si128(x[0], y[2], 0x00);
    t[3] = _mm_clmulepi64_si128(x[0], y[2], 0x11);
    t[0] = _mm_xor_si128(t[0], t[1]);
    t[1] = _mm_srli_si128(t[0], 8);
    t[0] = _mm_slli_si128(t[0], 8);
    t[2] = _mm_xor_si128(t[2], t[0]);
    t[3] = _mm_xor_si128(t[3], t[1]);
    z[1] = _mm_xor_si128(z[1], t[2]);
    z[2] = _mm_xor_si128(z[2], t[3]);

    _mm_storeu_si128((__m128i *)&lo[party][0], z[0]);
    _mm_storeu_si128((__m128i *)&lo[party][2], z[1]);
    _mm_storeu_si128((__m128i *)&hi[party][0], z[2]);
    _mm_storeu_si128((__m128i *)&hi[party][2], z[3]);
  }
#endif
}

void POLY_red_N(GF lo[AIMER_N], const GF hi[AIMER_N])
{
#if AIMER_N >= 64
  size_t party;

  for (party = 0; party + GF_BATCH <= AIMER_N; party += GF_BATCH)
  {
    __m512i lo0 = PACK_LO(lo, party);
    __m512i lo1 = PACK_HI(lo, party);
    gf_reduce(&lo0, &lo1, PACK_LO(hi, party), PACK_HI(hi, party), GF_IRR512);
    gf_store_lo(lo, party, lo0);
    gf_store_hi(lo, party, lo1);
  }
  GF_ZERoupper();
#else
  __m128i x[6], z[8];
  __m128i irr = _mm_set_epi64x(0x0, 0x425);

  for (size_t party = 0; party < AIMER_N; party += 2)
  {
    z[0] = _mm_loadu_si128((const __m128i *)&lo[party][0]);
    z[1] = _mm_loadu_si128((const __m128i *)&lo[party][2]);
    z[2] = _mm_loadu_si128((const __m128i *)&hi[party][0]);
    z[3] = _mm_loadu_si128((const __m128i *)&hi[party][2]);
    z[4] = _mm_loadu_si128((const __m128i *)&lo[party + 1][0]);
    z[5] = _mm_loadu_si128((const __m128i *)&lo[party + 1][2]);
    z[6] = _mm_loadu_si128((const __m128i *)&hi[party + 1][0]);
    z[7] = _mm_loadu_si128((const __m128i *)&hi[party + 1][2]);

    x[0] = _mm_clmulepi64_si128(z[2], irr, 0x01);
    x[1] = _mm_clmulepi64_si128(z[3], irr, 0x00);
    x[2] = _mm_clmulepi64_si128(z[3], irr, 0x01);
    x[3] = _mm_clmulepi64_si128(z[6], irr, 0x01);
    x[4] = _mm_clmulepi64_si128(z[7], irr, 0x00);
    x[5] = _mm_clmulepi64_si128(z[7], irr, 0x01);

    z[0] = _mm_xor_si128(z[0], _mm_slli_si128(x[0], 8));
    z[1] = _mm_xor_si128(z[1], _mm_srli_si128(x[0], 8));
    z[1] = _mm_xor_si128(z[1], x[1]);
    z[1] = _mm_xor_si128(z[1], _mm_slli_si128(x[2], 8));
    z[2] = _mm_xor_si128(z[2], _mm_srli_si128(x[2], 8));
    z[4] = _mm_xor_si128(z[4], _mm_slli_si128(x[3], 8));
    z[5] = _mm_xor_si128(z[5], _mm_srli_si128(x[3], 8));
    z[5] = _mm_xor_si128(z[5], x[4]);
    z[5] = _mm_xor_si128(z[5], _mm_slli_si128(x[5], 8));
    z[6] = _mm_xor_si128(z[6], _mm_srli_si128(x[5], 8));

    x[0] = _mm_clmulepi64_si128(z[2], irr, 0x00);
    x[1] = _mm_clmulepi64_si128(z[6], irr, 0x00);
    z[0] = _mm_xor_si128(z[0], x[0]);
    z[4] = _mm_xor_si128(z[4], x[1]);

    _mm_storeu_si128((__m128i *)&lo[party][0], z[0]);
    _mm_storeu_si128((__m128i *)&lo[party][2], z[1]);
    _mm_storeu_si128((__m128i *)&lo[party + 1][0], z[4]);
    _mm_storeu_si128((__m128i *)&lo[party + 1][2], z[5]);
  }
#endif
}
