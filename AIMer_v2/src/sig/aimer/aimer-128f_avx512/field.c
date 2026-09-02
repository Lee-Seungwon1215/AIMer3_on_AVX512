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
}

void GF_copy(GF out, const GF in)
{
  out[0] = in[0];
  out[1] = in[1];
}

void GF_add(GF c, const GF a, const GF b)
{
  c[0] = a[0] ^ b[0];
  c[1] = a[1] ^ b[1];
}

void GF_mul(GF c, const GF a, const GF b)
{
  __m128i temp_a, temp_b;
  __m128i temp_c[2];
  __m128i temp[3];
  __m128i irr = _mm_set_epi64x(0x0, 0x87);

  temp_a = _mm_loadu_si128((const __m128i *)a);
  temp_b = _mm_loadu_si128((const __m128i *)b);

  // polynomial multiplication
  temp_c[0] = _mm_clmulepi64_si128(temp_a, temp_b, 0x00);
  temp_c[1] = _mm_clmulepi64_si128(temp_a, temp_b, 0x11);

  temp[0] = _mm_clmulepi64_si128(temp_a, temp_b, 0x01);
  temp[1] = _mm_clmulepi64_si128(temp_a, temp_b, 0x10);

  temp[0] = _mm_xor_si128(temp[0], temp[1]);
  temp_c[0] = _mm_xor_si128(temp_c[0], _mm_slli_si128(temp[0], 8));
  temp_c[1] = _mm_xor_si128(temp_c[1], _mm_srli_si128(temp[0], 8));

  // modular reduction
  temp[0] = _mm_clmulepi64_si128(temp_c[1], irr, 0x01);
  temp[1] = _mm_slli_si128(temp[0], 8);
  temp[2] = _mm_srli_si128(temp[0], 8);
  temp[2] = _mm_xor_si128(temp[2], temp_c[1]);

  temp_c[0] = _mm_xor_si128(temp_c[0], _mm_clmulepi64_si128(temp[2], irr, 0x00));
  temp_c[0] = _mm_xor_si128(temp_c[0], temp[1]);

  _mm_storeu_si128((__m128i *)c, temp_c[0]);
}

// AVX-512 / VPCLMULQDQ: 4 parties per 512-bit register (lane i = party i).
// _mm512_clmulepi64_epi128 performs 4 independent 64x64->128 carry-less
// products, one per 128-bit lane. Each GF(2^128) element occupies one lane.
// Per-lane logic is identical to the AVX2 (__m128i) GF_mul_N; reduction poly 0x87.
void GF_mul_N(GF c[AIMER_N], const GF a[AIMER_N], const GF b)
{
  const __m512i irr = _mm512_broadcast_i32x4(_mm_set_epi64x(0x0, 0x87));
  const __m512i y   = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i *)b));

  for (size_t party = 0; party < AIMER_N; party += 4)
  {
    __m512i x = _mm512_loadu_si512((const void *)a[party]); // 4 parties

    // polynomial multiplication (4-way)
    __m512i z   = _mm512_clmulepi64_epi128(x, y, 0x00);
    __m512i zhi = _mm512_clmulepi64_epi128(x, y, 0x11);
    __m512i t   = _mm512_xor_si512(_mm512_clmulepi64_epi128(x, y, 0x01),
                                   _mm512_clmulepi64_epi128(x, y, 0x10));
    z   = _mm512_xor_si512(z,   _mm512_bslli_epi128(t, 8));
    zhi = _mm512_xor_si512(zhi, _mm512_bsrli_epi128(t, 8));

    // modular reduction (4-way)
    t   = _mm512_clmulepi64_epi128(zhi, irr, 0x01);
    z   = _mm512_xor_si512(z,   _mm512_bslli_epi128(t, 8));
    zhi = _mm512_xor_si512(zhi, _mm512_bsrli_epi128(t, 8));
    t   = _mm512_clmulepi64_epi128(zhi, irr, 0x00);
    z   = _mm512_xor_si512(z, t);

    _mm512_storeu_si512((void *)c[party], z);
  }
}

