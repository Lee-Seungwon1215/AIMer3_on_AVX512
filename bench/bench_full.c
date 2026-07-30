// Rich per-operation benchmark for one AIMER variant. Emits CSV to stdout.
// For each op (keypair/sign/verify) it collects N rdtsc samples (after a
// warmup phase that is discarded) and reports:
//   N, min, median, max, mean, std-dev, CV%, time/op (median & mean, us),
//   ops/sec. Cross-build speedup is computed afterwards by compare.py.
//
// NOTE: __rdtsc on this CPU is the invariant TSC (~base clock), so "cycles"
//       are reference-clock cycles (~ wall-time x base GHz), not retired core
//       cycles. With turbo on they still make a consistent same-condition
//       baseline; rely on the speedup ratio + CV for the comparison.
//
// usage: bench_full <build_label> <variant> <iters> [warmup]
#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <x86intrin.h>
#include <oqs/oqs.h>
#include "aimer_keccak_select.h"

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double) t.tv_sec + (double) t.tv_nsec / 1e9;
}
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *) a, y = *(const uint64_t *) b;
    return (x > y) - (x < y);
}

static void emit(uint64_t *s, size_t n, const char *build, const char *var,
                 const char *op, double cyc_per_us) {
    qsort(s, n, sizeof(uint64_t), cmp_u64);
    uint64_t mn = s[0], mx = s[n - 1], med = s[n / 2];
    double sum = 0;
    for (size_t i = 0; i < n; i++) sum += (double) s[i];
    double mean = sum / (double) n;
    double variance = 0;
    for (size_t i = 0; i < n; i++) { double d = (double) s[i] - mean; variance += d * d; }
    double sd = n > 1 ? sqrt(variance / (double) (n - 1)) : 0.0;
    double cv = mean > 0 ? sd / mean * 100.0 : 0.0;
    double med_us = (double) med / cyc_per_us;
    double mean_us = mean / cyc_per_us;
    double ops = mean_us > 0 ? 1e6 / mean_us : 0.0;
    // build,variant,op,N,min,median,max,mean,std,cv,med_us,mean_us,ops_per_sec
    printf("%s,%s,%s,%zu,%llu,%llu,%llu,%.1f,%.1f,%.2f,%.4f,%.4f,%.1f\n",
           build, var, op, n,
           (unsigned long long) mn, (unsigned long long) med, (unsigned long long) mx,
           mean, sd, cv, med_us, mean_us, ops);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <build_label> <variant> <iters> [warmup]\n", argv[0]);
        return 2;
    }
    const char *build = argv[1], *var = argv[2];
    size_t iters = strtoul(argv[3], NULL, 10);
    size_t warm = (argc > 4) ? strtoul(argv[4], NULL, 10) : (iters / 10 > 5 ? iters / 10 : 5);
    if (iters == 0) { fprintf(stderr, "iters must be > 0\n"); return 2; }

    OQS_init();
    aimer_keccak_select_from_env();
    OQS_SIG *sig = OQS_SIG_new(var);
    if (!sig) { fprintf(stderr, "variant %s not enabled\n", var); return 2; }

    uint8_t *pk = OQS_MEM_malloc(sig->length_public_key);
    uint8_t *sk = OQS_MEM_malloc(sig->length_secret_key);
    uint8_t *s  = OQS_MEM_malloc(sig->length_signature);
    uint64_t *kp = malloc(iters * sizeof(uint64_t));
    uint64_t *sg = malloc(iters * sizeof(uint64_t));
    uint64_t *vf = malloc(iters * sizeof(uint64_t));
    if (!pk || !sk || !s || !kp || !sg || !vf) { fprintf(stderr, "OOM\n"); return 2; }

    const uint8_t msg[64] = "AIMer-AVX512 benchmark message";
    const size_t mlen = sizeof(msg);
    size_t slen = 0;

    // calibrate cycles-per-microsecond (invariant TSC -> ~ base clock)
    double t0 = now_s();
    uint64_t c0 = __rdtsc();
    while (now_s() - t0 < 0.05) { /* spin */ }
    double us = (now_s() - t0) * 1e6;
    double cyc_per_us = (double) (__rdtsc() - c0) / us;

    // warmup, discarded
    for (size_t i = 0; i < warm; i++) {
        OQS_SIG_keypair(sig, pk, sk);
        OQS_SIG_sign(sig, s, &slen, msg, mlen, sk);
        OQS_SIG_verify(sig, msg, mlen, s, slen, pk);
    }

    for (size_t i = 0; i < iters; i++) {
        uint64_t a, b;
        a = __rdtsc(); OQS_SIG_keypair(sig, pk, sk);                 b = __rdtsc(); kp[i] = b - a;
        a = __rdtsc(); OQS_SIG_sign(sig, s, &slen, msg, mlen, sk);   b = __rdtsc(); sg[i] = b - a;
        a = __rdtsc(); OQS_SIG_verify(sig, msg, mlen, s, slen, pk);  b = __rdtsc(); vf[i] = b - a;
    }

    emit(kp, iters, build, var, "keypair", cyc_per_us);
    emit(sg, iters, build, var, "sign",    cyc_per_us);
    emit(vf, iters, build, var, "verify",  cyc_per_us);

    OQS_SIG_free(sig);
    OQS_destroy();
    return 0;
}
