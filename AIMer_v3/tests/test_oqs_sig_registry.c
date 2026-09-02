// SPDX-License-Identifier: MIT

#include <oqs/oqs.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct expected_sig {
	const char *name;
	size_t public_key_bytes;
	size_t secret_key_bytes;
	size_t signature_bytes;
	uint8_t nist_level;
};

static int test_algorithm(const struct expected_sig *expected) {
	static const uint8_t message[] = "AIMer v3 OQS registry test";
	static const uint8_t context[] = "multi-backend-port";
	OQS_SIG *sig = OQS_SIG_new(expected->name);
	if (sig == NULL) {
		fprintf(stderr, "OQS_SIG_new failed for %s\n", expected->name);
		return -1;
	}

	if (strcmp(sig->method_name, expected->name) != 0 ||
	    sig->length_public_key != expected->public_key_bytes ||
	    sig->length_secret_key != expected->secret_key_bytes ||
	    sig->length_signature != expected->signature_bytes ||
	    sig->claimed_nist_level != expected->nist_level ||
	    !sig->sig_with_ctx_support || !OQS_SIG_supports_ctx_str(expected->name)) {
		fprintf(stderr, "metadata mismatch for %s\n", expected->name);
		OQS_SIG_free(sig);
		return -1;
	}

	const char *forced = getenv("AIMER_V3_IMPL");
	const int cpu_has_avx2 =
	    OQS_CPU_has_extension(OQS_CPU_EXT_AES) &&
	    OQS_CPU_has_extension(OQS_CPU_EXT_AVX2) &&
	    OQS_CPU_has_extension(OQS_CPU_EXT_BMI2) &&
	    OQS_CPU_has_extension(OQS_CPU_EXT_PCLMULQDQ) &&
	    OQS_CPU_has_extension(OQS_CPU_EXT_POPCNT);
	const int cpu_has_avx512 =
	    cpu_has_avx2 && OQS_CPU_has_extension(OQS_CPU_EXT_AVX512) &&
	    OQS_CPU_has_extension(OQS_CPU_EXT_AVX512VL) &&
	    OQS_CPU_has_extension(OQS_CPU_EXT_VPCLMULQDQ);
	const char *expected_version;
	if (forced != NULL && strcmp(forced, "avx512") == 0) {
		expected_version = "AIMer-v3-avx512";
	} else if (forced != NULL && strcmp(forced, "avx2") == 0) {
		expected_version = "AIMer-v3-avx2";
	} else if (forced != NULL &&
	           (strcmp(forced, "ref") == 0 ||
	            strcmp(forced, "reference") == 0)) {
		expected_version = "AIMer-v3-ref";
	} else if (cpu_has_avx512) {
		expected_version = "AIMer-v3-avx512";
	} else if (cpu_has_avx2) {
		expected_version = "AIMer-v3-avx2";
	} else {
		expected_version = "AIMer-v3-ref";
	}
	if (strcmp(sig->alg_version, expected_version) != 0) {
		fprintf(stderr, "dispatch mismatch for %s: %s\n",
		        expected->name, sig->alg_version);
		OQS_SIG_free(sig);
		return -1;
	}

	uint8_t *public_key = OQS_MEM_malloc(sig->length_public_key);
	uint8_t *secret_key = OQS_MEM_malloc(sig->length_secret_key);
	uint8_t *signature = OQS_MEM_malloc(sig->length_signature);
	if (public_key == NULL || secret_key == NULL || signature == NULL) {
		OQS_MEM_insecure_free(public_key);
		OQS_MEM_secure_free(secret_key, sig->length_secret_key);
		OQS_MEM_insecure_free(signature);
		OQS_SIG_free(sig);
		return -1;
	}

	size_t signature_len = 0;
	int failed = OQS_SIG_keypair(sig, public_key, secret_key) != OQS_SUCCESS ||
	             OQS_SIG_sign_with_ctx_str(
	                 sig, signature, &signature_len,
	                 message, sizeof(message) - 1,
	                 context, sizeof(context) - 1, secret_key) != OQS_SUCCESS ||
	             signature_len != sig->length_signature ||
	             OQS_SIG_verify_with_ctx_str(
	                 sig, message, sizeof(message) - 1,
	                 signature, signature_len,
	                 context, sizeof(context) - 1, public_key) != OQS_SUCCESS;

	if (!failed) {
		signature[0] ^= 1;
		failed = OQS_SIG_verify_with_ctx_str(
		             sig, message, sizeof(message) - 1,
		             signature, signature_len,
		             context, sizeof(context) - 1, public_key) == OQS_SUCCESS;
		signature[0] ^= 1;
	}

	OQS_MEM_insecure_free(public_key);
	OQS_MEM_secure_free(secret_key, sig->length_secret_key);
	OQS_MEM_insecure_free(signature);
	OQS_SIG_free(sig);
	return failed ? -1 : 0;
}

int main(void) {
	static const struct expected_sig algorithms[] = {
		{OQS_SIG_alg_aimer_v3_128f, 32, 48, 6944, 1},
		{OQS_SIG_alg_aimer_v3_128s, 32, 48, 4704, 1},
		{OQS_SIG_alg_aimer_v3_192f, 48, 72, 15408, 3},
		{OQS_SIG_alg_aimer_v3_192s, 48, 72, 10320, 3},
		{OQS_SIG_alg_aimer_v3_256f, 64, 96, 31360, 5},
		{OQS_SIG_alg_aimer_v3_256s, 64, 96, 20224, 5},
	};

	OQS_init();
	if (OQS_SIG_alg_count() != (int)(sizeof(algorithms) / sizeof(algorithms[0])) ||
	    OQS_SIG_alg_is_enabled("not-an-algorithm") ||
	    OQS_SIG_new("not-an-algorithm") != NULL) {
		fprintf(stderr, "OQS_SIG registry metadata mismatch\n");
		return 1;
	}

	for (size_t i = 0; i < sizeof(algorithms) / sizeof(algorithms[0]); i++) {
		const char *identifier = OQS_SIG_alg_identifier(i);
		if (identifier == NULL || strcmp(identifier, algorithms[i].name) != 0 ||
		    !OQS_SIG_alg_is_enabled(identifier) || test_algorithm(&algorithms[i]) != 0) {
			return 1;
		}
	}
	if (OQS_SIG_alg_identifier(sizeof(algorithms) / sizeof(algorithms[0])) != NULL) {
		return 1;
	}

	OQS_destroy();
	puts("[PASS] OQS_SIG registry/context/tamper test (all six parameter sets)");
	return 0;
}
