// SPDX-License-Identifier: MIT

#include <oqs/oqs.h>

#include <stdlib.h>
#include <string.h>

#define DECLARE_AIMER_V3_IMPL(namespace)                                      \
	extern int namespace##_crypto_sign_keypair(uint8_t *pk, uint8_t *sk);       \
	extern int namespace##_crypto_sign_signature(                              \
	    uint8_t *sig, size_t *siglen, const uint8_t *message,                  \
	    size_t message_len, const uint8_t *ctx, size_t ctxlen,                  \
	    const uint8_t *sk);                                                     \
	extern int namespace##_crypto_sign_verify(                                 \
	    const uint8_t *sig, size_t siglen, const uint8_t *message,              \
	    size_t message_len, const uint8_t *ctx, size_t ctxlen,                  \
	    const uint8_t *pk)

DECLARE_AIMER_V3_IMPL(samsungsds_aimer_128s_ref);
DECLARE_AIMER_V3_IMPL(samsungsds_aimer_128s_avx2);
DECLARE_AIMER_V3_IMPL(samsungsds_aimer_128s_opt);

#undef DECLARE_AIMER_V3_IMPL

enum aimer_v3_128s_implementation {
	AIMER_V3_128S_REFERENCE,
	AIMER_V3_128S_AVX2,
	AIMER_V3_128S_AVX512,
};

static int cpu_supports_aimer_v3_128s_avx2(void) {
	return OQS_CPU_has_extension(OQS_CPU_EXT_AES) &&
	       OQS_CPU_has_extension(OQS_CPU_EXT_AVX2) &&
	       OQS_CPU_has_extension(OQS_CPU_EXT_BMI2) &&
	       OQS_CPU_has_extension(OQS_CPU_EXT_PCLMULQDQ) &&
	       OQS_CPU_has_extension(OQS_CPU_EXT_POPCNT);
}

static int cpu_supports_aimer_v3_128s_avx512(void) {
	return cpu_supports_aimer_v3_128s_avx2() &&
	       OQS_CPU_has_extension(OQS_CPU_EXT_AVX512) &&
	       OQS_CPU_has_extension(OQS_CPU_EXT_AVX512VL) &&
	       OQS_CPU_has_extension(OQS_CPU_EXT_VPCLMULQDQ);
}

static enum aimer_v3_128s_implementation
aimer_v3_128s_implementation(void) {
	const char *forced = getenv("AIMER_V3_IMPL");
	if (forced != NULL) {
		if (strcmp(forced, "ref") == 0 ||
		    strcmp(forced, "reference") == 0) {
			return AIMER_V3_128S_REFERENCE;
		}
		if (strcmp(forced, "avx2") == 0) {
			return AIMER_V3_128S_AVX2;
		}
		if (strcmp(forced, "avx512") == 0) {
			return AIMER_V3_128S_AVX512;
		}
	}
	if (cpu_supports_aimer_v3_128s_avx512()) {
		return AIMER_V3_128S_AVX512;
	}
	if (cpu_supports_aimer_v3_128s_avx2()) {
		return AIMER_V3_128S_AVX2;
	}
	return AIMER_V3_128S_REFERENCE;
}

OQS_SIG *OQS_SIG_aimer_v3_128s_new(void) {
	OQS_SIG *sig = OQS_MEM_calloc(1, sizeof(*sig));
	if (sig == NULL) {
		return NULL;
	}

	const enum aimer_v3_128s_implementation implementation =
	    aimer_v3_128s_implementation();
	sig->method_name = OQS_SIG_alg_aimer_v3_128s;
	sig->alg_version = implementation == AIMER_V3_128S_AVX512
	                       ? "AIMer-v3-avx512"
	                       : implementation == AIMER_V3_128S_AVX2
	                             ? "AIMer-v3-avx2"
	                             : "AIMer-v3-ref";
	sig->claimed_nist_level = 1;
	sig->euf_cma = true;
	sig->suf_cma = false;
	sig->sig_with_ctx_support = true;
	sig->length_public_key = OQS_SIG_aimer_v3_128s_length_public_key;
	sig->length_secret_key = OQS_SIG_aimer_v3_128s_length_secret_key;
	sig->length_signature = OQS_SIG_aimer_v3_128s_length_signature;
	sig->keypair = OQS_SIG_aimer_v3_128s_keypair;
	sig->sign = OQS_SIG_aimer_v3_128s_sign;
	sig->sign_with_ctx_str = OQS_SIG_aimer_v3_128s_sign_with_ctx_str;
	sig->verify = OQS_SIG_aimer_v3_128s_verify;
	sig->verify_with_ctx_str = OQS_SIG_aimer_v3_128s_verify_with_ctx_str;
	return sig;
}

