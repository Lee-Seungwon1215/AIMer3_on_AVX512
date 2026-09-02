// SPDX-License-Identifier: MIT
// Auto-generated: runtime-dispatched AIMer (192s) — selects avx512 > avx2 > ref
// based on CPU features (OQS_CPU_has_extension; requires OQS_DIST_BUILD).

#include <stdlib.h>
#include <string.h>

#include <oqs/sig_aimer.h>
#include <oqs/common.h>

#if defined(OQS_ENABLE_SIG_aimer_192s)

#define AIMER_DECL(ns) \
	extern int ns##_crypto_sign_keypair(uint8_t *pk, uint8_t *sk); \
	extern int ns##_crypto_sign_signature(uint8_t *sig, size_t *siglen, \
	        const uint8_t *m, size_t mlen, const uint8_t *ctx, size_t ctxlen, const uint8_t *sk); \
	extern int ns##_crypto_sign_verify(const uint8_t *sig, size_t siglen, \
	        const uint8_t *m, size_t mlen, const uint8_t *ctx, size_t ctxlen, const uint8_t *pk);
AIMER_DECL(samsungsds_aimer_192s_ref)
AIMER_DECL(samsungsds_aimer_192s_avx2)
AIMER_DECL(samsungsds_aimer_192s_avx512)
#undef AIMER_DECL

// 2 = avx512, 1 = avx2, 0 = ref. Env var AIMER_IMPL (ref|avx2|avx512) forces a
// choice (for testing/benchmarking); otherwise pick the best the CPU supports.
static int aimer_192s_impl(void) {
	const char *e = getenv("AIMER_IMPL");
	if (e != NULL) {
		if (strcmp(e, "ref") == 0) { return 0; }
		if (strcmp(e, "avx2") == 0) { return 1; }
		if (strcmp(e, "avx512") == 0) { return 2; }
	}
	if (OQS_CPU_has_extension(OQS_CPU_EXT_AVX512)) { return 2; }
	if (OQS_CPU_has_extension(OQS_CPU_EXT_AVX2)) { return 1; }
	return 0;
}

OQS_SIG *OQS_SIG_aimer_192s_new(void) {
	OQS_SIG *sig = OQS_MEM_calloc(1, sizeof(OQS_SIG));
	if (sig == NULL) {
		return NULL;
	}
	sig->method_name = OQS_SIG_alg_aimer_192s;
	switch (aimer_192s_impl()) {
	case 2:  sig->alg_version = "AIMer_avx512"; break;
	case 1:  sig->alg_version = "AIMer_avx2"; break;
	default: sig->alg_version = "AIMer_ref"; break;
	}

	sig->claimed_nist_level = 3;
	sig->euf_cma = true;
	sig->suf_cma = false;
	sig->sig_with_ctx_support = false;

	sig->length_public_key = OQS_SIG_aimer_192s_length_public_key;
	sig->length_secret_key = OQS_SIG_aimer_192s_length_secret_key;
	sig->length_signature  = OQS_SIG_aimer_192s_length_signature;

	sig->keypair = OQS_SIG_aimer_192s_keypair;
	sig->sign    = OQS_SIG_aimer_192s_sign;
	sig->verify  = OQS_SIG_aimer_192s_verify;

	return sig;
}

OQS_API OQS_STATUS OQS_SIG_aimer_192s_keypair(uint8_t *public_key, uint8_t *secret_key) {
	switch (aimer_192s_impl()) {
	case 2:  return (OQS_STATUS) samsungsds_aimer_192s_avx512_crypto_sign_keypair(public_key, secret_key);
	case 1:  return (OQS_STATUS) samsungsds_aimer_192s_avx2_crypto_sign_keypair(public_key, secret_key);
	default: return (OQS_STATUS) samsungsds_aimer_192s_ref_crypto_sign_keypair(public_key, secret_key);
	}
}

OQS_API OQS_STATUS OQS_SIG_aimer_192s_sign(uint8_t *signature, size_t *signature_len,
                                          const uint8_t *message, size_t message_len,
                                          const uint8_t *secret_key) {
	switch (aimer_192s_impl()) {
	case 2:  return (OQS_STATUS) samsungsds_aimer_192s_avx512_crypto_sign_signature(signature, signature_len, message, message_len, NULL, 0, secret_key);
	case 1:  return (OQS_STATUS) samsungsds_aimer_192s_avx2_crypto_sign_signature(signature, signature_len, message, message_len, NULL, 0, secret_key);
	default: return (OQS_STATUS) samsungsds_aimer_192s_ref_crypto_sign_signature(signature, signature_len, message, message_len, NULL, 0, secret_key);
	}
}

OQS_API OQS_STATUS OQS_SIG_aimer_192s_verify(const uint8_t *message, size_t message_len,
                                            const uint8_t *signature, size_t signature_len,
                                            const uint8_t *public_key) {
	switch (aimer_192s_impl()) {
	case 2:  return (OQS_STATUS) samsungsds_aimer_192s_avx512_crypto_sign_verify(signature, signature_len, message, message_len, NULL, 0, public_key);
	case 1:  return (OQS_STATUS) samsungsds_aimer_192s_avx2_crypto_sign_verify(signature, signature_len, message, message_len, NULL, 0, public_key);
	default: return (OQS_STATUS) samsungsds_aimer_192s_ref_crypto_sign_verify(signature, signature_len, message, message_len, NULL, 0, public_key);
	}
}

#endif
