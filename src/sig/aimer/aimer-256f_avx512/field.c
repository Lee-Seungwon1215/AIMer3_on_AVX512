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

// ---- AVX-512 / VPCLMULQDQ helpers for GF(2^256): 4 parties per 512-bit reg ----
// Each 256-bit element spans two 128-bit lanes: X0 lane = {a0,a1}, X1 lane =
// {a2,a3}. We transpose AoS GF[4] into lane-packed scratch, run the (per-lane
// identical) AVX2 Karatsuba schoolbook + reduction on 4 parties at once, then
// transpose back. Reduction poly 0x425.
#define CL512(a, b, i) _mm512_clmulepi64_epi128((a), (b), (i))
#define XX512(a, b)    _mm512_xor_si512((a), (b))
#define SLL512(a)      _mm512_bslli_epi128((a), 8)
#define SRL512(a)      _mm512_bsrli_epi128((a), 8)

void GF_mul_N(GF c[AIMER_N], const GF a[AIMER_N], const GF b)
{
  const __m512i irr = _mm512_broadcast_i32x4(_mm_set_epi64x(0x0, 0x425));
  const __m512i Y0  = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i *)&b[0])); // {b0,b1}
  const __m512i Y1  = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i *)&b[2])); // {b2,b3}
  const __m512i Y2  = XX512(Y0, Y1);

  uint64_t x0p[AIMER_N][2], x1p[AIMER_N][2], z0p[AIMER_N][2], z1p[AIMER_N][2];
  for (size_t i = 0; i < AIMER_N; i++)
  {
    x0p[i][0] = a[i][0]; x0p[i][1] = a[i][1];
    x1p[i][0] = a[i][2]; x1p[i][1] = a[i][3];
  }

  for (size_t p = 0; p < AIMER_N; p += 4)
  {
    __m512i X0 = _mm512_loadu_si512((const void *)x0p[p]);
    __m512i X1 = _mm512_loadu_si512((const void *)x1p[p]);

    __m512i t0 = CL512(X0, Y0, 0x10), t1 = CL512(X0, Y0, 0x01);
    __m512i z0 = CL512(X0, Y0, 0x00), z1 = CL512(X0, Y0, 0x11);
    t0 = XX512(t0, t1); t1 = SRL512(t0); t0 = SLL512(t0);
    z0 = XX512(z0, t0); z1 = XX512(z1, t1);

    __m512i t2 = CL512(X1, Y1, 0x10), t3 = CL512(X1, Y1, 0x01);
    __m512i z2 = CL512(X1, Y1, 0x00), z3 = CL512(X1, Y1, 0x11);
    t2 = XX512(t2, t3); t3 = SRL512(t2); t2 = SLL512(t2);
    z2 = XX512(z2, t2); z3 = XX512(z3, t3);

    // Karatsuba middle
    __m512i xk = XX512(X0, X1);
    t0 = CL512(xk, Y2, 0x00); t1 = CL512(xk, Y2, 0x11);
    t2 = CL512(xk, Y2, 0x01); t3 = CL512(xk, Y2, 0x10);
    t2 = XX512(t2, t3);
    t3 = SRL512(t2); t2 = SLL512(t2);
    t0 = XX512(t0, z0); t1 = XX512(t1, z1); t2 = XX512(z2, t2); t3 = XX512(z3, t3);
    t0 = XX512(t0, t2); t1 = XX512(t1, t3);
    z1 = XX512(z1, t0); z2 = XX512(z2, t1);

    // modular reduction
    t0 = CL512(z2, irr, 0x01);
    t1 = CL512(z3, irr, 0x00);
    t2 = CL512(z3, irr, 0x01);
    z0 = XX512(z0, SLL512(t0));
    z1 = XX512(z1, SRL512(t0));
    z1 = XX512(z1, t1);
    z1 = XX512(z1, SLL512(t2));
    z2 = XX512(z2, SRL512(t2));
    t0 = CL512(z2, irr, 0x00);
    z0 = XX512(z0, t0);

    _mm512_storeu_si512((void *)z0p[p], z0);
    _mm512_storeu_si512((void *)z1p[p], z1);
  }

  for (size_t i = 0; i < AIMER_N; i++)
  {
    c[i][0] = z0p[i][0]; c[i][1] = z0p[i][1];
    c[i][2] = z1p[i][0]; c[i][3] = z1p[i][1];
  }
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