void GF_mul_add(GF c, const GF a, const GF b)
{
  __m128i temp_a, temp_b;
  __m128i temp_c[2];
  __m128i temp[3];
  __m128i irr = _mm_set_epi64x(0x0, 0x87);

  temp_a = _mm_loadu_si128((const __m128i *)a);
  temp_b = _mm_loadu_si128((const __m128i *)b);

  // polynomial multiplication
  temp_c[0] = _mm_clmulepi64_si128(temp_a, temp_b, 0x00);
  temp_c[1] = _mm_clmulepi64_si128(temp_a, temp_b, 0x11);

  temp[0] = _mm_clmulepi64_si128(temp_a, temp_b, 0x01);
  temp[1] = _mm_clmulepi64_si128(temp_a, temp_b, 0x10);

  temp[0] = _mm_xor_si128(temp[0], temp[1]);
  temp_c[0] = _mm_xor_si128(temp_c[0], _mm_slli_si128(temp[0], 8));
  temp_c[1] = _mm_xor_si128(temp_c[1], _mm_srli_si128(temp[0], 8));

  // modular reduction
  temp[0] = _mm_clmulepi64_si128(temp_c[1], irr, 0x01);
  temp[1] = _mm_slli_si128(temp[0], 8);
  temp[2] = _mm_srli_si128(temp[0], 8);
  temp[2] = _mm_xor_si128(temp[2], temp_c[1]);

  temp_c[0] = _mm_xor_si128(temp_c[0], _mm_clmulepi64_si128(temp[2], irr, 0x00));
  temp_c[0] = _mm_xor_si128(temp_c[0], temp[1]);

  temp[0] = _mm_loadu_si128((const __m128i *)c);
  temp_c[0] = _mm_xor_si128(temp_c[0], temp[0]);
  _mm_storeu_si128((__m128i *)c, temp_c[0]);
}

// c[i] ^= a[i] * b  (4 parties per 512-bit register)
void GF_mul_add_N(GF c[AIMER_N], const GF a[AIMER_N], const GF b)
{
  const __m512i irr = _mm512_broadcast_i32x4(_mm_set_epi64x(0x0, 0x87));
  const __m512i y   = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i *)b));

  for (size_t party = 0; party < AIMER_N; party += 4)
  {
    __m512i x = _mm512_loadu_si512((const void *)a[party]);

    __m512i z   = _mm512_clmulepi64_epi128(x, y, 0x00);
    __m512i zhi = _mm512_clmulepi64_epi128(x, y, 0x11);
    __m512i t   = _mm512_xor_si512(_mm512_clmulepi64_epi128(x, y, 0x01),
                                   _mm512_clmulepi64_epi128(x, y, 0x10));
    z   = _mm512_xor_si512(z,   _mm512_bslli_epi128(t, 8));
    zhi = _mm512_xor_si512(zhi, _mm512_bsrli_epi128(t, 8));

    // modular reduction (4-way)
    t   = _mm512_clmulepi64_epi128(zhi, irr, 0x01);
    z   = _mm512_xor_si512(z,   _mm512_bslli_epi128(t, 8));
    zhi = _mm512_xor_si512(zhi, _mm512_bsrli_epi128(t, 8));
    t   = _mm512_clmulepi64_epi128(zhi, irr, 0x00);
    z   = _mm512_xor_si512(z, t);

    // accumulate into c
    z = _mm512_xor_si512(z, _mm512_loadu_si512((const void *)c[party]));
    _mm512_storeu_si512((void *)c[party], z);
  }
}

void GF_sqr(GF c, const GF a)
{
  __m128i temp_a;
  __m128i temp_c[2];
  __m128i temp[3];
  __m128i irr = _mm_set_epi64x(0x0, 0x87);

  temp_a = _mm_loadu_si128((const __m128i *)a);

  // polynomial squaring
  temp_c[0] = _mm_clmulepi64_si128(temp_a, temp_a, 0x00);
  temp_c[1] = _mm_clmulepi64_si128(temp_a, temp_a, 0x11);

  // modular reduction
  temp[0] = _mm_clmulepi64_si128(temp_c[1], irr, 0x01);
  temp[1] = _mm_slli_si128(temp[0], 8);
  temp[2] = _mm_srli_si128(temp[0], 8);
  temp[2] = _mm_xor_si128(temp[2], temp_c[1]);

  temp_c[0] = _mm_xor_si128(temp_c[0], _mm_clmulepi64_si128(temp[2], irr, 0x00));
  temp_c[0] = _mm_xor_si128(temp_c[0], temp[1]);

  _mm_storeu_si128((__m128i *)c, temp_c[0]);
}

