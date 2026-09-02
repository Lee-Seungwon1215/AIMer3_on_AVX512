// SPDX-License-Identifier: MIT

#include "field.h"
#include <stddef.h>
#include <stdint.h>
#include <immintrin.h>

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
}

void GF_copy(GF out, const GF in)
{
  out[0] = in[0];
  out[1] = in[1];
  out[2] = in[2];
}

void GF_add(GF c, const GF a, const GF b)
{
  c[0] = a[0] ^ b[0];
  c[1] = a[1] ^ b[1];
  c[2] = a[2] ^ b[2];
}

void GF_mul(GF c, const GF a, const GF b)
{
  __m128i x[2], y[2], t[6], z[3];
  __m128i irr = _mm_set_epi64x(0x0, 0x87);

  // polynomial multiplication
  x[0] = _mm_loadu_si128((const __m128i *)&a[0]); // a0 a1
  x[1] = _mm_loadl_epi64((const __m128i *)&a[2]); // a2 -
  y[0] = _mm_loadu_si128((const __m128i *)&b[0]); // b0 b1
  y[1] = _mm_loadl_epi64((const __m128i *)&b[2]); // b2 -

  t[0] = _mm_clmulepi64_si128(x[0], y[0], 0x01);
  t[1] = _mm_clmulepi64_si128(x[0], y[0], 0x10);

  t[2] = _mm_clmulepi64_si128(x[1], y[0], 0x00);
  t[3] = _mm_clmulepi64_si128(x[0], y[1], 0x00);

  t[4] = _mm_clmulepi64_si128(x[1], y[0], 0x10);
  t[5] = _mm_clmulepi64_si128(x[0], y[1], 0x01);

  z[0] = _mm_clmulepi64_si128(x[0], y[0], 0x00);
  z[1] = _mm_clmulepi64_si128(x[0], y[0], 0x11);
  z[2] = _mm_clmulepi64_si128(x[1], y[1], 0x00);

  t[0] = _mm_xor_si128(t[0], t[1]);
  t[2] = _mm_xor_si128(t[2], t[3]);
  t[4] = _mm_xor_si128(t[4], t[5]);

  t[1] = _mm_srli_si128(t[0], 8);
  t[0] = _mm_slli_si128(t[0], 8);

  t[5] = _mm_srli_si128(t[4], 8);
  t[4] = _mm_slli_si128(t[4], 8);

  z[0] = _mm_xor_si128(z[0], t[0]);
  z[1] = _mm_xor_si128(z[1], t[1]);
  z[1] = _mm_xor_si128(z[1], t[2]);
  z[1] = _mm_xor_si128(z[1], t[4]);
  z[2] = _mm_xor_si128(z[2], t[5]);

  // modular reduction
  t[0] = _mm_clmulepi64_si128(z[2], irr, 0x00); // 2 ^ 64
  t[1] = _mm_clmulepi64_si128(z[2], irr, 0x01); // 2 ^ 128

  z[0] = _mm_xor_si128(z[0], _mm_slli_si128(t[0], 8));
  z[1] = _mm_xor_si128(z[1], _mm_srli_si128(t[0], 8));
  z[1] = _mm_xor_si128(z[1], t[1]);

  t[0] = _mm_clmulepi64_si128(z[1], irr, 0x01); // 2 ^ 0
  z[0] = _mm_xor_si128(z[0], t[0]);

  _mm_storeu_si128((__m128i *)&c[0], z[0]);
  _mm_storel_epi64((__m128i *)&c[2], z[1]);
}

// ---- AVX-512 / VPCLMULQDQ helpers for GF(2^192): 4 parties per 512-bit reg ----
// _mm512_clmulepi64_epi128 does 4 independent 64x64->128 carry-less products
// (one per 128-bit lane). Each 192-bit element is carried as x0 lane = {a0,a1}
// and x1 lane = {a2,0}. We transpose the AoS GF[3] party array into lane-packed
// scratch, run the (per-lane identical) AVX2 schoolbook + reduction on 4 parties
// at once, then transpose the result back. Reduction poly 0x87.
#define CL512(a, b, i) _mm512_clmulepi64_epi128((a), (b), (i))
#define XX512(a, b)    _mm512_xor_si512((a), (b))
#define SLL512(a)      _mm512_bslli_epi128((a), 8)
#define SRL512(a)      _mm512_bsrli_epi128((a), 8)

