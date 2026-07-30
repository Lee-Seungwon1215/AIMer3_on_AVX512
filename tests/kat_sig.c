// SPDX-License-Identifier: MIT
//
// AIMer-AVX512 KAT test driver.
//
// For the given AIMER variant, regenerate the NIST KAT response file
// (sm = msg || sig) using the OQS_SIG_* interface seeded by the NIST
// AES-256-CTR-DRBG, then byte-compare each KAT field against the
// AIMer reference file under tests/KAT/aimer-<v>/PQCsignKAT_48.rsp.
//
// Usage:
//   kat_sig <variant>            run all 100 KAT counts and report PASS/FAIL
//   kat_sig <variant> <count>    run only the given count (0..99) and print fields
//
//   <variant> is one of: AIMER-128f, AIMER-128s, AIMER-192f, AIMER-192s,
//                        AIMER-256f, AIMER-256s
//                        (case-insensitive; underscore form also accepted)

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <oqs/oqs.h>
#include <oqs/rand_nist.h>
#include "aimer_keccak_select.h"

#include "test_helpers.h"

#define MAX_MARKER_LEN 64

/* -------------------- KAT .rsp parsing helpers -------------------- */

static int find_marker(FILE *fp, const char *marker) {
	char line[MAX_MARKER_LEN];
	size_t len = strlen(marker);
	if (len >= MAX_MARKER_LEN) {
		len = MAX_MARKER_LEN - 1;
	}
	for (size_t i = 0; i < len; i++) {
		int ch = fgetc(fp);
		if (ch == EOF) {
			return 0;
		}
		line[i] = (char) ch;
	}
	line[len] = '\0';
	while (1) {
		if (strncmp(line, marker, len) == 0) {
			return 1;
		}
		for (size_t i = 0; i < len - 1; i++) {
			line[i] = line[i + 1];
		}
		int ch = fgetc(fp);
		if (ch == EOF) {
			return 0;
		}
		line[len - 1] = (char) ch;
	}
}

static int read_hex(FILE *fp, uint8_t *out, size_t out_len, const char *marker) {
	if (!find_marker(fp, marker)) {
		return 0;
	}
	memset(out, 0, out_len);
	size_t bytes = 0;
	int high = -1;
	while (1) {
		int ch = fgetc(fp);
		if (ch == EOF || ch == '\n' || ch == '\r') {
			if (high != -1) {
				return 0;
			}
			return (int) bytes;
		}
		if (!isxdigit(ch)) {
			if (high != -1) {
				return 0;
			}
			continue;
		}
		int v = (ch <= '9') ? ch - '0' : (tolower(ch) - 'a' + 10);
		if (high == -1) {
			high = v;
		} else {
			if (bytes >= out_len) {
				/* keep consuming until newline so file position is sane */
				continue;
			}
			out[bytes++] = (uint8_t) ((high << 4) | v);
			high = -1;
		}
	}
}

static int read_size(FILE *fp, size_t *value, const char *marker) {
	if (!find_marker(fp, marker)) {
		return 0;
	}
	unsigned long long v;
	if (fscanf(fp, "%llu", &v) != 1) {
		return 0;
	}
	*value = (size_t) v;
	return 1;
}

/* -------------------- variant <-> KAT path -------------------- */

struct variant_info {
	const char *oqs_name;        /* OQS_SIG_new() identifier */
	const char *kat_dir_name;    /* tests/KAT/<dir> */
};

static const struct variant_info VARIANTS[] = {
	{"AIMER-128f", "aimer-128f"},
	{"AIMER-128s", "aimer-128s"},
	{"AIMER-192f", "aimer-192f"},
	{"AIMER-192s", "aimer-192s"},
	{"AIMER-256f", "aimer-256f"},
	{"AIMER-256s", "aimer-256s"},
};
static const size_t NUM_VARIANTS = sizeof(VARIANTS) / sizeof(VARIANTS[0]);

