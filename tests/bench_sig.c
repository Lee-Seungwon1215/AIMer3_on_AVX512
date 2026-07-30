// SPDX-License-Identifier: MIT
//
// AIMer-AVX512 benchmark.
//
// Measures keypair / sign / verify wall time (and rdtsc cycles when available)
// for one or all AIMER variants.
//
// Usage:
//   bench_sig                       run all 6 variants, default 100 iterations
//   bench_sig <variant>             run only that variant, default 100 iterations
//   bench_sig <variant> <iters>     custom iteration count
//   bench_sig all <iters>           all variants with custom iterations

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <oqs/oqs.h>
#include "aimer_keccak_select.h"

#if defined(__x86_64__) || defined(_M_X64)
#  include <x86intrin.h>
static inline uint64_t rdtsc_now(void) { return __rdtsc(); }
#  define HAVE_RDTSC 1
#else
static inline uint64_t rdtsc_now(void) { return 0; }
#  define HAVE_RDTSC 0
#endif

static double now_seconds(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
}

static const char *VARIANTS[] = {
	"AIMER-128f", "AIMER-128s",
	"AIMER-192f", "AIMER-192s",
	"AIMER-256f", "AIMER-256s",
};
static const size_t NUM_VARIANTS = sizeof(VARIANTS) / sizeof(VARIANTS[0]);

static int qsort_u64(const void *a, const void *b) {
	uint64_t x = *(const uint64_t *) a;
	uint64_t y = *(const uint64_t *) b;
	return (x > y) - (x < y);
}

static uint64_t median_u64(uint64_t *arr, size_t n) {
	qsort(arr, n, sizeof(*arr), qsort_u64);
	return arr[n / 2];
}

static double mean_us(const uint64_t *cycles, size_t n, double cyc_per_us) {
	double sum = 0;
	for (size_t i = 0; i < n; i++) {
		sum += (double) cycles[i];
	}
	return sum / (double) n / cyc_per_us;
}

