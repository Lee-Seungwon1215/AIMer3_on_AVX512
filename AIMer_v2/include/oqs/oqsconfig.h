// SPDX-License-Identifier: MIT

/** 
 * Version of liboqs as a string. Equivalent to {MAJOR}.{MINOR}.{PATCH}{PRE_RELEASE} 
 */
#define OQS_VERSION_TEXT "0.14.0"
/** 
 * Version levels of liboqs as integers.
 */
#define OQS_VERSION_MAJOR 0
#define OQS_VERSION_MINOR 14
#define OQS_VERSION_PATCH 0
/** 
 * OQS_VERSION_PRE_RELEASE is defined if this is a pre-release version of liboqs, otherwise it is undefined.
 * Examples: "-dev" or "-rc1".
 */
/* #undef OQS_VERSION_PRE_RELEASE */

#define OQS_COMPILE_BUILD_TARGET "x86_64-Linux-5.15.0-139-generic"
#define OQS_DIST_BUILD 1
// #define OQS_DIST_X86_64_BUILD 1
/* #undef OQS_DIST_X86_BUILD */
/* #undef OQS_DIST_ARM64_V8_BUILD */
/* #undef OQS_DIST_ARM32_V7_BUILD */
/* #undef OQS_DIST_PPC64LE_BUILD */
/* #undef OQS_DEBUG_BUILD */
// #define ARCH_X86_64 1
/* #undef ARCH_ARM64v8 */
/* #undef ARCH_ARM32v7 */
/* #undef BUILD_SHARED_LIBS */
/* #undef OQS_BUILD_ONLY_LIB */
#define OQS_OPT_TARGET "generic"
/* #undef USE_COVERAGE */
/* #undef USE_SANITIZER */
/* #undef CMAKE_BUILD_TYPE */

// #define OQS_USE_OPENSSL 1
/* #undef OQS_USE_AES_OPENSSL */
// #define OQS_USE_SHA2_OPENSSL 1
/* #undef OQS_USE_SHA3_OPENSSL */
/* #undef OQS_DLOPEN_OPENSSL */
/* #undef OQS_OPENSSL_CRYPTO_SONAME */

/* #undef OQS_EMBEDDED_BUILD */

#define OQS_USE_PTHREADS 1

/* #undef OQS_USE_ADX_INSTRUCTIONS */
/* #undef OQS_USE_AES_INSTRUCTIONS */
/* #undef OQS_USE_AVX_INSTRUCTIONS */
/* #undef OQS_USE_AVX2_INSTRUCTIONS */
/* #undef OQS_USE_AVX512_INSTRUCTIONS */
/* #undef OQS_USE_BMI1_INSTRUCTIONS */
/* #undef OQS_USE_BMI2_INSTRUCTIONS */
/* #undef OQS_USE_PCLMULQDQ_INSTRUCTIONS */
/* #undef OQS_USE_VPCLMULQDQ_INSTRUCTIONS */
/* #undef OQS_USE_POPCNT_INSTRUCTIONS */
/* #undef OQS_USE_SSE_INSTRUCTIONS */
/* #undef OQS_USE_SSE2_INSTRUCTIONS */
/* #undef OQS_USE_SSE3_INSTRUCTIONS */

/* #undef OQS_USE_ARM_AES_INSTRUCTIONS */
/* #undef OQS_USE_ARM_SHA2_INSTRUCTIONS */
/* #undef OQS_USE_ARM_SHA3_INSTRUCTIONS */
/* #undef OQS_USE_ARM_NEON_INSTRUCTIONS */

/* #undef OQS_SPEED_USE_ARM_PMU */

/* #undef OQS_ENABLE_TEST_CONSTANT_TIME */

// #define OQS_ENABLE_SHA3_xkcp_low_avx2 1
// #define OQS_USE_SHA3_AVX512VL 1

// #define OQS_USE_CUPQC 0

#define OQS_ENABLE_SIG_AIMER 1
#define OQS_ENABLE_SIG_aimer_128f 1
// #define OQS_ENABLE_SIG_aimer_128f_avx2 1
#define OQS_ENABLE_SIG_aimer_128s 1
// #define OQS_ENABLE_SIG_aimer_128s_avx2 1
#define OQS_ENABLE_SIG_aimer_192f 1
// #define OQS_ENABLE_SIG_aimer_192f_avx2 1
#define OQS_ENABLE_SIG_aimer_192s 1
// #define OQS_ENABLE_SIG_aimer_192s_avx2 1
#define OQS_ENABLE_SIG_aimer_256f 1
// #define OQS_ENABLE_SIG_aimer_256f_avx2 1
#define OQS_ENABLE_SIG_aimer_256s 1
// #define OQS_ENABLE_SIG_aimer_256s_avx2 1

///// OQS_COPY_FROM_UPSTREAM_FRAGMENT_ADD_ALG_ENABLE_DEFINES_END

