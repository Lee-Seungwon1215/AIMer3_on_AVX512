// SPDX-License-Identifier: MIT

#ifndef OQS_SIG_AIMER_V3_192F_H
#define OQS_SIG_AIMER_V3_192F_H

#include <oqs/common.h>

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define OQS_SIG_aimer_v3_192f_length_public_key 48
#define OQS_SIG_aimer_v3_192f_length_secret_key 72
#define OQS_SIG_aimer_v3_192f_length_signature 15408

struct OQS_SIG;
typedef struct OQS_SIG OQS_SIG;

OQS_SIG *OQS_SIG_aimer_v3_192f_new(void);
OQS_API OQS_STATUS OQS_SIG_aimer_v3_192f_keypair(uint8_t *public_key,
                                                 uint8_t *secret_key);
OQS_API OQS_STATUS OQS_SIG_aimer_v3_192f_sign(uint8_t *signature,
                                              size_t *signature_len,
                                              const uint8_t *message,
                                              size_t message_len,
                                              const uint8_t *secret_key);
OQS_API OQS_STATUS OQS_SIG_aimer_v3_192f_sign_with_ctx_str(
    uint8_t *signature, size_t *signature_len,
    const uint8_t *message, size_t message_len,
    const uint8_t *ctx_str, size_t ctx_str_len,
    const uint8_t *secret_key);
OQS_API OQS_STATUS OQS_SIG_aimer_v3_192f_verify(
    const uint8_t *message, size_t message_len,
    const uint8_t *signature, size_t signature_len,
    const uint8_t *public_key);
OQS_API OQS_STATUS OQS_SIG_aimer_v3_192f_verify_with_ctx_str(
    const uint8_t *message, size_t message_len,
    const uint8_t *signature, size_t signature_len,
    const uint8_t *ctx_str, size_t ctx_str_len,
    const uint8_t *public_key);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OQS_SIG_AIMER_V3_192F_H
