// SPDX-License-Identifier: MIT

#include "m55_field_batch.h"
#include "m55_mpc_batch.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(AIMER_PHASE_PROFILE)
#include "m55_platform.h"

static uint64_t mpc_profile_affine;
static uint64_t mpc_profile_frobenius;

void m55_mpc_profile_reset(void)
{
  mpc_profile_affine = 0u;
  mpc_profile_frobenius = 0u;
}

uint64_t m55_mpc_profile_affine_cycles(void)
{
  return mpc_profile_affine;
}

uint64_t m55_mpc_profile_frobenius_cycles(void)
{
  return mpc_profile_frobenius;
}
#endif

void m55_aim3_mpc_batch4(mult_chk_t checks[M55_PARTY_BATCH_LANES],
                         const aim_lin_t *lin,
                         const tape_t tapes[M55_PARTY_BATCH_LANES],
                         const gf ciphertext, size_t party_base,
                         size_t active_lanes)
{
  if (active_lanes > M55_PARTY_BATCH_LANES)
  {
    active_lanes = M55_PARTY_BATCH_LANES;
  }

#if defined(AIMER_PHASE_PROFILE)
  uint32_t profile_start = m55_cycle_count();
#endif

  for (size_t lane = 0; lane < active_lanes; ++lane)
  {
    mult_chk_t *const check = &checks[lane];
    const tape_t *const tape = &tapes[lane];
    const size_t party = party_base + lane;
    memset(check, 0, sizeof(*check));

    for (size_t ell = 0; ell < AIMER_L + 1u; ++ell)
    {
      gf_copy(check->a_shares[ell], tape->a_shares[ell]);
    }
    for (size_t ell = 0; ell < AIMER_L; ++ell)
    {
      gf_copy(check->b_shares[ell], tape->y_shares[ell]);
    }
    gf_copy(check->b_shares[AIMER_L], tape->pt_share);
    if (party == AIMER_N - 1u)
    {
      gf_add(check->b_shares[AIMER_L], check->b_shares[AIMER_L],
             ciphertext);
    }
    gf_copy(check->c_share, tape->c_share);

    for (size_t ell = 0; ell < AIMER_L; ++ell)
    {
      gf_mat_vec_mul(check->x_shares[ell], tape->pt_share, lin->mat_A[ell]);
      if (party == AIMER_N - 1u)
      {
        gf_add(check->x_shares[ell], check->x_shares[ell], lin->vec_b[ell]);
      }
    }

    gf_set0(check->x_shares[AIMER_L]);
    for (size_t ell = 0; ell < AIMER_L; ++ell)
    {
      gf_mat_vec_mul_add(check->x_shares[AIMER_L], tape->y_shares[ell],
                         lin->mat_A[ell + AIMER_L]);
    }
    if (party == AIMER_N - 1u)
    {
      gf_add(check->x_shares[AIMER_L], check->x_shares[AIMER_L],
             lin->vec_b[AIMER_L]);
    }
  }

#if defined(AIMER_PHASE_PROFILE)
  mpc_profile_affine += (uint32_t)(m55_cycle_count() - profile_start);
  profile_start = m55_cycle_count();
#endif

  for (size_t ell = 0; ell < AIMER_L + 1u; ++ell)
  {
    gf inputs[M55_PARTY_BATCH_LANES];
    gf outputs[M55_PARTY_BATCH_LANES];
    for (size_t lane = 0; lane < active_lanes; ++lane)
    {
      gf_copy(inputs[lane], checks[lane].x_shares[ell]);
    }
    m55_gf_frobenius_batch4(outputs, inputs, aim3_exponents[ell],
                            active_lanes);
    for (size_t lane = 0; lane < active_lanes; ++lane)
    {
      const size_t party = party_base + lane;
      gf_copy(checks[lane].z_shares[ell], outputs[lane]);
      if (party == AIMER_N - 1u)
      {
        checks[lane].z_shares[ell][0] ^= UINT64_C(1);
      }
    }
  }

#if defined(AIMER_PHASE_PROFILE)
  mpc_profile_frobenius += (uint32_t)(m55_cycle_count() - profile_start);
#endif
}
