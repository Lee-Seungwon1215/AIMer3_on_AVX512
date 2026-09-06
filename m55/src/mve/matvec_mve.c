// SPDX-License-Identifier: MIT

/*
 * Cortex-M55 affine kernels.  A matrix row is selected with the constant-time
 * operation acc ^= row & mask.  The single-input kernel uses the four 32-bit
 * lanes as a small output block.  The batch kernel instead assigns one party
 * to each lane so four parties share every matrix load and bit-selection
 * mask.  Inputs are copied before outputs are written to preserve in-place
 * operation.
 */

#include "field.h"
#include "m55_field_batch.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define M55_GF_WORDS32 (AIM3_NUM_BITS_FIELD / 32u)

#if defined(AIMER_MVE_PORTABLE_TEST)

static void m55_mat_vec_scalar(gf output, const gf input,
                               const gf matrix[AIM3_NUM_BITS_FIELD],
                               int add)
{
  gf source;
  gf result;
  memcpy(source, input, sizeof(source));
  if (add != 0)
  {
    memcpy(result, output, sizeof(result));
  }
  else
  {
    memset(result, 0, sizeof(result));
  }

  for (size_t bit = 0; bit < AIM3_NUM_BITS_FIELD; ++bit)
  {
    const uint64_t mask = UINT64_C(0) -
        ((source[bit >> 6] >> (bit & 63u)) & UINT64_C(1));
    for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
    {
      result[word] ^= matrix[bit][word] & mask;
    }
  }
  memcpy(output, result, sizeof(result));
}

void m55_gf_mat_vec_mul(gf output, const gf input,
                        const gf matrix[AIM3_NUM_BITS_FIELD])
{
  m55_mat_vec_scalar(output, input, matrix, 0);
}

void m55_gf_mat_vec_mul_add(gf output, const gf input,
                            const gf matrix[AIM3_NUM_BITS_FIELD])
{
  m55_mat_vec_scalar(output, input, matrix, 1);
}

void m55_gf_mat_vec_mul_batch4(
    gf output[M55_PARTY_BATCH_LANES],
    const gf input[M55_PARTY_BATCH_LANES],
    const gf matrix[AIM3_NUM_BITS_FIELD], size_t active_lanes)
{
  gf source[M55_PARTY_BATCH_LANES];
  if (active_lanes > M55_PARTY_BATCH_LANES)
  {
    active_lanes = M55_PARTY_BATCH_LANES;
  }
  for (size_t lane = 0; lane < active_lanes; ++lane)
  {
    memcpy(source[lane], input[lane], sizeof(gf));
  }
  for (size_t lane = 0; lane < active_lanes; ++lane)
  {
    m55_mat_vec_scalar(output[lane], source[lane], matrix, 0);
  }
}

void m55_gf_mat_vec_mul_add_batch4(
    gf output[M55_PARTY_BATCH_LANES],
    const gf input[M55_PARTY_BATCH_LANES],
    const gf matrix[AIM3_NUM_BITS_FIELD], size_t active_lanes)
{
  gf source[M55_PARTY_BATCH_LANES];
  if (active_lanes > M55_PARTY_BATCH_LANES)
  {
    active_lanes = M55_PARTY_BATCH_LANES;
  }
  for (size_t lane = 0; lane < active_lanes; ++lane)
  {
    memcpy(source[lane], input[lane], sizeof(gf));
  }
  for (size_t lane = 0; lane < active_lanes; ++lane)
  {
    m55_mat_vec_scalar(output[lane], source[lane], matrix, 1);
  }
}

#else

#include <arm_mve.h>

#if !defined(__ARM_FEATURE_MVE) || (__ARM_FEATURE_MVE != 3)
#error "AIMer affine backend requires Cortex-M55 integer and FP MVE"
#endif

/* The add entry point contains the MVE hot loop; mul clears its accumulator
 * after first preserving the input, then delegates here. */
__attribute__((noinline))
void m55_gf_mat_vec_mul_add(gf output, const gf input,
                            const gf matrix[AIM3_NUM_BITS_FIELD])
{
  gf source;
  uint32x4_t acc0;
#if SECURITY_BITS >= 192
  uint32x4_t acc1;
#endif
  memcpy(source, input, sizeof(source));
  memcpy(&acc0, &output[0], sizeof(acc0));
#if SECURITY_BITS == 192
  {
    _Alignas(16) uint64_t tail[2] = {output[2], UINT64_C(0)};
    memcpy(&acc1, tail, sizeof(acc1));
  }
#elif SECURITY_BITS == 256
  memcpy(&acc1, &output[2], sizeof(acc1));
#endif

  for (size_t bit = 0; bit < AIM3_NUM_BITS_FIELD; ++bit)
  {
    const uint32_t selected = (uint32_t)(
        (source[bit >> 6] >> (bit & 63u)) & UINT64_C(1));
    const uint32x4_t mask = vdupq_n_u32(UINT32_C(0) - selected);
    uint32x4_t row0;
    memcpy(&row0, &matrix[bit][0], sizeof(row0));
    acc0 = veorq_u32(acc0, vandq_u32(row0, mask));
#if SECURITY_BITS == 192
    {
      _Alignas(16) uint64_t tail[2] = {matrix[bit][2], UINT64_C(0)};
      uint32x4_t row1;
      memcpy(&row1, tail, sizeof(row1));
      acc1 = veorq_u32(acc1, vandq_u32(row1, mask));
    }
#elif SECURITY_BITS == 256
    {
      uint32x4_t row1;
      memcpy(&row1, &matrix[bit][2], sizeof(row1));
      acc1 = veorq_u32(acc1, vandq_u32(row1, mask));
    }
#endif
  }

  memcpy(&output[0], &acc0, sizeof(acc0));
#if SECURITY_BITS == 192
  {
    _Alignas(16) uint64_t tail[2];
    memcpy(tail, &acc1, sizeof(acc1));
    output[2] = tail[0];
  }
#elif SECURITY_BITS == 256
  memcpy(&output[2], &acc1, sizeof(acc1));
#endif
}

