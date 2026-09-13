// SPDX-License-Identifier: MIT

/*
 * Cause-analysis profile for the M55 port.  This is deliberately separate
 * from the release benchmark so adding diagnostic kernels cannot change its
 * recorded end-to-end input stream or memory high-water marks.
 */

#include "aim3.h"
#include "field.h"
#include "m55_field_batch.h"
#include "m55_mpc_batch.h"
#include "m55_platform.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef PROFILE_SAMPLES
#define PROFILE_SAMPLES 31u
#endif

#ifndef PROFILE_KERNEL_INNER
#define PROFILE_KERNEL_INNER 64u
#endif

#ifndef PROFILE_COMPLEX_INNER
#define PROFILE_COMPLEX_INNER 8u
#endif

#if PROFILE_SAMPLES < 1 || PROFILE_KERNEL_INNER < 1 || \
    PROFILE_COMPLEX_INNER < 1
#error "Profile sample and inner-loop counts must be positive"
#endif

#define M55_PROFILE_LIMBS (AIM3_NUM_BITS_FIELD / 16u)
#define M55_PROFILE_PADDED_LIMBS ((M55_PROFILE_LIMBS + 7u) & ~7u)

static aim_lin_t linear;
static tape_t tapes[M55_PARTY_BATCH_LANES];
static mult_chk_t checks[M55_PARTY_BATCH_LANES];
static gf batch_input[M55_PARTY_BATCH_LANES];
static gf batch_output[M55_PARTY_BATCH_LANES];
static gf field_a;
static gf field_b;
static gf field_output;
static volatile uint64_t profile_checksum;
static uint64_t prng_state = UINT64_C(0x6a09e667f3bcc909);

static uint64_t next_word(void)
{
  uint64_t value = prng_state;
  value ^= value << 13;
  value ^= value >> 7;
  value ^= value << 17;
  prng_state = value;
  return value;
}

static void fill_field(gf value)
{
  for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
  {
    value[word] = next_word();
  }
}

static void fill_linear(void)
{
  for (size_t matrix = 0; matrix < 2u * AIMER_L; ++matrix)
  {
    for (size_t row = 0; row < AIM3_NUM_BITS_FIELD; ++row)
    {
      fill_field(linear.mat_A[matrix][row]);
    }
  }
  for (size_t vector = 0; vector < AIMER_L + 1u; ++vector)
  {
    fill_field(linear.vec_b[vector]);
  }
}

static void fill_tapes(void)
{
  for (size_t lane = 0; lane < M55_PARTY_BATCH_LANES; ++lane)
  {
    fill_field(tapes[lane].pt_share);
    for (size_t ell = 0; ell < AIMER_L; ++ell)
    {
      fill_field(tapes[lane].y_shares[ell]);
    }
    for (size_t ell = 0; ell < AIMER_L + 1u; ++ell)
    {
      fill_field(tapes[lane].a_shares[ell]);
    }
    fill_field(tapes[lane].c_share);
  }
}

static void sort_samples(uint64_t samples[PROFILE_SAMPLES])
{
  for (size_t i = 1; i < PROFILE_SAMPLES; ++i)
  {
    const uint64_t value = samples[i];
    size_t position = i;
    while (position > 0u && samples[position - 1u] > value)
    {
      samples[position] = samples[position - 1u];
      --position;
    }
    samples[position] = value;
  }
}

static void print_stats(const char *operation, const uint64_t *samples,
                        size_t inner, size_t items)
{
  uint64_t sorted[PROFILE_SAMPLES];
  double mean = 0.0;
  double variance = 0.0;

  memcpy(sorted, samples, sizeof(sorted));
  sort_samples(sorted);
  for (size_t sample = 0; sample < PROFILE_SAMPLES; ++sample)
  {
    mean += (double)samples[sample] / (double)inner;
  }
  mean /= (double)PROFILE_SAMPLES;
  for (size_t sample = 0; sample < PROFILE_SAMPLES; ++sample)
  {
    const double difference =
        (double)samples[sample] / (double)inner - mean;
    variance += difference * difference;
  }
  variance /= (double)PROFILE_SAMPLES;

  for (size_t sample = 0; sample < PROFILE_SAMPLES; ++sample)
  {
    printf("PROFILE_SAMPLE,param=%s,config=%s,backend=%s,matvec=%s,"
           "operation=%s,sample=%lu,inner=%lu,items=%lu,raw_cycles=%llu\n",
           xstr(PARAMS), xstr(AIMER_CONFIG), xstr(AIMER_BACKEND),
           xstr(AIMER_MATVEC), operation, (unsigned long)sample,
           (unsigned long)inner, (unsigned long)items,
           (unsigned long long)samples[sample]);
  }

  printf("PROFILE_RESULT,param=%s,config=%s,backend=%s,matvec=%s,"
         "operation=%s,samples=%u,"
         "inner=%lu,items=%lu,min=%.2f,median=%.2f,mean=%.2f,"
         "stddev=%.2f,max=%.2f,cycles_per_item_mean=%.2f\n",
         xstr(PARAMS), xstr(AIMER_CONFIG), xstr(AIMER_BACKEND),
         xstr(AIMER_MATVEC), operation,
         (unsigned int)PROFILE_SAMPLES, (unsigned long)inner,
         (unsigned long)items, (double)sorted[0] / (double)inner,
         (double)sorted[PROFILE_SAMPLES / 2u] / (double)inner, mean,
         sqrt(variance),
         (double)sorted[PROFILE_SAMPLES - 1u] / (double)inner,
         mean / (double)items);
}