void GF_mul_N(GF c[AIMER_N], const GF a[AIMER_N], const GF b)
{
  const __m512i irr = _mm512_broadcast_i32x4(_mm_set_epi64x(0x0, 0x87));
  const __m512i Y0  = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i *)&b[0])); // {b0,b1}
  const __m512i Y1  = _mm512_broadcast_i32x4(_mm_set_epi64x(0x0, (long long)b[2]));     // {b2,0}

  uint64_t x0p[AIMER_N][2], x1p[AIMER_N][2], z0p[AIMER_N][2], z1p[AIMER_N][2];
  for (size_t i = 0; i < AIMER_N; i++)
  {
    x0p[i][0] = a[i][0]; x0p[i][1] = a[i][1];
    x1p[i][0] = a[i][2]; x1p[i][1] = 0;
  }

  for (size_t p = 0; p < AIMER_N; p += 4)
  {
    __m512i x0 = _mm512_loadu_si512((const void *)x0p[p]);
    __m512i x1 = _mm512_loadu_si512((const void *)x1p[p]);

    __m512i t0 = CL512(x0, Y0, 0x01), t1 = CL512(x0, Y0, 0x10);
    __m512i t2 = CL512(x1, Y0, 0x00), t3 = CL512(x0, Y1, 0x00);
    __m512i t4 = CL512(x1, Y0, 0x10), t5 = CL512(x0, Y1, 0x01);
    __m512i z0 = CL512(x0, Y0, 0x00);
    __m512i z1 = CL512(x0, Y0, 0x11);
    __m512i z2 = CL512(x1, Y1, 0x00);

    t0 = XX512(t0, t1); t2 = XX512(t2, t3); t4 = XX512(t4, t5);
    t1 = SRL512(t0); t0 = SLL512(t0);
    t5 = SRL512(t4); t4 = SLL512(t4);
    z0 = XX512(z0, t0);
    z1 = XX512(z1, t1); z1 = XX512(z1, t2); z1 = XX512(z1, t4);
    z2 = XX512(z2, t5);

    // modular reduction
    t0 = CL512(z2, irr, 0x00);
    t1 = CL512(z2, irr, 0x01);
    z0 = XX512(z0, SLL512(t0));
    z1 = XX512(z1, SRL512(t0));
    z1 = XX512(z1, t1);
    t0 = CL512(z1, irr, 0x01);
    z0 = XX512(z0, t0);

    _mm512_storeu_si512((void *)z0p[p], z0);
    _mm512_storeu_si512((void *)z1p[p], z1);
  }

  for (size_t i = 0; i < AIMER_N; i++)
  {
    c[i][0] = z0p[i][0]; c[i][1] = z0p[i][1]; c[i][2] = z1p[i][0];
  }
}

