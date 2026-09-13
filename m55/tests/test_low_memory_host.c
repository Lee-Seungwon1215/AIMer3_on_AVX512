// SPDX-License-Identifier: MIT

#include "api.h"
#include "common/rng.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AIMER_WRAP_INNER(name) __wrap_##name
#define AIMER_WRAP(name) AIMER_WRAP_INNER(name)

int AIMER_WRAP(crypto_sign_signature)(uint8_t *sig, size_t *siglen,
                                      const uint8_t *m, size_t mlen,
                                      const uint8_t *ctx, size_t ctxlen,
                                      const uint8_t *sk);
int AIMER_WRAP(crypto_sign_verify)(const uint8_t *sig, size_t siglen,
                                   const uint8_t *m, size_t mlen,
                                   const uint8_t *ctx, size_t ctxlen,
                                   const uint8_t *pk);

int main(void)
{
  uint8_t entropy[48];
  uint8_t message[73];
  uint8_t public_key[CRYPTO_PUBLICKEYBYTES];
  uint8_t secret_key[CRYPTO_SECRETKEYBYTES];
  uint8_t *const reference_signature = malloc(CRYPTO_BYTES);
  uint8_t *const low_memory_signature = malloc(CRYPTO_BYTES);
  size_t reference_length = 0;
  size_t low_memory_length = 0;

  if ((reference_signature == NULL) || (low_memory_signature == NULL))
  {
    return 1;
  }
  for (size_t i = 0; i < sizeof(entropy); ++i)
  {
    entropy[i] = (uint8_t)i;
  }
  for (size_t i = 0; i < sizeof(message); ++i)
  {
    message[i] = (uint8_t)(5u * i + 7u);
  }

  randombytes_init(entropy, NULL, 256);
  if (crypto_sign_keypair(public_key, secret_key) != 0)
  {
    printf("LOW_MEMORY_DIFF_FAIL param=%s stage=keypair\n", xstr(PARAMS));
    return 1;
  }

  for (size_t i = 0; i < sizeof(entropy); ++i)
  {
    entropy[i] ^= 0xa5u;
  }
  randombytes_init(entropy, NULL, 256);
  if (crypto_sign_signature(reference_signature, &reference_length, message,
                            sizeof(message), NULL, 0, secret_key) != 0)
  {
    printf("LOW_MEMORY_DIFF_FAIL param=%s stage=reference_sign\n",
           xstr(PARAMS));
    return 1;
  }

  randombytes_init(entropy, NULL, 256);
  if (AIMER_WRAP(crypto_sign_signature)(
          low_memory_signature, &low_memory_length, message, sizeof(message),
          NULL, 0, secret_key) != 0)
  {
    printf("LOW_MEMORY_DIFF_FAIL param=%s stage=low_memory_sign\n",
           xstr(PARAMS));
    return 1;
  }

  if ((reference_length != low_memory_length) ||
      (memcmp(reference_signature, low_memory_signature,
              reference_length) != 0))
  {
    size_t first_difference = 0;
    const size_t common_length =
        (reference_length < low_memory_length) ? reference_length
                                               : low_memory_length;
    while ((first_difference < common_length) &&
           (reference_signature[first_difference] ==
            low_memory_signature[first_difference]))
    {
      ++first_difference;
    }
    printf("LOW_MEMORY_DIFF_FAIL param=%s stage=bytes byte=%lu "
           "reference_length=%lu low_memory_length=%lu\n",
           xstr(PARAMS), (unsigned long)first_difference,
           (unsigned long)reference_length, (unsigned long)low_memory_length);
    return 1;
  }

  if (AIMER_WRAP(crypto_sign_verify)(
          low_memory_signature, low_memory_length, message, sizeof(message),
          NULL, 0, public_key) != 0)
  {
    printf("LOW_MEMORY_DIFF_FAIL param=%s stage=verify\n", xstr(PARAMS));
    return 1;
  }

  printf("LOW_MEMORY_DIFF_PASS param=%s bytes=%lu\n", xstr(PARAMS),
         (unsigned long)low_memory_length);
  free(reference_signature);
  free(low_memory_signature);
  return 0;
}
