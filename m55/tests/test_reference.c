// SPDX-License-Identifier: MIT

#include "field.h"
#include "m55_field_batch.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t prng_state = UINT64_C(0x8f3d9a27c4b165e0);

static uint64_t next_word(void)
{
  uint64_t x = prng_state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  prng_state = x;
  return x;
}

static void fill_random(gf value)
{
  for (size_t i = 0; i < AIM3_NUM_WORDS_FIELD; ++i)
  {
    value[i] = next_word();
  }
}

static uint64_t modulus_low(void)
{
#if SECURITY_BITS == 128 || SECURITY_BITS == 192
  return UINT64_C(0x87);
#elif SECURITY_BITS == 256
  return UINT64_C(0x425);
#else
#error "Unsupported AIMer field size"
#endif
}

/* Independent, deliberately simple GF(2) multiplier used only by tests. */
static void generic_mul(gf out, const gf a, const gf b)
{
  uint64_t product[2 * AIM3_NUM_WORDS_FIELD];
  memset(product, 0, sizeof(product));

  for (size_t bit = 0; bit < AIM3_NUM_BITS_FIELD; ++bit)
  {
    const uint64_t mask = UINT64_C(0) -
                          ((b[bit / 64] >> (bit % 64)) & UINT64_C(1));
    const size_t word_shift = bit / 64;
    const unsigned int bit_shift = (unsigned int)(bit % 64);

    for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
    {
      product[word_shift + word] ^= (a[word] << bit_shift) & mask;
      if ((bit_shift != 0u) &&
          (word_shift + word + 1 < 2 * AIM3_NUM_WORDS_FIELD))
      {
        product[word_shift + word + 1] ^=
            (a[word] >> (64u - bit_shift)) & mask;
      }
    }
  }

  for (size_t degree = 2 * AIM3_NUM_BITS_FIELD - 2;
       degree >= AIM3_NUM_BITS_FIELD; --degree)
  {
    const size_t source_word = degree / 64;
    const unsigned int source_bit = (unsigned int)(degree % 64);
    const uint64_t mask = UINT64_C(0) -
                          ((product[source_word] >> source_bit) & UINT64_C(1));
    const size_t shift = degree - AIM3_NUM_BITS_FIELD;
    const size_t target_word = shift / 64;
    const unsigned int target_bit = (unsigned int)(shift % 64);
    const uint64_t polynomial = modulus_low();

    product[source_word] ^= (UINT64_C(1) << source_bit) & mask;
    product[target_word] ^= (polynomial << target_bit) & mask;
    if ((target_bit != 0u) &&
        (target_word + 1 < 2 * AIM3_NUM_WORDS_FIELD))
    {
      product[target_word + 1] ^=
          (polynomial >> (64u - target_bit)) & mask;
    }
  }

  memcpy(out, product, sizeof(gf));
}

static void generic_matrix(gf out, const gf a,
                           const gf matrix[AIM3_NUM_BITS_FIELD])
{
  memset(out, 0, sizeof(gf));
  for (size_t bit = 0; bit < AIM3_NUM_BITS_FIELD; ++bit)
  {
    const uint64_t mask = UINT64_C(0) -
                          ((a[bit / 64] >> (bit % 64)) & UINT64_C(1));
    for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
    {
      out[word] ^= matrix[bit][word] & mask;
    }
  }
}

static int compare(const char *operation, size_t test_case,
                   const gf expected, const gf actual)
{
  for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
  {
    if (expected[word] != actual[word])
    {
      printf("REF_GF_FAIL param=%s op=%s case=%lu word=%lu "
             "expected=%08lx%08lx actual=%08lx%08lx\n",
             xstr(PARAMS), operation, (unsigned long)test_case,
             (unsigned long)word,
             (unsigned long)(expected[word] >> 32),
             (unsigned long)(expected[word] & UINT32_MAX),
             (unsigned long)(actual[word] >> 32),
             (unsigned long)(actual[word] & UINT32_MAX));
      return -1;
    }
  }
  return 0;
}