void GF_mul_add(GF c, const GF a, const GF b)
{
  __m128i x[2], y[2], t[6], z[3];
  __m128i irr = _mm_set_epi64x(0x0, 0x87);

  // polynomial multiplication
  x[0] = _mm_loadu_si128((const __m128i *)&a[0]); // a0 a1
  x[1] = _mm_loadl_epi64((const __m128i *)&a[2]); // a2 -
  y[0] = _mm_loadu_si128((const __m128i *)&b[0]); // b0 b1
  y[1] = _mm_loadl_epi64((const __m128i *)&b[2]); // b2 -
  z[0] = _mm_loadu_si128((const __m128i *)&c[0]);
  z[1] = _mm_loadl_epi64((const __m128i *)&c[2]);

  t[0] = _mm_clmulepi64_si128(x[0], y[0], 0x01);
  t[1] = _mm_clmulepi64_si128(x[0], y[0], 0x10);

  t[2] = _mm_clmulepi64_si128(x[1], y[0], 0x00);
  t[3] = _mm_clmulepi64_si128(x[0], y[1], 0x00);

  t[4] = _mm_clmulepi64_si128(x[1], y[0], 0x10);
  t[5] = _mm_clmulepi64_si128(x[0], y[1], 0x01);

  z[2] = _mm_clmulepi64_si128(x[1], y[1], 0x00);
  z[0] = _mm_xor_si128(z[0], _mm_clmulepi64_si128(x[0], y[0], 0x00));
  z[1] = _mm_xor_si128(z[1], _mm_clmulepi64_si128(x[0], y[0], 0x11));

  t[0] = _mm_xor_si128(t[0], t[1]);
  t[2] = _mm_xor_si128(t[2], t[3]);
  t[4] = _mm_xor_si128(t[4], t[5]);

  t[1] = _mm_srli_si128(t[0], 8);
  t[0] = _mm_slli_si128(t[0], 8);

  t[5] = _mm_srli_si128(t[4], 8);
  t[4] = _mm_slli_si128(t[4], 8);

  z[0] = _mm_xor_si128(z[0], t[0]);
  z[1] = _mm_xor_si128(z[1], t[1]);
  z[1] = _mm_xor_si128(z[1], t[2]);
  z[1] = _mm_xor_si128(z[1], t[4]);
  z[2] = _mm_xor_si128(z[2], t[5]);

  // modular reduction
  t[0] = _mm_clmulepi64_si128(z[2], irr, 0x00); // 2 ^ 64
  t[1] = _mm_clmulepi64_si128(z[2], irr, 0x01); // 2 ^ 128

  z[0] = _mm_xor_si128(z[0], _mm_slli_si128(t[0], 8));
  z[1] = _mm_xor_si128(z[1], _mm_srli_si128(t[0], 8));
  z[1] = _mm_xor_si128(z[1], t[1]);

  t[0] = _mm_clmulepi64_si128(z[1], irr, 0x01); // 2 ^ 0
  z[0] = _mm_xor_si128(z[0], t[0]);

  _mm_storeu_si128((__m128i *)&c[0], z[0]);
  _mm_storel_epi64((__m128i *)&c[2], z[1]);
}

// c[i] ^= a[i] * b  (4 parties per 512-bit register)
void GF_mul_add_N(GF c[AIMER_N], const GF a[AIMER_N], const GF b)
{
  const __m512i irr = _mm512_broadcast_i32x4(_mm_set_epi64x(0x0, 0x87));
  const __m512i Y0  = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i *)&b[0]));
  const __m512i Y1  = _mm512_broadcast_i32x4(_mm_set_epi64x(0x0, (long long)b[2]));

  uint64_t x0p[AIMER_N][2], x1p[AIMER_N][2], z0p[AIMER_N][2], z1p[AIMER_N][2];
  for (size_t i = 0; i < AIMER_N; i++)
  {
    x0p[i][0] = a[i][0]; x0p[i][1] = a[i][1];
    x1p[i][0] = a[i][2]; x1p[i][1] = 0;
  }

  for (size_t p = 0; p < AIMER_N; p += 4)
  {
    __m512i x0 = _mm512_loadu_si512((const void *)x0p[p]);
    __m512i x1 = _mm512_loadu_si512((const void *)x1p[p]);

    __m512i t0 = CL512(x0, Y0, 0x01), t1 = CL512(x0, Y0, 0x10);
    __m512i t2 = CL512(x1, Y0, 0x00), t3 = CL512(x0, Y1, 0x00);
    __m512i t4 = CL512(x1, Y0, 0x10), t5 = CL512(x0, Y1, 0x01);
    __m512i z0 = CL512(x0, Y0, 0x00);
    __m512i z1 = CL512(x0, Y0, 0x11);
    __m512i z2 = CL512(x1, Y1, 0x00);

    t0 = XX512(t0, t1); t2 = XX512(t2, t3); t4 = XX512(t4, t5);
    t1 = SRL512(t0); t0 = SLL512(t0);
    t5 = SRL512(t4); t4 = SLL512(t4);
    z0 = XX512(z0, t0);
    z1 = XX512(z1, t1); z1 = XX512(z1, t2); z1 = XX512(z1, t4);
    z2 = XX512(z2, t5);

    t0 = CL512(z2, irr, 0x00);
    t1 = CL512(z2, irr, 0x01);
    z0 = XX512(z0, SLL512(t0));
    z1 = XX512(z1, SRL512(t0));
    z1 = XX512(z1, t1);
    t0 = CL512(z1, irr, 0x01);
    z0 = XX512(z0, t0);

    _mm512_storeu_si512((void *)z0p[p], z0);
    _mm512_storeu_si512((void *)z1p[p], z1);
  }

  for (size_t i = 0; i < AIMER_N; i++)
  {
    c[i][0] ^= z0p[i][0]; c[i][1] ^= z0p[i][1]; c[i][2] ^= z1p[i][0];
  }
}

