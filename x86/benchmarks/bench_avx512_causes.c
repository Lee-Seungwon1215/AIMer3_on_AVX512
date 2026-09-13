// SPDX-License-Identifier: MIT
// Diagnostic decomposition for the AIMer v3 reference and AVX-512 backends.

#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <oqs/oqs.h>

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
#error "The AVX-512 cause benchmark requires x86-64 RDTSC"
#endif

typedef void (*binary_fn)(void *, const void *, const void *);
typedef void (*unary_fn)(void *, const void *);
typedef void (*matrix_fn)(void *, const void *, const void *);
typedef void (*mpc_fn)(void *, const void *, const void *, const void *);
typedef size_t (*size_fn)(void);

struct cause_api {
  const char *backend;
  binary_fn mul;
  binary_fn mul_add;
  unary_fn sqr;
  unary_fn inv;
  matrix_fn matrix;
  unary_fn generate_linear;
  mpc_fn mpc_affine_setup;
  mpc_fn mpc_frobenius;
  mpc_fn mpc_batch;
  size_fn words;
  size_fn bits;
  size_fn parties;
  size_fn sboxes;
  size_fn repetitions;
  size_fn exponent_sum;
  size_fn tape_bytes;
  size_fn linear_bytes;
  size_fn check_bytes;
};

#define DECLARE_WRAPPER(namespace)                                           \
  extern void namespace##_bench_field_mul(void *, const void *,             \
                                           const void *);                    \
  extern void namespace##_bench_field_mul_add(void *, const void *,         \
                                               const void *);                \
  extern void namespace##_bench_field_sqr(void *, const void *);            \
  extern void namespace##_bench_field_inv(void *, const void *);            \
  extern void namespace##_bench_gf_matrix(void *, const void *,             \
                                           const void *);                    \
  extern void namespace##_bench_generate_linear(void *, const void *);      \
  extern void namespace##_bench_mpc_affine_setup(                           \
      void *, const void *, const void *, const void *);                     \
  extern void namespace##_bench_mpc_frobenius(                              \
      void *, const void *, const void *, const void *);                     \
  extern void namespace##_bench_mpc_batch(                                  \
      void *, const void *, const void *, const void *);                     \
  extern size_t namespace##_bench_words(void);                              \
  extern size_t namespace##_bench_bits(void);                               \
  extern size_t namespace##_bench_parties(void);                            \
  extern size_t namespace##_bench_sboxes(void);                             \
  extern size_t namespace##_bench_repetitions(void);                        \
  extern size_t namespace##_bench_exponent_sum(void);                       \
  extern size_t namespace##_bench_tape_bytes(void);                         \
  extern size_t namespace##_bench_linear_bytes(void);                       \
  extern size_t namespace##_bench_check_bytes(void)

