// SPDX-License-Identifier: MIT
// See aimer_keccak_select.h. Kept in tests/ so the algorithm never reconfigures
// the global SHA3 backend.
#include <stdlib.h>
#include <string.h>
#include <oqs/sha3.h>
#include <oqs/sha3x4.h>
#include "aimer_keccak_select.h"

extern struct OQS_SHA3_callbacks sha3_default_callbacks;        // scalar
extern struct OQS_SHA3_callbacks sha3_avx2_callbacks;           // AVX2
extern const struct OQS_SHA3_callbacks sha3_avx512vl_callbacks; // AVX512VL
extern struct OQS_SHA3_x4_callbacks sha3_x4_default_callbacks;
extern struct OQS_SHA3_x4_callbacks sha3_x4_avx2_callbacks;
extern const struct OQS_SHA3_x4_callbacks sha3_x4_avx512vl_callbacks;

void aimer_keccak_select_from_env(void) {
	// AIMER_KECCAK overrides the Keccak backend independently of the field impl,
	// so a run can pin (e.g.) avx2 field + avx512vl Keccak. This isolates the
	// field-arithmetic speedup from the Keccak speedup in the same measurement.
	// Falls back to AIMER_IMPL when AIMER_KECCAK is unset.
	const char *e = getenv("AIMER_KECCAK");
	if (e == NULL) {
		e = getenv("AIMER_IMPL");
	}
	if (e == NULL) {
		return;
	}
	if (strcmp(e, "avx512") == 0) {
		OQS_SHA3_set_callbacks((struct OQS_SHA3_callbacks *)&sha3_avx512vl_callbacks);
		OQS_SHA3_x4_set_callbacks((struct OQS_SHA3_x4_callbacks *)&sha3_x4_avx512vl_callbacks);
	} else if (strcmp(e, "avx2") == 0) {
		OQS_SHA3_set_callbacks(&sha3_avx2_callbacks);
		OQS_SHA3_x4_set_callbacks(&sha3_x4_avx2_callbacks);
	} else if (strcmp(e, "ref") == 0) {
		OQS_SHA3_set_callbacks(&sha3_default_callbacks);
		OQS_SHA3_x4_set_callbacks(&sha3_x4_default_callbacks);
	}
}