void GF_sqr(GF c, const GF a)
{
  __m128i x[2], z[3], t[2];
  __m128i irr = _mm_set_epi64x(0x0, 0x87);

  // polynomial multiplication
  x[0] = _mm_loadu_si128((const __m128i *)&a[0]); // a0 a1
  x[1] = _mm_loadl_epi64((const __m128i *)&a[2]); // a2 -

  z[0] = _mm_clmulepi64_si128(x[0], x[0], 0x00);
  z[1] = _mm_clmulepi64_si128(x[0], x[0], 0x11);
  z[2] = _mm_clmulepi64_si128(x[1], x[1], 0x00);

  // modular reduction
  t[0] = _mm_clmulepi64_si128(z[2], irr, 0x00); // 2 ^ 64
  t[1] = _mm_clmulepi64_si128(z[2], irr, 0x01); // 2 ^ 128

  z[0] = _mm_xor_si128(z[0], _mm_slli_si128(t[0], 8));
  z[1] = _mm_xor_si128(z[1], _mm_srli_si128(t[0], 8));
  z[1] = _mm_xor_si128(z[1], t[1]);

  t[0] = _mm_clmulepi64_si128(z[1], irr, 0x01); // 2 ^ 0
  z[0] = _mm_xor_si128(z[0], t[0]);

  _mm_storeu_si128((__m128i *)&c[0], z[0]);
  _mm_storel_epi64((__m128i *)&c[2], z[1]);
}

// c[i] = a[i]^2  (4 parties per 512-bit register)
void GF_sqr_N(GF c[AIMER_N], const GF a[AIMER_N])
{
  const __m512i irr = _mm512_broadcast_i32x4(_mm_set_epi64x(0x0, 0x87));

  uint64_t x0p[AIMER_N][2], x1p[AIMER_N][2], z0p[AIMER_N][2], z1p[AIMER_N][2];
  for (size_t i = 0; i < AIMER_N; i++)
  {
    x0p[i][0] = a[i][0]; x0p[i][1] = a[i][1];
    x1p[i][0] = a[i][2]; x1p[i][1] = 0;
  }

  for (size_t p = 0; p < AIMER_N; p += 4)
  {
    __m512i x0 = _mm512_loadu_si512((const void *)x0p[p]);
    __m512i x1 = _mm512_loadu_si512((const void *)x1p[p]);

    __m512i z0 = CL512(x0, x0, 0x00); // a0*a0
    __m512i z1 = CL512(x0, x0, 0x11); // a1*a1
    __m512i z2 = CL512(x1, x1, 0x00); // a2*a2

    // modular reduction
    __m512i t0 = CL512(z2, irr, 0x00);
    __m512i t1 = CL512(z2, irr, 0x01);
    z0 = XX512(z0, SLL512(t0));
    z1 = XX512(z1, SRL512(t0));
    z1 = XX512(z1, t1);
    t0 = CL512(z1, irr, 0x01);
    z0 = XX512(z0, t0);

    _mm512_storeu_si512((void *)z0p[p], z0);
    _mm512_storeu_si512((void *)z1p[p], z1);
  }

  for (size_t i = 0; i < AIMER_N; i++)
  {
    c[i][0] = z0p[i][0]; c[i][1] = z0p[i][1]; c[i][2] = z1p[i][0];
  }
}