// c[i] ^= a[i] * b  (4 parties per 512-bit register)
void GF_mul_add_N(GF c[AIMER_N], const GF a[AIMER_N], const GF b)
{
  const __m512i irr = _mm512_broadcast_i32x4(_mm_set_epi64x(0x0, 0x425));
  const __m512i Y0  = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i *)&b[0]));
  const __m512i Y1  = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i *)&b[2]));
  const __m512i Y2  = XX512(Y0, Y1);

  uint64_t x0p[AIMER_N][2], x1p[AIMER_N][2], z0p[AIMER_N][2], z1p[AIMER_N][2];
  for (size_t i = 0; i < AIMER_N; i++)
  {
    x0p[i][0] = a[i][0]; x0p[i][1] = a[i][1];
    x1p[i][0] = a[i][2]; x1p[i][1] = a[i][3];
  }

  for (size_t p = 0; p < AIMER_N; p += 4)
  {
    __m512i X0 = _mm512_loadu_si512((const void *)x0p[p]);
    __m512i X1 = _mm512_loadu_si512((const void *)x1p[p]);

    __m512i t0 = CL512(X0, Y0, 0x10), t1 = CL512(X0, Y0, 0x01);
    __m512i z0 = CL512(X0, Y0, 0x00), z1 = CL512(X0, Y0, 0x11);
    t0 = XX512(t0, t1); t1 = SRL512(t0); t0 = SLL512(t0);
    z0 = XX512(z0, t0); z1 = XX512(z1, t1);

    __m512i t2 = CL512(X1, Y1, 0x10), t3 = CL512(X1, Y1, 0x01);
    __m512i z2 = CL512(X1, Y1, 0x00), z3 = CL512(X1, Y1, 0x11);
    t2 = XX512(t2, t3); t3 = SRL512(t2); t2 = SLL512(t2);
    z2 = XX512(z2, t2); z3 = XX512(z3, t3);

    __m512i xk = XX512(X0, X1);
    t0 = CL512(xk, Y2, 0x00); t1 = CL512(xk, Y2, 0x11);
    t2 = CL512(xk, Y2, 0x01); t3 = CL512(xk, Y2, 0x10);
    t2 = XX512(t2, t3);
    t3 = SRL512(t2); t2 = SLL512(t2);
    t0 = XX512(t0, z0); t1 = XX512(t1, z1); t2 = XX512(z2, t2); t3 = XX512(z3, t3);
    t0 = XX512(t0, t2); t1 = XX512(t1, t3);
    z1 = XX512(z1, t0); z2 = XX512(z2, t1);

    t0 = CL512(z2, irr, 0x01);
    t1 = CL512(z3, irr, 0x00);
    t2 = CL512(z3, irr, 0x01);
    z0 = XX512(z0, SLL512(t0));
    z1 = XX512(z1, SRL512(t0));
    z1 = XX512(z1, t1);
    z1 = XX512(z1, SLL512(t2));
    z2 = XX512(z2, SRL512(t2));
    t0 = CL512(z2, irr, 0x00);
    z0 = XX512(z0, t0);

    _mm512_storeu_si512((void *)z0p[p], z0);
    _mm512_storeu_si512((void *)z1p[p], z1);
  }

  for (size_t i = 0; i < AIMER_N; i++)
  {
    c[i][0] ^= z0p[i][0]; c[i][1] ^= z0p[i][1];
    c[i][2] ^= z1p[i][0]; c[i][3] ^= z1p[i][1];
  }
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

