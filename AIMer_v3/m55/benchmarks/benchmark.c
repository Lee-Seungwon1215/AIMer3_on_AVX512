// SPDX-License-Identifier: MIT

#include "api.h"
#include "common/rng.h"
#include "field.h"
#include "m55_field_batch.h"
#include "m55_platform.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef BENCH_KERNEL_SAMPLES
#define BENCH_KERNEL_SAMPLES 31u
#endif

#ifndef BENCH_E2E_SAMPLES
#define BENCH_E2E_SAMPLES 7u
#endif

#ifndef BENCH_KERNEL_INNER
#define BENCH_KERNEL_INNER 64u
#endif

#ifndef BENCH_WARMUP
#define BENCH_WARMUP 1u
#endif

#if BENCH_KERNEL_SAMPLES < 1 || BENCH_E2E_SAMPLES < 1 || \
    BENCH_KERNEL_INNER < 1
#error "Benchmark sample and inner-loop counts must be positive"
#endif

#define MESSAGE_LENGTH 59u
#define MAX_BENCH_SAMPLES                                                   \
  ((BENCH_KERNEL_SAMPLES > BENCH_E2E_SAMPLES) ? BENCH_KERNEL_SAMPLES       \
                                               : BENCH_E2E_SAMPLES)

static uint8_t public_key[CRYPTO_PUBLICKEYBYTES];
static uint8_t secret_key[CRYPTO_SECRETKEYBYTES];
static uint8_t signature[CRYPTO_BYTES];
static uint8_t message[MESSAGE_LENGTH];
static volatile uint64_t benchmark_checksum;
static uint64_t prng_state = UINT64_C(0x8f3d9a27c4b165e0);

static uint64_t next_word(void)
{
  uint64_t value = prng_state;
  value ^= value << 13;
  value ^= value >> 7;
  value ^= value << 17;
  prng_state = value;
  return value;
}

static void fill_field(gf value)
{
  for (size_t word = 0; word < AIM3_NUM_WORDS_FIELD; ++word)
  {
    value[word] = next_word();
  }
}

static void sort_samples(uint64_t *samples, size_t count)
{
  for (size_t i = 1; i < count; ++i)
  {
    const uint64_t value = samples[i];
    size_t position = i;
    while (position > 0u && samples[position - 1u] > value)
    {
      samples[position] = samples[position - 1u];
      --position;
    }
    samples[position] = value;
  }
}

static void print_stats(const char *operation, const char *clock,
                        const uint64_t *samples, size_t count,
                        size_t inner, size_t items)
{
  uint64_t sorted[MAX_BENCH_SAMPLES];
  double mean = 0.0;
  double variance = 0.0;

  memcpy(sorted, samples, count * sizeof(*samples));
  sort_samples(sorted, count);
  for (size_t i = 0; i < count; ++i)
  {
    mean += (double)samples[i] / (double)inner;
  }
  mean /= (double)count;
  for (size_t i = 0; i < count; ++i)
  {
    const double difference = (double)samples[i] / (double)inner - mean;
    variance += difference * difference;
  }
  variance /= (double)count;

  const double minimum = (double)sorted[0] / (double)inner;
  const double maximum = (double)sorted[count - 1u] / (double)inner;
  double median;
  if ((count & 1u) != 0u)
  {
    median = (double)sorted[count / 2u] / (double)inner;
  }
  else
  {
    median = ((double)sorted[count / 2u - 1u] +
              (double)sorted[count / 2u]) /
             (2.0 * (double)inner);
  }

  printf("BENCH_RESULT,param=%s,backend=%s,operation=%s,clock=%s,"
         "samples=%lu,inner=%lu,items=%lu,min=%.2f,median=%.2f,"
         "mean=%.2f,stddev=%.2f,max=%.2f,cycles_per_item_mean=%.2f\n",
         xstr(PARAMS), xstr(AIMER_BACKEND), operation, clock,
         (unsigned long)count, (unsigned long)inner, (unsigned long)items,
         minimum, median, mean, sqrt(variance), maximum,
         mean / (double)items);
}