///// OQS_COPY_FROM_LIBJADE_FRAGMENT_ADD_ALG_ENABLE_DEFINES_START

#define OQS_LIBJADE_BUILD 0

/* #undef OQS_ENABLE_LIBJADE_KEM_KYBER */
/* #undef OQS_ENABLE_LIBJADE_KEM_kyber_512 */
/* #undef OQS_ENABLE_LIBJADE_KEM_kyber_512_avx2 */
/* #undef OQS_ENABLE_LIBJADE_KEM_kyber_768 */
/* #undef OQS_ENABLE_LIBJADE_KEM_kyber_768_avx2 */
///// OQS_COPY_FROM_LIBJADE_FRAGMENT_ADD_ALG_ENABLE_DEFINES_END

/* #undef OQS_ENABLE_SIG_STFL_XMSS */
/* #undef OQS_ENABLE_SIG_STFL_xmss_sha256_h10 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_sha256_h16 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_sha256_h20 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_shake128_h10 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_shake128_h16 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_shake128_h20 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_sha512_h10 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_sha512_h16 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_sha512_h20 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_shake256_h10 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_shake256_h16 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_shake256_h20 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_sha256_h10_192 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_sha256_h16_192 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_sha256_h20_192 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_shake256_h10_192 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_shake256_h16_192 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_shake256_h20_192 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_shake256_h10_256 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_shake256_h16_256 */
/* #undef OQS_ENABLE_SIG_STFL_xmss_shake256_h20_256 */

/* #undef OQS_ENABLE_SIG_STFL_xmssmt_sha256_h20_2 */
/* #undef OQS_ENABLE_SIG_STFL_xmssmt_sha256_h20_4 */
/* #undef OQS_ENABLE_SIG_STFL_xmssmt_sha256_h40_2 */
/* #undef OQS_ENABLE_SIG_STFL_xmssmt_sha256_h40_4 */
/* #undef OQS_ENABLE_SIG_STFL_xmssmt_sha256_h40_8 */
/* #undef OQS_ENABLE_SIG_STFL_xmssmt_sha256_h60_3 */
/* #undef OQS_ENABLE_SIG_STFL_xmssmt_sha256_h60_6 */
/* #undef OQS_ENABLE_SIG_STFL_xmssmt_sha256_h60_12 */
/* #undef OQS_ENABLE_SIG_STFL_xmssmt_shake128_h20_2 */
/* #undef OQS_ENABLE_SIG_STFL_xmssmt_shake128_h20_4 */
/* #undef OQS_ENABLE_SIG_STFL_xmssmt_shake128_h40_2 */
/* #undef OQS_ENABLE_SIG_STFL_xmssmt_shake128_h40_4 */
/* #undef OQS_ENABLE_SIG_STFL_xmssmt_shake128_h40_8 */
/* #undef OQS_ENABLE_SIG_STFL_xmssmt_shake128_h60_3 */
/* #undef OQS_ENABLE_SIG_STFL_xmssmt_shake128_h60_6 */
/* #undef OQS_ENABLE_SIG_STFL_xmssmt_shake128_h60_12 */


/* #undef OQS_ENABLE_SIG_STFL_LMS */
/* #undef OQS_ENABLE_SIG_STFL_lms_sha256_h5_w1 */
/* #undef OQS_ENABLE_SIG_STFL_lms_sha256_h5_w2 */
/* #undef OQS_ENABLE_SIG_STFL_lms_sha256_h5_w4 */
/* #undef OQS_ENABLE_SIG_STFL_lms_sha256_h5_w8 */
/* #undef OQS_ENABLE_SIG_STFL_lms_sha256_h10_w1 */
/* #undef OQS_ENABLE_SIG_STFL_lms_sha256_h10_w2 */
/* #undef OQS_ENABLE_SIG_STFL_lms_sha256_h10_w4 */
/* #undef OQS_ENABLE_SIG_STFL_lms_sha256_h10_w8 */
/* #undef OQS_ENABLE_SIG_STFL_lms_sha256_h15_w1 */
/* #undef OQS_ENABLE_SIG_STFL_lms_sha256_h15_w2 */
/* #undef OQS_ENABLE_SIG_STFL_lms_sha256_h15_w4 */
/* #undef OQS_ENABLE_SIG_STFL_lms_sha256_h5_w8_h5_w8 */
/* #undef OQS_ENABLE_SIG_STFL_lms_sha256_h10_w4_h5_w8 */

/* #undef OQS_HAZARDOUS_EXPERIMENTAL_ENABLE_SIG_STFL_KEY_SIG_GEN */
/* #undef OQS_ALLOW_STFL_KEY_AND_SIG_GEN */
/* #undef OQS_ALLOW_XMSS_KEY_AND_SIG_GEN */
/* #undef OQS_ALLOW_LMS_KEY_AND_SIG_GEN */