static const struct variant_info *resolve_variant(const char *name) {
	/* Accept both AIMER-128f and aimer_128f forms (case-insensitive). */
	char norm[32];
	size_t n = 0;
	for (size_t i = 0; name[i] && n + 1 < sizeof(norm); i++) {
		char c = (char) toupper((unsigned char) name[i]);
		if (c == '_') {
			c = '-';
		}
		norm[n++] = c;
	}
	norm[n] = '\0';
	for (size_t i = 0; i < NUM_VARIANTS; i++) {
		if (strcasecmp(norm, VARIANTS[i].oqs_name) == 0) {
			return &VARIANTS[i];
		}
	}
	return NULL;
}

/* -------------------- per-count KAT execution -------------------- */

struct kat_buffers {
	uint8_t seed[48];
	uint8_t *msg;
	uint8_t *pk;
	uint8_t *sk;
	uint8_t *signature;
	uint8_t *sm;        /* msg || signature */
	uint8_t *ref_pk;
	uint8_t *ref_sk;
	uint8_t *ref_sm;
	uint8_t *ref_msg;
};

static void zero_buffers(struct kat_buffers *b) {
	memset(b, 0, sizeof(*b));
}

static void free_buffers(struct kat_buffers *b) {
	OQS_MEM_insecure_free(b->msg);
	OQS_MEM_insecure_free(b->pk);
	OQS_MEM_insecure_free(b->sk);
	OQS_MEM_insecure_free(b->signature);
	OQS_MEM_insecure_free(b->sm);
	OQS_MEM_insecure_free(b->ref_pk);
	OQS_MEM_insecure_free(b->ref_sk);
	OQS_MEM_insecure_free(b->ref_sm);
	OQS_MEM_insecure_free(b->ref_msg);
}

/*
 * Process one KAT count: read fields from req_fp + rsp_fp, run the algorithm,
 * compare. Returns:
 *   1  success
 *   0  test failure (mismatch) - reason printed to stderr
 *  -1  I/O / unexpected error
 */