static int check_timers(void)
{
  gf a;
  gf b;
  gf output;
  fill_field(a);
  fill_field(b);

  m55_measure_start64();
  m55_measure_start();
  for (size_t i = 0; i < BENCH_KERNEL_INNER; ++i)
  {
    gf_mul(output, a, b);
  }
  const uint32_t pmu_cycles = m55_measure_end();
  const uint64_t timer_cycles = m55_measure_end64();
  benchmark_checksum ^= output[0];

  const uint64_t difference =
      (timer_cycles > pmu_cycles) ? timer_cycles - pmu_cycles
                                  : (uint64_t)pmu_cycles - timer_cycles;
  const uint64_t error_ppm =
      (pmu_cycles == 0u) ? UINT64_MAX : difference * 1000000u / pmu_cycles;
  printf("BENCH_TIMER_CHECK,param=%s,backend=%s,pmu_cycles=%lu,"
         "timer_cycles=%llu,error_ppm=%llu\n",
         xstr(PARAMS), xstr(AIMER_BACKEND), (unsigned long)pmu_cycles,
         (unsigned long long)timer_cycles, (unsigned long long)error_ppm);
  return error_ppm <= 20000u ? 0 : -1;
}

static void benchmark_field_kernels(void)
{
  uint64_t samples[BENCH_KERNEL_SAMPLES];
  gf a;
  gf b;
  gf output;
  gf batch_input[M55_PARTY_BATCH_LANES];
  gf batch_output[M55_PARTY_BATCH_LANES];

  for (size_t sample = 0; sample < BENCH_KERNEL_SAMPLES; ++sample)
  {
    fill_field(a);
    fill_field(b);
    m55_measure_start();
    for (size_t inner = 0; inner < BENCH_KERNEL_INNER; ++inner)
    {
      gf_mul(output, a, b);
    }
    samples[sample] = m55_measure_end();
    benchmark_checksum ^= output[sample % AIM3_NUM_WORDS_FIELD];
  }
  print_stats("gf_mul", "pmu", samples, BENCH_KERNEL_SAMPLES,
              BENCH_KERNEL_INNER, 1u);

  for (size_t sample = 0; sample < BENCH_KERNEL_SAMPLES; ++sample)
  {
    fill_field(a);
    m55_measure_start();
    for (size_t inner = 0; inner < BENCH_KERNEL_INNER; ++inner)
    {
      gf_sqr(output, a);
    }
    samples[sample] = m55_measure_end();
    benchmark_checksum ^= output[sample % AIM3_NUM_WORDS_FIELD];
  }
  print_stats("gf_sqr", "pmu", samples, BENCH_KERNEL_SAMPLES,
              BENCH_KERNEL_INNER, 1u);

  for (size_t sample = 0; sample < BENCH_KERNEL_SAMPLES; ++sample)
  {
    for (size_t lane = 0; lane < M55_PARTY_BATCH_LANES; ++lane)
    {
      fill_field(batch_input[lane]);
    }
    m55_measure_start();
    for (size_t inner = 0; inner < BENCH_KERNEL_INNER; ++inner)
    {
      m55_gf_sqr_batch4(batch_output, batch_input,
                        M55_PARTY_BATCH_LANES);
    }
    samples[sample] = m55_measure_end();
    benchmark_checksum ^=
        batch_output[sample % M55_PARTY_BATCH_LANES]
                    [sample % AIM3_NUM_WORDS_FIELD];
  }
  print_stats("gf_sqr_batch4", "pmu", samples, BENCH_KERNEL_SAMPLES,
              BENCH_KERNEL_INNER, M55_PARTY_BATCH_LANES);

  for (size_t sample = 0; sample < BENCH_KERNEL_SAMPLES; ++sample)
  {
    fill_field(b);
    for (size_t lane = 0; lane < M55_PARTY_BATCH_LANES; ++lane)
    {
      fill_field(batch_input[lane]);
    }
    m55_measure_start();
    for (size_t inner = 0; inner < BENCH_KERNEL_INNER; ++inner)
    {
      m55_gf_mul_const_batch4(batch_output, batch_input, b,
                              M55_PARTY_BATCH_LANES);
    }
    samples[sample] = m55_measure_end();
    benchmark_checksum ^=
        batch_output[sample % M55_PARTY_BATCH_LANES]
                    [sample % AIM3_NUM_WORDS_FIELD];
  }
  print_stats("gf_mul_const_batch4", "pmu", samples,
              BENCH_KERNEL_SAMPLES, BENCH_KERNEL_INNER,
              M55_PARTY_BATCH_LANES);
}

