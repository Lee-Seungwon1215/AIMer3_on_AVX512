// SPDX-License-Identifier: MIT

#include "field.h"
#include "field_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(AIMER_MVE_PORTABLE_TEST)
/* Scalar emulation is compiled only by the host-side differential test. */
#else
#include <arm_mve.h>
#if !defined(__ARM_FEATURE_MVE) || (__ARM_FEATURE_MVE != 3)
#error "AIMer MVE field backend requires Cortex-M55 integer and FP MVE"
#endif
#endif

#define M55_LIMB_BITS 16u
#define M55_LIMBS (AIM3_NUM_BITS_FIELD / M55_LIMB_BITS)
#define M55_PADDED_LIMBS ((M55_LIMBS + 7u) & ~7u)

static void m55_gf_to_limbs(uint16_t limbs[M55_PADDED_LIMBS], const gf value)
{
  memset(limbs, 0, M55_PADDED_LIMBS * sizeof(*limbs));
  for (size_t i = 0; i < M55_LIMBS; ++i)
  {
    limbs[i] = (uint16_t)(value[i >> 2] >> (16u * (i & 3u)));
  }
}

static void m55_limbs_to_words(
    uint64_t words[2u * AIM3_NUM_WORDS_FIELD],
    const uint16_t limbs[2u * M55_LIMBS])
{
  for (size_t word = 0; word < 2u * AIM3_NUM_WORDS_FIELD; ++word)
  {
    words[word] = (uint64_t)limbs[4u * word] |
                  ((uint64_t)limbs[4u * word + 1u] << 16) |
                  ((uint64_t)limbs[4u * word + 2u] << 32) |
                  ((uint64_t)limbs[4u * word + 3u] << 48);
  }
}

/*
 * Convolve 16-bit polynomial limbs.  Each pair of VMULLB.P16/VMULLT.P16
 * computes eight independent carry-less 16x16->32 products.
 */
__attribute__((noinline))
static void m55_clmul_convolution(uint16_t product[2u * M55_LIMBS],
                                  const uint16_t a[M55_PADDED_LIMBS],
                                  const uint16_t b[M55_PADDED_LIMBS])
{
  memset(product, 0, 2u * M55_LIMBS * sizeof(*product));

  for (size_t i = 0; i < M55_LIMBS; ++i)
  {
#if !defined(AIMER_MVE_PORTABLE_TEST)
    const uint16x8_t a_vector = vdupq_n_u16(a[i]);
#endif
    for (size_t block = 0; block < M55_LIMBS; block += 8u)
    {
      _Alignas(16) uint32_t lanes[8];
#if defined(AIMER_MVE_PORTABLE_TEST)
      for (size_t lane = 0; lane < 8u; ++lane)
      {
        uint32_t lane_product = 0;
        for (size_t bit = 0; bit < 16u; ++bit)
        {
          const uint32_t mask = UINT32_C(0) -
                                ((b[block + lane] >> bit) & UINT32_C(1));
          lane_product ^= ((uint32_t)a[i] << bit) & mask;
        }
        lanes[lane] = lane_product;
      }
#else
      const uint16x8_t b_vector = vld1q_u16(&b[block]);
      const uint32x4_t bottom = vmullbq_poly_p16(a_vector, b_vector);
      const uint32x4_t top = vmulltq_poly_p16(a_vector, b_vector);
      _Alignas(16) uint32_t even_lanes[4];
      _Alignas(16) uint32_t odd_lanes[4];
      vst1q_u32(even_lanes, bottom);
      vst1q_u32(odd_lanes, top);

      /* MVE widening bottom/top instructions select the even/odd 16-bit
       * elements respectively; they do not select contiguous lower/upper
       * groups of four.  Restore the original limb order for convolution. */
      for (size_t pair = 0; pair < 4u; ++pair)
      {
        lanes[2u * pair] = even_lanes[pair];
        lanes[2u * pair + 1u] = odd_lanes[pair];
      }
#endif

      size_t lane_count = M55_LIMBS - block;
      if (lane_count > 8u)
      {
        lane_count = 8u;
      }
      for (size_t lane = 0; lane < lane_count; ++lane)
      {
        const size_t output_limb = i + block + lane;
        product[output_limb] ^= (uint16_t)lanes[lane];
        product[output_limb + 1u] ^= (uint16_t)(lanes[lane] >> 16);
      }
    }
  }
}

/*
 * Fold the high half of a carry-less product a word at a time.  These are the
 * same reductions as the AIMer v3 reference backend, kept here so the MVE
 * multiplier does not fall back to an O(n)-bit reduction loop.
 */