static int test_pair(const gf a, const gf b, size_t test_case)
{
  gf expected;
  gf actual;

  generic_mul(expected, a, b);
  gf_mul(actual, a, b);
  if (compare("mul", test_case, expected, actual) != 0)
  {
    return -1;
  }

  generic_mul(expected, a, a);
  gf_sqr(actual, a);
  return compare("sqr", test_case, expected, actual);
}

static int test_matrix_inputs(const gf matrix[AIM3_NUM_BITS_FIELD])
{
  struct guarded_field
  {
    uint64_t before;
    gf value;
    uint64_t after;
  } input_guard, output_guard;
  gf expected;
  gf initial;
  size_t test_case = 0u;

  input_guard.before = UINT64_C(0x13579bdf2468ace0);
  input_guard.after = UINT64_C(0xfdb97531eca86420);
  output_guard.before = UINT64_C(0x0123456789abcdef);
  output_guard.after = UINT64_C(0xfedcba9876543210);

  for (size_t pattern = 0; pattern < AIM3_NUM_BITS_FIELD + 3u; ++pattern)
  {
    gf_set0(input_guard.value);
    if (pattern == 1u)
    {
      input_guard.value[0] = 1u;
    }
    else if (pattern >= 2u && pattern < AIM3_NUM_BITS_FIELD + 2u)
    {
      const size_t bit = pattern - 2u;
      input_guard.value[bit >> 6] = UINT64_C(1) << (bit & 63u);
    }
    else if (pattern == AIM3_NUM_BITS_FIELD + 2u)
    {
      for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
      {
        input_guard.value[word] = UINT64_MAX;
      }
    }

    generic_matrix(expected, input_guard.value, matrix);
    gf_mat_vec_mul(output_guard.value, input_guard.value, matrix);
    if (compare("matrix_edge", test_case, expected, output_guard.value) != 0)
    {
      return -1;
    }

    fill_random(initial);
    gf_copy(output_guard.value, initial);
    for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
    {
      expected[word] ^= initial[word];
    }
    gf_mat_vec_mul_add(output_guard.value, input_guard.value, matrix);
    if (compare("matrix_add_edge", test_case, expected,
                output_guard.value) != 0)
    {
      return -1;
    }

    gf_copy(output_guard.value, input_guard.value);
    generic_matrix(expected, input_guard.value, matrix);
    gf_mat_vec_mul(output_guard.value, output_guard.value, matrix);
    if (compare("matrix_alias", test_case, expected, output_guard.value) != 0)
    {
      return -1;
    }

    gf_copy(output_guard.value, input_guard.value);
    gf_copy(initial, input_guard.value);
    generic_matrix(expected, input_guard.value, matrix);
    for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
    {
      expected[word] ^= initial[word];
    }
    gf_mat_vec_mul_add(output_guard.value, output_guard.value, matrix);
    if (compare("matrix_add_alias", test_case, expected,
                output_guard.value) != 0)
    {
      return -1;
    }
    ++test_case;
  }

  for (size_t round = 0; round < 32u; ++round)
  {
    fill_random(input_guard.value);
    generic_matrix(expected, input_guard.value, matrix);
    gf_mat_vec_mul(output_guard.value, input_guard.value, matrix);
    if (compare("matrix_random", test_case++, expected,
                output_guard.value) != 0)
    {
      return -1;
    }
  }

  if (input_guard.before != UINT64_C(0x13579bdf2468ace0) ||
      input_guard.after != UINT64_C(0xfdb97531eca86420) ||
      output_guard.before != UINT64_C(0x0123456789abcdef) ||
      output_guard.after != UINT64_C(0xfedcba9876543210))
  {
    printf("REF_GF_FAIL param=%s op=matrix_guard\n", xstr(PARAMS));
    return -1;
  }

#if defined(AIMER_M55_MVE_MATVEC)
  fill_random(input_guard.value);
  fill_random(output_guard.value);
  gf_copy(initial, output_guard.value);
  generic_matrix(expected, input_guard.value, matrix);
  for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
  {
    expected[word] ^= initial[word];
  }
  m55_gf_mat_vec_mul_add(output_guard.value, input_guard.value, matrix);
  if (compare("mve_matrix_add_direct", test_case, expected,
              output_guard.value) != 0)
  {
    return -1;
  }
#endif
  return 0;
}

