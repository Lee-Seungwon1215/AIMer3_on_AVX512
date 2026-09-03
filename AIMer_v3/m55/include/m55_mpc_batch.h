// SPDX-License-Identifier: MIT

#ifndef AIMER_M55_MPC_BATCH_H
#define AIMER_M55_MPC_BATCH_H

#include "aim3.h"
#include "m55_field_batch.h"

#include <stddef.h>
#include <stdint.h>

/* Evaluate the unchanged AIM3 MPC equations for up to four contiguous
 * parties.  The affine layers remain scalar; only the independent Frobenius
 * powers used to form z_shares are kept in the MVE four-party layout. */
void m55_aim3_mpc_batch4(mult_chk_t checks[M55_PARTY_BATCH_LANES],
                         const aim_lin_t *lin,
                         const tape_t tapes[M55_PARTY_BATCH_LANES],
                         const gf ciphertext, size_t party_base,
                         size_t active_lanes);

#if defined(AIMER_PHASE_PROFILE)
void m55_mpc_profile_reset(void);
uint64_t m55_mpc_profile_affine_cycles(void);
uint64_t m55_mpc_profile_frobenius_cycles(void);
#endif

#endif
