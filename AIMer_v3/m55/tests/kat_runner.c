// SPDX-License-Identifier: MIT

#include "api.h"
#include "common/rng.h"
#include "m55_platform.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_MESSAGE_LENGTH 3300u
#define HOST_READ_BUFFER_LENGTH 32768u
#define STRINGIFY_INNER(value) #value
#define STRINGIFY(value) STRINGIFY_INNER(value)
#define KAT_PATH "../KAT/aimer-" STRINGIFY(PARAMS) \
                 "/PQCsignKAT_" STRINGIFY(CRYPTO_SECRETKEYBYTES) ".rsp"

#if defined(AIMER_KAT_LOW_MEMORY)
#define AIMER_WRAP_INNER(name) __wrap_##name
#define AIMER_WRAP(name) AIMER_WRAP_INNER(name)
int AIMER_WRAP(crypto_sign)(uint8_t *sm, size_t *smlen, const uint8_t *m,
                            size_t mlen, const uint8_t *ctx, size_t ctxlen,
                            const uint8_t *sk);
int AIMER_WRAP(crypto_sign_open)(uint8_t *m, size_t *mlen, const uint8_t *sm,
                                 size_t smlen, const uint8_t *ctx,
                                 size_t ctxlen, const uint8_t *pk);
#define KAT_CRYPTO_SIGN AIMER_WRAP(crypto_sign)
#define KAT_CRYPTO_OPEN AIMER_WRAP(crypto_sign_open)
#else
#define KAT_CRYPTO_SIGN crypto_sign
#define KAT_CRYPTO_OPEN crypto_sign_open
#endif

typedef struct
{
  int handle;
  uint8_t buffer[HOST_READ_BUFFER_LENGTH];
  size_t position;
  size_t available;
} host_stream;

static uint8_t expected_public_key[CRYPTO_PUBLICKEYBYTES];
static uint8_t expected_secret_key[CRYPTO_SECRETKEYBYTES];
static uint8_t actual_public_key[CRYPTO_PUBLICKEYBYTES];
static uint8_t actual_secret_key[CRYPTO_SECRETKEYBYTES];
static uint8_t seed[48];
static uint8_t message[MAX_MESSAGE_LENGTH];
static uint8_t expected_signed_message[MAX_MESSAGE_LENGTH + CRYPTO_BYTES];
static uint8_t actual_signed_message[MAX_MESSAGE_LENGTH + CRYPTO_BYTES];
static uint8_t opened_message[MAX_MESSAGE_LENGTH];

static int stream_getc(host_stream *stream)
{
  if (stream->position == stream->available)
  {
    stream->available = m55_host_read(stream->handle, stream->buffer,
                                      sizeof(stream->buffer));
    stream->position = 0;
    if (stream->available == 0)
    {
      return -1;
    }
  }
  return stream->buffer[stream->position++];
}

static int find_marker(host_stream *stream, const char *marker)
{
  size_t matched = 0;
  const size_t marker_length = strlen(marker);
  while (matched < marker_length)
  {
    const int input = stream_getc(stream);
    if (input < 0)
    {
      return -1;
    }
    if ((uint8_t)input == (uint8_t)marker[matched])
    {
      ++matched;
    }
    else
    {
      matched = ((uint8_t)input == (uint8_t)marker[0]) ? 1u : 0u;
    }
  }
  return 0;
}

static int read_decimal(host_stream *stream, size_t *value)
{
  int input;
  do
  {
    input = stream_getc(stream);
    if (input < 0)
    {
      return -1;
    }
  }
  while ((input < '0') || (input > '9'));

  size_t result = 0;
  do
  {
    result = result * 10u + (size_t)(input - '0');
    input = stream_getc(stream);
  }
  while ((input >= '0') && (input <= '9'));

  *value = result;
  return 0;
}

static int hex_value(int input)
{
  if ((input >= '0') && (input <= '9'))
  {
    return input - '0';
  }
  if ((input >= 'A') && (input <= 'F'))
  {
    return input - 'A' + 10;
  }
  if ((input >= 'a') && (input <= 'f'))
  {
    return input - 'a' + 10;
  }
  return -1;
}

static int read_hex(host_stream *stream, uint8_t *output, size_t length)
{
  for (size_t i = 0; i < length; ++i)
  {
    int high;
    do
    {
      high = stream_getc(stream);
      if (high < 0)
      {
        return -1;
      }
    }
    while (hex_value(high) < 0);

    const int low = stream_getc(stream);
    if (hex_value(low) < 0)
    {
      return -1;
    }
    output[i] = (uint8_t)((unsigned int)hex_value(high) * 16u +
                          (unsigned int)hex_value(low));
  }
  return 0;
}

static int compare_bytes(const char *field, size_t count,
                         const uint8_t *expected, const uint8_t *actual,
                         size_t length)
{
  for (size_t i = 0; i < length; ++i)
  {
    if (expected[i] != actual[i])
    {
      printf("KAT_FAIL param=%s count=%lu field=%s byte=%lu "
             "expected=%02x actual=%02x\n",
             STRINGIFY(PARAMS), (unsigned long)count, field,
             (unsigned long)i, (unsigned int)expected[i],
             (unsigned int)actual[i]);
      return -1;
    }
  }
  return 0;
}

