// SPDX-License-Identifier: MIT

#include "api.h"
#include "common/rng.h"
#include "m55_platform.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MESSAGE_LENGTH 59u

static uint8_t public_key[CRYPTO_PUBLICKEYBYTES];
static uint8_t secret_key[CRYPTO_SECRETKEYBYTES];
static uint8_t signature[CRYPTO_BYTES];
static uint8_t message[MESSAGE_LENGTH];

static uint64_t checksum_bytes(uint64_t state, const uint8_t *input,
                               size_t length)
{
  for (size_t i = 0; i < length; ++i)
  {
    state ^= input[i];
    state *= UINT64_C(0x100000001b3);
  }
  return state;
}

int main(void)
{
  uint8_t entropy[48];
  size_t signature_length = 0;

  for (size_t i = 0; i < sizeof(entropy); ++i)
  {
    entropy[i] = (uint8_t)i;
  }
  for (size_t i = 0; i < sizeof(message); ++i)
  {
    message[i] = (uint8_t)(3u * i + 1u);
  }
  randombytes_init(entropy, NULL, 256);

  m55_measure_start();
  if (crypto_sign_keypair(public_key, secret_key) != 0)
  {
    printf("SIGN_TEST_FAIL param=%s stage=keypair\n", xstr(PARAMS));
    return 1;
  }
  const uint32_t keypair_cycles = m55_measure_end();

  m55_measure_start();
  if (crypto_sign_signature(signature, &signature_length, message,
                            sizeof(message), NULL, 0, secret_key) != 0)
  {
    printf("SIGN_TEST_FAIL param=%s stage=sign\n", xstr(PARAMS));
    return 1;
  }
  const uint32_t sign_cycles = m55_measure_end();

  if (signature_length != CRYPTO_BYTES)
  {
    printf("SIGN_TEST_FAIL param=%s stage=siglen actual=%lu expected=%u\n",
           xstr(PARAMS), (unsigned long)signature_length,
           (unsigned int)CRYPTO_BYTES);
    return 1;
  }

  m55_measure_start();
  if (crypto_sign_verify(signature, signature_length, message,
                         sizeof(message), NULL, 0, public_key) != 0)
  {
    printf("SIGN_TEST_FAIL param=%s stage=verify\n", xstr(PARAMS));
    return 1;
  }
  const uint32_t verify_cycles = m55_measure_end();

  uint64_t checksum = UINT64_C(0xcbf29ce484222325);
  checksum = checksum_bytes(checksum, public_key, sizeof(public_key));
  checksum = checksum_bytes(checksum, secret_key, sizeof(secret_key));
  checksum = checksum_bytes(checksum, signature, signature_length);

  signature[signature_length / 2] ^= 1u;
  if (crypto_sign_verify(signature, signature_length, message,
                         sizeof(message), NULL, 0, public_key) == 0)
  {
    printf("SIGN_TEST_FAIL param=%s stage=tamper\n", xstr(PARAMS));
    return 1;
  }

  printf("SIGN_TEST_PASS param=%s config=%s backend=%s matvec=%s "
         "keypair_cycles=%lu sign_cycles=%lu verify_cycles=%lu "
         "checksum=%08lx%08lx\n",
         xstr(PARAMS), xstr(AIMER_CONFIG), xstr(AIMER_BACKEND),
         xstr(AIMER_MATVEC), (unsigned long)keypair_cycles,
         (unsigned long)sign_cycles, (unsigned long)verify_cycles,
         (unsigned long)(checksum >> 32),
         (unsigned long)(checksum & UINT32_MAX));
  return 0;
}
