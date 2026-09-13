// SPDX-License-Identifier: MIT

#include <oqs/rand_nist.h>

#include "common/rng.h"

#include <stdlib.h>

void OQS_randombytes_nist_kat_init_256bit(
    const uint8_t *entropy_input, const uint8_t *personalization_string) {
	randombytes_init((unsigned char *)entropy_input,
	                 (unsigned char *)personalization_string, 256);
}

void OQS_randombytes_nist_kat(uint8_t *random_array, size_t bytes_to_read) {
	if (randombytes(random_array, (unsigned long long)bytes_to_read) != RNG_SUCCESS) {
		abort();
	}
}