static int test_matrix_batch4(const gf matrix[AIM3_NUM_BITS_FIELD])
{
  gf input[M55_PARTY_BATCH_LANES];
  gf output[M55_PARTY_BATCH_LANES];
  gf initial[M55_PARTY_BATCH_LANES];
  gf expected;

  gf_set0(input[0]);
  gf_set0(input[1]);
  input[1][0] = 1u;
  gf_set0(input[2]);
  input[2][AIM3_NUM_WORDS_FIELD - 1u] = UINT64_C(1) << 63;
  fill_random(input[3]);

  for (size_t active_lanes = 0u;
       active_lanes <= M55_PARTY_BATCH_LANES; ++active_lanes)
  {
    for (size_t lane = 0; lane < M55_PARTY_BATCH_LANES; ++lane)
    {
      fill_random(output[lane]);
      gf_copy(initial[lane], output[lane]);
    }
    m55_gf_mat_vec_mul_batch4(output, input, matrix, active_lanes);
    for (size_t lane = 0; lane < active_lanes; ++lane)
    {
      generic_matrix(expected, input[lane], matrix);
      if (compare("matrix_batch4", 8u * active_lanes + lane,
                  expected, output[lane]) != 0)
      {
        return -1;
      }
    }
    for (size_t lane = active_lanes; lane < M55_PARTY_BATCH_LANES; ++lane)
    {
      if (memcmp(output[lane], initial[lane], sizeof(gf)) != 0)
      {
        printf("REF_GF_FAIL param=%s op=matrix_batch4_inactive lane=%lu\n",
               xstr(PARAMS), (unsigned long)lane);
        return -1;
      }
    }

    for (size_t lane = 0; lane < M55_PARTY_BATCH_LANES; ++lane)
    {
      fill_random(output[lane]);
      gf_copy(initial[lane], output[lane]);
    }
    m55_gf_mat_vec_mul_add_batch4(output, input, matrix, active_lanes);
    for (size_t lane = 0; lane < active_lanes; ++lane)
    {
      generic_matrix(expected, input[lane], matrix);
      for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
      {
        expected[word] ^= initial[lane][word];
      }
      if (compare("matrix_add_batch4", 8u * active_lanes + lane,
                  expected, output[lane]) != 0)
      {
        return -1;
      }
    }

    for (size_t lane = 0; lane < M55_PARTY_BATCH_LANES; ++lane)
    {
      gf_copy(output[lane], input[lane]);
      gf_copy(initial[lane], input[lane]);
    }
    m55_gf_mat_vec_mul_batch4(output, output, matrix, active_lanes);
    for (size_t lane = 0; lane < active_lanes; ++lane)
    {
      generic_matrix(expected, initial[lane], matrix);
      if (compare("matrix_batch4_alias", 8u * active_lanes + lane,
                  expected, output[lane]) != 0)
      {
        return -1;
      }
    }

    for (size_t lane = 0; lane < M55_PARTY_BATCH_LANES; ++lane)
    {
      gf_copy(output[lane], input[lane]);
      gf_copy(initial[lane], input[lane]);
    }
    m55_gf_mat_vec_mul_add_batch4(output, output, matrix, active_lanes);
    for (size_t lane = 0; lane < active_lanes; ++lane)
    {
      generic_matrix(expected, initial[lane], matrix);
      for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
      {
        expected[word] ^= initial[lane][word];
      }
      if (compare("matrix_add_batch4_alias", 8u * active_lanes + lane,
                  expected, output[lane]) != 0)
      {
        return -1;
      }
    }
  }
  return 0;
}