static void profile_basic_kernels(void)
{
  uint64_t samples[PROFILE_SAMPLES];

  for (size_t sample = 0; sample < PROFILE_SAMPLES; ++sample)
  {
    fill_field(field_a);
    fill_field(field_b);
    m55_measure_start();
    for (size_t inner = 0; inner < PROFILE_KERNEL_INNER; ++inner)
    {
      gf_mul(field_output, field_a, field_b);
    }
    samples[sample] = m55_measure_end();
    profile_checksum ^= field_output[sample % AIM3_NUM_WORDS_FIELD];
  }
  print_stats("gf_mul", samples, PROFILE_KERNEL_INNER, 1u);

  for (size_t sample = 0; sample < PROFILE_SAMPLES; ++sample)
  {
    fill_field(field_a);
    m55_measure_start();
    for (size_t inner = 0; inner < PROFILE_KERNEL_INNER; ++inner)
    {
      gf_sqr(field_output, field_a);
    }
    samples[sample] = m55_measure_end();
    profile_checksum ^= field_output[sample % AIM3_NUM_WORDS_FIELD];
  }
  print_stats("gf_sqr", samples, PROFILE_KERNEL_INNER, 1u);

  for (size_t sample = 0; sample < PROFILE_SAMPLES; ++sample)
  {
    fill_field(field_a);
    m55_measure_start();
    for (size_t inner = 0; inner < PROFILE_COMPLEX_INNER; ++inner)
    {
      gf_inv(field_output, field_a);
    }
    samples[sample] = m55_measure_end();
    profile_checksum ^= field_output[sample % AIM3_NUM_WORDS_FIELD];
  }
  print_stats("gf_inv", samples, PROFILE_COMPLEX_INNER, 1u);

  for (size_t sample = 0; sample < PROFILE_SAMPLES; ++sample)
  {
    fill_field(field_a);
    m55_measure_start();
    for (size_t inner = 0; inner < PROFILE_KERNEL_INNER; ++inner)
    {
      gf_mat_vec_mul(field_output, field_a, linear.mat_A[0]);
    }
    samples[sample] = m55_measure_end();
    profile_checksum ^= field_output[sample % AIM3_NUM_WORDS_FIELD];
  }
  print_stats("gf_mat_vec_mul", samples, PROFILE_KERNEL_INNER, 1u);

  for (size_t sample = 0; sample < PROFILE_SAMPLES; ++sample)
  {
    fill_field(field_a);
    fill_field(field_output);
    m55_measure_start();
    for (size_t inner = 0; inner < PROFILE_KERNEL_INNER; ++inner)
    {
      gf_mat_vec_mul_add(field_output, field_a, linear.mat_A[0]);
    }
    samples[sample] = m55_measure_end();
    profile_checksum ^= field_output[sample % AIM3_NUM_WORDS_FIELD];
  }
  print_stats("gf_mat_vec_mul_add", samples, PROFILE_KERNEL_INNER, 1u);
}

