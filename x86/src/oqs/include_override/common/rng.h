// SPDX-License-Identifier: MIT
// OQS-only replacement for the NIST KAT RNG header included by AIM3 sign.c.

#ifndef rng_h
#define rng_h

#include <oqs/rand.h>

#include <stddef.h>

#define RNG_SUCCESS 0

static inline int randombytes(unsigned char *out, unsigned long long out_len) {
	OQS_randombytes(out, (size_t)out_len);
	return RNG_SUCCESS;
}

#endif // rng_h
