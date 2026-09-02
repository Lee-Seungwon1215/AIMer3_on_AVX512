// SPDX-License-Identifier: MIT

#include <oqs/oqs.h>

#include <strings.h>

OQS_API const char *OQS_SIG_alg_identifier(size_t i) {
	static const char *algorithms[OQS_SIG_algs_length] = {
		OQS_SIG_alg_aimer_v3_128f,
	};
	return i < OQS_SIG_algs_length ? algorithms[i] : NULL;
}

OQS_API int OQS_SIG_alg_count(void) {
	return OQS_SIG_algs_length;
}

OQS_API int OQS_SIG_alg_is_enabled(const char *method_name) {
	return method_name != NULL &&
	       strcasecmp(method_name, OQS_SIG_alg_aimer_v3_128f) == 0;
}

OQS_API OQS_SIG *OQS_SIG_new(const char *method_name) {
	if (OQS_SIG_alg_is_enabled(method_name)) {
		return OQS_SIG_aimer_v3_128f_new();
	}
	return NULL;
}

OQS_API void OQS_SIG_free(OQS_SIG *sig) {
	OQS_MEM_insecure_free(sig);
}

OQS_API OQS_STATUS OQS_SIG_keypair(const OQS_SIG *sig,
                                   uint8_t *public_key, uint8_t *secret_key) {
	if (sig == NULL || sig->keypair == NULL) {
		return OQS_ERROR;
	}
	return sig->keypair(public_key, secret_key);
}

OQS_API OQS_STATUS OQS_SIG_sign(const OQS_SIG *sig,
                                uint8_t *signature, size_t *signature_len,
                                const uint8_t *message, size_t message_len,
                                const uint8_t *secret_key) {
	if (sig == NULL || sig->sign == NULL) {
		return OQS_ERROR;
	}
	return sig->sign(signature, signature_len, message, message_len, secret_key);
}

OQS_API OQS_STATUS OQS_SIG_sign_with_ctx_str(
    const OQS_SIG *sig, uint8_t *signature, size_t *signature_len,
    const uint8_t *message, size_t message_len,
    const uint8_t *ctx_str, size_t ctx_str_len,
    const uint8_t *secret_key) {
	if (sig == NULL || sig->sign_with_ctx_str == NULL) {
		return OQS_ERROR;
	}
	return sig->sign_with_ctx_str(signature, signature_len, message, message_len,
	                              ctx_str, ctx_str_len, secret_key);
}

OQS_API OQS_STATUS OQS_SIG_verify(const OQS_SIG *sig,
                                  const uint8_t *message, size_t message_len,
                                  const uint8_t *signature, size_t signature_len,
                                  const uint8_t *public_key) {
	if (sig == NULL || sig->verify == NULL) {
		return OQS_ERROR;
	}
	return sig->verify(message, message_len, signature, signature_len, public_key);
}

OQS_API OQS_STATUS OQS_SIG_verify_with_ctx_str(
    const OQS_SIG *sig, const uint8_t *message, size_t message_len,
    const uint8_t *signature, size_t signature_len,
    const uint8_t *ctx_str, size_t ctx_str_len,
    const uint8_t *public_key) {
	if (sig == NULL || sig->verify_with_ctx_str == NULL) {
		return OQS_ERROR;
	}
	return sig->verify_with_ctx_str(message, message_len, signature, signature_len,
	                                ctx_str, ctx_str_len, public_key);
}

OQS_API bool OQS_SIG_supports_ctx_str(const char *alg_name) {
	OQS_SIG *sig = OQS_SIG_new(alg_name);
	if (sig == NULL) {
		return false;
	}
	const bool supported = sig->sig_with_ctx_support;
	OQS_SIG_free(sig);
	return supported;
}
