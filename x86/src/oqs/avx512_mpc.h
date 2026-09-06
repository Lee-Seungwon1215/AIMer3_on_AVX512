// SPDX-License-Identifier: MIT

#ifndef AIMER_V3_AVX512_MPC_H
#define AIMER_V3_AVX512_MPC_H

#include "aim3.h"

#define aim3_mpc_N AIMER_NAMESPACE(aim3_mpc_N)
void aim3_mpc_N(mult_chk_t mult_checks[AIMER_N], const aim_lin_t *lin,
                const tape_t tapes[AIMER_N], const gf ciphertext);

#endif // AIMER_V3_AVX512_MPC_H