void GF_transposed_matmul(GF c, const GF a, const GF b[AIM2_NUM_BITS_FIELD])
{
  const __m256i shift = _mm256_set_epi64x(0, 1, 2, 3);
  const __m256i zero = _mm256_setzero_si256();

  __m256i c0 = _mm256_setzero_si256();
  __m256i c1 = _mm256_setzero_si256();
  __m256i c2 = _mm256_setzero_si256();
  __m256i m0, m1, m2, a0, a1, a2;
  __m256i mask;
  __m128i z[2];

  for (int i = 2; i >= 0; i--)
  {
    mask = _mm256_set1_epi64x(a[i]);
    mask = _mm256_sllv_epi64(mask, shift);
    for (int row = 64 * (i + 1); row > 64 * i; row -= 4)
    {
      m0 = _mm256_loadu_si256((const __m256i *)&b[row - 4][0]);
      m1 = _mm256_loadu_si256((const __m256i *)&b[row - 3][1]);
      m2 = _mm256_loadu_si256((const __m256i *)&b[row - 2][2]);

      a0 = _mm256_permute4x64_epi64(mask, 0x40);
      a1 = _mm256_permute4x64_epi64(mask, 0xa5);
      a2 = _mm256_permute4x64_epi64(mask, 0xfe);

      a0 = _mm256_cmpgt_epi64(zero, a0);
      a1 = _mm256_cmpgt_epi64(zero, a1);
      a2 = _mm256_cmpgt_epi64(zero, a2);

      c0 = _mm256_xor_si256(c0, _mm256_and_si256(m0, a0));
      c1 = _mm256_xor_si256(c1, _mm256_and_si256(m1, a1));
      c2 = _mm256_xor_si256(c2, _mm256_and_si256(m2, a2));

      mask = _mm256_slli_epi64(mask, 4);
    }
  }

  a1 = _mm256_permute2x128_si256(c0, c1, 0x21);  // 2 3 4 5
  a2 = _mm256_permute2x128_si256(c1, c2, 0x21);  // 6 7 8 9

  a1 = _mm256_permute4x64_epi64(a1, 0x39); // 3 4 5 2
  c2 = _mm256_permute4x64_epi64(c2, 0x39); // 9 a b 8

  c0 = _mm256_xor_si256(c0, a1);
  c2 = _mm256_xor_si256(c2, a2);
  c0 = _mm256_xor_si256(c0, c2);

  z[0] = _mm256_extracti128_si256(c0, 0);
  z[1] = _mm256_extracti128_si256(c0, 1);

  _mm_storeu_si128((__m128i *)&c[0], z[0]);
  _mm_storel_epi64((__m128i *)&c[2], z[1]);
}

