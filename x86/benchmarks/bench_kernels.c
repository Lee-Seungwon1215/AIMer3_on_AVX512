// SPDX-License-Identifier: MIT
// Microbenchmarks for only the kernels optimized in the AIMer v3 port.

#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <oqs/oqs.h>

#include "common/fips202.h"
#include "oqs/avx2_shake.h"
#include "oqs/avx2_shake256.h"
#include "oqs/avx2_shake_x4.h"
#include "oqs/avx2_shake256_x4.h"
#include "oqs/avx512_shake.h"
#include "oqs/avx512_shake256.h"
#include "oqs/avx512_shake_x4.h"
#include "oqs/avx512_shake256_x4.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
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
#error "AIMer v3 SIMD kernel benchmarks require x86-64 RDTSC"
#endif

typedef void (*binary_fn)(void *, const void *, const void *);
typedef void (*unary_fn)(void *, const void *);
typedef void (*matrix_fn)(void *, const void *, const void *);
typedef void (*mpc_fn)(void *, const void *, const void *, const void *);
typedef size_t (*size_fn)(void);

struct kernel_api {
  const char *backend;
  binary_fn mul;
  unary_fn sqr;
  unary_fn inv;
  matrix_fn matrix;
  unary_fn sqr_batch;
  binary_fn mul_add_batch;
  matrix_fn matrix_batch;
  matrix_fn matrix_add_batch;
  mpc_fn mpc_batch;
  size_fn words;
  size_fn bits;
  size_fn parties;
  size_fn sboxes;
  size_fn tape_bytes;
  size_fn linear_bytes;
  size_fn check_bytes;
};

#define DECLARE_WRAPPER(namespace)                                           \
  extern void namespace##_bench_field_mul(void *, const void *, const void *); \
  extern void namespace##_bench_field_sqr(void *, const void *);                \
  extern void namespace##_bench_field_inv(void *, const void *);                \
  extern void namespace##_bench_gf_matrix(void *, const void *,              \
                                           const void *);                     \
  extern void namespace##_bench_gf_sqr_batch(void *, const void *);          \
  extern void namespace##_bench_gf_mul_add_batch(void *, const void *,       \
                                                  const void *);              \
  extern void namespace##_bench_gf_matrix_batch(void *, const void *,        \
                                                 const void *);               \
  extern void namespace##_bench_gf_matrix_add_batch(void *, const void *,    \
                                                     const void *);           \
  extern void namespace##_bench_mpc_batch(void *, const void *, const void *, \
                                           const void *);                     \
  extern size_t namespace##_bench_words(void);                               \
  extern size_t namespace##_bench_bits(void);                                \
  extern size_t namespace##_bench_parties(void);                             \
  extern size_t namespace##_bench_sboxes(void);                              \
  extern size_t namespace##_bench_tape_bytes(void);                          \
  extern size_t namespace##_bench_linear_bytes(void);                        \
  extern size_t namespace##_bench_check_bytes(void)