static int run_e2e(size_t count, uint64_t *keypair_samples,
                   uint64_t *sign_samples, uint64_t *verify_samples)
{
  for (size_t sample = 0; sample < count; ++sample)
  {
    size_t signature_length = 0u;
    message[0] = (uint8_t)sample;

    m55_measure_start64();
    if (crypto_sign_keypair(public_key, secret_key) != 0)
    {
      return -1;
    }
    const uint64_t keypair_cycles = m55_measure_end64();

    m55_measure_start64();
    if (crypto_sign_signature(signature, &signature_length, message,
                              sizeof(message), NULL, 0u, secret_key) != 0)
    {
      return -1;
    }
    const uint64_t sign_cycles = m55_measure_end64();
    if (signature_length != CRYPTO_BYTES)
    {
      return -1;
    }

    m55_measure_start64();
    if (crypto_sign_verify(signature, signature_length, message,
                           sizeof(message), NULL, 0u, public_key) != 0)
    {
      return -1;
    }
    const uint64_t verify_cycles = m55_measure_end64();

    benchmark_checksum ^= signature[sample % signature_length];
    if (keypair_samples != NULL)
    {
      keypair_samples[sample] = keypair_cycles;
      sign_samples[sample] = sign_cycles;
      verify_samples[sample] = verify_cycles;
    }
  }
  return 0;
}

static int benchmark_end_to_end(void)
{
  uint64_t keypair_samples[BENCH_E2E_SAMPLES];
  uint64_t sign_samples[BENCH_E2E_SAMPLES];
  uint64_t verify_samples[BENCH_E2E_SAMPLES];
  uint8_t entropy[48];

  for (size_t i = 0; i < sizeof(entropy); ++i)
  {
    entropy[i] = (uint8_t)i;
  }
  for (size_t i = 0; i < sizeof(message); ++i)
  {
    message[i] = (uint8_t)(3u * i + 1u);
  }

  randombytes_init(entropy, NULL, 256);
  if (run_e2e(BENCH_WARMUP, NULL, NULL, NULL) != 0)
  {
    return -1;
  }

  /* Keep the measured input stream independent of the warm-up count. */
  randombytes_init(entropy, NULL, 256);
  if (run_e2e(BENCH_E2E_SAMPLES, keypair_samples, sign_samples,
              verify_samples) != 0)
  {
    return -1;
  }

  print_stats("keypair", "systick-extended", keypair_samples,
              BENCH_E2E_SAMPLES, 1u, 1u);
  print_stats("sign", "systick-extended", sign_samples, BENCH_E2E_SAMPLES,
              1u, 1u);
  print_stats("verify", "systick-extended", verify_samples,
              BENCH_E2E_SAMPLES, 1u, 1u);
  return 0;
}

int main(void)
{
  printf("BENCH_CONFIG,param=%s,backend=%s,cpu_hz=%lu,long_timer_hz=%lu,"
         "kernel_samples=%u,e2e_samples=%u,kernel_inner=%u,warmup=%u\n",
         xstr(PARAMS), xstr(AIMER_BACKEND),
         (unsigned long)m55_cpu_hz(), (unsigned long)m55_long_timer_hz(),
         (unsigned int)BENCH_KERNEL_SAMPLES,
         (unsigned int)BENCH_E2E_SAMPLES,
         (unsigned int)BENCH_KERNEL_INNER, (unsigned int)BENCH_WARMUP);

  if (check_timers() != 0)
  {
    printf("BENCH_FAIL,param=%s,backend=%s,stage=timer_check\n",
           xstr(PARAMS), xstr(AIMER_BACKEND));
    return 1;
  }
  benchmark_field_kernels();
  if (benchmark_end_to_end() != 0)
  {
    printf("BENCH_FAIL,param=%s,backend=%s,stage=end_to_end\n",
           xstr(PARAMS), xstr(AIMER_BACKEND));
    return 1;
  }

  printf("BENCH_MEMORY,param=%s,backend=%s,static_ram_bytes=%lu,"
         "stack_reserved_bytes=%lu,stack_peak_bytes=%lu,"
         "heap_capacity_bytes=%lu,heap_peak_bytes=%lu\n",
         xstr(PARAMS), xstr(AIMER_BACKEND),
         (unsigned long)m55_static_ram_bytes(),
         (unsigned long)m55_stack_reserved_bytes(),
         (unsigned long)m55_stack_peak_bytes(),
         (unsigned long)m55_heap_capacity_bytes(),
         (unsigned long)m55_heap_peak_bytes());

  printf("BENCH_PASS,param=%s,backend=%s,checksum=%08lx%08lx\n",
         xstr(PARAMS), xstr(AIMER_BACKEND),
         (unsigned long)(benchmark_checksum >> 32),
         (unsigned long)(benchmark_checksum & UINT32_MAX));
  return 0;
}
