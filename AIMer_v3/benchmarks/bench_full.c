// SPDX-License-Identifier: MIT
// AIMer v3 reference/AVX2/AVX-512 benchmark with AIMer_v2-compatible CSV.

#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <oqs/oqs.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#define HAVE_RDTSC 1
static inline uint64_t cycles_begin(void) {
	_mm_lfence();
	const uint64_t value = __rdtsc();
	_mm_lfence();
	return value;
}
static inline uint64_t cycles_end(void) {
	unsigned int auxiliary;
	const uint64_t value = __rdtscp(&auxiliary);
	_mm_lfence();
	return value;
}
#else
#define HAVE_RDTSC 0
static inline uint64_t cycles_begin(void) {
	return 0;
}
static inline uint64_t cycles_end(void) {
	return 0;
}
#endif

struct backend {
	const char *name;
	const char *version;
};

static const struct backend backends[] = {
	{"ref", "AIMer-v3-ref"},
	{"avx2", "AIMer-v3-avx2"},
	{"avx512", "AIMer-v3-avx512"},
};

static double now_seconds(void) {
	struct timespec timestamp;
#ifdef CLOCK_MONOTONIC_RAW
	clock_gettime(CLOCK_MONOTONIC_RAW, &timestamp);
#else
	clock_gettime(CLOCK_MONOTONIC, &timestamp);
#endif
	return (double)timestamp.tv_sec + (double)timestamp.tv_nsec / 1e9;
}

static int compare_u64(const void *left, const void *right) {
	const uint64_t x = *(const uint64_t *)left;
	const uint64_t y = *(const uint64_t *)right;
	return (x > y) - (x < y);
}

static const struct backend *find_backend(const char *name) {
	for (size_t i = 0; i < sizeof(backends) / sizeof(backends[0]); i++) {
		if (strcmp(name, backends[i].name) == 0) {
			return &backends[i];
		}
	}
	return NULL;
}

static int backend_is_supported(const struct backend *backend) {
	if (strcmp(backend->name, "ref") == 0) {
		return 1;
	}
	const int avx2 = OQS_CPU_has_extension(OQS_CPU_EXT_AES) &&
	                 OQS_CPU_has_extension(OQS_CPU_EXT_AVX2) &&
	                 OQS_CPU_has_extension(OQS_CPU_EXT_BMI2) &&
	                 OQS_CPU_has_extension(OQS_CPU_EXT_PCLMULQDQ) &&
	                 OQS_CPU_has_extension(OQS_CPU_EXT_POPCNT);
	if (strcmp(backend->name, "avx2") == 0) {
		return avx2;
	}
	return avx2 && OQS_CPU_has_extension(OQS_CPU_EXT_AVX512) &&
	       OQS_CPU_has_extension(OQS_CPU_EXT_AVX512VL) &&
	       OQS_CPU_has_extension(OQS_CPU_EXT_VPCLMULQDQ);
}

static int parse_count(const char *text, size_t *value) {
	char *end = NULL;
	const unsigned long long parsed = strtoull(text, &end, 10);
	if (text[0] == '\0' || end == NULL || *end != '\0' || parsed == 0 ||
	    parsed > SIZE_MAX / sizeof(uint64_t)) {
		return -1;
	}
	*value = (size_t)parsed;
	return 0;
}

static double calibrate_cycles_per_us(void) {
	if (!HAVE_RDTSC) {
		return 1.0;
	}
	const double start_time = now_seconds();
	const uint64_t start_cycles = cycles_begin();
	double end_time;
	do {
		end_time = now_seconds();
	} while (end_time - start_time < 0.1);
	const uint64_t end_cycles = cycles_end();
	return (double)(end_cycles - start_cycles) /
	       ((end_time - start_time) * 1e6);
}

static void emit_csv(uint64_t *samples, size_t count, const char *backend,
	                 const char *variant, const char *operation,
	                 double cycles_per_us) {
	qsort(samples, count, sizeof(*samples), compare_u64);
	const uint64_t minimum = samples[0];
	const uint64_t median = samples[count / 2];
	const uint64_t maximum = samples[count - 1];
	double sum = 0.0;
	for (size_t i = 0; i < count; i++) {
		sum += (double)samples[i];
	}
	const double mean = sum / (double)count;
	double variance = 0.0;
	for (size_t i = 0; i < count; i++) {
		const double delta = (double)samples[i] - mean;
		variance += delta * delta;
	}
	const double standard_deviation =
	    count > 1 ? sqrt(variance / (double)(count - 1)) : 0.0;
	const double coefficient_of_variation =
	    mean > 0.0 ? standard_deviation / mean * 100.0 : 0.0;
	const double median_us = (double)median / cycles_per_us;
	const double mean_us = mean / cycles_per_us;
	const double operations_per_second =
	    median_us > 0.0 ? 1e6 / median_us : 0.0;

	// backend,variant,op,N,min,median,max,mean,std,cv,med_us,mean_us,ops
	printf("%s,%s,%s,%zu,%llu,%llu,%llu,%.1f,%.1f,%.2f,%.4f,%.4f,%.1f\n",
	       backend, variant, operation, count,
	       (unsigned long long)minimum, (unsigned long long)median,
	       (unsigned long long)maximum, mean, standard_deviation,
	       coefficient_of_variation, median_us, mean_us,
	       operations_per_second);
}

