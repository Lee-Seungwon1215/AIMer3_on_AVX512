// SPDX-License-Identifier: MIT
// Test-harness helper: when AIMER_IMPL is set, pin the OQS SHA3 backend to match
// the AIMer impl so an avx2-vs-avx512 run measures field+Keccak, not field alone.
// Not used by the AIMer algorithm.
#ifndef AIMER_KECCAK_SELECT_H
#define AIMER_KECCAK_SELECT_H

void aimer_keccak_select_from_env(void);

#endif