static void profile_batch_kernels(void)
{
  uint64_t samples[PROFILE_SAMPLES];

  for (size_t sample = 0; sample < PROFILE_SAMPLES; ++sample)
  {
    for (size_t lane = 0; lane < M55_PARTY_BATCH_LANES; ++lane)
    {
      fill_field(batch_input[lane]);
    }
    m55_measure_start();
    for (size_t inner = 0; inner < PROFILE_KERNEL_INNER; ++inner)
    {
      m55_gf_sqr_batch4(batch_output, batch_input,
                        M55_PARTY_BATCH_LANES);
    }
    samples[sample] = m55_measure_end();
    profile_checksum ^=
        batch_output[sample % M55_PARTY_BATCH_LANES]
                    [sample % AIM3_NUM_WORDS_FIELD];
  }
  print_stats("gf_sqr_batch4", samples, PROFILE_KERNEL_INNER,
              M55_PARTY_BATCH_LANES);

  for (size_t ell = 0; ell < AIMER_L + 1u; ++ell)
  {
    char operation[40];
    (void)snprintf(operation, sizeof(operation),
                   "gf_frobenius_batch4_e%lu",
                   (unsigned long)aim3_exponents[ell]);
    for (size_t sample = 0; sample < PROFILE_SAMPLES; ++sample)
    {
      for (size_t lane = 0; lane < M55_PARTY_BATCH_LANES; ++lane)
      {
        fill_field(batch_input[lane]);
      }
      m55_measure_start();
      for (size_t inner = 0; inner < PROFILE_COMPLEX_INNER; ++inner)
      {
        m55_gf_frobenius_batch4(batch_output, batch_input,
                                aim3_exponents[ell],
                                M55_PARTY_BATCH_LANES);
      }
      samples[sample] = m55_measure_end();
      profile_checksum ^=
          batch_output[sample % M55_PARTY_BATCH_LANES]
                      [sample % AIM3_NUM_WORDS_FIELD];
    }
    print_stats(operation, samples, PROFILE_COMPLEX_INNER,
                M55_PARTY_BATCH_LANES);
  }

  for (size_t sample = 0; sample < PROFILE_SAMPLES; ++sample)
  {
    fill_field(field_b);
    for (size_t lane = 0; lane < M55_PARTY_BATCH_LANES; ++lane)
    {
      fill_field(batch_input[lane]);
    }
    m55_measure_start();
    for (size_t inner = 0; inner < PROFILE_KERNEL_INNER; ++inner)
    {
      m55_gf_mul_const_batch4(batch_output, batch_input, field_b,
                              M55_PARTY_BATCH_LANES);
    }
    samples[sample] = m55_measure_end();
    profile_checksum ^=
        batch_output[sample % M55_PARTY_BATCH_LANES]
                    [sample % AIM3_NUM_WORDS_FIELD];
  }
  print_stats("gf_mul_const_batch4", samples, PROFILE_KERNEL_INNER,
              M55_PARTY_BATCH_LANES);

  for (size_t sample = 0; sample < PROFILE_SAMPLES; ++sample)
  {
    for (size_t lane = 0; lane < M55_PARTY_BATCH_LANES; ++lane)
    {
      fill_field(batch_input[lane]);
    }
    m55_measure_start();
    for (size_t inner = 0; inner < PROFILE_KERNEL_INNER; ++inner)
    {
      m55_gf_mat_vec_mul_batch4(batch_output, batch_input,
                                linear.mat_A[0],
                                M55_PARTY_BATCH_LANES);
    }
    samples[sample] = m55_measure_end();
    profile_checksum ^=
        batch_output[sample % M55_PARTY_BATCH_LANES]
                    [sample % AIM3_NUM_WORDS_FIELD];
  }
  print_stats("gf_mat_vec_mul_batch4", samples, PROFILE_KERNEL_INNER,
              M55_PARTY_BATCH_LANES);

  for (size_t sample = 0; sample < PROFILE_SAMPLES; ++sample)
  {
    for (size_t lane = 0; lane < M55_PARTY_BATCH_LANES; ++lane)
    {
      fill_field(batch_input[lane]);
      fill_field(batch_output[lane]);
    }
    m55_measure_start();
    for (size_t inner = 0; inner < PROFILE_KERNEL_INNER; ++inner)
    {
      m55_gf_mat_vec_mul_add_batch4(batch_output, batch_input,
                                    linear.mat_A[0],
                                    M55_PARTY_BATCH_LANES);
    }
    samples[sample] = m55_measure_end();
    profile_checksum ^=
        batch_output[sample % M55_PARTY_BATCH_LANES]
                    [sample % AIM3_NUM_WORDS_FIELD];
  }
  print_stats("gf_mat_vec_mul_add_batch4", samples,
              PROFILE_KERNEL_INNER, M55_PARTY_BATCH_LANES);
}