OQS_API OQS_STATUS OQS_SIG_aimer_v3_128s_keypair(
    uint8_t *public_key, uint8_t *secret_key) {
	int status;
	switch (aimer_v3_128s_implementation()) {
		case AIMER_V3_128S_AVX512:
			status = samsungsds_aimer_128s_opt_crypto_sign_keypair(
			    public_key, secret_key);
			break;
		case AIMER_V3_128S_AVX2:
			status = samsungsds_aimer_128s_avx2_crypto_sign_keypair(
			    public_key, secret_key);
			break;
		default:
			status = samsungsds_aimer_128s_ref_crypto_sign_keypair(
			    public_key, secret_key);
			break;
	}
	return status == 0 ? OQS_SUCCESS : OQS_ERROR;
}

OQS_API OQS_STATUS OQS_SIG_aimer_v3_128s_sign(
    uint8_t *signature, size_t *signature_len,
    const uint8_t *message, size_t message_len,
    const uint8_t *secret_key) {
	return OQS_SIG_aimer_v3_128s_sign_with_ctx_str(
	    signature, signature_len, message, message_len, NULL, 0, secret_key);
}

OQS_API OQS_STATUS OQS_SIG_aimer_v3_128s_sign_with_ctx_str(
    uint8_t *signature, size_t *signature_len,
    const uint8_t *message, size_t message_len,
    const uint8_t *ctx_str, size_t ctx_str_len,
    const uint8_t *secret_key) {
	int status;
	switch (aimer_v3_128s_implementation()) {
		case AIMER_V3_128S_AVX512:
			status = samsungsds_aimer_128s_opt_crypto_sign_signature(
			    signature, signature_len, message, message_len,
			    ctx_str, ctx_str_len, secret_key);
			break;
		case AIMER_V3_128S_AVX2:
			status = samsungsds_aimer_128s_avx2_crypto_sign_signature(
			    signature, signature_len, message, message_len,
			    ctx_str, ctx_str_len, secret_key);
			break;
		default:
			status = samsungsds_aimer_128s_ref_crypto_sign_signature(
			    signature, signature_len, message, message_len,
			    ctx_str, ctx_str_len, secret_key);
			break;
	}
	return status == 0 ? OQS_SUCCESS : OQS_ERROR;
}

OQS_API OQS_STATUS OQS_SIG_aimer_v3_128s_verify(
    const uint8_t *message, size_t message_len,
    const uint8_t *signature, size_t signature_len,
    const uint8_t *public_key) {
	return OQS_SIG_aimer_v3_128s_verify_with_ctx_str(
	    message, message_len, signature, signature_len, NULL, 0, public_key);
}

OQS_API OQS_STATUS OQS_SIG_aimer_v3_128s_verify_with_ctx_str(
    const uint8_t *message, size_t message_len,
    const uint8_t *signature, size_t signature_len,
    const uint8_t *ctx_str, size_t ctx_str_len,
    const uint8_t *public_key) {
	int status;
	switch (aimer_v3_128s_implementation()) {
		case AIMER_V3_128S_AVX512:
			status = samsungsds_aimer_128s_opt_crypto_sign_verify(
			    signature, signature_len, message, message_len,
			    ctx_str, ctx_str_len, public_key);
			break;
		case AIMER_V3_128S_AVX2:
			status = samsungsds_aimer_128s_avx2_crypto_sign_verify(
			    signature, signature_len, message, message_len,
			    ctx_str, ctx_str_len, public_key);
			break;
		default:
			status = samsungsds_aimer_128s_ref_crypto_sign_verify(
			    signature, signature_len, message, message_len,
			    ctx_str, ctx_str_len, public_key);
			break;
	}
	return status == 0 ? OQS_SUCCESS : OQS_ERROR;
}
