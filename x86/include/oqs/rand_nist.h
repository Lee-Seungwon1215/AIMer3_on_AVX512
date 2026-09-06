// SPDX-License-Identifier: MIT

#ifndef OQS_RAND_NIST_H
#define OQS_RAND_NIST_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

void OQS_randombytes_nist_kat_init_256bit(const uint8_t *entropy_input,
                                          const uint8_t *personalization_string);
void OQS_randombytes_nist_kat(uint8_t *random_array, size_t bytes_to_read);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OQS_RAND_NIST_H
