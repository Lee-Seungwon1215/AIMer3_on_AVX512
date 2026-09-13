// SPDX-License-Identifier: MIT
// Direct differential tests for AIM3 reference, AVX2, and AVX-512 GF kernels.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*gf_binary_fn)(uint64_t *, const uint64_t *, const uint64_t *);
typedef void (*gf_unary_fn)(uint64_t *, const uint64_t *);
typedef void (*gf_matrix_fn)(uint64_t *, const uint64_t *, const uint64_t *);

#define DECLARE_FIELD(namespace)                                               \
	extern void namespace##_gf_mul(uint64_t *, const uint64_t *,                \
	                               const uint64_t *);                            \
	extern void namespace##_gf_sqr(uint64_t *, const uint64_t *);                \
	extern void namespace##_gf_inv(uint64_t *, const uint64_t *);                \
	extern void namespace##_gf_mat_vec_mul(uint64_t *, const uint64_t *,         \
	                                       const uint64_t *);                    \
	extern void namespace##_gf_mat_vec_mul_add(uint64_t *, const uint64_t *,     \
	                                       const uint64_t *)

#define DECLARE_BATCH(namespace)                                               \
	extern void namespace##_gf_sqr_N(uint64_t *, const uint64_t *);              \
	extern void namespace##_gf_mul_add_N(uint64_t *, const uint64_t *,           \
	                                     const uint64_t *);                      \
	extern void namespace##_gf_mat_vec_mul_N(uint64_t *, const uint64_t *,       \
	                                         const uint64_t *);                  \
	extern void namespace##_gf_mat_vec_mul_add_N(                                \
	    uint64_t *, const uint64_t *, const uint64_t *)

