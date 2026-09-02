// SPDX-License-Identifier: MIT

#ifndef SIGN_H
#define SIGN_H

#include "field.h"
#include "hash.h"
#include "params.h"
#include <stddef.h>
#include <stdint.h>

#define RANDOMIZED_SIGNING

typedef struct tape_t
{
  gf pt_share;
  gf y_shares[AIMER_L];
  gf a_shares[AIMER_L + 1];
  gf c_share;
} tape_t;

typedef struct proof_t
{
  uint8_t reveal_path[AIMER_LOGN][AIMER_SEED_SIZE];
  uint8_t missing_commitment[AIMER_COMMIT_SIZE];
  uint8_t delta_pt_bytes[AIM3_NUM_BYTES_FIELD];
  uint8_t delta_ys_bytes[AIMER_L][AIM3_NUM_BYTES_FIELD];
  uint8_t delta_c_bytes[AIM3_NUM_BYTES_FIELD];
  uint8_t missing_alpha_share_bytes[AIMER_L + 1][AIM3_NUM_BYTES_FIELD];
} proof_t;

typedef struct signature_t
{
  uint8_t salt[AIMER_SALT_SIZE];
  uint8_t h_1[AIMER_COMMIT_SIZE];
  uint8_t h_2[AIMER_COMMIT_SIZE];
  proof_t proofs[AIMER_T];
} signature_t;

#endif // SIGN_H
