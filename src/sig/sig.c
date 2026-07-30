// SPDX-License-Identifier: MIT

#include <assert.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <string.h>
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#include <oqs/oqs.h>

OQS_API const char *OQS_SIG_alg_identifier(size_t i) {
	const char *a[OQS_SIG_algs_length] = {
		OQS_SIG_alg_aimer_128f,
		OQS_SIG_alg_aimer_128s,
		OQS_SIG_alg_aimer_192f,
		OQS_SIG_alg_aimer_192s,
		OQS_SIG_alg_aimer_256f,
		OQS_SIG_alg_aimer_256s,
	};
	if (i >= OQS_SIG_algs_length) { return NULL; }
	else { return a[i]; }
}

OQS_API int OQS_SIG_alg_count(void) {
	return OQS_SIG_algs_length;
}

OQS_API int OQS_SIG_alg_is_enabled(const char *method_name) {
	if (method_name == NULL) { return 0; }
	if (0) {
	}
	else if (0 == strcasecmp(method_name, OQS_SIG_alg_aimer_128f)) {
#ifdef OQS_ENABLE_SIG_aimer_128f
		return 1;
#else
		return 0;
#endif
	}
	else if (0 == strcasecmp(method_name, OQS_SIG_alg_aimer_128s)) {
#ifdef OQS_ENABLE_SIG_aimer_128s
		return 1;
#else
		return 0;
#endif
	}
	else if (0 == strcasecmp(method_name, OQS_SIG_alg_aimer_192f)) {
#ifdef OQS_ENABLE_SIG_aimer_192f
		return 1;
#else
		return 0;
#endif
	}
	else if (0 == strcasecmp(method_name, OQS_SIG_alg_aimer_192s)) {
#ifdef OQS_ENABLE_SIG_aimer_192s
		return 1;
#else
		return 0;
#endif
	}
	else if (0 == strcasecmp(method_name, OQS_SIG_alg_aimer_256f)) {
#ifdef OQS_ENABLE_SIG_aimer_256f
		return 1;
#else
		return 0;
#endif
	}
	else if (0 == strcasecmp(method_name, OQS_SIG_alg_aimer_256s)) {
#ifdef OQS_ENABLE_SIG_aimer_256s
		return 1;
#else
		return 0;
#endif
	}
	else {
		return 0;
	}
}

OQS_API OQS_SIG *OQS_SIG_new(const char *method_name) {
	if (method_name == NULL) { return NULL; }
	if (0) {
	}
	else if (0 == strcasecmp(method_name, OQS_SIG_alg_aimer_128f)) {
#ifdef OQS_ENABLE_SIG_aimer_128f
		return OQS_SIG_aimer_128f_new();
#else
		return NULL;
#endif
	}
	else if (0 == strcasecmp(method_name, OQS_SIG_alg_aimer_128s)) {
#ifdef OQS_ENABLE_SIG_aimer_128s
		return OQS_SIG_aimer_128s_new();
#else
		return NULL;
#endif
	}
	else if (0 == strcasecmp(method_name, OQS_SIG_alg_aimer_192f)) {
#ifdef OQS_ENABLE_SIG_aimer_192f
		return OQS_SIG_aimer_192f_new();
#else
		return NULL;
#endif
	}
	else if (0 == strcasecmp(method_name, OQS_SIG_alg_aimer_192s)) {
#ifdef OQS_ENABLE_SIG_aimer_192s
		return OQS_SIG_aimer_192s_new();
#else
		return NULL;
#endif
	}
	else if (0 == strcasecmp(method_name, OQS_SIG_alg_aimer_256f)) {
#ifdef OQS_ENABLE_SIG_aimer_256f
		return OQS_SIG_aimer_256f_new();
#else
		return NULL;
#endif
	}
	else if (0 == strcasecmp(method_name, OQS_SIG_alg_aimer_256s)) {
#ifdef OQS_ENABLE_SIG_aimer_256s
		return OQS_SIG_aimer_256s_new();
#else
		return NULL;
#endif
	}
	else {
		return NULL;
	}
}

OQS_API OQS_STATUS OQS_SIG_keypair(const OQS_SIG *sig, uint8_t *public_key, uint8_t *secret_key) {
	if (sig == NULL || sig->keypair(public_key, secret_key) != OQS_SUCCESS) { return OQS_ERROR; }
	else { return OQS_SUCCESS; }
}

OQS_API OQS_STATUS OQS_SIG_sign(const OQS_SIG *sig, uint8_t *signature, size_t *signature_len, const uint8_t *message, size_t message_len, const uint8_t *secret_key) {
	if (sig == NULL || sig->sign(signature, signature_len, message, message_len, secret_key) != OQS_SUCCESS) { return OQS_ERROR; }
	else { return OQS_SUCCESS; }
}

OQS_API OQS_STATUS OQS_SIG_sign_with_ctx_str(const OQS_SIG *sig, uint8_t *signature, size_t *signature_len, const uint8_t *message, size_t message_len, const uint8_t *ctx_str, size_t ctx_str_len, const uint8_t *secret_key) {
	if (sig == NULL || sig->sign_with_ctx_str(signature, signature_len, message, message_len, ctx_str, ctx_str_len, secret_key) != OQS_SUCCESS) { return OQS_ERROR; }
	else { return OQS_SUCCESS; }
}

OQS_API OQS_STATUS OQS_SIG_verify(const OQS_SIG *sig, const uint8_t *message, size_t message_len, const uint8_t *signature, size_t signature_len, const uint8_t *public_key) {
	if (sig == NULL || sig->verify(message, message_len, signature, signature_len, public_key) != OQS_SUCCESS) { return OQS_ERROR; }
	else { return OQS_SUCCESS; }
}

OQS_API OQS_STATUS OQS_SIG_verify_with_ctx_str(const OQS_SIG *sig, const uint8_t *message, size_t message_len, const uint8_t *signature, size_t signature_len, const uint8_t *ctx_str, size_t ctx_str_len, const uint8_t *public_key) {
	if (sig == NULL || sig->verify_with_ctx_str(message, message_len, signature, signature_len, ctx_str, ctx_str_len, public_key) != OQS_SUCCESS) { return OQS_ERROR; }
	else { return OQS_SUCCESS; }
}

OQS_API bool OQS_SIG_supports_ctx_str(const char *alg_name) {
	OQS_SIG *sig = OQS_SIG_new(alg_name);
	if (sig == NULL) {
		return false;
	}
	bool result = sig->sig_with_ctx_support;
	OQS_SIG_free(sig);
	return result;
}

OQS_API void OQS_SIG_free(OQS_SIG *sig) {
	OQS_MEM_insecure_free(sig);
}