void m55_gf_mat_vec_mul(gf output, const gf input,
                        const gf matrix[AIM3_NUM_BITS_FIELD])
{
  gf source;
  memcpy(source, input, sizeof(source));
  memset(output, 0, sizeof(gf));
  m55_gf_mat_vec_mul_add(output, source, matrix);
}

/* Each output block is one 64-bit matrix word represented by two independent
 * Q accumulators.  The input bits vector and its mask are reused for both
 * halves, while the same matrix word is broadcast to all four parties. */
__attribute__((noinline))
void m55_gf_mat_vec_mul_add_batch4(
    gf output[M55_PARTY_BATCH_LANES],
    const gf input[M55_PARTY_BATCH_LANES],
    const gf matrix[AIM3_NUM_BITS_FIELD], size_t active_lanes)
{
  gf source[M55_PARTY_BATCH_LANES];
  const uint32x4_t zero = vdupq_n_u32(0);
  const uint32x4_t one = vdupq_n_u32(1);

  if (active_lanes > M55_PARTY_BATCH_LANES)
  {
    active_lanes = M55_PARTY_BATCH_LANES;
  }
  if (active_lanes == 0u)
  {
    return;
  }
  memset(source, 0, sizeof(source));
  for (size_t lane = 0; lane < active_lanes; ++lane)
  {
    memcpy(source[lane], input[lane], sizeof(gf));
  }

  for (size_t output_word = 0; output_word < AIM3_NUM_WORDS_FIELD;
       ++output_word)
  {
    _Alignas(16) uint32_t low_words[M55_PARTY_BATCH_LANES] = {0,};
    _Alignas(16) uint32_t high_words[M55_PARTY_BATCH_LANES] = {0,};
    for (size_t lane = 0; lane < active_lanes; ++lane)
    {
      low_words[lane] = (uint32_t)output[lane][output_word];
      high_words[lane] = (uint32_t)(output[lane][output_word] >> 32);
    }
    uint32x4_t acc_low = vld1q_u32(low_words);
    uint32x4_t acc_high = vld1q_u32(high_words);

    for (size_t input_word = 0; input_word < M55_GF_WORDS32; ++input_word)
    {
      _Alignas(16) uint32_t party_words[M55_PARTY_BATCH_LANES] = {0,};
      for (size_t lane = 0; lane < active_lanes; ++lane)
      {
        party_words[lane] = (uint32_t)(
            source[lane][input_word >> 1] >>
            (32u * (unsigned int)(input_word & 1u)));
      }
      uint32x4_t bits = vld1q_u32(party_words);

      for (size_t offset = 0; offset < 32u; ++offset)
      {
        const size_t bit = 32u * input_word + offset;
        const uint32x4_t selected = vandq_u32(bits, one);
        const uint32x4_t mask = vsubq_u32(zero, selected);
        const uint64_t row = matrix[bit][output_word];
        const uint32x4_t row_low = vdupq_n_u32((uint32_t)row);
        const uint32x4_t row_high = vdupq_n_u32((uint32_t)(row >> 32));
        acc_low = veorq_u32(acc_low, vandq_u32(row_low, mask));
        acc_high = veorq_u32(acc_high, vandq_u32(row_high, mask));
        bits = vshrq_n_u32(bits, 1);
      }
    }

    vst1q_u32(low_words, acc_low);
    vst1q_u32(high_words, acc_high);
    for (size_t lane = 0; lane < active_lanes; ++lane)
    {
      output[lane][output_word] =
          (uint64_t)low_words[lane] | ((uint64_t)high_words[lane] << 32);
    }
  }
}

void m55_gf_mat_vec_mul_batch4(
    gf output[M55_PARTY_BATCH_LANES],
    const gf input[M55_PARTY_BATCH_LANES],
    const gf matrix[AIM3_NUM_BITS_FIELD], size_t active_lanes)
{
  gf source[M55_PARTY_BATCH_LANES];
  if (active_lanes > M55_PARTY_BATCH_LANES)
  {
    active_lanes = M55_PARTY_BATCH_LANES;
  }
  for (size_t lane = 0; lane < active_lanes; ++lane)
  {
    memcpy(source[lane], input[lane], sizeof(gf));
    memset(output[lane], 0, sizeof(gf));
  }
  m55_gf_mat_vec_mul_add_batch4(output, source, matrix, active_lanes);
}

#endif

/* Selected by field.h callers only in the explicit MATVEC=mve build. */
void gf_mat_vec_mul(gf output, const gf input,
                    const gf matrix[AIM3_NUM_BITS_FIELD])
{
  m55_gf_mat_vec_mul(output, input, matrix);
}

/* The m55 Makefile wraps the namespaced reference field-common add symbol in
 * the explicit D build so every ordinary gf_mat_vec_mul_add caller reaches
 * the direct MVE accumulator instead of materializing a temporary result. */
#define M55_JOIN_INNER(left, right) left##right
#define M55_JOIN(left, right) M55_JOIN_INNER(left, right)
#define M55_WRAP_NAMESPACE(name) M55_JOIN(__wrap_, AIMER_NAMESPACE(name))

#undef gf_mat_vec_mul_add
void M55_WRAP_NAMESPACE(gf_mat_vec_mul_add)(
    gf output, const gf input, const gf matrix[AIM3_NUM_BITS_FIELD])
{
  m55_gf_mat_vec_mul_add(output, input, matrix);
}
