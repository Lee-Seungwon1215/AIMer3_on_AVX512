// SPDX-License-Identifier: MIT

#include <oqs/rand.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void oqs_randombytes_system(uint8_t *random_array, size_t bytes_to_read) {
	FILE *random_source = fopen("/dev/urandom", "rb");
	if (random_source == NULL) {
		perror("OQS_randombytes");
		exit(EXIT_FAILURE);
	}

	const size_t bytes_read = fread(random_array, 1, bytes_to_read, random_source);
	if (bytes_read != bytes_to_read || ferror(random_source)) {
		fclose(random_source);
		fprintf(stderr, "OQS_randombytes: failed to read system randomness\n");
		exit(EXIT_FAILURE);
	}
	fclose(random_source);
}

static void (*randombytes_algorithm)(uint8_t *, size_t) = oqs_randombytes_system;

OQS_API OQS_STATUS OQS_randombytes_switch_algorithm(const char *algorithm) {
	if (algorithm != NULL && strcmp(algorithm, OQS_RAND_alg_system) == 0) {
		randombytes_algorithm = oqs_randombytes_system;
		return OQS_SUCCESS;
	}
	return OQS_ERROR;
}

OQS_API void OQS_randombytes_custom_algorithm(void (*algorithm_ptr)(uint8_t *, size_t)) {
	if (algorithm_ptr != NULL) {
		randombytes_algorithm = algorithm_ptr;
	}
}

OQS_API void OQS_randombytes(uint8_t *random_array, size_t bytes_to_read) {
	randombytes_algorithm(random_array, bytes_to_read);
}
