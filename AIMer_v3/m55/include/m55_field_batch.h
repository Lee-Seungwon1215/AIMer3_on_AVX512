// SPDX-License-Identifier: MIT

#ifndef AIMER_M55_FIELD_BATCH_H
#define AIMER_M55_FIELD_BATCH_H

#include "field.h"

#include <stddef.h>

#define M55_PARTY_BATCH_LANES 4u

/* Multiply up to four party field elements by one common challenge. */
void m55_gf_mul_const_batch4(gf output[M55_PARTY_BATCH_LANES],
                             const gf input[M55_PARTY_BATCH_LANES],
                             const gf constant, size_t active_lanes);

/* Square up to four independent field elements in parallel. */
void m55_gf_sqr_batch4(gf output[M55_PARTY_BATCH_LANES],
                       const gf input[M55_PARTY_BATCH_LANES],
                       size_t active_lanes);

/* Compute input^(2^exponent), retaining the four-party SoA layout across all
 * repeated squarings instead of packing and unpacking every iteration. */
void m55_gf_frobenius_batch4(gf output[M55_PARTY_BATCH_LANES],
                             const gf input[M55_PARTY_BATCH_LANES],
                             size_t exponent, size_t active_lanes);

#endif