static void m55_gf_reduce_words(
    gf c, const uint64_t a[2u * AIM3_NUM_WORDS_FIELD])
{
#if SECURITY_BITS == 128 || SECURITY_BITS == 192
  uint64_t t = a[AIM3_NUM_WORDS_FIELD] ^
      ((a[2u * AIM3_NUM_WORDS_FIELD - 1u] >> 57) ^
       (a[2u * AIM3_NUM_WORDS_FIELD - 1u] >> 62) ^
       (a[2u * AIM3_NUM_WORDS_FIELD - 1u] >> 63));

  for (size_t word = AIM3_NUM_WORDS_FIELD - 1u; word > 0u; --word)
  {
    const uint64_t high = a[AIM3_NUM_WORDS_FIELD + word];
    const uint64_t carry =
        (word == 1u) ? t : a[AIM3_NUM_WORDS_FIELD + word - 1u];
    c[word] = a[word] ^ high;
    c[word] ^= (high << 7) | (carry >> 57);
    c[word] ^= (high << 2) | (carry >> 62);
    c[word] ^= (high << 1) | (carry >> 63);
  }
  c[0] = a[0] ^ t ^ (t << 7) ^ (t << 2) ^ (t << 1);
#elif SECURITY_BITS == 256
  const uint64_t t = a[4] ^
      ((a[7] >> 54) ^ (a[7] >> 59) ^ (a[7] >> 62));

  c[3] = a[3] ^ a[7];
  c[3] ^= (a[7] << 10) | (a[6] >> 54);
  c[3] ^= (a[7] << 5) | (a[6] >> 59);
  c[3] ^= (a[7] << 2) | (a[6] >> 62);

  c[2] = a[2] ^ a[6];
  c[2] ^= (a[6] << 10) | (a[5] >> 54);
  c[2] ^= (a[6] << 5) | (a[5] >> 59);
  c[2] ^= (a[6] << 2) | (a[5] >> 62);

  c[1] = a[1] ^ a[5];
  c[1] ^= (a[5] << 10) | (t >> 54);
  c[1] ^= (a[5] << 5) | (t >> 59);
  c[1] ^= (a[5] << 2) | (t >> 62);

  c[0] = a[0] ^ t ^ (t << 10) ^ (t << 5) ^ (t << 2);
#else
#error "Unsupported AIMer field size"
#endif
}

void gf_mul(gf c, const gf a, const gf b)
{
  _Alignas(16) uint16_t a_limbs[M55_PADDED_LIMBS];
  _Alignas(16) uint16_t b_limbs[M55_PADDED_LIMBS];
  _Alignas(16) uint16_t product[2u * M55_LIMBS];
  uint64_t product_words[2u * AIM3_NUM_WORDS_FIELD];

  m55_gf_to_limbs(a_limbs, a);
  m55_gf_to_limbs(b_limbs, b);
  m55_clmul_convolution(product, a_limbs, b_limbs);
  m55_limbs_to_words(product_words, product);
  m55_gf_reduce_words(c, product_words);
}

void gf_sqr(gf c, const gf a)
{
  uint64_t product[2u * AIM3_NUM_WORDS_FIELD] = {0,};

  for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
  {
    poly64_sqr(&product[2u * word + 1u], &product[2u * word], a[word]);
  }
  m55_gf_reduce_words(c, product);
}

#if defined(AIMER_CUSTOM_GF_INV)
/* Compute x^(2^s) * y.  Separate temporaries make the helper safe when the
 * output aliases either input, as required by the fixed inversion chains. */
static void gf_sqr_n_mul(gf out, const gf x, size_t s, const gf y)
{
  gf squared;
  gf product;

  gf_copy(squared, x);
  for (size_t i = 0; i < s; ++i)
  {
    gf_sqr(squared, squared);
  }
  gf_mul(product, squared, y);
  gf_copy(out, product);
}

void gf_inv(gf c, const gf a)
{
#if SECURITY_BITS == 128
  gf a3;
  gf a7;
  gf t;

  /* Let A_k = a^(2^k - 1).  The final square maps A_127 to
   * a^(2^128 - 2). */
  gf_sqr_n_mul(t, a, 1, a);      /* A_2   = A_1^(2^1)   * A_1 */
  gf_sqr_n_mul(a3, t, 1, a);     /* A_3   = A_2^(2^1)   * A_1 */
  gf_sqr_n_mul(t, a3, 3, a3);    /* A_6   = A_3^(2^3)   * A_3 */
  gf_sqr_n_mul(a7, t, 1, a);     /* A_7   = A_6^(2^1)   * A_1 */
  gf_sqr_n_mul(t, a7, 7, a7);    /* A_14  = A_7^(2^7)   * A_7 */
  gf_sqr_n_mul(t, t, 1, a);      /* A_15  = A_14^(2^1)  * A_1 */
  gf_sqr_n_mul(t, t, 15, t);     /* A_30  = A_15^(2^15) * A_15 */
  gf_sqr_n_mul(t, t, 30, t);     /* A_60  = A_30^(2^30) * A_30 */
  gf_sqr_n_mul(t, t, 60, t);     /* A_120 = A_60^(2^60) * A_60 */
  gf_sqr_n_mul(t, t, 7, a7);     /* A_127 = A_120^(2^7) * A_7 */
#elif SECURITY_BITS == 192
  gf a2;
  gf a3;
  gf t;

  /* Build A_191 with eleven general multiplications, then square once to
   * obtain a^(2^192 - 2). */
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
#elif SECURITY_BITS == 256
  gf a2;
  gf a3;
  gf t;

  /* Preserve A_5 and A_15 for the non-doubling links in the chain. */
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
#else
#error "Unsupported AIMer field size"
#endif

  gf_sqr(c, t);
}
#endif

#if !defined(AIMER_M55_REFERENCE_MATVEC) && \
    !defined(AIMER_M55_MVE_MATVEC)
void gf_mat_vec_mul(gf c, const gf a,
                    const gf b[AIM3_NUM_BITS_FIELD])
{
  uint64_t result[AIM3_NUM_WORDS_FIELD] = {0,};

  for (size_t bit = 0; bit < AIM3_NUM_BITS_FIELD; ++bit)
  {
    const uint64_t coefficient =
        (a[bit >> 6] >> (bit & 63u)) & UINT64_C(1);
    const uint64_t mask = UINT64_C(0) - coefficient;
    for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
    {
      result[word] ^= b[bit][word] & mask;
    }
  }

  for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
  {
    c[word] = result[word];
  }
}
#endif