// c[i] = a[i]^2  (4 parties per 512-bit register)
void GF_sqr_N(GF c[AIMER_N], const GF a[AIMER_N])
{
  const __m512i irr = _mm512_broadcast_i32x4(_mm_set_epi64x(0x0, 0x87));

  for (size_t party = 0; party < AIMER_N; party += 4)
  {
    __m512i x = _mm512_loadu_si512((const void *)a[party]);

    // polynomial squaring (4-way): no cross terms
    __m512i z   = _mm512_clmulepi64_epi128(x, x, 0x00);
    __m512i zhi = _mm512_clmulepi64_epi128(x, x, 0x11);

    // modular reduction (4-way)
    __m512i t = _mm512_clmulepi64_epi128(zhi, irr, 0x01);
    z   = _mm512_xor_si512(z,   _mm512_bslli_epi128(t, 8));
    zhi = _mm512_xor_si512(zhi, _mm512_bsrli_epi128(t, 8));
    t   = _mm512_clmulepi64_epi128(zhi, irr, 0x00);
    z   = _mm512_xor_si512(z, t);

    _mm512_storeu_si512((void *)c[party], z);
  }
}

void GF_transposed_matmul(GF c, const GF a, const GF b[AIM2_NUM_BITS_FIELD])
{
  unsigned int i, j;
  const uint32_t *a_ptr = (uint32_t *)a;
  const __m256i shift = _mm256_set_epi32(0, 2, 4, 6, 1, 3, 5, 7);
  __m256i temp[2] = {_mm256_setzero_si256(), _mm256_setzero_si256()};
  __m256i matrix_data[4];

  for (i = 0; i < 4; i++)
  {
    __m256i index = _mm256_set1_epi32(a_ptr[i]);
    for (j = 32 * (i + 1); j > 32 * i; j -= 8)
    {
      __m256i mask = _mm256_sllv_epi32(index, shift);
      matrix_data[0] = _mm256_loadu_si256((const __m256i *)&b[j - 2]);
      matrix_data[1] = _mm256_loadu_si256((const __m256i *)&b[j - 4]);
      matrix_data[2] = _mm256_loadu_si256((const __m256i *)&b[j - 6]);
      matrix_data[3] = _mm256_loadu_si256((const __m256i *)&b[j - 8]);

      temp[0] = _mm256_xor_si256(temp[0],
        _mm256_and_si256(matrix_data[0],
          _mm256_srai_epi32(_mm256_shuffle_epi32(mask, 0xff), 31)));
      temp[1] = _mm256_xor_si256(temp[1],
        _mm256_and_si256(matrix_data[1],
          _mm256_srai_epi32(_mm256_shuffle_epi32(mask, 0xaa), 31)));
      temp[0] = _mm256_xor_si256(temp[0],
        _mm256_and_si256(matrix_data[2],
          _mm256_srai_epi32(_mm256_shuffle_epi32(mask, 0x55), 31)));
      temp[1] = _mm256_xor_si256(temp[1],
        _mm256_and_si256(matrix_data[3],
          _mm256_srai_epi32(_mm256_shuffle_epi32(mask, 0x00), 31)));
      index = _mm256_slli_epi32(index, 8);
    }
  }

  temp[0] = _mm256_xor_si256(temp[0], temp[1]);

  _mm_storeu_si128((__m128i *)c,
                  _mm_xor_si128(_mm256_extracti128_si256(temp[0], 0),
                  _mm256_extracti128_si256(temp[0], 1)));
}