void GF_transposed_matmul_add_N(GF c[AIMER_N], const GF a[AIMER_N],
                                const GF b[AIM2_NUM_BITS_FIELD])
{
  // GF(2^192) = 3 words in the low 3 qwords of a 256-bit reg; maskz load/store
  // (mask 0x7) avoids over-reading the 24-byte GF[3] stride. 8 parties/iter.
  const __mmask8 m3 = 0x07;
  for (size_t party = 0; party < AIMER_N; party += 8)
  {
    __m256i k0 = _mm256_maskz_loadu_epi64(m3, c[party]);
    __m256i k1 = _mm256_maskz_loadu_epi64(m3, c[party + 1]);
    __m256i k2 = _mm256_maskz_loadu_epi64(m3, c[party + 2]);
    __m256i k3 = _mm256_maskz_loadu_epi64(m3, c[party + 3]);
    __m256i k4 = _mm256_maskz_loadu_epi64(m3, c[party + 4]);
    __m256i k5 = _mm256_maskz_loadu_epi64(m3, c[party + 5]);
    __m256i k6 = _mm256_maskz_loadu_epi64(m3, c[party + 6]);
    __m256i k7 = _mm256_maskz_loadu_epi64(m3, c[party + 7]);

    for (int w = 0; w < 3; w++)
    {
      __m256i h0 = _mm256_set1_epi64x(a[party][w]);
      __m256i h1 = _mm256_set1_epi64x(a[party + 1][w]);
      __m256i h2 = _mm256_set1_epi64x(a[party + 2][w]);
      __m256i h3 = _mm256_set1_epi64x(a[party + 3][w]);
      __m256i h4 = _mm256_set1_epi64x(a[party + 4][w]);
      __m256i h5 = _mm256_set1_epi64x(a[party + 5][w]);
      __m256i h6 = _mm256_set1_epi64x(a[party + 6][w]);
      __m256i h7 = _mm256_set1_epi64x(a[party + 7][w]);
      const GF *brow = &b[64 * w];
      for (int kk = 63; kk >= 0; kk--)
      {
        __m256i bc = _mm256_maskz_loadu_epi64(m3, brow[kk]);
        k0 = _mm256_ternarylogic_epi64(k0, _mm256_srai_epi64(h0, 63), bc, 0x78);
        k1 = _mm256_ternarylogic_epi64(k1, _mm256_srai_epi64(h1, 63), bc, 0x78);
        k2 = _mm256_ternarylogic_epi64(k2, _mm256_srai_epi64(h2, 63), bc, 0x78);
        k3 = _mm256_ternarylogic_epi64(k3, _mm256_srai_epi64(h3, 63), bc, 0x78);
        k4 = _mm256_ternarylogic_epi64(k4, _mm256_srai_epi64(h4, 63), bc, 0x78);
        k5 = _mm256_ternarylogic_epi64(k5, _mm256_srai_epi64(h5, 63), bc, 0x78);
        k6 = _mm256_ternarylogic_epi64(k6, _mm256_srai_epi64(h6, 63), bc, 0x78);
        k7 = _mm256_ternarylogic_epi64(k7, _mm256_srai_epi64(h7, 63), bc, 0x78);
        h0 = _mm256_slli_epi64(h0, 1); h1 = _mm256_slli_epi64(h1, 1);
        h2 = _mm256_slli_epi64(h2, 1); h3 = _mm256_slli_epi64(h3, 1);
        h4 = _mm256_slli_epi64(h4, 1); h5 = _mm256_slli_epi64(h5, 1);
        h6 = _mm256_slli_epi64(h6, 1); h7 = _mm256_slli_epi64(h7, 1);
      }
    }
    _mm256_mask_storeu_epi64(c[party],     m3, k0);
    _mm256_mask_storeu_epi64(c[party + 1], m3, k1);
    _mm256_mask_storeu_epi64(c[party + 2], m3, k2);
    _mm256_mask_storeu_epi64(c[party + 3], m3, k3);
    _mm256_mask_storeu_epi64(c[party + 4], m3, k4);
    _mm256_mask_storeu_epi64(c[party + 5], m3, k5);
    _mm256_mask_storeu_epi64(c[party + 6], m3, k6);
    _mm256_mask_storeu_epi64(c[party + 7], m3, k7);
  }
}