int main(void)
{
  /* Keep the RSP read buffer out of the signer call stack.  Large semihosting
   * reads preserve the exact parser and KAT comparisons while avoiding tens
   * of thousands of debugger round trips for the multi-megabyte RSP files. */
  static host_stream stream;
  stream.handle = m55_host_open_read(KAT_PATH);
  if (stream.handle < 0)
  {
    printf("KAT_FAIL param=%s stage=open path=%s\n", STRINGIFY(PARAMS),
           KAT_PATH);
    return 1;
  }

  for (size_t expected_count = 0; expected_count < 100; ++expected_count)
  {
    size_t count;
    size_t message_length;
    size_t expected_signed_length;
    size_t actual_signed_length = 0;
    size_t opened_length = 0;

    if ((find_marker(&stream, "count = ") != 0) ||
        (read_decimal(&stream, &count) != 0) ||
        (count != expected_count) ||
        (find_marker(&stream, "seed = ") != 0) ||
        (read_hex(&stream, seed, sizeof(seed)) != 0) ||
        (find_marker(&stream, "mlen = ") != 0) ||
        (read_decimal(&stream, &message_length) != 0) ||
        (message_length > MAX_MESSAGE_LENGTH) ||
        (find_marker(&stream, "msg = ") != 0) ||
        (read_hex(&stream, message, message_length) != 0) ||
        (find_marker(&stream, "pk = ") != 0) ||
        (read_hex(&stream, expected_public_key,
                  sizeof(expected_public_key)) != 0) ||
        (find_marker(&stream, "sk = ") != 0) ||
        (read_hex(&stream, expected_secret_key,
                  sizeof(expected_secret_key)) != 0) ||
        (find_marker(&stream, "smlen = ") != 0) ||
        (read_decimal(&stream, &expected_signed_length) != 0) ||
        (expected_signed_length > sizeof(expected_signed_message)) ||
        (find_marker(&stream, "sm = ") != 0) ||
        (read_hex(&stream, expected_signed_message,
                  expected_signed_length) != 0))
    {
      printf("KAT_FAIL param=%s count=%lu stage=parse\n",
             STRINGIFY(PARAMS), (unsigned long)expected_count);
      (void)m55_host_close(stream.handle);
      return 1;
    }

    randombytes_init(seed, NULL, 256);
    if (crypto_sign_keypair(actual_public_key, actual_secret_key) != 0)
    {
      printf("KAT_FAIL param=%s count=%lu stage=keypair\n",
             STRINGIFY(PARAMS), (unsigned long)count);
      (void)m55_host_close(stream.handle);
      return 1;
    }
    if ((compare_bytes("pk", count, expected_public_key, actual_public_key,
                       sizeof(actual_public_key)) != 0) ||
        (compare_bytes("sk", count, expected_secret_key, actual_secret_key,
                       sizeof(actual_secret_key)) != 0))
    {
      (void)m55_host_close(stream.handle);
      return 1;
    }

    if (KAT_CRYPTO_SIGN(actual_signed_message, &actual_signed_length, message,
                        message_length, NULL, 0, actual_secret_key) != 0)
    {
      printf("KAT_FAIL param=%s count=%lu stage=sign\n",
             STRINGIFY(PARAMS), (unsigned long)count);
      (void)m55_host_close(stream.handle);
      return 1;
    }
    if (actual_signed_length != expected_signed_length)
    {
      printf("KAT_FAIL param=%s count=%lu field=smlen expected=%lu "
             "actual=%lu\n",
             STRINGIFY(PARAMS), (unsigned long)count,
             (unsigned long)expected_signed_length,
             (unsigned long)actual_signed_length);
      (void)m55_host_close(stream.handle);
      return 1;
    }
    if (compare_bytes("sm", count, expected_signed_message,
                      actual_signed_message, actual_signed_length) != 0)
    {
      (void)m55_host_close(stream.handle);
      return 1;
    }

    if ((KAT_CRYPTO_OPEN(opened_message, &opened_length,
                         actual_signed_message, actual_signed_length,
                         NULL, 0, actual_public_key) != 0) ||
        (opened_length != message_length) ||
        (compare_bytes("opened_message", count, message, opened_message,
                       message_length) != 0))
    {
      printf("KAT_FAIL param=%s count=%lu stage=open\n",
             STRINGIFY(PARAMS), (unsigned long)count);
      (void)m55_host_close(stream.handle);
      return 1;
    }

    printf("KAT_VECTOR_PASS param=%s count=%lu\n", STRINGIFY(PARAMS),
           (unsigned long)count);
  }

  (void)m55_host_close(stream.handle);
  printf("KAT_PASS param=%s vectors=100\n", STRINGIFY(PARAMS));
  return 0;
}