static int run_one_kat(OQS_SIG *sig, FILE *rsp_fp, int expected_count,
                       bool verbose) {
	size_t count_val = 0;
	if (!read_size(rsp_fp, &count_val, "count = ")) {
		return -1;
	}
	if ((int) count_val != expected_count) {
		fprintf(stderr, "  count mismatch in .rsp: expected %d got %zu\n",
		        expected_count, count_val);
		return -1;
	}

	struct kat_buffers b;
	zero_buffers(&b);
	int rc = -1;

	if (read_hex(rsp_fp, b.seed, sizeof(b.seed), "seed = ") != 48) {
		fprintf(stderr, "  failed to read seed\n");
		goto out;
	}

	size_t mlen = 0;
	if (!read_size(rsp_fp, &mlen, "mlen = ")) {
		fprintf(stderr, "  failed to read mlen\n");
		goto out;
	}

	b.msg = OQS_MEM_malloc(mlen);
	b.ref_msg = OQS_MEM_malloc(mlen);
	if (!b.msg || !b.ref_msg) {
		goto out;
	}
	if (read_hex(rsp_fp, b.msg, mlen, "msg = ") != (int) mlen) {
		fprintf(stderr, "  failed to read msg\n");
		goto out;
	}
	memcpy(b.ref_msg, b.msg, mlen);

	b.pk = OQS_MEM_malloc(sig->length_public_key);
	b.sk = OQS_MEM_malloc(sig->length_secret_key);
	b.ref_pk = OQS_MEM_malloc(sig->length_public_key);
	b.ref_sk = OQS_MEM_malloc(sig->length_secret_key);
	b.signature = OQS_MEM_malloc(sig->length_signature);
	b.sm = OQS_MEM_malloc(mlen + sig->length_signature);
	b.ref_sm = OQS_MEM_malloc(mlen + sig->length_signature);
	if (!b.pk || !b.sk || !b.ref_pk || !b.ref_sk || !b.signature || !b.sm || !b.ref_sm) {
		goto out;
	}
	if (read_hex(rsp_fp, b.ref_pk, sig->length_public_key, "pk = ") != (int) sig->length_public_key) {
		fprintf(stderr, "  failed to read pk\n");
		goto out;
	}
	if (read_hex(rsp_fp, b.ref_sk, sig->length_secret_key, "sk = ") != (int) sig->length_secret_key) {
		fprintf(stderr, "  failed to read sk\n");
		goto out;
	}
	size_t ref_smlen = 0;
	if (!read_size(rsp_fp, &ref_smlen, "smlen = ")) {
		fprintf(stderr, "  failed to read smlen\n");
		goto out;
	}
	if (ref_smlen != mlen + sig->length_signature) {
		fprintf(stderr, "  smlen mismatch (ref=%zu expected=%zu)\n",
		        ref_smlen, mlen + sig->length_signature);
		goto out;
	}
	if (read_hex(rsp_fp, b.ref_sm, ref_smlen, "sm = ") != (int) ref_smlen) {
		fprintf(stderr, "  failed to read sm\n");
		goto out;
	}

	/* Seed the NIST KAT DRBG with the per-count seed, then run keypair + sign. */
	OQS_randombytes_nist_kat_init_256bit(b.seed, NULL);

	if (OQS_SIG_keypair(sig, b.pk, b.sk) != OQS_SUCCESS) {
		fprintf(stderr, "  keypair failed\n");
		goto out;
	}
	if (memcmp(b.pk, b.ref_pk, sig->length_public_key) != 0) {
		fprintf(stderr, "  PK mismatch\n");
		goto out;
	}
	if (memcmp(b.sk, b.ref_sk, sig->length_secret_key) != 0) {
		fprintf(stderr, "  SK mismatch\n");
		goto out;
	}

	size_t siglen = 0;
	if (OQS_SIG_sign(sig, b.signature, &siglen, b.msg, mlen, b.sk) != OQS_SUCCESS) {
		fprintf(stderr, "  sign failed\n");
		goto out;
	}
	if (siglen != sig->length_signature) {
		fprintf(stderr, "  signature length mismatch (%zu vs %zu)\n",
		        siglen, sig->length_signature);
		goto out;
	}

	/* AIMer KAT format: sm = msg || signature, smlen = mlen + siglen */
	memcpy(b.sm, b.msg, mlen);
	memcpy(b.sm + mlen, b.signature, siglen);

	if (memcmp(b.sm, b.ref_sm, mlen + siglen) != 0) {
		fprintf(stderr, "  SM mismatch\n");
		goto out;
	}

	/* Round-trip verify on detached form. */
	if (OQS_SIG_verify(sig, b.msg, mlen, b.signature, siglen, b.pk) != OQS_SUCCESS) {
		fprintf(stderr, "  verify failed\n");
		goto out;
	}

	if (verbose) {
		printf("count = %zu\n", count_val);
		OQS_fprintBstr(stdout, "seed = ", b.seed, 48);
		printf("mlen = %zu\n", mlen);
		OQS_fprintBstr(stdout, "msg  = ", b.msg, mlen);
		OQS_fprintBstr(stdout, "pk   = ", b.pk, sig->length_public_key);
		OQS_fprintBstr(stdout, "sk   = ", b.sk, sig->length_secret_key);
		printf("smlen = %zu\n", mlen + siglen);
	}

	rc = 1;
out:
	free_buffers(&b);
	return rc;
}

/* -------------------- main -------------------- */

static void usage(FILE *fp) {
	fprintf(fp,
	        "Usage: kat_sig <variant> [count]\n"
	        "  <variant> = AIMER-128f | AIMER-128s | AIMER-192f | AIMER-192s\n"
	        "            | AIMER-256f | AIMER-256s\n"
	        "  [count]   = optional 0..99 to run a single KAT count verbosely\n"
	        "\n"
	        "Reference KAT files are read from:\n"
	        "  tests/KAT/<variant>/PQCsignKAT_<sk_bytes>.rsp\n");
}