// c[i] = a[i]^2  (4 parties per 512-bit register)
void GF_sqr_N(GF c[AIMER_N], const GF a[AIMER_N])
{
  const __m512i irr = _mm512_broadcast_i32x4(_mm_set_epi64x(0x0, 0x425));

  uint64_t x0p[AIMER_N][2], x1p[AIMER_N][2], z0p[AIMER_N][2], z1p[AIMER_N][2];
  for (size_t i = 0; i < AIMER_N; i++)
  {
    x0p[i][0] = a[i][0]; x0p[i][1] = a[i][1];
    x1p[i][0] = a[i][2]; x1p[i][1] = a[i][3];
  }

  for (size_t p = 0; p < AIMER_N; p += 4)
  {
    __m512i X0 = _mm512_loadu_si512((const void *)x0p[p]);
    __m512i X1 = _mm512_loadu_si512((const void *)x1p[p]);

    __m512i z0 = CL512(X0, X0, 0x00);
    __m512i z1 = CL512(X0, X0, 0x11);
    __m512i z2 = CL512(X1, X1, 0x00);
    __m512i z3 = CL512(X1, X1, 0x11);

    // modular reduction
    __m512i t0 = CL512(z2, irr, 0x01);
    __m512i t1 = CL512(z3, irr, 0x00);
    __m512i t2 = CL512(z3, irr, 0x01);
    z0 = XX512(z0, SLL512(t0));
    z1 = XX512(z1, SRL512(t0));
    z1 = XX512(z1, t1);
    z1 = XX512(z1, SLL512(t2));
    z2 = XX512(z2, SRL512(t2));
    t0 = CL512(z2, irr, 0x00);
    z0 = XX512(z0, t0);

    _mm512_storeu_si512((void *)z0p[p], z0);
    _mm512_storeu_si512((void *)z1p[p], z1);
  }

  for (size_t i = 0; i < AIMER_N; i++)
  {
    c[i][0] = z0p[i][0]; c[i][1] = z0p[i][1];
    c[i][2] = z1p[i][0]; c[i][3] = z1p[i][1];
  }
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

// Deferred-reduction multiply-accumulate for GF(2^256).
// Internal unreduced layout (private to this file's POLY_* pair): the 512-bit
// Karatsuba product words c0..c7 are stored as lo[i]={c0,c1,c2,c3},
// hi[i]={c4,c5,c6,c7}. Lane-packed: ZA={c0,c1}, ZB={c2,c3}, ZC={c4,c5},
// ZD={c6,c7} = the pre-reduction (z0,z1,z2,z3) of GF_mul_N. Reduction (deferred
// to POLY_red_N) is GF(2)-linear, so accumulate-then-reduce == sum-of-reduced.
void POLY_mul_add_N(GF lo[AIMER_N], GF hi[AIMER_N],
                    const GF a[AIMER_N], const GF b)
{
  const __m512i Y0 = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i *)&b[0]));
  const __m512i Y1 = _mm512_broadcast_i32x4(_mm_loadu_si128((const __m128i *)&b[2]));
  const __m512i Y2 = XX512(Y0, Y1);

  uint64_t x0p[AIMER_N][2], x1p[AIMER_N][2];
  uint64_t zap[AIMER_N][2], zbp[AIMER_N][2], zcp[AIMER_N][2], zdp[AIMER_N][2];
  for (size_t i = 0; i < AIMER_N; i++)
  {
    x0p[i][0] = a[i][0]; x0p[i][1] = a[i][1];
    x1p[i][0] = a[i][2]; x1p[i][1] = a[i][3];
    zap[i][0] = lo[i][0]; zap[i][1] = lo[i][1];
    zbp[i][0] = lo[i][2]; zbp[i][1] = lo[i][3];
    zcp[i][0] = hi[i][0]; zcp[i][1] = hi[i][1];
    zdp[i][0] = hi[i][2]; zdp[i][1] = hi[i][3];
  }

  for (size_t p = 0; p < AIMER_N; p += 4)
  {
    __m512i X0 = _mm512_loadu_si512((const void *)x0p[p]);
    __m512i X1 = _mm512_loadu_si512((const void *)x1p[p]);

    __m512i t0 = CL512(X0, Y0, 0x10), t1 = CL512(X0, Y0, 0x01);
    __m512i z0 = CL512(X0, Y0, 0x00), z1 = CL512(X0, Y0, 0x11);
    t0 = XX512(t0, t1); t1 = SRL512(t0); t0 = SLL512(t0);
    z0 = XX512(z0, t0); z1 = XX512(z1, t1);

    __m512i t2 = CL512(X1, Y1, 0x10), t3 = CL512(X1, Y1, 0x01);
    __m512i z2 = CL512(X1, Y1, 0x00), z3 = CL512(X1, Y1, 0x11);
    t2 = XX512(t2, t3); t3 = SRL512(t2); t2 = SLL512(t2);
    z2 = XX512(z2, t2); z3 = XX512(z3, t3);

    __m512i xk = XX512(X0, X1);
    t0 = CL512(xk, Y2, 0x00); t1 = CL512(xk, Y2, 0x11);
    t2 = CL512(xk, Y2, 0x01); t3 = CL512(xk, Y2, 0x10);
    t2 = XX512(t2, t3);
    t3 = SRL512(t2); t2 = SLL512(t2);
    t0 = XX512(t0, z0); t1 = XX512(t1, z1); t2 = XX512(z2, t2); t3 = XX512(z3, t3);
    t0 = XX512(t0, t2); t1 = XX512(t1, t3);
    z1 = XX512(z1, t0); z2 = XX512(z2, t1);

    // accumulate unreduced product into lo|hi
    z0 = XX512(z0, _mm512_loadu_si512((const void *)zap[p]));
    z1 = XX512(z1, _mm512_loadu_si512((const void *)zbp[p]));
    z2 = XX512(z2, _mm512_loadu_si512((const void *)zcp[p]));
    z3 = XX512(z3, _mm512_loadu_si512((const void *)zdp[p]));

    _mm512_storeu_si512((void *)zap[p], z0);
    _mm512_storeu_si512((void *)zbp[p], z1);
    _mm512_storeu_si512((void *)zcp[p], z2);
    _mm512_storeu_si512((void *)zdp[p], z3);
  }

  for (size_t i = 0; i < AIMER_N; i++)
  {
    lo[i][0] = zap[i][0]; lo[i][1] = zap[i][1]; lo[i][2] = zbp[i][0]; lo[i][3] = zbp[i][1];
    hi[i][0] = zcp[i][0]; hi[i][1] = zcp[i][1]; hi[i][2] = zdp[i][0]; hi[i][3] = zdp[i][1];
  }
}