static void profile_mpc(void)
{
  uint64_t samples[PROFILE_SAMPLES];
  uint64_t affine_samples[PROFILE_SAMPLES];
  uint64_t frobenius_samples[PROFILE_SAMPLES];
  gf ciphertext;
  fill_field(ciphertext);

  for (size_t sample = 0; sample < PROFILE_SAMPLES; ++sample)
  {
    fill_tapes();
    m55_mpc_profile_reset();
    m55_measure_start();
    for (size_t inner = 0; inner < PROFILE_COMPLEX_INNER; ++inner)
    {
      m55_aim3_mpc_batch4(checks, &linear, tapes, ciphertext, 0u,
                          M55_PARTY_BATCH_LANES);
    }
    samples[sample] = m55_measure_end();
    affine_samples[sample] = m55_mpc_profile_affine_cycles();
    frobenius_samples[sample] = m55_mpc_profile_frobenius_cycles();
    profile_checksum ^=
        checks[sample % M55_PARTY_BATCH_LANES]
              .z_shares[sample % (AIMER_L + 1u)]
                       [sample % AIM3_NUM_WORDS_FIELD];
  }
  print_stats("aim3_mpc_batch4", samples, PROFILE_COMPLEX_INNER,
              M55_PARTY_BATCH_LANES);
  print_stats("aim3_mpc_batch4_affine", affine_samples,
              PROFILE_COMPLEX_INNER, M55_PARTY_BATCH_LANES);
  print_stats("aim3_mpc_batch4_frobenius", frobenius_samples,
              PROFILE_COMPLEX_INNER, M55_PARTY_BATCH_LANES);

  for (size_t sample = 0; sample < PROFILE_SAMPLES; ++sample)
  {
    fill_tapes();
    m55_measure_start();
    for (size_t inner = 0; inner < PROFILE_COMPLEX_INNER; ++inner)
    {
      aim3_mpc(&checks[0], &linear, &tapes[0], ciphertext, 0u);
    }
    samples[sample] = m55_measure_end();
    profile_checksum ^=
        checks[0].z_shares[sample % (AIMER_L + 1u)]
                          [sample % AIM3_NUM_WORDS_FIELD];
  }
  print_stats("aim3_mpc_scalar", samples, PROFILE_COMPLEX_INNER, 1u);
}

int main(void)
{
  size_t exponent_sum = 0u;
  for (size_t ell = 0; ell < AIMER_L + 1u; ++ell)
  {
    exponent_sum += aim3_exponents[ell];
  }

  const size_t party_batches =
      (AIMER_N + M55_PARTY_BATCH_LANES - 1u) / M55_PARTY_BATCH_LANES;
  const size_t keygen_inversions = AIMER_L + 1u;
  const size_t keygen_multiplications = keygen_inversions * SECURITY_BITS;
  const size_t keygen_squarings =
      keygen_inversions * (SECURITY_BITS - 1u) + exponent_sum;
  const size_t linear_generation_matvec =
      2u * AIMER_L * AIM3_NUM_BITS_FIELD;
  const size_t keygen_matvec = linear_generation_matvec + 2u * AIMER_L;
  const size_t sign_batch_mpc_calls = 2u * AIMER_T * party_batches;
  const size_t verify_batch_mpc_calls = AIMER_T * party_batches;
  const size_t challenge_batch_mul_calls =
      3u * (AIMER_L + 1u) * AIMER_T * party_batches;

  printf("PROFILE_CONFIG,param=%s,config=%s,backend=%s,matvec=%s,cpu_hz=%lu,"
         "bits=%u,"
         "limbs16=%u,padded_limbs16=%u,L=%u,N=%u,T=%u,exponent_sum=%lu\n",
         xstr(PARAMS), xstr(AIMER_CONFIG), xstr(AIMER_BACKEND),
         xstr(AIMER_MATVEC),
         (unsigned long)m55_cpu_hz(), (unsigned int)SECURITY_BITS,
         (unsigned int)M55_PROFILE_LIMBS,
         (unsigned int)M55_PROFILE_PADDED_LIMBS,
         (unsigned int)AIMER_L, (unsigned int)AIMER_N,
         (unsigned int)AIMER_T, (unsigned long)exponent_sum);
  printf("PROFILE_COUNTS,param=%s,keygen_inv=%lu,keygen_mul=%lu,"
         "keygen_sqr=%lu,linear_generation_matvec=%lu,"
         "keygen_total_matvec=%lu,sign_batch_mpc=%lu,"
         "sign_scalar_mpc=%u,verify_batch_mpc=%lu,"
         "challenge_batch_mul_sign=%lu,challenge_batch_mul_verify=%lu\n",
         xstr(PARAMS), (unsigned long)keygen_inversions,
         (unsigned long)keygen_multiplications,
         (unsigned long)keygen_squarings,
         (unsigned long)linear_generation_matvec,
         (unsigned long)keygen_matvec,
         (unsigned long)sign_batch_mpc_calls, (unsigned int)AIMER_T,
         (unsigned long)verify_batch_mpc_calls,
         (unsigned long)challenge_batch_mul_calls,
         (unsigned long)challenge_batch_mul_calls);

  fill_linear();
  profile_basic_kernels();
  profile_batch_kernels();
  profile_mpc();

  printf("PROFILE_PASS,param=%s,config=%s,backend=%s,matvec=%s,"
         "checksum=%08lx%08lx\n",
         xstr(PARAMS), xstr(AIMER_CONFIG), xstr(AIMER_BACKEND),
         xstr(AIMER_MATVEC),
         (unsigned long)(profile_checksum >> 32),
         (unsigned long)(profile_checksum & UINT32_MAX));
  return 0;
}