int main(void)
{
  gf a = {0};
  gf b = {0};
  gf expected;
  gf add_expected;
  gf actual;
  gf matrix[AIM3_NUM_BITS_FIELD];
  size_t test_case = 0;

  fill_random(b);
  if (test_pair(a, b, test_case++) != 0)
  {
    return 1;
  }

  a[0] = 1;
  if (test_pair(a, b, test_case++) != 0)
  {
    return 1;
  }

  for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
  {
    a[word] = UINT64_MAX;
    b[word] = UINT64_MAX;
  }
  if (test_pair(a, b, test_case++) != 0)
  {
    return 1;
  }

  fill_random(b);
  for (size_t bit = 0; bit < AIM3_NUM_BITS_FIELD; ++bit)
  {
    memset(a, 0, sizeof(a));
    a[bit / 64] = UINT64_C(1) << (bit % 64);
    if (test_pair(a, b, test_case++) != 0)
    {
      return 1;
    }
  }

  for (size_t round = 0; round < 128; ++round)
  {
    fill_random(a);
    fill_random(b);
    if (test_pair(a, b, test_case++) != 0)
    {
      return 1;
    }
  }

  for (size_t bit = 0; bit < AIM3_NUM_BITS_FIELD; ++bit)
  {
    fill_random(matrix[bit]);
  }
  if (test_matrix_inputs(matrix) != 0 || test_matrix_batch4(matrix) != 0)
  {
    return 1;
  }
  for (size_t round = 0; round < 16; ++round)
  {
    fill_random(a);
    generic_matrix(expected, a, matrix);
    gf_mat_vec_mul(actual, a, matrix);
    if (compare("matrix", round, expected, actual) != 0)
    {
      return 1;
    }

    fill_random(b);
    for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
    {
      add_expected[word] = b[word] ^ expected[word];
      actual[word] = b[word];
    }
    gf_mat_vec_mul_add(actual, a, matrix);
    if (compare("matrix_add", round, add_expected, actual) != 0)
    {
      return 1;
    }
  }

  for (size_t round = 0; round < 16; ++round)
  {
    gf batch_input[M55_PARTY_BATCH_LANES];
    gf batch_output[M55_PARTY_BATCH_LANES];
    gf constant;
    fill_random(constant);
    for (size_t lane = 0; lane < M55_PARTY_BATCH_LANES; ++lane)
    {
      fill_random(batch_input[lane]);
    }
    m55_gf_mul_const_batch4(batch_output, batch_input, constant,
                            M55_PARTY_BATCH_LANES);
    for (size_t lane = 0; lane < M55_PARTY_BATCH_LANES; ++lane)
    {
      generic_mul(expected, batch_input[lane], constant);
      if (compare("batch4", 4u * round + lane, expected,
                  batch_output[lane]) != 0)
      {
        return 1;
      }
    }
  }

  for (size_t round = 0; round < 16; ++round)
  {
    gf batch_input[M55_PARTY_BATCH_LANES];
    gf batch_output[M55_PARTY_BATCH_LANES];
    const size_t active_lanes = 1u + (round % M55_PARTY_BATCH_LANES);
    const size_t exponent = 1u + (round % 13u);
    for (size_t lane = 0; lane < M55_PARTY_BATCH_LANES; ++lane)
    {
      fill_random(batch_input[lane]);
      gf_set0(batch_output[lane]);
    }
    m55_gf_frobenius_batch4(batch_output, batch_input, exponent,
                            active_lanes);
    for (size_t lane = 0; lane < active_lanes; ++lane)
    {
      gf_copy(expected, batch_input[lane]);
      for (size_t i = 0; i < exponent; ++i)
      {
        generic_mul(expected, expected, expected);
      }
      if (compare("frobenius_batch4", 4u * round + lane, expected,
                  batch_output[lane]) != 0)
      {
        return 1;
      }
    }
  }

  for (size_t round = 0; round < 4; ++round)
  {
    fill_random(a);
    a[0] |= UINT64_C(1);
    gf_inv(b, a);
    gf_mul(actual, a, b);
    memset(expected, 0, sizeof(expected));
    expected[0] = 1;
    if (compare("inv", round, expected, actual) != 0)
    {
      return 1;
    }
  }

  printf("REF_GF_PASS param=%s cases=%lu\n", xstr(PARAMS),
         (unsigned long)test_case);
#if defined(AIMER_M55_MVE_MATVEC)
  printf("AFFINE_DIFF_PASS param=%s active_lanes=0..4 alias=pass "
         "tail=pass\n", xstr(PARAMS));
#endif
  return 0;
}
