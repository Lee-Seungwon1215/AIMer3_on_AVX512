// SPDX-License-Identifier: MIT

#ifndef OQS_RAND_H
#define OQS_RAND_H

#include <oqs/common.h>

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define OQS_RAND_alg_system "system"

OQS_API OQS_STATUS OQS_randombytes_switch_algorithm(const char *algorithm);
OQS_API void OQS_randombytes_custom_algorithm(void (*algorithm_ptr)(uint8_t *, size_t));
OQS_API void OQS_randombytes(uint8_t *random_array, size_t bytes_to_read);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OQS_RAND_H
