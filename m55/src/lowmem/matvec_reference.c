// SPDX-License-Identifier: MIT

/*
 * Matrix-only copy of the official AIMer v3 reference implementations.
 * Keeping this in a separate translation unit lets the controlled MVE build
 * retain its gf_mul/gf_sqr/reduction and party-batch kernels while replacing
 * only gf_mat_vec_mul.  gf_mat_vec_mul_add in field_common.c therefore calls
 * this selected implementation through the normal field API.
 */

#include "field.h"

#include <stddef.h>
#include <stdint.h>

#if SECURITY_BITS == 128

// c = sum_i a[i] * b[i]
void gf_mat_vec_mul(gf c, const gf a, const gf b[AIM3_NUM_BITS_FIELD])
{
  const uint64_t *a_ptr = a;
  const gf *b_ptr = b;

  uint64_t temp_c0 = 0;
  uint64_t temp_c1 = 0;
  uint64_t mask;
  for (size_t i = AIM3_NUM_WORDS_FIELD; i; --i, ++a_ptr)
  {
    uint64_t index = *a_ptr;
    for (size_t j = AIM3_NUM_BITS_WORD; j; j -= 8, index >>= 8, b_ptr += 8)
    {
      mask = 0U - (index & 1);
      temp_c0 ^= (b_ptr[0][0] & mask);
      temp_c1 ^= (b_ptr[0][1] & mask);

      mask = 0U - ((index >> 1) & 1);
      temp_c0 ^= (b_ptr[1][0] & mask);
      temp_c1 ^= (b_ptr[1][1] & mask);

      mask = 0U - ((index >> 2) & 1);
      temp_c0 ^= (b_ptr[2][0] & mask);
      temp_c1 ^= (b_ptr[2][1] & mask);

      mask = 0U - ((index >> 3) & 1);
      temp_c0 ^= (b_ptr[3][0] & mask);
      temp_c1 ^= (b_ptr[3][1] & mask);

      mask = 0U - ((index >> 4) & 1);
      temp_c0 ^= (b_ptr[4][0] & mask);
      temp_c1 ^= (b_ptr[4][1] & mask);

      mask = 0U - ((index >> 5) & 1);
      temp_c0 ^= (b_ptr[5][0] & mask);
      temp_c1 ^= (b_ptr[5][1] & mask);

      mask = 0U - ((index >> 6) & 1);
      temp_c0 ^= (b_ptr[6][0] & mask);
      temp_c1 ^= (b_ptr[6][1] & mask);

      mask = 0U - ((index >> 7) & 1);
      temp_c0 ^= (b_ptr[7][0] & mask);
      temp_c1 ^= (b_ptr[7][1] & mask);
    }
  }
  c[0] = temp_c0;
  c[1] = temp_c1;
}

#elif SECURITY_BITS == 192

void gf_mat_vec_mul(gf c, const gf a, const gf b[AIM3_NUM_BITS_FIELD])
{
  const uint64_t *a_ptr = a;
  const gf *b_ptr = b;

  uint64_t temp_c0 = 0;
  uint64_t temp_c1 = 0;
  uint64_t temp_c2 = 0;
  uint64_t mask;
  for (size_t i = AIM3_NUM_WORDS_FIELD; i; --i, ++a_ptr)
  {
    uint64_t index = *a_ptr;
    for (size_t j = AIM3_NUM_BITS_WORD; j; j -= 4, index >>= 4, b_ptr += 4)
    {
      mask = 0U - (index & 1);
      temp_c0 ^= (b_ptr[0][0] & mask);
      temp_c1 ^= (b_ptr[0][1] & mask);
      temp_c2 ^= (b_ptr[0][2] & mask);

      mask = 0U - ((index >> 1) & 1);
      temp_c0 ^= (b_ptr[1][0] & mask);
      temp_c1 ^= (b_ptr[1][1] & mask);
      temp_c2 ^= (b_ptr[1][2] & mask);

      mask = 0U - ((index >> 2) & 1);
      temp_c0 ^= (b_ptr[2][0] & mask);
      temp_c1 ^= (b_ptr[2][1] & mask);
      temp_c2 ^= (b_ptr[2][2] & mask);

      mask = 0U - ((index >> 3) & 1);
      temp_c0 ^= (b_ptr[3][0] & mask);
      temp_c1 ^= (b_ptr[3][1] & mask);
      temp_c2 ^= (b_ptr[3][2] & mask);
    }
  }
  c[0] = temp_c0;
  c[1] = temp_c1;
  c[2] = temp_c2;
}

#elif SECURITY_BITS == 256

void gf_mat_vec_mul(gf c, const gf a, const gf b[AIM3_NUM_BITS_FIELD])
{
  const uint64_t *a_ptr = a;
  const gf *b_ptr = b;

  uint64_t temp_c0 = 0;
  uint64_t temp_c1 = 0;
  uint64_t temp_c2 = 0;
  uint64_t temp_c3 = 0;
  uint64_t mask;
  for (size_t i = AIM3_NUM_WORDS_FIELD; i; --i, ++a_ptr)
  {
    uint64_t index = *a_ptr;
    for (size_t j = AIM3_NUM_BITS_WORD; j; j -= 4, index >>= 4, b_ptr += 4)
    {
      mask = 0U - (index & 1);
      temp_c0 ^= (b_ptr[0][0] & mask);
      temp_c1 ^= (b_ptr[0][1] & mask);
      temp_c2 ^= (b_ptr[0][2] & mask);
      temp_c3 ^= (b_ptr[0][3] & mask);

      mask = 0U - ((index >> 1) & 1);
      temp_c0 ^= (b_ptr[1][0] & mask);
      temp_c1 ^= (b_ptr[1][1] & mask);
      temp_c2 ^= (b_ptr[1][2] & mask);
      temp_c3 ^= (b_ptr[1][3] & mask);

      mask = 0U - ((index >> 2) & 1);
      temp_c0 ^= (b_ptr[2][0] & mask);
      temp_c1 ^= (b_ptr[2][1] & mask);
      temp_c2 ^= (b_ptr[2][2] & mask);
      temp_c3 ^= (b_ptr[2][3] & mask);

      mask = 0U - ((index >> 3) & 1);
      temp_c0 ^= (b_ptr[3][0] & mask);
      temp_c1 ^= (b_ptr[3][1] & mask);
      temp_c2 ^= (b_ptr[3][2] & mask);
      temp_c3 ^= (b_ptr[3][3] & mask);
    }
  }
  c[0] = temp_c0;
  c[1] = temp_c1;
  c[2] = temp_c2;
  c[3] = temp_c3;
}

#else
#error "Unsupported AIMer field size"
#endif
