// SPDX-License-Identifier: MIT

#include <oqs/common.h>

#include <stdlib.h>
#include <string.h>

static unsigned int cpu_extensions[OQS_CPU_EXT_COUNT];
static int cpu_extensions_initialized;

static void detect_cpu_extensions(void) {
	if (cpu_extensions_initialized) {
		return;
	}
	cpu_extensions_initialized = 1;
	cpu_extensions[OQS_CPU_EXT_INIT] = 1;

#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
	__builtin_cpu_init();
	cpu_extensions[OQS_CPU_EXT_AES] = __builtin_cpu_supports("aes") != 0;
	cpu_extensions[OQS_CPU_EXT_AVX2] = __builtin_cpu_supports("avx2") != 0;
	cpu_extensions[OQS_CPU_EXT_AVX512] =
	    __builtin_cpu_supports("avx512f") != 0 &&
	    __builtin_cpu_supports("avx512bw") != 0 &&
	    __builtin_cpu_supports("avx512dq") != 0;
	cpu_extensions[OQS_CPU_EXT_AVX512VL] = __builtin_cpu_supports("avx512vl") != 0;
	cpu_extensions[OQS_CPU_EXT_BMI2] = __builtin_cpu_supports("bmi2") != 0;
	cpu_extensions[OQS_CPU_EXT_PCLMULQDQ] = __builtin_cpu_supports("pclmul") != 0;
	cpu_extensions[OQS_CPU_EXT_VPCLMULQDQ] = __builtin_cpu_supports("vpclmulqdq") != 0;
	cpu_extensions[OQS_CPU_EXT_POPCNT] = __builtin_cpu_supports("popcnt") != 0;
#endif
}

OQS_API int OQS_CPU_has_extension(OQS_CPU_EXT ext) {
	detect_cpu_extensions();
	if (ext <= OQS_CPU_EXT_INIT || ext >= OQS_CPU_EXT_COUNT) {
		return 0;
	}
	return (int)cpu_extensions[ext];
}

OQS_API void OQS_init(void) {
	detect_cpu_extensions();
}

OQS_API void OQS_destroy(void) {
}

OQS_API const char *OQS_version(void) {
	return "AIMer-v3-liboqs-port-0.1";
}

OQS_API void *OQS_MEM_malloc(size_t size) {
	return malloc(size);
}

OQS_API void *OQS_MEM_calloc(size_t num_elements, size_t element_size) {
	return calloc(num_elements, element_size);
}

OQS_API void OQS_MEM_cleanse(void *ptr, size_t len) {
	if (ptr == NULL) {
		return;
	}
	typedef void *(*memset_fn)(void *, int, size_t);
	static volatile memset_fn cleanse = memset;
	cleanse(ptr, 0, len);
}

OQS_API void OQS_MEM_secure_free(void *ptr, size_t len) {
	if (ptr != NULL) {
		OQS_MEM_cleanse(ptr, len);
		free(ptr);
	}
}

OQS_API void OQS_MEM_insecure_free(void *ptr) {
	free(ptr);
}