#define DECLARE_SET(parameter)                                               \
  DECLARE_WRAPPER(samsungsds_aimer_##parameter##_ref);                       \
  DECLARE_WRAPPER(samsungsds_aimer_##parameter##_avx2);                      \
  DECLARE_WRAPPER(samsungsds_aimer_##parameter##_opt)

DECLARE_SET(128f);
DECLARE_SET(128s);
DECLARE_SET(192f);
DECLARE_SET(192s);
DECLARE_SET(256f);
DECLARE_SET(256s);

#define API(namespace, label)                                                \
  {                                                                          \
    label, namespace##_bench_field_mul, namespace##_bench_field_sqr,               \
        namespace##_bench_field_inv, namespace##_bench_gf_matrix,               \
        namespace##_bench_gf_sqr_batch,                                      \
        namespace##_bench_gf_mul_add_batch,                                  \
        namespace##_bench_gf_matrix_batch,                                   \
        namespace##_bench_gf_matrix_add_batch,                               \
        namespace##_bench_mpc_batch, namespace##_bench_words,                \
        namespace##_bench_bits, namespace##_bench_parties,                   \
        namespace##_bench_sboxes, namespace##_bench_tape_bytes,              \
        namespace##_bench_linear_bytes, namespace##_bench_check_bytes        \
  }

#define SET(parameter, security)                                             \
  {                                                                          \
    "AIMER-v3-" #parameter, security,                                        \
        {API(samsungsds_aimer_##parameter##_ref, "ref"),                     \
         API(samsungsds_aimer_##parameter##_avx2, "avx2"),                   \
         API(samsungsds_aimer_##parameter##_opt, "avx512")}                 \
  }

struct parameter_set {
  const char *variant;
  size_t security_bits;
  struct kernel_api api[3];
};

static const struct parameter_set parameter_sets[] = {
    SET(128f, 128), SET(128s, 128), SET(192f, 192),
    SET(192s, 192), SET(256f, 256), SET(256s, 256),
};

static volatile uint64_t output_sink;
static uint64_t random_state = UINT64_C(0x82d473a91fbc506e);

static uint64_t next_word(void) {
  uint64_t value = random_state;
  value ^= value << 13;
  value ^= value >> 7;
  value ^= value << 17;
  random_state = value;
  return value;
}

static void fill_random(void *buffer, size_t bytes) {
  uint8_t *out = buffer;
  size_t offset = 0;
  while (offset < bytes) {
    const uint64_t value = next_word();
    const size_t take = bytes - offset < sizeof(value) ? bytes - offset
                                                       : sizeof(value);
    memcpy(out + offset, &value, take);
    offset += take;
  }
}

static void *allocate_aligned(size_t bytes) {
  const size_t rounded = (bytes + 63U) & ~(size_t)63U;
  return aligned_alloc(64, rounded == 0 ? 64 : rounded);
}

static int compare_double(const void *left, const void *right) {
  const double x = *(const double *)left;
  const double y = *(const double *)right;
  return (x > y) - (x < y);
}

static void emit_csv(double *samples, size_t count, const char *backend,
                     const char *variant, const char *kernel,
                     size_t work_items, size_t inner) {
  const char *raw_path = getenv("AIMER_BENCH_RAW");
  if (raw_path != NULL) {
    FILE *raw = fopen(raw_path, "a");
    if (raw == NULL) { perror(raw_path); exit(1); }
    for (size_t i = 0; i < count; i++) {
      fprintf(raw, "%s,%s,%s,%zu,%.9f\n", backend, variant, kernel,
              i, samples[i]);
    }
    if (fclose(raw) != 0) { perror(raw_path); exit(1); }
  }
  qsort(samples, count, sizeof(*samples), compare_double);
  const double minimum = samples[0];
  const double median = samples[count / 2];
  const double maximum = samples[count - 1];
  double sum = 0.0;
  for (size_t i = 0; i < count; i++) {
    sum += samples[i];
  }
  const double mean = sum / (double)count;
  double variance = 0.0;
  for (size_t i = 0; i < count; i++) {
    const double delta = samples[i] - mean;
    variance += delta * delta;
  }
  const double deviation =
      count > 1 ? sqrt(variance / (double)(count - 1)) : 0.0;
  const double cv = mean > 0.0 ? deviation / mean * 100.0 : 0.0;
  printf("%s,%s,%s,%zu,%zu,%zu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
         backend, variant, kernel, work_items, count, inner, minimum, median,
         maximum, mean, deviation, cv, median / (double)work_items);
}

typedef void (*runner_fn)(void *);

static int measure(runner_fn runner, void *context, const void *output,
                   size_t output_bytes, size_t samples, size_t inner,
                   size_t warmup, double *cycles) {
  for (size_t i = 0; i < warmup; i++) {
    for (size_t repetition = 0; repetition < inner; repetition++) {
      runner(context);
    }
  }
  for (size_t sample = 0; sample < samples; sample++) {
    const uint64_t start = cycles_begin();
    for (size_t repetition = 0; repetition < inner; repetition++) {
      runner(context);
    }
    const uint64_t end = cycles_end();
    cycles[sample] = (double)(end - start) / (double)inner;
    const uint8_t *bytes = output;
    if (output_bytes != 0) {
      output_sink ^= bytes[(sample * 131U) % output_bytes];
    }
  }
  return 0;
}

enum gf_operation {
  GF_MUL,
  GF_SQR,
  GF_INV,
  GF_MATRIX,
  GF_SQR_BATCH,
  GF_MUL_ADD_BATCH,
  GF_MATRIX_BATCH,
  GF_MATRIX_ADD_BATCH,
};

struct gf_context {
  const struct kernel_api *api;
  enum gf_operation operation;
  void *out;
  const void *left;
  const void *right;
  const void *matrix;
};

static void run_gf(void *raw) {
  struct gf_context *context = raw;
  switch (context->operation) {
  case GF_MUL:
    context->api->mul(context->out, context->left, context->right);
    break;
  case GF_SQR:
    context->api->sqr(context->out, context->left);
    break;
  case GF_INV:
    context->api->inv(context->out, context->left);
    break;
  case GF_MATRIX:
    context->api->matrix(context->out, context->left, context->matrix);
    break;
  case GF_SQR_BATCH:
    context->api->sqr_batch(context->out, context->left);
    break;
  case GF_MUL_ADD_BATCH:
    context->api->mul_add_batch(context->out, context->left, context->right);
    break;
  case GF_MATRIX_BATCH:
    context->api->matrix_batch(context->out, context->left, context->matrix);
    break;
  case GF_MATRIX_ADD_BATCH:
    context->api->matrix_add_batch(context->out, context->left,
                                   context->matrix);
    break;
  }
}

struct mpc_context {
  const struct kernel_api *api;
  void *checks;
  const void *linear;
  const void *tapes;
  const void *ciphertext;
};

static void run_mpc(void *raw) {
  struct mpc_context *context = raw;
  context->api->mpc_batch(context->checks, context->linear, context->tapes,
                          context->ciphertext);
}

enum backend_id { BACKEND_REF, BACKEND_AVX2, BACKEND_AVX512 };

struct shake_context {
  enum backend_id backend;
  size_t security_bits;
  size_t lanes;
  const uint8_t *input[4];
  uint8_t *output[4];
  size_t input_len;
  size_t output_len;
};

static void shake_ref_lane(size_t bits, uint8_t *output, size_t output_len,
                           const uint8_t *input, size_t input_len) {
  if (bits == 128) {
    shake128incctx state;
    shake128_inc_init(&state);
    shake128_inc_absorb(&state, input, input_len);
    shake128_inc_finalize(&state);
    shake128_inc_squeeze(output, output_len, &state);
    shake128_inc_ctx_release(&state);
  } else {
    shake256incctx state;
    shake256_inc_init(&state);
    shake256_inc_absorb(&state, input, input_len);
    shake256_inc_finalize(&state);
    shake256_inc_squeeze(output, output_len, &state);
    shake256_inc_ctx_release(&state);
  }
}

static void run_shake_ref(struct shake_context *context) {
  for (size_t lane = 0; lane < context->lanes; lane++) {
    shake_ref_lane(context->security_bits, context->output[lane],
                   context->output_len, context->input[lane],
                   context->input_len);
  }
}

static void run_shake_avx2(struct shake_context *context) {
  if (context->lanes == 1 && context->security_bits == 128) {
    aimer_v3_avx2_shake128incctx state;
    aimer_v3_avx2_shake128_inc_init(&state);
    aimer_v3_avx2_shake128_inc_absorb(&state, context->input[0],
                                      context->input_len);
    aimer_v3_avx2_shake128_inc_finalize(&state);
    aimer_v3_avx2_shake128_inc_squeeze(context->output[0],
                                       context->output_len, &state);
    aimer_v3_avx2_shake128_inc_ctx_release(&state);
  } else if (context->lanes == 1) {
    aimer_v3_avx2_shake256incctx state;
    aimer_v3_avx2_shake256_inc_init(&state);
    aimer_v3_avx2_shake256_inc_absorb(&state, context->input[0],
                                      context->input_len);
    aimer_v3_avx2_shake256_inc_finalize(&state);
    aimer_v3_avx2_shake256_inc_squeeze(context->output[0],
                                       context->output_len, &state);
    aimer_v3_avx2_shake256_inc_ctx_release(&state);
  } else if (context->security_bits == 128) {
    aimer_v3_avx2_shake128x4incctx state;
    aimer_v3_avx2_shake128_x4_inc_init(&state);
    aimer_v3_avx2_shake128_x4_inc_absorb(
        &state, context->input[0], context->input[1], context->input[2],
        context->input[3], context->input_len);
    aimer_v3_avx2_shake128_x4_inc_finalize(&state);
    aimer_v3_avx2_shake128_x4_inc_squeeze(
        context->output[0], context->output[1], context->output[2],
        context->output[3], context->output_len, &state);
    aimer_v3_avx2_shake128_x4_inc_ctx_release(&state);
  } else {
    aimer_v3_avx2_shake256x4incctx state;
    aimer_v3_avx2_shake256_x4_inc_init(&state);
    aimer_v3_avx2_shake256_x4_inc_absorb(
        &state, context->input[0], context->input[1], context->input[2],
        context->input[3], context->input_len);
    aimer_v3_avx2_shake256_x4_inc_finalize(&state);
    aimer_v3_avx2_shake256_x4_inc_squeeze(
        context->output[0], context->output[1], context->output[2],
        context->output[3], context->output_len, &state);
    aimer_v3_avx2_shake256_x4_inc_ctx_release(&state);
  }
}

static void run_shake_avx512(struct shake_context *context) {
  if (context->lanes == 1 && context->security_bits == 128) {
    aimer_v3_avx512_shake128incctx state;
    aimer_v3_avx512_shake128_inc_init(&state);
    aimer_v3_avx512_shake128_inc_absorb(&state, context->input[0],
                                        context->input_len);
    aimer_v3_avx512_shake128_inc_finalize(&state);
    aimer_v3_avx512_shake128_inc_squeeze(context->output[0],
                                         context->output_len, &state);
    aimer_v3_avx512_shake128_inc_ctx_release(&state);
  } else if (context->lanes == 1) {
    aimer_v3_avx512_shake256incctx state;
    aimer_v3_avx512_shake256_inc_init(&state);
    aimer_v3_avx512_shake256_inc_absorb(&state, context->input[0],
                                        context->input_len);
    aimer_v3_avx512_shake256_inc_finalize(&state);
    aimer_v3_avx512_shake256_inc_squeeze(context->output[0],
                                         context->output_len, &state);
    aimer_v3_avx512_shake256_inc_ctx_release(&state);
  } else if (context->security_bits == 128) {
    aimer_v3_avx512_shake128x4incctx state;
    aimer_v3_avx512_shake128_x4_inc_init(&state);
    aimer_v3_avx512_shake128_x4_inc_absorb(
        &state, context->input[0], context->input[1], context->input[2],
        context->input[3], context->input_len);
    aimer_v3_avx512_shake128_x4_inc_finalize(&state);
    aimer_v3_avx512_shake128_x4_inc_squeeze(
        context->output[0], context->output[1], context->output[2],
        context->output[3], context->output_len, &state);
    aimer_v3_avx512_shake128_x4_inc_ctx_release(&state);
  } else {
    aimer_v3_avx512_shake256x4incctx state;
    aimer_v3_avx512_shake256_x4_inc_init(&state);
    aimer_v3_avx512_shake256_x4_inc_absorb(
        &state, context->input[0], context->input[1], context->input[2],
        context->input[3], context->input_len);
    aimer_v3_avx512_shake256_x4_inc_finalize(&state);
    aimer_v3_avx512_shake256_x4_inc_squeeze(
        context->output[0], context->output[1], context->output[2],
        context->output[3], context->output_len, &state);
    aimer_v3_avx512_shake256_x4_inc_ctx_release(&state);
  }
}

static void run_shake(void *raw) {
  struct shake_context *context = raw;
  if (context->backend == BACKEND_REF) {
    run_shake_ref(context);
  } else if (context->backend == BACKEND_AVX2) {
    run_shake_avx2(context);
  } else {
    run_shake_avx512(context);
  }
}

static const struct parameter_set *find_set(const char *name) {
  for (size_t i = 0; i < sizeof(parameter_sets) / sizeof(parameter_sets[0]);
       i++) {
    if (strcmp(name, parameter_sets[i].variant) == 0) {
      return &parameter_sets[i];
    }
  }
  return NULL;
}

static int find_backend(const char *name, enum backend_id *backend) {
  if (strcmp(name, "ref") == 0) {
    *backend = BACKEND_REF;
  } else if (strcmp(name, "avx2") == 0) {
    *backend = BACKEND_AVX2;
  } else if (strcmp(name, "avx512") == 0) {
    *backend = BACKEND_AVX512;
  } else {
    return -1;
  }
  return 0;
}

static int cpu_supports(enum backend_id backend) {
  if (backend == BACKEND_REF) {
    return 1;
  }
  const int avx2 = OQS_CPU_has_extension(OQS_CPU_EXT_AES) &&
                   OQS_CPU_has_extension(OQS_CPU_EXT_AVX2) &&
                   OQS_CPU_has_extension(OQS_CPU_EXT_BMI2) &&
                   OQS_CPU_has_extension(OQS_CPU_EXT_PCLMULQDQ) &&
                   OQS_CPU_has_extension(OQS_CPU_EXT_POPCNT);
  return backend == BACKEND_AVX2
             ? avx2
             : avx2 && OQS_CPU_has_extension(OQS_CPU_EXT_AVX512) &&
                   OQS_CPU_has_extension(OQS_CPU_EXT_AVX512VL) &&
                   OQS_CPU_has_extension(OQS_CPU_EXT_VPCLMULQDQ);
}

static int parse_positive(const char *text, size_t *value) {
  char *end = NULL;
  const unsigned long long parsed = strtoull(text, &end, 10);
  if (text[0] == '\0' || end == NULL || *end != '\0' || parsed == 0 ||
      parsed > SIZE_MAX) {
    return -1;
  }
  *value = (size_t)parsed;
  return 0;
}

static int check_metadata(const struct kernel_api *reference,
                          const struct kernel_api *selected) {
  return reference->words() == selected->words() &&
         reference->bits() == selected->bits() &&
         reference->parties() == selected->parties() &&
         reference->sboxes() == selected->sboxes() &&
         reference->tape_bytes() == selected->tape_bytes() &&
         reference->linear_bytes() == selected->linear_bytes() &&
         reference->check_bytes() == selected->check_bytes();
}

static int benchmark_gf(const struct parameter_set *set,
                        const struct kernel_api *api, size_t samples,
                        size_t inner, size_t warmup, const char *filter,
                        double *cycles) {
  const struct kernel_api *reference = &set->api[BACKEND_REF];
  const size_t words = api->words();
  const size_t parties = api->parties();
  const size_t scalar_bytes = words * sizeof(uint64_t);
  const size_t batch_bytes = parties * scalar_bytes;
  const size_t matrix_bytes = api->bits() * scalar_bytes;
  void *left = allocate_aligned(batch_bytes);
  void *right = allocate_aligned(scalar_bytes);
  void *matrix = allocate_aligned(matrix_bytes);
  void *out = allocate_aligned(batch_bytes);
  void *expected = allocate_aligned(batch_bytes);
  if (left == NULL || right == NULL || matrix == NULL || out == NULL ||
      expected == NULL) {
    free(left); free(right); free(matrix); free(out); free(expected);
    return -1;
  }
  fill_random(left, batch_bytes);
  fill_random(right, scalar_bytes);
  fill_random(matrix, matrix_bytes);

  static const struct {
    const char *name;
    enum gf_operation operation;
    int batch;
  } kernels[] = {
      {"gf_mul", GF_MUL, 0},
      {"gf_sqr", GF_SQR, 0},
      {"gf_inv", GF_INV, 0},
      {"gf_mat_vec", GF_MATRIX, 0},
      {"gf_sqr_batch", GF_SQR_BATCH, 1},
      {"gf_mul_add_batch", GF_MUL_ADD_BATCH, 1},
      {"gf_mat_vec_batch", GF_MATRIX_BATCH, 1},
      {"gf_mat_vec_add_batch", GF_MATRIX_ADD_BATCH, 1},
  };

  for (size_t kernel = 0; kernel < sizeof(kernels) / sizeof(kernels[0]);
       kernel++) {
    if (filter != NULL && strcmp(filter, kernels[kernel].name) != 0) {
      continue;
    }
    const size_t bytes = kernels[kernel].batch ? batch_bytes : scalar_bytes;
    memset(out, 0, batch_bytes);
    memset(expected, 0, batch_bytes);
    struct gf_context ref_context = {reference, kernels[kernel].operation,
                                     expected, left, right, matrix};
    struct gf_context context = {api, kernels[kernel].operation,
                                 out, left, right, matrix};
    run_gf(&ref_context);
    run_gf(&context);
    if (memcmp(expected, out, bytes) != 0) {
      fprintf(stderr, "kernel mismatch: %s %s %s\n", set->variant,
              api->backend, kernels[kernel].name);
      free(left); free(right); free(matrix); free(out); free(expected);
      return -1;
    }
    memset(out, 0, batch_bytes);
    measure(run_gf, &context, out, bytes, samples, inner, warmup, cycles);
    emit_csv(cycles, samples, api->backend, set->variant,
             kernels[kernel].name, kernels[kernel].batch ? parties : 1,
             inner);
  }

  free(left); free(right); free(matrix); free(out); free(expected);
  return 0;
}

static int benchmark_mpc(const struct parameter_set *set,
                         const struct kernel_api *api, size_t samples,
                         size_t inner, size_t warmup, const char *filter,
                         double *cycles) {
  if (filter != NULL && strcmp(filter, "aim3_mpc_batch") != 0) {
    return 0;
  }
  const struct kernel_api *reference = &set->api[BACKEND_REF];
  const size_t parties = api->parties();
  const size_t checks_bytes = parties * api->check_bytes();
  const size_t tapes_bytes = parties * api->tape_bytes();
  const size_t field_bytes = api->words() * sizeof(uint64_t);
  void *checks = allocate_aligned(checks_bytes);
  void *expected = allocate_aligned(checks_bytes);
  void *linear = allocate_aligned(api->linear_bytes());
  void *tapes = allocate_aligned(tapes_bytes);
  void *ciphertext = allocate_aligned(field_bytes);
  if (checks == NULL || expected == NULL || linear == NULL || tapes == NULL ||
      ciphertext == NULL) {
    free(checks); free(expected); free(linear); free(tapes); free(ciphertext);
    return -1;
  }
  fill_random(linear, api->linear_bytes());
  fill_random(tapes, tapes_bytes);
  fill_random(ciphertext, field_bytes);
  memset(checks, 0, checks_bytes);
  memset(expected, 0, checks_bytes);
  struct mpc_context ref_context = {reference, expected, linear, tapes,
                                    ciphertext};
  struct mpc_context context = {api, checks, linear, tapes, ciphertext};
  run_mpc(&ref_context);
  run_mpc(&context);
  if (memcmp(expected, checks, checks_bytes) != 0) {
    fprintf(stderr, "kernel mismatch: %s %s aim3_mpc_batch\n", set->variant,
            api->backend);
    free(checks); free(expected); free(linear); free(tapes); free(ciphertext);
    return -1;
  }
  measure(run_mpc, &context, checks, checks_bytes, samples, inner, warmup,
          cycles);
  emit_csv(cycles, samples, api->backend, set->variant, "aim3_mpc_batch",
           parties, inner);
  free(checks); free(expected); free(linear); free(tapes); free(ciphertext);
  return 0;
}

static int benchmark_shake(const struct parameter_set *set,
                           enum backend_id backend, size_t samples,
                           size_t inner, size_t warmup, const char *filter,
                           double *cycles) {
  const size_t security_bytes = set->security_bits / 8;
  const size_t input_len = 2 * security_bytes + 2;
  const size_t sboxes = set->api[backend].sboxes();
  const size_t tape_bytes = (3 + 2 * sboxes) * security_bytes;
  const size_t output_len = 2 * security_bytes + tape_bytes;
  uint8_t *inputs[4] = {NULL, NULL, NULL, NULL};
  uint8_t *outputs[4] = {NULL, NULL, NULL, NULL};
  uint8_t *expected[4] = {NULL, NULL, NULL, NULL};
  for (size_t lane = 0; lane < 4; lane++) {
    inputs[lane] = malloc(input_len);
    outputs[lane] = malloc(output_len);
    expected[lane] = malloc(output_len);
    if (inputs[lane] == NULL || outputs[lane] == NULL ||
        expected[lane] == NULL) {
      for (size_t i = 0; i < 4; i++) {
        free(inputs[i]); free(outputs[i]); free(expected[i]);
      }
      return -1;
    }
    fill_random(inputs[lane], input_len);
  }

  for (size_t lanes = 1; lanes <= 4; lanes += 3) {
    const char *kernel_name =
        lanes == 1 ? "shake_commit_tape_x1" : "shake_commit_tape_x4";
    if (filter != NULL && strcmp(filter, kernel_name) != 0) {
      continue;
    }
    struct shake_context reference = {BACKEND_REF, set->security_bits, lanes,
                                      {inputs[0], inputs[1], inputs[2], inputs[3]},
                                      {expected[0], expected[1], expected[2],
                                       expected[3]},
                                      input_len, output_len};
    struct shake_context context = {backend, set->security_bits, lanes,
                                    {inputs[0], inputs[1], inputs[2], inputs[3]},
                                    {outputs[0], outputs[1], outputs[2],
                                     outputs[3]},
                                    input_len, output_len};
    run_shake(&reference);
    run_shake(&context);
    for (size_t lane = 0; lane < lanes; lane++) {
      if (memcmp(expected[lane], outputs[lane], output_len) != 0) {
        fprintf(stderr, "kernel mismatch: %s %s shake_x%zu\n", set->variant,
                set->api[backend].backend, lanes);
        for (size_t i = 0; i < 4; i++) {
          free(inputs[i]); free(outputs[i]); free(expected[i]);
        }
        return -1;
      }
    }
    measure(run_shake, &context, outputs[0], output_len, samples,
            inner, warmup, cycles);
    emit_csv(cycles, samples, set->api[backend].backend, set->variant,
             kernel_name, lanes, inner);
  }

  for (size_t lane = 0; lane < 4; lane++) {
    free(inputs[lane]); free(outputs[lane]); free(expected[lane]);
  }
  return 0;
}

static void usage(const char *program) {
  fprintf(stderr,
          "usage: %s <ref|avx2|avx512> <AIMER-v3-variant> <samples> "
          "<inner> [warmup [kernel]]\n",
          program);
}

static int valid_kernel_name(const char *name) {
  static const char *const names[] = {
      "gf_mul", "gf_sqr", "gf_inv", "gf_mat_vec", "gf_sqr_batch",
      "gf_mul_add_batch", "gf_mat_vec_batch", "gf_mat_vec_add_batch",
      "aim3_mpc_batch", "shake_commit_tape_x1", "shake_commit_tape_x4",
  };
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    if (strcmp(name, names[i]) == 0) {
      return 1;
    }
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 5 || argc > 7) {
    usage(argv[0]);
    return 2;
  }
  enum backend_id backend;
  const struct parameter_set *set = find_set(argv[2]);
  const char *filter = argc == 7 ? argv[6] : NULL;
  size_t samples, inner, warmup = 5;
  if (find_backend(argv[1], &backend) != 0 || set == NULL ||
      parse_positive(argv[3], &samples) != 0 ||
      parse_positive(argv[4], &inner) != 0 ||
      (argc >= 6 && parse_positive(argv[5], &warmup) != 0) ||
      (filter != NULL && !valid_kernel_name(filter))) {
    usage(argv[0]);
    return 2;
  }
  OQS_init();
  if (!cpu_supports(backend)) {
    fprintf(stderr, "backend %s is not supported by this CPU\n", argv[1]);
    OQS_destroy();
    return 2;
  }
  const struct kernel_api *api = &set->api[backend];
  if (!check_metadata(&set->api[BACKEND_REF], api)) {
    fprintf(stderr, "kernel metadata mismatch: %s %s\n", set->variant,
            api->backend);
    OQS_destroy();
    return 1;
  }
  double *cycles = malloc(samples * sizeof(*cycles));
  if (cycles == NULL) {
    OQS_destroy();
    return 1;
  }
  const int result = benchmark_gf(set, api, samples, inner, warmup, filter, cycles) != 0 ||
                     benchmark_mpc(set, api, samples, inner, warmup, filter, cycles) != 0 ||
                     benchmark_shake(set, backend, samples, inner, warmup,
                                     filter, cycles) != 0;
  free(cycles);
  OQS_destroy();
  return result ? 1 : 0;
}
