// SPDX-License-Identifier: MIT

#ifndef AIMER_V3_AVX512_FIELD128_BATCH_H
#define AIMER_V3_AVX512_FIELD128_BATCH_H

#include "field.h"

#define gf_sqr_N AIMER_NAMESPACE(gf_sqr_N)
void gf_sqr_N(gf out[AIMER_N], const gf in[AIMER_N]);

#define gf_mul_add_N AIMER_NAMESPACE(gf_mul_add_N)
void gf_mul_add_N(gf accum[AIMER_N], const gf in[AIMER_N],
                  const gf multiplier);

#define gf_mat_vec_mul_N AIMER_NAMESPACE(gf_mat_vec_mul_N)
void gf_mat_vec_mul_N(gf out[AIMER_N], const gf in[AIMER_N],
                      const gf matrix[AIM3_NUM_BITS_FIELD]);

#define gf_mat_vec_mul_add_N AIMER_NAMESPACE(gf_mat_vec_mul_add_N)
void gf_mat_vec_mul_add_N(gf accum[AIMER_N], const gf in[AIMER_N],
                          const gf matrix[AIM3_NUM_BITS_FIELD]);

#endif // AIMER_V3_AVX512_FIELD128_BATCH_H
