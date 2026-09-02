// SPDX-License-Identifier: MIT

#ifndef OQS_SIG_AIMER_V3_256S_H
#define OQS_SIG_AIMER_V3_256S_H

#include <oqs/common.h>

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define OQS_SIG_aimer_v3_256s_length_public_key 64
#define OQS_SIG_aimer_v3_256s_length_secret_key 96
#define OQS_SIG_aimer_v3_256s_length_signature 20224

struct OQS_SIG;
typedef struct OQS_SIG OQS_SIG;

OQS_SIG *OQS_SIG_aimer_v3_256s_new(void);
OQS_API OQS_STATUS OQS_SIG_aimer_v3_256s_keypair(uint8_t *, uint8_t *);
OQS_API OQS_STATUS OQS_SIG_aimer_v3_256s_sign(
    uint8_t *, size_t *, const uint8_t *, size_t, const uint8_t *);
OQS_API OQS_STATUS OQS_SIG_aimer_v3_256s_sign_with_ctx_str(
    uint8_t *, size_t *, const uint8_t *, size_t,
    const uint8_t *, size_t, const uint8_t *);
OQS_API OQS_STATUS OQS_SIG_aimer_v3_256s_verify(
    const uint8_t *, size_t, const uint8_t *, size_t, const uint8_t *);
OQS_API OQS_STATUS OQS_SIG_aimer_v3_256s_verify_with_ctx_str(
    const uint8_t *, size_t, const uint8_t *, size_t,
    const uint8_t *, size_t, const uint8_t *);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OQS_SIG_AIMER_V3_256S_H
