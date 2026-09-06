// SPDX-License-Identifier: MIT

#include "m55_field_batch.h"

#include <stddef.h>
#include <stdint.h>

#define M55_GF_WORDS32 (AIM3_NUM_BITS_FIELD / 32u)

#if !defined(AIMER_M55_MVE_MATVEC)
void m55_gf_mat_vec_mul_batch4(
    gf output[M55_PARTY_BATCH_LANES],
    const gf input[M55_PARTY_BATCH_LANES],
    const gf matrix[AIM3_NUM_BITS_FIELD], size_t active_lanes)
{
  if (active_lanes > M55_PARTY_BATCH_LANES)
  {
    active_lanes = M55_PARTY_BATCH_LANES;
  }
  for (size_t lane = 0; lane < active_lanes; ++lane)
  {
    gf_mat_vec_mul(output[lane], input[lane], matrix);
  }
}

void m55_gf_mat_vec_mul_add_batch4(
    gf output[M55_PARTY_BATCH_LANES],
    const gf input[M55_PARTY_BATCH_LANES],
    const gf matrix[AIM3_NUM_BITS_FIELD], size_t active_lanes)
{
  if (active_lanes > M55_PARTY_BATCH_LANES)
  {
    active_lanes = M55_PARTY_BATCH_LANES;
  }
  for (size_t lane = 0; lane < active_lanes; ++lane)
  {
    gf_mat_vec_mul_add(output[lane], input[lane], matrix);
  }
}
#endif

#if defined(AIMER_MVE_BATCH)
#include <arm_mve.h>

#if !defined(__ARM_FEATURE_MVE) || (__ARM_FEATURE_MVE != 3)
#error "AIMer party batching requires Cortex-M55 MVE"
#endif

static void m55_pack_batch4(uint32x4_t packed[M55_GF_WORDS32],
                            const gf input[M55_PARTY_BATCH_LANES],
                            size_t active_lanes)
{
  for (size_t word = 0; word < M55_GF_WORDS32; ++word)
  {
    _Alignas(16) uint32_t party_words[M55_PARTY_BATCH_LANES] = {0,};
    for (size_t lane = 0; lane < active_lanes; ++lane)
    {
      party_words[lane] = (uint32_t)(
          input[lane][word >> 1] >> (32u * (unsigned int)(word & 1u)));
    }
    packed[word] = vld1q_u32(party_words);
  }
}

static void m55_unpack_batch4(gf output[M55_PARTY_BATCH_LANES],
                              const uint32x4_t packed[M55_GF_WORDS32],
                              size_t active_lanes)
{
  for (size_t lane = 0; lane < active_lanes; ++lane)
  {
    gf_set0(output[lane]);
  }
  for (size_t word = 0; word < M55_GF_WORDS32; ++word)
  {
    _Alignas(16) uint32_t party_words[M55_PARTY_BATCH_LANES];
    vst1q_u32(party_words, packed[word]);
    for (size_t lane = 0; lane < active_lanes; ++lane)
    {
      output[lane][word >> 1] |=
          (uint64_t)party_words[lane] <<
          (32u * (unsigned int)(word & 1u));
    }
  }
}

/*
 * Reduce four products in a structure-of-arrays layout.  With
 * p(x)=x^n+r(x), the high half H is folded as L ^= H*r.  Since deg(r) is at
 * most ten, only the final 32-bit word can overflow and one more fold is
 * sufficient.  VMULLB/T.P16 multiply the low/high halfwords of all four
 * parties by r in parallel.
 */
static void m55_square_reduce_batch4(uint32x4_t state[M55_GF_WORDS32])
{
  uint32x4_t product[2u * M55_GF_WORDS32];
#if SECURITY_BITS == 128 || SECURITY_BITS == 192
  const uint16x8_t modulus = vdupq_n_u16(UINT16_C(0x87));
#elif SECURITY_BITS == 256
  const uint16x8_t modulus = vdupq_n_u16(UINT16_C(0x425));
#else
#error "Unsupported AIMer field size"
#endif

  for (size_t word = 0; word < M55_GF_WORDS32; ++word)
  {
    const uint16x8_t halves = vreinterpretq_u16_u32(state[word]);
    product[2u * word] = vmullbq_poly_p16(halves, halves);
    product[2u * word + 1u] = vmulltq_poly_p16(halves, halves);
  }

  for (size_t word = 0; word < M55_GF_WORDS32; ++word)
  {
    state[word] = product[word];
  }

  uint32x4_t overflow = vdupq_n_u32(0);
  for (size_t word = 0; word < M55_GF_WORDS32; ++word)
  {
    const uint16x8_t halves =
        vreinterpretq_u16_u32(product[M55_GF_WORDS32 + word]);
    const uint32x4_t low = vmullbq_poly_p16(halves, modulus);
    const uint32x4_t high = vmulltq_poly_p16(halves, modulus);
    state[word] = veorq_u32(state[word], low);
    state[word] = veorq_u32(state[word], vshlq_n_u32(high, 16));
    if (word + 1u < M55_GF_WORDS32)
    {
      state[word + 1u] =
          veorq_u32(state[word + 1u], vshrq_n_u32(high, 16));
    }
    else
    {
      overflow = vshrq_n_u32(high, 16);
    }
  }

  state[0] = veorq_u32(
      state[0], vmullbq_poly_p16(vreinterpretq_u16_u32(overflow), modulus));
}

