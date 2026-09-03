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