int main(int argc, char **argv) {
	if (argc < 2 || argc > 3) {
		usage(stderr);
		return 2;
	}

	OQS_init();
	aimer_keccak_select_from_env(); // measurement: pin Keccak to AIMER_IMPL
	/* Route OQS_randombytes through the NIST AES-256-CTR-DRBG so that
	 * OQS_randombytes_nist_kat_init_256bit() actually controls keypair/sign
	 * randomness. Without this, OQS_randombytes defaults to /dev/urandom. */
	OQS_randombytes_custom_algorithm(&OQS_randombytes_nist_kat);

	const struct variant_info *v = resolve_variant(argv[1]);
	if (!v) {
		fprintf(stderr, "Unknown variant: %s\n", argv[1]);
		usage(stderr);
		OQS_destroy();
		return 2;
	}

	int single_count = -1;
	if (argc == 3) {
		single_count = atoi(argv[2]);
		if (single_count < 0 || single_count > 99) {
			fprintf(stderr, "count must be 0..99\n");
			OQS_destroy();
			return 2;
		}
	}

	OQS_SIG *sig = OQS_SIG_new(v->oqs_name);
	if (!sig) {
		fprintf(stderr, "%s is not enabled in this build\n", v->oqs_name);
		OQS_destroy();
		return 2;
	}

	char rsp_path[256];
	snprintf(rsp_path, sizeof(rsp_path),
	         "tests/KAT/%s/PQCsignKAT_%zu.rsp",
	         v->kat_dir_name, sig->length_secret_key);

	FILE *rsp_fp = fopen(rsp_path, "r");
	if (!rsp_fp) {
		fprintf(stderr, "Cannot open %s: %s\n", rsp_path, strerror(errno));
		OQS_SIG_free(sig);
		OQS_destroy();
		return 2;
	}

	printf("[KAT] %s\n", v->oqs_name);
	printf("  reference: %s\n", rsp_path);
	printf("  pk=%zuB sk=%zuB sig=%zuB\n",
	       sig->length_public_key, sig->length_secret_key, sig->length_signature);

	int total = (single_count >= 0) ? 1 : 100;
	int passed = 0, failed = 0;
	int rc = 0;

	if (single_count >= 0) {
		/* Skip to the requested count entry. Each entry is one block. */
		char header[64];
		snprintf(header, sizeof(header), "count = %d\n", single_count);
		/* Coarse seek: rewind and scan. */
		rewind(rsp_fp);
		char line[256];
		while (fgets(line, sizeof(line), rsp_fp)) {
			if (strncmp(line, header, strlen(header) - 1) == 0 &&
			    (line[strlen(header) - 1] == '\n' || line[strlen(header) - 1] == '\r' ||
			     line[strlen(header) - 1] == '\0')) {
				/* push back the line content by re-positioning before it */
				fseek(rsp_fp, -(long) strlen(line), SEEK_CUR);
				break;
			}
		}
		int r = run_one_kat(sig, rsp_fp, single_count, true);
		if (r == 1) {
			passed = 1;
			printf("[PASS] count=%d\n", single_count);
		} else {
			failed = 1;
			rc = 1;
			printf("[FAIL] count=%d\n", single_count);
		}
	} else {
		for (int i = 0; i < total; i++) {
			int r = run_one_kat(sig, rsp_fp, i, false);
			if (r == 1) {
				passed++;
			} else {
				failed++;
				rc = 1;
				printf("[FAIL] count=%d\n", i);
				/* keep going to count more failures */
			}
		}
		printf("\nSummary: %d/%d passed", passed, total);
		if (failed) {
			printf(", %d FAILED", failed);
		}
		printf("\n");
	}

	fclose(rsp_fp);
	OQS_SIG_free(sig);
	OQS_destroy();
	return rc;
}