// Deferred-reduction multiply-accumulate for GF(2^192).
// Internal unreduced layout (private to this file's POLY_* pair): the 384-bit
// schoolbook product words c0..c5 are stored as lo[i]={c0,c1,c2}, hi[i]={c3,c4,c5}.
// Lane-packed accumulators: ZA={c0,c1}, ZB={c2,c3}, ZC={c4,c5}. Reduction is
// deferred to POLY_red_N (reduction is GF(2)-linear, so accumulate-then-reduce
// equals sum-of-reduced). 4 parties per 512-bit register.
void POLY_mul_add_N(GF lo[AIMER_N], GF hi[AIMER_N],
                    const GF a[AIMER_N], const GF b)
{
  const __m512i Y0 = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i *)&b[0]));
  const __m512i Y1 = _mm512_broadcast_i32x4(_mm_set_epi64x(0x0, (long long)b[2]));

  uint64_t x0p[AIMER_N][2], x1p[AIMER_N][2];
  uint64_t zap[AIMER_N][2], zbp[AIMER_N][2], zcp[AIMER_N][2];
  for (size_t i = 0; i < AIMER_N; i++)
  {
    x0p[i][0] = a[i][0]; x0p[i][1] = a[i][1];
    x1p[i][0] = a[i][2]; x1p[i][1] = 0;
    zap[i][0] = lo[i][0]; zap[i][1] = lo[i][1];
    zbp[i][0] = lo[i][2]; zbp[i][1] = hi[i][0];
    zcp[i][0] = hi[i][1]; zcp[i][1] = hi[i][2];
  }

  for (size_t p = 0; p < AIMER_N; p += 4)
  {
    __m512i x0 = _mm512_loadu_si512((const void *)x0p[p]);
    __m512i x1 = _mm512_loadu_si512((const void *)x1p[p]);
    __m512i ZA = _mm512_loadu_si512((const void *)zap[p]);
    __m512i ZB = _mm512_loadu_si512((const void *)zbp[p]);
    __m512i ZC = _mm512_loadu_si512((const void *)zcp[p]);

    __m512i P00 = CL512(x0, Y0, 0x00); // a0*b0  -> c0,c1
    __m512i P22 = CL512(x1, Y1, 0x00); // a2*b2  -> c4,c5
    __m512i cross1 = XX512(CL512(x0, Y0, 0x01), CL512(x0, Y0, 0x10)); // a0*b1^a1*b0 -> c1,c2
    __m512i mid    = XX512(XX512(CL512(x0, Y0, 0x11), CL512(x1, Y0, 0x00)),
                           CL512(x0, Y1, 0x00));                       // a1*b1^a2*b0^a0*b2 -> c2,c3
    __m512i cross3 = XX512(CL512(x1, Y0, 0x10), CL512(x0, Y1, 0x01)); // a2*b1^a1*b2 -> c3,c4

    ZA = XX512(ZA, P00);
    ZA = XX512(ZA, SLL512(cross1));
    ZB = XX512(ZB, SRL512(cross1));
    ZB = XX512(ZB, mid);
    ZB = XX512(ZB, SLL512(cross3));
    ZC = XX512(ZC, SRL512(cross3));
    ZC = XX512(ZC, P22);

    _mm512_storeu_si512((void *)zap[p], ZA);
    _mm512_storeu_si512((void *)zbp[p], ZB);
    _mm512_storeu_si512((void *)zcp[p], ZC);
  }

  for (size_t i = 0; i < AIMER_N; i++)
  {
    lo[i][0] = zap[i][0]; lo[i][1] = zap[i][1]; lo[i][2] = zbp[i][0];
    hi[i][0] = zbp[i][1]; hi[i][1] = zcp[i][0]; hi[i][2] = zcp[i][1];
  }
}

// Batch modular reduction of the deferred lo|hi products (4 parties per register).
// Reads the {c0..c5} layout written by POLY_mul_add_N and reduces mod the
// GF(2^192) polynomial (0x87), mirroring the scalar GF_mul reduction per lane.
void POLY_red_N(GF lo[AIMER_N], const GF hi[AIMER_N])
{
  const __m512i irr = _mm512_broadcast_i32x4(_mm_set_epi64x(0x0, 0x87));

  uint64_t zap[AIMER_N][2], zbp[AIMER_N][2], zcp[AIMER_N][2], z1p[AIMER_N][2];
  for (size_t i = 0; i < AIMER_N; i++)
  {
    zap[i][0] = lo[i][0]; zap[i][1] = lo[i][1]; // {c0,c1}
    zbp[i][0] = lo[i][2]; zbp[i][1] = hi[i][0]; // {c2,c3}
    zcp[i][0] = hi[i][1]; zcp[i][1] = hi[i][2]; // {c4,c5}
  }

  for (size_t p = 0; p < AIMER_N; p += 4)
  {
    __m512i ZA = _mm512_loadu_si512((const void *)zap[p]); // z0 = {c0,c1}
    __m512i ZB = _mm512_loadu_si512((const void *)zbp[p]); // z1 = {c2,c3}
    __m512i ZC = _mm512_loadu_si512((const void *)zcp[p]); // z2 = {c4,c5}

    __m512i t0 = CL512(ZC, irr, 0x00);
    __m512i t1 = CL512(ZC, irr, 0x01);
    ZA = XX512(ZA, SLL512(t0));
    ZB = XX512(ZB, SRL512(t0));
    ZB = XX512(ZB, t1);
    t0 = CL512(ZB, irr, 0x01);
    ZA = XX512(ZA, t0);

    _mm512_storeu_si512((void *)zap[p], ZA);
    _mm512_storeu_si512((void *)z1p[p], ZB);
  }

  for (size_t i = 0; i < AIMER_N; i++)
  {
    lo[i][0] = zap[i][0]; lo[i][1] = zap[i][1]; lo[i][2] = z1p[i][0];
  }
}

#undef CL512
#undef XX512
#undef SLL512
#undef SRL512