void GF_transposed_matmul_add_N(GF c[AIMER_N], const GF a[AIMER_N],
                                const GF b[AIM2_NUM_BITS_FIELD])
{
  for (size_t p = 0; p < AIMER_N; p += 16)
  {
    __m512i v0 = _mm512_loadu_si512((const void *)a[p]);
    __m512i v1 = _mm512_loadu_si512((const void *)a[p + 4]);
    __m512i v2 = _mm512_loadu_si512((const void *)a[p + 8]);
    __m512i v3 = _mm512_loadu_si512((const void *)a[p + 12]);
    __m512i k0 = _mm512_loadu_si512((const void *)c[p]);
    __m512i k1 = _mm512_loadu_si512((const void *)c[p + 4]);
    __m512i k2 = _mm512_loadu_si512((const void *)c[p + 8]);
    __m512i k3 = _mm512_loadu_si512((const void *)c[p + 12]);
    __m512i h0 = _mm512_shuffle_epi32(v0,(_MM_PERM_ENUM)0xEE), l0 = _mm512_shuffle_epi32(v0,(_MM_PERM_ENUM)0x44);
    __m512i h1 = _mm512_shuffle_epi32(v1,(_MM_PERM_ENUM)0xEE), l1 = _mm512_shuffle_epi32(v1,(_MM_PERM_ENUM)0x44);
    __m512i h2 = _mm512_shuffle_epi32(v2,(_MM_PERM_ENUM)0xEE), l2 = _mm512_shuffle_epi32(v2,(_MM_PERM_ENUM)0x44);
    __m512i h3 = _mm512_shuffle_epi32(v3,(_MM_PERM_ENUM)0xEE), l3 = _mm512_shuffle_epi32(v3,(_MM_PERM_ENUM)0x44);
    for (int kk = 63; kk >= 0; kk--) {
      __m512i bc = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i *)b[64 + kk]));
      k0 = _mm512_ternarylogic_epi64(k0, _mm512_srai_epi64(h0,63), bc, 0x78);
      k1 = _mm512_ternarylogic_epi64(k1, _mm512_srai_epi64(h1,63), bc, 0x78);
      k2 = _mm512_ternarylogic_epi64(k2, _mm512_srai_epi64(h2,63), bc, 0x78);
      k3 = _mm512_ternarylogic_epi64(k3, _mm512_srai_epi64(h3,63), bc, 0x78);
      h0 = _mm512_slli_epi64(h0,1); h1 = _mm512_slli_epi64(h1,1);
      h2 = _mm512_slli_epi64(h2,1); h3 = _mm512_slli_epi64(h3,1);
    }
    for (int kk = 63; kk >= 0; kk--) {
      __m512i bc = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i *)b[kk]));
      k0 = _mm512_ternarylogic_epi64(k0, _mm512_srai_epi64(l0,63), bc, 0x78);
      k1 = _mm512_ternarylogic_epi64(k1, _mm512_srai_epi64(l1,63), bc, 0x78);
      k2 = _mm512_ternarylogic_epi64(k2, _mm512_srai_epi64(l2,63), bc, 0x78);
      k3 = _mm512_ternarylogic_epi64(k3, _mm512_srai_epi64(l3,63), bc, 0x78);
      l0 = _mm512_slli_epi64(l0,1); l1 = _mm512_slli_epi64(l1,1);
      l2 = _mm512_slli_epi64(l2,1); l3 = _mm512_slli_epi64(l3,1);
    }
    _mm512_storeu_si512((void *)c[p],      k0);
    _mm512_storeu_si512((void *)c[p + 4],  k1);
    _mm512_storeu_si512((void *)c[p + 8],  k2);
    _mm512_storeu_si512((void *)c[p + 12], k3);
  }
}

// Deferred-reduction multiply-accumulate: lo[i]|hi[i] ^= unreduced(a[i] * b).
// 4 parties per 512-bit register; reduction deferred to POLY_red_N.
void POLY_mul_add_N(GF lo[AIMER_N], GF hi[AIMER_N],
                    const GF a[AIMER_N], const GF b)
{
  const __m512i y = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i *)b));

  for (size_t party = 0; party < AIMER_N; party += 4)
  {
    __m512i x = _mm512_loadu_si512((const void *)a[party]);

    __m512i z   = _mm512_clmulepi64_epi128(x, y, 0x00);
    __m512i zhi = _mm512_clmulepi64_epi128(x, y, 0x11);

    z   = _mm512_xor_si512(_mm512_loadu_si512((const void *)lo[party]), z);
    zhi = _mm512_xor_si512(_mm512_loadu_si512((const void *)hi[party]), zhi);

    __m512i t = _mm512_xor_si512(_mm512_clmulepi64_epi128(x, y, 0x01),
                                 _mm512_clmulepi64_epi128(x, y, 0x10));
    z   = _mm512_xor_si512(z,   _mm512_bslli_epi128(t, 8));
    zhi = _mm512_xor_si512(zhi, _mm512_bsrli_epi128(t, 8));

    _mm512_storeu_si512((void *)lo[party], z);
    _mm512_storeu_si512((void *)hi[party], zhi);
  }
}

// Batch modular reduction of the deferred lo|hi products (4 parties per register).
void POLY_red_N(GF lo[AIMER_N], const GF hi[AIMER_N])
{
  const __m512i irr = _mm512_broadcast_i32x4(_mm_set_epi64x(0x0, 0x87));

  for (size_t party = 0; party < AIMER_N; party += 4)
  {
    __m512i z   = _mm512_loadu_si512((const void *)lo[party]);
    __m512i zhi = _mm512_loadu_si512((const void *)hi[party]);

    __m512i t = _mm512_clmulepi64_epi128(zhi, irr, 0x01);
    z   = _mm512_xor_si512(z,   _mm512_bslli_epi128(t, 8));
    zhi = _mm512_xor_si512(zhi, _mm512_bsrli_epi128(t, 8));
    t   = _mm512_clmulepi64_epi128(zhi, irr, 0x00);
    z   = _mm512_xor_si512(z, t);

    _mm512_storeu_si512((void *)lo[party], z);
  }
}