void m55_gf_mul_const_batch4(gf output[M55_PARTY_BATCH_LANES],
                             const gf input[M55_PARTY_BATCH_LANES],
                             const gf constant, size_t active_lanes)
{
  uint32x4_t multiplicand[M55_GF_WORDS32];
  uint32x4_t result[M55_GF_WORDS32];
  const uint32x4_t zero = vdupq_n_u32(0);

  if (active_lanes > M55_PARTY_BATCH_LANES)
  {
    active_lanes = M55_PARTY_BATCH_LANES;
  }

  for (size_t word = 0; word < M55_GF_WORDS32; ++word)
  {
    _Alignas(16) uint32_t party_words[M55_PARTY_BATCH_LANES] = {0,};
    for (size_t lane = 0; lane < active_lanes; ++lane)
    {
      const uint64_t source = input[lane][word >> 1];
      party_words[lane] =
          (uint32_t)(source >> (32u * (unsigned int)(word & 1u)));
    }
    multiplicand[word] = vld1q_u32(party_words);
    result[word] = zero;
  }

  for (size_t bit = 0; bit < AIM3_NUM_BITS_FIELD; ++bit)
  {
    const uint32_t coefficient =
        (uint32_t)((constant[bit >> 6] >> (bit & 63u)) & UINT64_C(1));
    const uint32x4_t select = vdupq_n_u32(UINT32_C(0) - coefficient);

    for (size_t word = 0; word < M55_GF_WORDS32; ++word)
    {
      result[word] = veorq_u32(result[word],
                               vandq_u32(multiplicand[word], select));
    }

    const uint32x4_t high_bit =
        vshrq_n_u32(multiplicand[M55_GF_WORDS32 - 1u], 31);
    for (size_t word = M55_GF_WORDS32 - 1u; word > 0; --word)
    {
      multiplicand[word] = vorrq_u32(
          vshlq_n_u32(multiplicand[word], 1),
          vshrq_n_u32(multiplicand[word - 1u], 31));
    }
    multiplicand[0] = vshlq_n_u32(multiplicand[0], 1);

    const uint32x4_t reduction_mask = vsubq_u32(zero, high_bit);
#if SECURITY_BITS == 128 || SECURITY_BITS == 192
    const uint32x4_t modulus_low = vdupq_n_u32(UINT32_C(0x87));
#elif SECURITY_BITS == 256
    const uint32x4_t modulus_low = vdupq_n_u32(UINT32_C(0x425));
#else
#error "Unsupported AIMer field size"
#endif
    multiplicand[0] = veorq_u32(
        multiplicand[0], vandq_u32(reduction_mask, modulus_low));
  }

  for (size_t word = 0; word < M55_GF_WORDS32; ++word)
  {
    _Alignas(16) uint32_t party_words[M55_PARTY_BATCH_LANES];
    vst1q_u32(party_words, result[word]);
    for (size_t lane = 0; lane < active_lanes; ++lane)
    {
      const size_t output_word = word >> 1;
      const unsigned int shift = 32u * (unsigned int)(word & 1u);
      if (shift == 0u)
      {
        output[lane][output_word] = party_words[lane];
      }
      else
      {
        output[lane][output_word] |= (uint64_t)party_words[lane] << shift;
      }
    }
  }
}

void m55_gf_frobenius_batch4(gf output[M55_PARTY_BATCH_LANES],
                             const gf input[M55_PARTY_BATCH_LANES],
                             size_t exponent, size_t active_lanes)
{
  uint32x4_t state[M55_GF_WORDS32];

  if (active_lanes > M55_PARTY_BATCH_LANES)
  {
    active_lanes = M55_PARTY_BATCH_LANES;
  }
  m55_pack_batch4(state, input, active_lanes);
  for (size_t i = 0; i < exponent; ++i)
  {
    m55_square_reduce_batch4(state);
  }
  m55_unpack_batch4(output, state, active_lanes);
}

void m55_gf_sqr_batch4(gf output[M55_PARTY_BATCH_LANES],
                       const gf input[M55_PARTY_BATCH_LANES],
                       size_t active_lanes)
{
  m55_gf_frobenius_batch4(output, input, 1u, active_lanes);
}

#else

void m55_gf_mul_const_batch4(gf output[M55_PARTY_BATCH_LANES],
                             const gf input[M55_PARTY_BATCH_LANES],
                             const gf constant, size_t active_lanes)
{
  if (active_lanes > M55_PARTY_BATCH_LANES)
  {
    active_lanes = M55_PARTY_BATCH_LANES;
  }
  for (size_t lane = 0; lane < active_lanes; ++lane)
  {
    gf_mul(output[lane], input[lane], constant);
  }
}

void m55_gf_frobenius_batch4(gf output[M55_PARTY_BATCH_LANES],
                             const gf input[M55_PARTY_BATCH_LANES],
                             size_t exponent, size_t active_lanes)
{
  if (active_lanes > M55_PARTY_BATCH_LANES)
  {
    active_lanes = M55_PARTY_BATCH_LANES;
  }
  for (size_t lane = 0; lane < active_lanes; ++lane)
  {
    gf_copy(output[lane], input[lane]);
    for (size_t i = 0; i < exponent; ++i)
    {
      gf_sqr(output[lane], output[lane]);
    }
  }
}

void m55_gf_sqr_batch4(gf output[M55_PARTY_BATCH_LANES],
                       const gf input[M55_PARTY_BATCH_LANES],
                       size_t active_lanes)
{
  m55_gf_frobenius_batch4(output, input, 1u, active_lanes);
}

#endif