// Batch modular reduction of the deferred lo|hi products (4 parties per register).
// Reads {c0..c7} (lo={c0..c3}, hi={c4..c7}) and reduces mod GF(2^256) (0x425),
// mirroring the scalar GF_mul reduction per lane.
void POLY_red_N(GF lo[AIMER_N], const GF hi[AIMER_N])
{
  const __m512i irr = _mm512_broadcast_i32x4(_mm_set_epi64x(0x0, 0x425));

  uint64_t zap[AIMER_N][2], zbp[AIMER_N][2], zcp[AIMER_N][2], zdp[AIMER_N][2];
  uint64_t z0p[AIMER_N][2], z1p[AIMER_N][2];
  for (size_t i = 0; i < AIMER_N; i++)
  {
    zap[i][0] = lo[i][0]; zap[i][1] = lo[i][1]; // {c0,c1}
    zbp[i][0] = lo[i][2]; zbp[i][1] = lo[i][3]; // {c2,c3}
    zcp[i][0] = hi[i][0]; zcp[i][1] = hi[i][1]; // {c4,c5}
    zdp[i][0] = hi[i][2]; zdp[i][1] = hi[i][3]; // {c6,c7}
  }

  for (size_t p = 0; p < AIMER_N; p += 4)
  {
    __m512i z0 = _mm512_loadu_si512((const void *)zap[p]);
    __m512i z1 = _mm512_loadu_si512((const void *)zbp[p]);
    __m512i z2 = _mm512_loadu_si512((const void *)zcp[p]);
    __m512i z3 = _mm512_loadu_si512((const void *)zdp[p]);

    __m512i t0 = CL512(z2, irr, 0x01);
    __m512i t1 = CL512(z3, irr, 0x00);
    __m512i t2 = CL512(z3, irr, 0x01);
    z0 = XX512(z0, SLL512(t0));
    z1 = XX512(z1, SRL512(t0));
    z1 = XX512(z1, t1);
    z1 = XX512(z1, SLL512(t2));
    z2 = XX512(z2, SRL512(t2));
    t0 = CL512(z2, irr, 0x00);
    z0 = XX512(z0, t0);

    _mm512_storeu_si512((void *)z0p[p], z0);
    _mm512_storeu_si512((void *)z1p[p], z1);
  }

  for (size_t i = 0; i < AIMER_N; i++)
  {
    lo[i][0] = z0p[i][0]; lo[i][1] = z0p[i][1]; lo[i][2] = z1p[i][0]; lo[i][3] = z1p[i][1];
  }
}

#undef CL512
#undef XX512
#undef SLL512
#undef SRL512