static int bench_one(const char *name, size_t iters) {
	OQS_SIG *sig = OQS_SIG_new(name);
	if (!sig) {
		fprintf(stderr, "  [skip] %s not enabled\n", name);
		return -1;
	}

	uint8_t *pk = OQS_MEM_malloc(sig->length_public_key);
	uint8_t *sk = OQS_MEM_malloc(sig->length_secret_key);
	uint8_t *signature = OQS_MEM_malloc(sig->length_signature);
	uint64_t *cyc_kp = OQS_MEM_malloc(iters * sizeof(uint64_t));
	uint64_t *cyc_sg = OQS_MEM_malloc(iters * sizeof(uint64_t));
	uint64_t *cyc_vf = OQS_MEM_malloc(iters * sizeof(uint64_t));
	if (!pk || !sk || !signature || !cyc_kp || !cyc_sg || !cyc_vf) {
		fprintf(stderr, "  [error] OOM\n");
		OQS_MEM_insecure_free(pk);
		OQS_MEM_insecure_free(sk);
		OQS_MEM_insecure_free(signature);
		OQS_MEM_insecure_free(cyc_kp);
		OQS_MEM_insecure_free(cyc_sg);
		OQS_MEM_insecure_free(cyc_vf);
		OQS_SIG_free(sig);
		return -1;
	}

	const uint8_t msg[64] = "AIMer-AVX512 benchmark message"; /* arbitrary but stable */
	const size_t msg_len = sizeof(msg);
	size_t siglen = 0;

	/* Calibrate cycles-per-microsecond by timing 50ms of __rdtsc(). */
	double cyc_per_us = 1.0;
	if (HAVE_RDTSC) {
		double t0 = now_seconds();
		uint64_t c0 = rdtsc_now();
		while (now_seconds() - t0 < 0.05) {
			/* spin */
		}
		double elapsed_us = (now_seconds() - t0) * 1e6;
		uint64_t c1 = rdtsc_now();
		cyc_per_us = (double) (c1 - c0) / elapsed_us;
	}

	double t_start = now_seconds();
	for (size_t i = 0; i < iters; i++) {
		uint64_t c0 = rdtsc_now();
		OQS_STATUS rc = OQS_SIG_keypair(sig, pk, sk);
		uint64_t c1 = rdtsc_now();
		if (rc != OQS_SUCCESS) {
			fprintf(stderr, "  [error] keypair failed at iter %zu\n", i);
			goto cleanup;
		}
		cyc_kp[i] = c1 - c0;

		c0 = rdtsc_now();
		rc = OQS_SIG_sign(sig, signature, &siglen, msg, msg_len, sk);
		c1 = rdtsc_now();
		if (rc != OQS_SUCCESS) {
			fprintf(stderr, "  [error] sign failed at iter %zu\n", i);
			goto cleanup;
		}
		cyc_sg[i] = c1 - c0;

		c0 = rdtsc_now();
		rc = OQS_SIG_verify(sig, msg, msg_len, signature, siglen, pk);
		c1 = rdtsc_now();
		if (rc != OQS_SUCCESS) {
			fprintf(stderr, "  [error] verify failed at iter %zu\n", i);
			goto cleanup;
		}
		cyc_vf[i] = c1 - c0;
	}
	double t_total = now_seconds() - t_start;

	uint64_t med_kp = median_u64(cyc_kp, iters);
	uint64_t med_sg = median_u64(cyc_sg, iters);
	uint64_t med_vf = median_u64(cyc_vf, iters);
	double mean_kp_us = mean_us(cyc_kp, iters, cyc_per_us);
	double mean_sg_us = mean_us(cyc_sg, iters, cyc_per_us);
	double mean_vf_us = mean_us(cyc_vf, iters, cyc_per_us);

	printf("\n  %s  (iters=%zu, total %.2fs)\n", name, iters, t_total);
	printf("  pk=%zuB  sk=%zuB  sig=%zuB\n",
	       sig->length_public_key, sig->length_secret_key, sig->length_signature);
	if (HAVE_RDTSC) {
		printf("  %-8s %15s %15s\n", "op", "median cycles", "mean us");
		printf("  %-8s %15" PRIu64 " %15.1f\n", "keypair", med_kp, mean_kp_us);
		printf("  %-8s %15" PRIu64 " %15.1f\n", "sign",    med_sg, mean_sg_us);
		printf("  %-8s %15" PRIu64 " %15.1f\n", "verify",  med_vf, mean_vf_us);
	} else {
		printf("  (rdtsc not available — cycle counts may be 0)\n");
		printf("  %-8s %15s\n", "op", "mean us");
		printf("  %-8s %15.1f\n", "keypair", mean_kp_us);
		printf("  %-8s %15.1f\n", "sign",    mean_sg_us);
		printf("  %-8s %15.1f\n", "verify",  mean_vf_us);
	}

cleanup:
	OQS_MEM_insecure_free(pk);
	OQS_MEM_secure_free(sk, sig->length_secret_key);
	OQS_MEM_insecure_free(signature);
	OQS_MEM_insecure_free(cyc_kp);
	OQS_MEM_insecure_free(cyc_sg);
	OQS_MEM_insecure_free(cyc_vf);
	OQS_SIG_free(sig);
	return 0;
}

static void usage(FILE *fp) {
	fprintf(fp,
	        "Usage: bench_sig [variant|all] [iterations]\n"
	        "  variant   = AIMER-128f|128s|192f|192s|256f|256s, or 'all' (default)\n"
	        "  iterations defaults to 100\n");
}

int main(int argc, char **argv) {
	const char *target = "all";
	size_t iters = 100;
	if (argc >= 2) {
		target = argv[1];
	}
	if (argc >= 3) {
		long v = strtol(argv[2], NULL, 10);
		if (v <= 0) {
			usage(stderr);
			return 2;
		}
		iters = (size_t) v;
	}
	if (argc > 3) {
		usage(stderr);
		return 2;
	}

	OQS_init();
	aimer_keccak_select_from_env(); // measurement: pin Keccak to AIMER_IMPL
	printf("AIMer-AVX512 benchmark (rdtsc=%s)\n", HAVE_RDTSC ? "yes" : "no");

	int rc = 0;
	if (strcasecmp(target, "all") == 0) {
		for (size_t i = 0; i < NUM_VARIANTS; i++) {
			if (bench_one(VARIANTS[i], iters) != 0) {
				rc = 1;
			}
		}
	} else {
		/* allow case variations and underscore */
		char norm[32];
		size_t n = 0;
		for (size_t i = 0; target[i] && n + 1 < sizeof(norm); i++) {
			char c = (char) toupper((unsigned char) target[i]);
			if (c == '_') {
				c = '-';
			}
			norm[n++] = c;
		}
		norm[n] = '\0';
		if (bench_one(norm, iters) != 0) {
			rc = 1;
		}
	}

	OQS_destroy();
	return rc;
}
