// SPDX-License-Identifier: MIT

#ifndef OQS_SIG_H
#define OQS_SIG_H

#include <oqs/common.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define OQS_SIG_alg_aimer_v3_128f "AIMER-v3-128f"
#define OQS_SIG_alg_aimer_v3_128s "AIMER-v3-128s"
#define OQS_SIG_alg_aimer_v3_192f "AIMER-v3-192f"
#define OQS_SIG_alg_aimer_v3_192s "AIMER-v3-192s"
#define OQS_SIG_alg_aimer_v3_256f "AIMER-v3-256f"
#define OQS_SIG_alg_aimer_v3_256s "AIMER-v3-256s"
#define OQS_SIG_algs_length 6

typedef struct OQS_SIG {
	const char *method_name;
	const char *alg_version;
	uint8_t claimed_nist_level;
	bool euf_cma;
	bool suf_cma;
	bool sig_with_ctx_support;
	size_t length_public_key;
	size_t length_secret_key;
	size_t length_signature;

	OQS_STATUS (*keypair)(uint8_t *public_key, uint8_t *secret_key);
	OQS_STATUS (*sign)(uint8_t *signature, size_t *signature_len,
	                   const uint8_t *message, size_t message_len,
	                   const uint8_t *secret_key);
	OQS_STATUS (*sign_with_ctx_str)(uint8_t *signature, size_t *signature_len,
	                                const uint8_t *message, size_t message_len,
	                                const uint8_t *ctx_str, size_t ctx_str_len,
	                                const uint8_t *secret_key);
	OQS_STATUS (*verify)(const uint8_t *message, size_t message_len,
	                     const uint8_t *signature, size_t signature_len,
	                     const uint8_t *public_key);
	OQS_STATUS (*verify_with_ctx_str)(const uint8_t *message, size_t message_len,
	                                  const uint8_t *signature, size_t signature_len,
	                                  const uint8_t *ctx_str, size_t ctx_str_len,
	                                  const uint8_t *public_key);
} OQS_SIG;

OQS_API const char *OQS_SIG_alg_identifier(size_t i);
OQS_API int OQS_SIG_alg_count(void);
OQS_API int OQS_SIG_alg_is_enabled(const char *method_name);
OQS_API OQS_SIG *OQS_SIG_new(const char *method_name);
OQS_API void OQS_SIG_free(OQS_SIG *sig);

OQS_API OQS_STATUS OQS_SIG_keypair(const OQS_SIG *sig,
                                   uint8_t *public_key, uint8_t *secret_key);
OQS_API OQS_STATUS OQS_SIG_sign(const OQS_SIG *sig,
                                uint8_t *signature, size_t *signature_len,
                                const uint8_t *message, size_t message_len,
                                const uint8_t *secret_key);
OQS_API OQS_STATUS OQS_SIG_sign_with_ctx_str(
    const OQS_SIG *sig, uint8_t *signature, size_t *signature_len,
    const uint8_t *message, size_t message_len,
    const uint8_t *ctx_str, size_t ctx_str_len,
    const uint8_t *secret_key);
OQS_API OQS_STATUS OQS_SIG_verify(const OQS_SIG *sig,
                                  const uint8_t *message, size_t message_len,
                                  const uint8_t *signature, size_t signature_len,
                                  const uint8_t *public_key);
OQS_API OQS_STATUS OQS_SIG_verify_with_ctx_str(
    const OQS_SIG *sig, const uint8_t *message, size_t message_len,
    const uint8_t *signature, size_t signature_len,
    const uint8_t *ctx_str, size_t ctx_str_len,
    const uint8_t *public_key);
OQS_API bool OQS_SIG_supports_ctx_str(const char *alg_name);

#include <oqs/sig_aimer_v3.h>
#include <oqs/sig_aimer_v3_128s.h>
#include <oqs/sig_aimer_v3_192f.h>
#include <oqs/sig_aimer_v3_192s.h>
#include <oqs/sig_aimer_v3_256f.h>
#include <oqs/sig_aimer_v3_256s.h>

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OQS_SIG_H