#define DECLARE_SET(parameter)                                              \
  DECLARE_WRAPPER(samsungsds_aimer_##parameter##_ref);                      \
  DECLARE_WRAPPER(samsungsds_aimer_##parameter##_opt)

DECLARE_SET(128f);
DECLARE_SET(128s);
DECLARE_SET(192f);
DECLARE_SET(192s);
DECLARE_SET(256f);
DECLARE_SET(256s);

#define API(namespace, label)                                               \
  {                                                                         \
    label, namespace##_bench_field_mul, namespace##_bench_field_mul_add,    \
        namespace##_bench_field_sqr, namespace##_bench_field_inv,           \
        namespace##_bench_gf_matrix, namespace##_bench_generate_linear,     \
        namespace##_bench_mpc_affine_setup,                                 \
        namespace##_bench_mpc_frobenius, namespace##_bench_mpc_batch,       \
        namespace##_bench_words, namespace##_bench_bits,                    \
        namespace##_bench_parties, namespace##_bench_sboxes,                \
        namespace##_bench_repetitions, namespace##_bench_exponent_sum,      \
        namespace##_bench_tape_bytes, namespace##_bench_linear_bytes,       \
        namespace##_bench_check_bytes                                       \
  }

#define SET(parameter)                                                      \
  {                                                                         \
    "AIMER-v3-" #parameter,                                                \
        {API(samsungsds_aimer_##parameter##_ref, "ref"),                   \
         API(samsungsds_aimer_##parameter##_opt, "avx512")}               \
  }

struct parameter_set {
  const char *variant;
  struct cause_api api[2];
};

static const struct parameter_set parameter_sets[] = {
    SET(128f), SET(128s), SET(192f), SET(192s), SET(256f), SET(256s),
};

static volatile uint64_t output_sink;
static uint64_t random_state = UINT64_C(0xe6a90f471d3bc825);

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
  for (size_t offset = 0; offset < bytes;) {
    const uint64_t value = next_word();
    const size_t take = bytes - offset < sizeof(value)
                            ? bytes - offset
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

static void emit_csv(double *values, size_t count, const char *backend,
                     const char *variant, const char *kernel,
                     size_t work_items, size_t inner) {
  qsort(values, count, sizeof(*values), compare_double);
  double sum = 0.0;
  for (size_t i = 0; i < count; i++) {
    sum += values[i];
  }
  const double mean = sum / (double)count;
  double variance = 0.0;
  for (size_t i = 0; i < count; i++) {
    const double delta = values[i] - mean;
    variance += delta * delta;
  }
  const double deviation =
      count > 1 ? sqrt(variance / (double)(count - 1)) : 0.0;
  const double cv = mean > 0.0 ? deviation / mean * 100.0 : 0.0;
  const double median = values[count / 2];
  printf("%s,%s,%s,%zu,%zu,%zu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
         backend, variant, kernel, work_items, count, inner, values[0],
         median, values[count - 1], mean, deviation, cv,
         median / (double)work_items);
}

typedef void (*runner_fn)(void *);

static void measure(runner_fn runner, void *context, const void *output,
                    size_t output_bytes, size_t samples, size_t inner,
                    size_t warmup, double *values) {
  for (size_t sample = 0; sample < warmup; sample++) {
    runner(context);
  }
  for (size_t sample = 0; sample < samples; sample++) {
    const uint64_t start = cycles_begin();
    for (size_t repetition = 0; repetition < inner; repetition++) {
      runner(context);
    }
    values[sample] = (double)(cycles_end() - start) / (double)inner;
    if (output_bytes != 0) {
      const uint8_t *bytes = output;
      output_sink ^= bytes[(sample * 131U) % output_bytes];
    }
  }
}

enum operation {
  OP_MUL,
  OP_MUL_ADD,
  OP_SQR,
  OP_INV,
  OP_MATRIX,
  OP_GENERATE_LINEAR,
  OP_MPC_AFFINE_SETUP,
  OP_MPC_FROBENIUS,
  OP_MPC_BATCH,
};

static const struct operation_info {
  const char *name;
  enum operation operation;
  int party_batch;
} operations[] = {
    {"gf_mul", OP_MUL, 0},
    {"gf_mul_add", OP_MUL_ADD, 0},
    {"gf_sqr", OP_SQR, 0},
    {"gf_inv", OP_INV, 0},
    {"gf_mat_vec", OP_MATRIX, 0},
    {"aim3_generate_linear", OP_GENERATE_LINEAR, 0},
    {"aim3_mpc_affine_setup", OP_MPC_AFFINE_SETUP, 1},
    {"aim3_mpc_frobenius", OP_MPC_FROBENIUS, 1},
    {"aim3_mpc_batch", OP_MPC_BATCH, 1},
};

struct context {
  const struct cause_api *api;
  enum operation operation;
  void *out;
  const void *left;
  const void *right;
  const void *matrix;
  const void *linear;
  const void *tapes;
  const void *ciphertext;
};

static void run_operation(void *raw) {
  struct context *context = raw;
  switch (context->operation) {
  case OP_MUL:
    context->api->mul(context->out, context->left, context->right);
    break;
  case OP_MUL_ADD:
    context->api->mul_add(context->out, context->left, context->right);
    break;
  case OP_SQR:
    context->api->sqr(context->out, context->left);
    break;
  case OP_INV:
    context->api->inv(context->out, context->left);
    break;
  case OP_MATRIX:
    context->api->matrix(context->out, context->left, context->matrix);
    break;
  case OP_GENERATE_LINEAR:
    context->api->generate_linear(context->out, context->left);
    break;
  case OP_MPC_AFFINE_SETUP:
    context->api->mpc_affine_setup(context->out, context->linear,
                                   context->tapes, context->ciphertext);
    break;
  case OP_MPC_FROBENIUS:
    context->api->mpc_frobenius(context->out, context->linear,
                                context->tapes, context->ciphertext);
    break;
  case OP_MPC_BATCH:
    context->api->mpc_batch(context->out, context->linear, context->tapes,
                            context->ciphertext);
    break;
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

static const struct operation_info *find_operation(const char *name) {
  for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); i++) {
    if (strcmp(name, operations[i].name) == 0) {
      return &operations[i];
    }
  }
  return NULL;
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

static int metadata_equal(const struct cause_api *reference,
                          const struct cause_api *selected) {
  return reference->words() == selected->words() &&
         reference->bits() == selected->bits() &&
         reference->parties() == selected->parties() &&
         reference->sboxes() == selected->sboxes() &&
         reference->repetitions() == selected->repetitions() &&
         reference->exponent_sum() == selected->exponent_sum() &&
         reference->tape_bytes() == selected->tape_bytes() &&
         reference->linear_bytes() == selected->linear_bytes() &&
         reference->check_bytes() == selected->check_bytes();
}

static int cpu_supports_avx512(void) {
  return OQS_CPU_has_extension(OQS_CPU_EXT_AES) &&
         OQS_CPU_has_extension(OQS_CPU_EXT_AVX2) &&
         OQS_CPU_has_extension(OQS_CPU_EXT_BMI2) &&
         OQS_CPU_has_extension(OQS_CPU_EXT_PCLMULQDQ) &&
         OQS_CPU_has_extension(OQS_CPU_EXT_POPCNT) &&
         OQS_CPU_has_extension(OQS_CPU_EXT_AVX512) &&
         OQS_CPU_has_extension(OQS_CPU_EXT_AVX512VL) &&
         OQS_CPU_has_extension(OQS_CPU_EXT_VPCLMULQDQ);
}

static int benchmark(const struct parameter_set *set,
                     const struct cause_api *api,
                     const struct operation_info *operation, size_t samples,
                     size_t inner, size_t warmup, double *values) {
  const struct cause_api *reference = &set->api[0];
  const size_t field_bytes = api->words() * sizeof(uint64_t);
  const size_t matrix_bytes = api->bits() * field_bytes;
  const size_t checks_bytes = api->parties() * api->check_bytes();
  const size_t tapes_bytes = api->parties() * api->tape_bytes();
  const size_t output_bytes =
      operation->operation == OP_GENERATE_LINEAR
          ? api->linear_bytes()
          : (operation->party_batch ? checks_bytes : field_bytes);

  void *left = allocate_aligned(field_bytes);
  void *right = allocate_aligned(field_bytes);
  void *matrix = allocate_aligned(matrix_bytes);
  void *linear = allocate_aligned(api->linear_bytes());
  void *tapes = allocate_aligned(tapes_bytes);
  void *ciphertext = allocate_aligned(field_bytes);
  void *out = allocate_aligned(output_bytes);
  void *expected = allocate_aligned(output_bytes);
  if (left == NULL || right == NULL || matrix == NULL || linear == NULL ||
      tapes == NULL || ciphertext == NULL || out == NULL || expected == NULL) {
    free(left); free(right); free(matrix); free(linear); free(tapes);
    free(ciphertext); free(out); free(expected);
    return -1;
  }

  fill_random(left, field_bytes);
  fill_random(right, field_bytes);
  fill_random(matrix, matrix_bytes);
  fill_random(linear, api->linear_bytes());
  fill_random(tapes, tapes_bytes);
  fill_random(ciphertext, field_bytes);
  fill_random(out, output_bytes);
  memcpy(expected, out, output_bytes);

  struct context ref_context = {reference,
                                operation->operation,
                                expected,
                                left,
                                right,
                                matrix,
                                linear,
                                tapes,
                                ciphertext};
  struct context context = {api,
                            operation->operation,
                            out,
                            left,
                            right,
                            matrix,
                            linear,
                            tapes,
                            ciphertext};
  run_operation(&ref_context);
  run_operation(&context);
  if (memcmp(expected, out, output_bytes) != 0) {
    fprintf(stderr, "diagnostic mismatch: %s %s %s\n", set->variant,
            api->backend, operation->name);
    free(left); free(right); free(matrix); free(linear); free(tapes);
    free(ciphertext); free(out); free(expected);
    return -1;
  }

  measure(run_operation, &context, out, output_bytes, samples, inner, warmup,
          values);
  emit_csv(values, samples, api->backend, set->variant, operation->name,
           operation->party_batch ? api->parties() : 1, inner);

  free(left); free(right); free(matrix); free(linear); free(tapes);
  free(ciphertext); free(out); free(expected);
  return 0;
}

static void usage(const char *program) {
  fprintf(stderr,
          "usage: %s <ref|avx512> <AIMER-v3-variant> <samples> <inner> "
          "[warmup [kernel]]\n",
          program);
}

int main(int argc, char **argv) {
  if (argc < 5 || argc > 7) {
    usage(argv[0]);
    return 2;
  }
  const struct parameter_set *set = find_set(argv[2]);
  const int backend = strcmp(argv[1], "ref") == 0
                          ? 0
                          : (strcmp(argv[1], "avx512") == 0 ? 1 : -1);
  size_t samples, inner, warmup = 5;
  const struct operation_info *filter =
      argc == 7 ? find_operation(argv[6]) : NULL;
  if (set == NULL || backend < 0 || parse_positive(argv[3], &samples) != 0 ||
      parse_positive(argv[4], &inner) != 0 ||
      (argc >= 6 && parse_positive(argv[5], &warmup) != 0) ||
      (argc == 7 && filter == NULL)) {
    usage(argv[0]);
    return 2;
  }

  OQS_init();
  if (backend == 1 && !cpu_supports_avx512()) {
    fprintf(stderr, "backend avx512 is not supported by this CPU\n");
    OQS_destroy();
    return 2;
  }
  const struct cause_api *api = &set->api[backend];
  if (!metadata_equal(&set->api[0], api)) {
    fprintf(stderr, "diagnostic metadata mismatch: %s %s\n", set->variant,
            api->backend);
    OQS_destroy();
    return 1;
  }
  double *values = malloc(samples * sizeof(*values));
  if (values == NULL) {
    OQS_destroy();
    return 1;
  }

  int failed = 0;
  for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); i++) {
    if (filter != NULL && filter != &operations[i]) {
      continue;
    }
    if (benchmark(set, api, &operations[i], samples, inner, warmup, values) !=
        0) {
      failed = 1;
      break;
    }
  }

  free(values);
  OQS_destroy();
  return failed;
}