#define DECLARE_SET(parameter)                                                 \
	DECLARE_FIELD(samsungsds_aimer_##parameter##_ref);                           \
	DECLARE_FIELD(samsungsds_aimer_##parameter##_avx2);                          \
	DECLARE_BATCH(samsungsds_aimer_##parameter##_avx2);                          \
	DECLARE_FIELD(samsungsds_aimer_##parameter##_opt);                           \
	DECLARE_BATCH(samsungsds_aimer_##parameter##_opt)

DECLARE_SET(128f);
DECLARE_SET(128s);
DECLARE_SET(192f);
DECLARE_SET(192s);
DECLARE_SET(256f);
DECLARE_SET(256s);

struct field_backend {
	const char *name;
	gf_binary_fn mul;
	gf_unary_fn sqr;
	gf_unary_fn inv;
	gf_matrix_fn matrix;
	gf_matrix_fn matrix_add;
	gf_unary_fn sqr_n;
	gf_binary_fn mul_add_n;
	gf_matrix_fn matrix_n;
	gf_matrix_fn matrix_add_n;
};

struct field_set {
	const char *name;
	size_t words;
	size_t parties;
	gf_binary_fn ref_mul;
	gf_unary_fn ref_sqr;
	gf_unary_fn ref_inv;
	gf_matrix_fn ref_matrix;
	struct field_backend backends[2];
};

#define FIELD_BACKEND(parameter, suffix, label)                               \
	{                                                                            \
		label,                                                                     \
		samsungsds_aimer_##parameter##_##suffix##_gf_mul,                         \
		samsungsds_aimer_##parameter##_##suffix##_gf_sqr,                         \
		samsungsds_aimer_##parameter##_##suffix##_gf_inv,                         \
		samsungsds_aimer_##parameter##_##suffix##_gf_mat_vec_mul,                 \
		samsungsds_aimer_##parameter##_##suffix##_gf_mat_vec_mul_add,             \
		samsungsds_aimer_##parameter##_##suffix##_gf_sqr_N,                       \
		samsungsds_aimer_##parameter##_##suffix##_gf_mul_add_N,                   \
		samsungsds_aimer_##parameter##_##suffix##_gf_mat_vec_mul_N,               \
		samsungsds_aimer_##parameter##_##suffix##_gf_mat_vec_mul_add_N            \
	}

#define FIELD_SET(parameter, word_count, party_count)                         \
	{                                                                            \
		#parameter, word_count, party_count,                                       \
		samsungsds_aimer_##parameter##_ref_gf_mul,                                \
		samsungsds_aimer_##parameter##_ref_gf_sqr,                                \
		samsungsds_aimer_##parameter##_ref_gf_inv,                                \
		samsungsds_aimer_##parameter##_ref_gf_mat_vec_mul,                        \
		{                                                                          \
			FIELD_BACKEND(parameter, avx2, "AVX2"),                                  \
			FIELD_BACKEND(parameter, opt, "AVX-512"),                                \
		}                                                                          \
	}

static uint64_t random_state = UINT64_C(0x8f3d9a27c4b165e0);

static uint64_t next_word(void) {
	uint64_t x = random_state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	random_state = x;
	return x;
}

static void fill_words(uint64_t *out, size_t count) {
	for (size_t i = 0; i < count; i++) {
		out[i] = next_word();
	}
}

static int check_equal(const struct field_set *set,
                       const struct field_backend *backend,
                       const char *operation, const uint64_t *expected,
                       const uint64_t *actual, size_t words) {
	if (memcmp(expected, actual, words * sizeof(uint64_t)) == 0) {
		return 0;
	}
	fprintf(stderr, "GF differential mismatch: %s %s %s\n",
	        set->name, backend->name, operation);
	return -1;
}

/* Every basis vector independently identifies a matrix row. Exercise all
 * uint64_t-aligned offsets modulo 32, zero/identity/random matrices, zero and
 * all-one inputs, random inputs, additive output, and exact input/output alias.
 * Canaries also catch a 192-bit output accidentally padded to 256 bits. */
static int test_matrix_edges(const struct field_set *set,
                             const struct field_backend *backend) {
	const size_t bits = 64 * set->words;
	const size_t matrix_words = bits * set->words;
	const size_t matrix_bytes = matrix_words * sizeof(uint64_t);
	uint64_t *storage = aligned_alloc(32, matrix_bytes + 32);
	if (storage == NULL) {
		return -1;
	}
	for (size_t offset = 0; offset < 4; offset++) {
		uint64_t *matrix = storage + offset;
		for (size_t kind = 0; kind < 3; kind++) {
			memset(matrix, 0, matrix_bytes);
			if (kind == 1) {
				for (size_t bit = 0; bit < bits; bit++) {
					matrix[bit * set->words + bit / 64] = UINT64_C(1) << (bit % 64);
				}
			} else if (kind == 2) {
				fill_words(matrix, matrix_words);
			}
			for (size_t round = 0; round < bits + 66; round++) {
				const uint64_t canary = UINT64_C(0xc397be16d05a482f);
				uint64_t input[4] = {0}, expected[4], initial[4], sum[4];
				_Alignas(32) uint64_t buffer[12];
				uint64_t *actual = buffer + 4 + offset;
				if (round < bits) {
					input[round / 64] = UINT64_C(1) << (round % 64);
					memcpy(expected, matrix + round * set->words,
					       set->words * sizeof(uint64_t));
				} else {
					if (round == bits + 1) {
						memset(input, 0xff, set->words * sizeof(uint64_t));
					} else if (round > bits + 1) {
						fill_words(input, set->words);
					}
					set->ref_matrix(expected, input, matrix);
				}
				actual[-1] = canary;
				actual[set->words] = canary;
				backend->matrix(actual, input, matrix);
				if (check_equal(set, backend, "matrix edge", expected, actual,
				                set->words) != 0) goto fail;
				memcpy(actual, input, set->words * sizeof(uint64_t));
				backend->matrix(actual, actual, matrix);
				if (check_equal(set, backend, "matrix in-place", expected, actual,
				                set->words) != 0) goto fail;
				fill_words(initial, set->words);
				for (size_t word = 0; word < set->words; word++) {
					sum[word] = initial[word] ^ expected[word];
				}
				memcpy(actual, initial, set->words * sizeof(uint64_t));
				backend->matrix_add(actual, input, matrix);
				if (check_equal(set, backend, "matrix_add edge", sum, actual,
				                set->words) != 0) goto fail;
				for (size_t word = 0; word < set->words; word++) {
					sum[word] = input[word] ^ expected[word];
				}
				memcpy(actual, input, set->words * sizeof(uint64_t));
				backend->matrix_add(actual, actual, matrix);
				if (check_equal(set, backend, "matrix_add in-place", sum, actual,
				                set->words) != 0) goto fail;
				if (actual[-1] != canary || actual[set->words] != canary) {
					fprintf(stderr, "matrix output bounds mismatch: %s %s\n",
					        set->name, backend->name);
					goto fail;
				}
			}
		}
	}
	free(storage);
	return 0;
fail:
	free(storage);
	return -1;
}

static int test_scalar(const struct field_set *set,
                       const struct field_backend *backend,
                       const uint64_t *matrix) {
	uint64_t a[4], b[4], ref[4], actual[4], identity[4];
	for (size_t round = 0; round < 64; round++) {
		fill_words(a, set->words);
		fill_words(b, set->words);
		if (round == 0) {
			memset(a, 0, set->words * sizeof(uint64_t));
			a[0] = 1;
		}
		set->ref_mul(ref, a, b);
		backend->mul(actual, a, b);
		if (check_equal(set, backend, "mul", ref, actual, set->words) != 0) {
			return -1;
		}
		set->ref_sqr(ref, a);
		backend->sqr(actual, a);
		if (check_equal(set, backend, "sqr", ref, actual, set->words) != 0) {
			return -1;
		}
		set->ref_matrix(ref, a, matrix);
		backend->matrix(actual, a, matrix);
		if (check_equal(set, backend, "matrix", ref, actual,
		                set->words) != 0) {
			return -1;
		}
		if (round < 16) {
			set->ref_inv(ref, a);
			backend->inv(actual, a);
			if (check_equal(set, backend, "inv", ref, actual,
			                set->words) != 0) {
				return -1;
			}
			set->ref_mul(identity, a, ref);
			if (identity[0] != 1) {
				return -1;
			}
			for (size_t i = 1; i < set->words; i++) {
				if (identity[i] != 0) {
					return -1;
				}
			}
		}
	}
	return 0;
}

static int test_batch(const struct field_set *set,
                      const struct field_backend *backend,
                      const uint64_t *matrix) {
	const size_t party_words = set->parties * set->words;
	uint64_t *input = malloc(party_words * sizeof(uint64_t));
	uint64_t *actual = malloc(party_words * sizeof(uint64_t));
	uint64_t *expected = malloc(party_words * sizeof(uint64_t));
	uint64_t *initial = malloc(party_words * sizeof(uint64_t));
	uint64_t multiplier[4];
	uint64_t product[4];
	if (input == NULL || actual == NULL || expected == NULL || initial == NULL) {
		free(input);
		free(actual);
		free(expected);
		free(initial);
		return -1;
	}

	fill_words(input, party_words);
	fill_words(multiplier, set->words);
	fill_words(initial, party_words);

	backend->sqr_n(actual, input);
	for (size_t party = 0; party < set->parties; party++) {
		set->ref_sqr(&expected[party * set->words],
		             &input[party * set->words]);
	}
	if (check_equal(set, backend, "sqr_N", expected, actual,
	                party_words) != 0) {
		goto fail;
	}

	memcpy(actual, initial, party_words * sizeof(uint64_t));
	memcpy(expected, initial, party_words * sizeof(uint64_t));
	backend->mul_add_n(actual, input, multiplier);
	for (size_t party = 0; party < set->parties; party++) {
		set->ref_mul(product, &input[party * set->words], multiplier);
		for (size_t word = 0; word < set->words; word++) {
			expected[party * set->words + word] ^= product[word];
		}
	}
	if (check_equal(set, backend, "mul_add_N", expected, actual,
	                party_words) != 0) {
		goto fail;
	}

	backend->matrix_n(actual, input, matrix);
	for (size_t party = 0; party < set->parties; party++) {
		set->ref_matrix(&expected[party * set->words],
		                &input[party * set->words], matrix);
	}
	if (check_equal(set, backend, "matrix_N", expected, actual,
	                party_words) != 0) {
		goto fail;
	}

	memcpy(actual, initial, party_words * sizeof(uint64_t));
	memcpy(expected, initial, party_words * sizeof(uint64_t));
	backend->matrix_add_n(actual, input, matrix);
	for (size_t party = 0; party < set->parties; party++) {
		set->ref_matrix(product, &input[party * set->words], matrix);
		for (size_t word = 0; word < set->words; word++) {
			expected[party * set->words + word] ^= product[word];
		}
	}
	if (check_equal(set, backend, "matrix_add_N", expected, actual,
	                party_words) != 0) {
		goto fail;
	}

	free(input);
	free(actual);
	free(expected);
	free(initial);
	return 0;

fail:
	free(input);
	free(actual);
	free(expected);
	free(initial);
	return -1;
}

int main(void) {
	static const struct field_set sets[] = {
		FIELD_SET(128f, 2, 16),
		FIELD_SET(128s, 2, 256),
		FIELD_SET(192f, 3, 16),
		FIELD_SET(192s, 3, 256),
		FIELD_SET(256f, 4, 16),
		FIELD_SET(256s, 4, 256),
	};

	for (size_t i = 0; i < sizeof(sets) / sizeof(sets[0]); i++) {
		const struct field_set *set = &sets[i];
		const size_t bits = 64 * set->words;
		uint64_t *matrix = malloc(bits * set->words * sizeof(uint64_t));
		if (matrix == NULL) {
			return 1;
		}
		fill_words(matrix, bits * set->words);
		for (size_t backend = 0; backend < 2; backend++) {
			if (test_matrix_edges(set, &set->backends[backend]) != 0 ||
			    test_scalar(set, &set->backends[backend], matrix) != 0 ||
			    test_batch(set, &set->backends[backend], matrix) != 0) {
				free(matrix);
				return 1;
			}
		}
		free(matrix);
	}

	puts("[PASS] GF reference/AVX2/AVX-512 differential test "
	     "(single/batch, matrix basis/alias/alignment, all six parameter sets)");
	return 0;
}
