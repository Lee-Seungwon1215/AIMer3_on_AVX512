// SPDX-License-Identifier: MIT

#ifndef AIMER_M55_PLATFORM_H
#define AIMER_M55_PLATFORM_H

#include <stdint.h>
#include <stddef.h>

uint32_t m55_cycle_count(void);
void m55_measure_start(void);
uint32_t m55_measure_end(void);

/*
 * Long measurements extend the CPU-clocked 24-bit SysTick counter in its
 * overflow handler.  Unlike the PMU helpers, they can span any practical
 * number of 32-bit PMU cycle-counter wraps.
 */
void m55_measure_start64(void);
uint64_t m55_measure_end64(void);
uint64_t m55_cycle_count64(void);
uint32_t m55_icache_refill_count(void);
uint32_t m55_dcache_refill_count(void);
uint32_t m55_long_timer_hz(void);
uint32_t m55_cpu_hz(void);
size_t m55_static_ram_bytes(void);
size_t m55_stack_reserved_bytes(void);
size_t m55_stack_peak_bytes(void);
size_t m55_heap_capacity_bytes(void);
size_t m55_heap_peak_bytes(void);

int m55_host_open_read(const char *path);
size_t m55_host_read(int handle, void *buffer, size_t length);
int m55_host_close(int handle);

#endif
