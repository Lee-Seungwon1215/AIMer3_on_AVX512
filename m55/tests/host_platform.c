// SPDX-License-Identifier: MIT

#include "m55_platform.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

int m55_host_open_read(const char *path)
{
  return open(path, O_RDONLY);
}

size_t m55_host_read(int handle, void *buffer, size_t length)
{
  const ssize_t result = read(handle, buffer, length);
  return (result > 0) ? (size_t)result : 0u;
}

int m55_host_close(int handle)
{
  return close(handle);
}

uint32_t m55_cycle_count(void)
{
  return 0;
}

void m55_measure_start(void)
{
}

uint32_t m55_measure_end(void)
{
  return 0;
}