static int run_operations(OQS_SIG *signature, uint8_t *public_key,
	                      uint8_t *secret_key, uint8_t *signed_message,
	                      size_t *signed_message_len, const uint8_t *message,
	                      size_t message_len) {
	if (OQS_SIG_keypair(signature, public_key, secret_key) != OQS_SUCCESS) {
		return -1;
	}
	if (OQS_SIG_sign(signature, signed_message, signed_message_len,
	                 message, message_len, secret_key) != OQS_SUCCESS) {
		return -1;
	}
	if (*signed_message_len != signature->length_signature ||
	    OQS_SIG_verify(signature, message, message_len, signed_message,
	                   *signed_message_len, public_key) != OQS_SUCCESS) {
		return -1;
	}
	return 0;
}

static void usage(const char *program) {
	fprintf(stderr,
	        "usage: %s <ref|avx2|avx512> <AIMER-v3-variant> <iters> "
	        "[warmup]\n",
	        program);
}

int main(int argc, char **argv) {
	if (argc < 4 || argc > 5) {
		usage(argv[0]);
		return 2;
	}
	const struct backend *backend = find_backend(argv[1]);
	if (backend == NULL) {
		usage(argv[0]);
		return 2;
	}
	size_t iterations;
	if (parse_count(argv[3], &iterations) != 0) {
		fprintf(stderr, "iterations must be a positive integer\n");
		return 2;
	}
	size_t warmup = iterations / 10;
	if (warmup < 5) {
		warmup = 5;
	}
	if (argc == 5 && parse_count(argv[4], &warmup) != 0) {
		fprintf(stderr, "warmup must be a positive integer\n");
		return 2;
	}

	if (setenv("AIMER_V3_IMPL", backend->name, 1) != 0) {
		perror("setenv");
		return 2;
	}
	OQS_init();
	if (!backend_is_supported(backend)) {
		fprintf(stderr, "backend %s is not supported by this CPU\n",
		        backend->name);
		OQS_destroy();
		return 2;
	}

	OQS_SIG *signature = OQS_SIG_new(argv[2]);
	if (signature == NULL) {
		fprintf(stderr, "variant %s is not enabled\n", argv[2]);
		OQS_destroy();
		return 2;
	}
	if (strcmp(signature->alg_version, backend->version) != 0) {
		fprintf(stderr, "dispatch mismatch: requested %s, selected %s\n",
		        backend->version, signature->alg_version);
		OQS_SIG_free(signature);
		OQS_destroy();
		return 2;
	}

	uint8_t *public_key = OQS_MEM_malloc(signature->length_public_key);
	uint8_t *secret_key = OQS_MEM_malloc(signature->length_secret_key);
	uint8_t *signed_message = OQS_MEM_malloc(signature->length_signature);
	uint64_t *keypair_cycles = malloc(iterations * sizeof(uint64_t));
	uint64_t *sign_cycles = malloc(iterations * sizeof(uint64_t));
	uint64_t *verify_cycles = malloc(iterations * sizeof(uint64_t));
	if (public_key == NULL || secret_key == NULL || signed_message == NULL ||
	    keypair_cycles == NULL || sign_cycles == NULL ||
	    verify_cycles == NULL) {
		fprintf(stderr, "out of memory\n");
		goto failure;
	}

	static const uint8_t message[64] = "AIMer v3 SIMD benchmark message";
	size_t signed_message_len = 0;
	for (size_t i = 0; i < warmup; i++) {
		if (run_operations(signature, public_key, secret_key, signed_message,
		                   &signed_message_len, message, sizeof(message)) != 0) {
			fprintf(stderr, "warmup failed at iteration %zu\n", i);
			goto failure;
		}
	}

	const double cycles_per_us = calibrate_cycles_per_us();
	for (size_t i = 0; i < iterations; i++) {
		uint64_t start = cycles_begin();
		const OQS_STATUS keypair_status =
		    OQS_SIG_keypair(signature, public_key, secret_key);
		keypair_cycles[i] = cycles_end() - start;
		if (keypair_status != OQS_SUCCESS) {
			fprintf(stderr, "keypair failed at iteration %zu\n", i);
			goto failure;
		}

		start = cycles_begin();
		const OQS_STATUS sign_status = OQS_SIG_sign(
		    signature, signed_message, &signed_message_len, message,
		    sizeof(message), secret_key);
		sign_cycles[i] = cycles_end() - start;
		if (sign_status != OQS_SUCCESS ||
		    signed_message_len != signature->length_signature) {
			fprintf(stderr, "sign failed at iteration %zu\n", i);
			goto failure;
		}

		start = cycles_begin();
		const OQS_STATUS verify_status = OQS_SIG_verify(
		    signature, message, sizeof(message), signed_message,
		    signed_message_len, public_key);
		verify_cycles[i] = cycles_end() - start;
		if (verify_status != OQS_SUCCESS) {
			fprintf(stderr, "verify failed at iteration %zu\n", i);
			goto failure;
		}
	}

	emit_csv(keypair_cycles, iterations, backend->name,
	         signature->method_name, "keypair", cycles_per_us);
	emit_csv(sign_cycles, iterations, backend->name,
	         signature->method_name, "sign", cycles_per_us);
	emit_csv(verify_cycles, iterations, backend->name,
	         signature->method_name, "verify", cycles_per_us);

	OQS_MEM_insecure_free(public_key);
	OQS_MEM_secure_free(secret_key, signature->length_secret_key);
	OQS_MEM_insecure_free(signed_message);
	free(keypair_cycles);
	free(sign_cycles);
	free(verify_cycles);
	OQS_SIG_free(signature);
	OQS_destroy();
	return 0;

failure:
	OQS_MEM_insecure_free(public_key);
	OQS_MEM_secure_free(secret_key, signature->length_secret_key);
	OQS_MEM_insecure_free(signed_message);
	free(keypair_cycles);
	free(sign_cycles);
	free(verify_cycles);
	OQS_SIG_free(signature);
	OQS_destroy();
	return 1;
}
