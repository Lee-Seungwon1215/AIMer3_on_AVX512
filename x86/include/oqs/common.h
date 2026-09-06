// SPDX-License-Identifier: MIT

#ifndef OQS_COMMON_H
#define OQS_COMMON_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(_WIN32)
#define OQS_API __declspec(dllexport)
#else
#define OQS_API __attribute__((visibility("default")))
#endif

typedef enum {
	OQS_ERROR = -1,
	OQS_SUCCESS = 0,
} OQS_STATUS;

/* Kept compatible with the AIMer v2/liboqs integration. AVX512VL is tracked
 * separately because the AIM3 Keccak backend requires it. */
typedef enum {
	OQS_CPU_EXT_INIT,
	OQS_CPU_EXT_AES,
	OQS_CPU_EXT_AVX2,
	OQS_CPU_EXT_AVX512,
	OQS_CPU_EXT_AVX512VL,
	OQS_CPU_EXT_BMI2,
	OQS_CPU_EXT_PCLMULQDQ,
	OQS_CPU_EXT_VPCLMULQDQ,
	OQS_CPU_EXT_POPCNT,
	OQS_CPU_EXT_COUNT,
} OQS_CPU_EXT;

OQS_API int OQS_CPU_has_extension(OQS_CPU_EXT ext);
OQS_API void OQS_init(void);
OQS_API void OQS_destroy(void);
OQS_API const char *OQS_version(void);

OQS_API void *OQS_MEM_malloc(size_t size);
OQS_API void *OQS_MEM_calloc(size_t num_elements, size_t element_size);
OQS_API void OQS_MEM_cleanse(void *ptr, size_t len);
OQS_API void OQS_MEM_secure_free(void *ptr, size_t len);
OQS_API void OQS_MEM_insecure_free(void *ptr);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OQS_COMMON_H
