// SPDX-License-Identifier: MIT
// Adapts the official 192s PQCgenKAT_sign driver to the detached OQS_SIG API.

#ifndef AIMER_V3_OQS_KAT_COMPAT_192S_H
#define AIMER_V3_OQS_KAT_COMPAT_192S_H

#define rng_h
#define API_H

#include <oqs/oqs.h>
#include <oqs/rand_nist.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CRYPTO_PUBLICKEYBYTES OQS_SIG_aimer_v3_192s_length_public_key
#define CRYPTO_SECRETKEYBYTES OQS_SIG_aimer_v3_192s_length_secret_key
#define CRYPTO_BYTES OQS_SIG_aimer_v3_192s_length_signature
#define CRYPTO_ALGNAME "aimer-192s"

static OQS_SIG *oqs_kat_sig(void) {
	static OQS_SIG *sig;
	if (sig == NULL) {
		OQS_init();
		sig = OQS_SIG_new(OQS_SIG_alg_aimer_v3_192s);
	}
	return sig;
}

static void oqs_kat_randombytes_init(unsigned char *entropy_input,
                                     unsigned char *personalization_string,
                                     int security_strength) {
	(void)security_strength;
	OQS_randombytes_nist_kat_init_256bit(entropy_input,
	                                      personalization_string);
	OQS_randombytes_custom_algorithm(OQS_randombytes_nist_kat);
}

static int oqs_kat_randombytes(unsigned char *out,
                               unsigned long long out_len) {
	OQS_randombytes(out, (size_t)out_len);
	return 0;
}

static int oqs_kat_crypto_sign_keypair(uint8_t *public_key,
                                       uint8_t *secret_key) {
	OQS_SIG *sig = oqs_kat_sig();
	return sig != NULL && OQS_SIG_keypair(sig, public_key, secret_key) == OQS_SUCCESS
	           ? 0
	           : -1;
}

static int oqs_kat_crypto_sign(uint8_t *signed_message,
                               size_t *signed_message_len,
                               const uint8_t *message, size_t message_len,
                               const uint8_t *ctx, size_t ctx_len,
                               const uint8_t *secret_key) {
	OQS_SIG *sig = oqs_kat_sig();
	if (sig == NULL) {
		return -1;
	}

	size_t signature_len = 0;
	OQS_STATUS status;
	if (ctx_len == 0) {
		status = OQS_SIG_sign(sig, signed_message + message_len, &signature_len,
		                      message, message_len, secret_key);
	} else {
		status = OQS_SIG_sign_with_ctx_str(
		    sig, signed_message + message_len, &signature_len,
		    message, message_len, ctx, ctx_len, secret_key);
	}
	if (status != OQS_SUCCESS) {
		return -1;
	}

	memcpy(signed_message, message, message_len);
	*signed_message_len = message_len + signature_len;
	return 0;
}

static int oqs_kat_crypto_sign_open(uint8_t *message, size_t *message_len,
                                    const uint8_t *signed_message,
                                    size_t signed_message_len,
                                    const uint8_t *ctx, size_t ctx_len,
                                    const uint8_t *public_key) {
	OQS_SIG *sig = oqs_kat_sig();
	if (sig == NULL || signed_message_len < sig->length_signature) {
		return -1;
	}

	const size_t recovered_len = signed_message_len - sig->length_signature;
	const uint8_t *signature = signed_message + recovered_len;
	OQS_STATUS status;
	if (ctx_len == 0) {
		status = OQS_SIG_verify(sig, signed_message, recovered_len,
		                        signature, sig->length_signature, public_key);
	} else {
		status = OQS_SIG_verify_with_ctx_str(
		    sig, signed_message, recovered_len,
		    signature, sig->length_signature, ctx, ctx_len, public_key);
	}
	if (status != OQS_SUCCESS) {
		return -1;
	}

	memmove(message, signed_message, recovered_len);
	*message_len = recovered_len;
	return 0;
}

#define randombytes_init oqs_kat_randombytes_init
#define randombytes oqs_kat_randombytes
#define crypto_sign_keypair oqs_kat_crypto_sign_keypair
#define crypto_sign oqs_kat_crypto_sign
#define crypto_sign_open oqs_kat_crypto_sign_open

#endif // AIMER_V3_OQS_KAT_COMPAT_192S_H
