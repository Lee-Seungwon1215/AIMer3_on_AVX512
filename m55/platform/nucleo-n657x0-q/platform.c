// SPDX-License-Identifier: MIT

#include "m55_platform.h"

#include <stm32n6xx_hal.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SEMIHOST_SYS_WRITEC 0x03u
#define SEMIHOST_SYS_WRITE0 0x04u
#define SEMIHOST_SYS_OPEN 0x01u
#define SEMIHOST_SYS_CLOSE 0x02u
#define SEMIHOST_SYS_READ 0x06u
#define SEMIHOST_REPORT_EXCEPTION 0x18u
#define SEMIHOST_APPLICATION_EXIT 0x20026u
#define SYSTICK_PERIOD_CYCLES (UINT32_C(1) << 24)
#define STACK_CANARY UINT32_C(0xa5a5a5a5)
#define STACK_FILL_GUARD 256u

static uint32_t measurement_start;
static uint64_t measurement_start64;
static volatile uint64_t systick_wraps;
static char *heap_end;
static uintptr_t heap_peak;
static uintptr_t stack_fill_end;

void Error_Handler(void);

static uint32_t semihosting_call(uint32_t operation, uintptr_t argument)
{
  register uint32_t r0 __asm__("r0") = operation;
  register uintptr_t r1 __asm__("r1") = argument;

  __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
  return r0;
}

int m55_host_open_read(const char *path)
{
  uintptr_t arguments[3];
  arguments[0] = (uintptr_t)path;
  arguments[1] = 0u; /* semihosting mode "r" */
  arguments[2] = strlen(path);
  return (int)semihosting_call(SEMIHOST_SYS_OPEN, (uintptr_t)arguments);
}

size_t m55_host_read(int handle, void *buffer, size_t length)
{
  uintptr_t arguments[3];
  arguments[0] = (uintptr_t)handle;
  arguments[1] = (uintptr_t)buffer;
  arguments[2] = length;
  const uint32_t unread =
      semihosting_call(SEMIHOST_SYS_READ, (uintptr_t)arguments);
  if ((unread == UINT32_MAX) || (unread > length))
  {
    return 0;
  }
  return length - unread;
}

int m55_host_close(int handle)
{
  const uintptr_t argument = (uintptr_t)handle;
  return (int)semihosting_call(SEMIHOST_SYS_CLOSE, (uintptr_t)&argument);
}

int __wrap__write(int file, const char *buffer, int length)
{
  if ((file != 1) && (file != 2))
  {
    errno = EBADF;
    return -1;
  }

  const int original_length = length;
  while (length > 0)
  {
    char chunk[128];
    int chunk_length = length;
    if (chunk_length > (int)sizeof(chunk) - 1)
    {
      chunk_length = (int)sizeof(chunk) - 1;
    }
    memcpy(chunk, buffer, (size_t)chunk_length);
    chunk[chunk_length] = '\0';
    (void)semihosting_call(SEMIHOST_SYS_WRITE0, (uintptr_t)chunk);
    buffer += chunk_length;
    length -= chunk_length;
  }
  return original_length;
}

int __wrap__read(int file, char *buffer, int length)
{
  (void)file;
  (void)buffer;
  (void)length;
  errno = ENOSYS;
  return -1;
}

extern char _sheap;
extern char _eheap;

void *_sbrk(ptrdiff_t increment)
{
  char *const previous = (heap_end == NULL) ? &_sheap : heap_end;

  if ((increment > 0) && ((uintptr_t)previous + (uintptr_t)increment >
                          (uintptr_t)&_eheap))
  {
    errno = ENOMEM;
    return (void *)-1;
  }
  if ((increment < 0) && ((uintptr_t)(previous + increment) <
                          (uintptr_t)&_sheap))
  {
    errno = EINVAL;
    return (void *)-1;
  }

  heap_end = previous + increment;
  if ((uintptr_t)heap_end > heap_peak)
  {
    heap_peak = (uintptr_t)heap_end;
  }
  return previous;
}

static void benchmark_clock_config(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  if (HAL_PWREx_ConfigSupply(PWR_EXTERNAL_SOURCE_SUPPLY) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  osc.HSIState = RCC_HSI_ON;
  osc.HSIDiv = RCC_HSI_DIV1;
  osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  osc.PLL1.PLLState = RCC_PLL_NONE;
  osc.PLL2.PLLState = RCC_PLL_NONE;
  osc.PLL3.PLLState = RCC_PLL_NONE;
  osc.PLL4.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_RCC_GetClockConfig(&clk);
  if ((clk.CPUCLKSource == RCC_CPUCLKSOURCE_IC1) ||
      (clk.SYSCLKSource == RCC_SYSCLKSOURCE_IC2_IC6_IC11))
  {
    clk.ClockType = RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_SYSCLK;
    clk.CPUCLKSource = RCC_CPUCLKSOURCE_HSI;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    if (HAL_RCC_ClockConfig(&clk) != HAL_OK)
    {
      Error_Handler();
    }
  }

  memset(&osc, 0, sizeof(osc));
  osc.OscillatorType = RCC_OSCILLATORTYPE_NONE;
  osc.PLL1.PLLState = RCC_PLL_ON;
  osc.PLL1.PLLSource = RCC_PLLSOURCE_HSI;
  osc.PLL1.PLLM = 4;
  osc.PLL1.PLLN = 75;
  osc.PLL1.PLLFractional = 0;
  osc.PLL1.PLLP1 = 1;
  osc.PLL1.PLLP2 = 1;
  osc.PLL2.PLLState = RCC_PLL_NONE;
  osc.PLL3.PLLState = RCC_PLL_NONE;
  osc.PLL4.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    Error_Handler();
  }

  memset(&clk, 0, sizeof(clk));
  clk.ClockType = RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_HCLK |
                  RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
                  RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK4 |
                  RCC_CLOCKTYPE_PCLK5;
  clk.CPUCLKSource = RCC_CPUCLKSOURCE_IC1;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_IC2_IC6_IC11;
  clk.AHBCLKDivider = RCC_HCLK_DIV2;
  clk.APB1CLKDivider = RCC_APB1_DIV1;
  clk.APB2CLKDivider = RCC_APB2_DIV1;
  clk.APB4CLKDivider = RCC_APB4_DIV1;
  clk.APB5CLKDivider = RCC_APB5_DIV1;
  clk.IC1Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  clk.IC1Selection.ClockDivider = 2;
  clk.IC2Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  clk.IC2Selection.ClockDivider = 3;
  clk.IC6Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  clk.IC6Selection.ClockDivider = 4;
  clk.IC11Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  clk.IC11Selection.ClockDivider = 3;
  if (HAL_RCC_ClockConfig(&clk) != HAL_OK)
  {
    Error_Handler();
  }
}

static void benchmark_pmu_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  ARM_PMU_Disable();
  ARM_PMU_CYCCNT_Reset();
#if defined(AIMER_PHASE_PROFILE)
  ARM_PMU_Set_EVTYPER(0u, ARM_PMU_L1I_CACHE_REFILL);
  ARM_PMU_Set_EVTYPER(1u, ARM_PMU_L1D_CACHE_REFILL);
  ARM_PMU_EVCNTR_ALL_Reset();
#endif
  ARM_PMU_Enable();
  ARM_PMU_CNTR_Enable(PMU_CNTENSET_CCNTR_ENABLE_Msk
#if defined(AIMER_PHASE_PROFILE)
                      | 3u
#endif
  );
}

void SysTick_Handler(void)
{
  ++systick_wraps;
  (void)SysTick->CTRL; /* Clear COUNTFLAG for the overflow just handled. */
}

#if defined(AIMER_BENCHMARK)
static void benchmark_long_timer_init(void)
{
  systick_wraps = 0u;
  SysTick->CTRL = 0u;
  SysTick->LOAD = SYSTICK_PERIOD_CYCLES - 1u;
  SysTick->VAL = 0u;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
  NVIC_SetPriority(SysTick_IRQn, (1u << __NVIC_PRIO_BITS) - 1u);
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk |
                  SysTick_CTRL_ENABLE_Msk;
  __enable_irq();
}
#endif

static uint64_t benchmark_long_cycle_count(void)
{
  const uint32_t saved_primask = __get_PRIMASK();
  uint64_t wraps;
  uint32_t value;

  __disable_irq();
  __DSB();
  __ISB();

  wraps = systick_wraps;
  if ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) != 0u)
  {
    ++wraps;
    systick_wraps = wraps;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
  }
  value = SysTick->VAL;

  /* Cover the narrow race in which SysTick wraps after VAL was sampled. */
  if ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) != 0u)
  {
    ++wraps;
    systick_wraps = wraps;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    value = SysTick->VAL;
  }

  if (saved_primask == 0u)
  {
    __enable_irq();
  }
  return wraps * SYSTICK_PERIOD_CYCLES +
         (SYSTICK_PERIOD_CYCLES - 1u - value);
}

extern char _sdata;
extern char _ebss;
extern char _sstack;
extern char _estack;

#if defined(AIMER_BENCHMARK)
static void benchmark_stack_canary_init(void)
{
  const uint32_t saved_primask = __get_PRIMASK();
  uintptr_t end = (uintptr_t)__get_MSP();
  if (end > STACK_FILL_GUARD)
  {
    end -= STACK_FILL_GUARD;
  }
  end &= ~(uintptr_t)(sizeof(uint32_t) - 1u);
  if (end < (uintptr_t)&_sstack)
  {
    end = (uintptr_t)&_sstack;
  }

  __disable_irq();
  for (uint32_t *word = (uint32_t *)(uintptr_t)&_sstack;
       (uintptr_t)word < end; ++word)
  {
    *word = STACK_CANARY;
  }
  __DSB();
  stack_fill_end = end;
  if (saved_primask == 0u)
  {
    __enable_irq();
  }
}
#endif

uint32_t m55_cycle_count(void)
{
  return ARM_PMU_Get_CCNTR();
}

void m55_measure_start(void)
{
  __DSB();
  __ISB();
  measurement_start = ARM_PMU_Get_CCNTR();
}

uint32_t m55_measure_end(void)
{
  const uint32_t finish = ARM_PMU_Get_CCNTR();
  return finish - measurement_start;
}

void m55_measure_start64(void)
{
  measurement_start64 = benchmark_long_cycle_count();
}

uint64_t m55_measure_end64(void)
{
  return benchmark_long_cycle_count() - measurement_start64;
}

uint64_t m55_cycle_count64(void)
{
  return benchmark_long_cycle_count();
}

uint32_t m55_icache_refill_count(void)
{
  return ARM_PMU_Get_EVCNTR(0u);
}

uint32_t m55_dcache_refill_count(void)
{
  return ARM_PMU_Get_EVCNTR(1u);
}

uint32_t m55_long_timer_hz(void)
{
  return SystemCoreClock;
}

uint32_t m55_cpu_hz(void)
{
  return SystemCoreClock;
}

size_t m55_static_ram_bytes(void)
{
  return (size_t)((uintptr_t)&_ebss - (uintptr_t)&_sdata);
}

size_t m55_stack_reserved_bytes(void)
{
  return (size_t)((uintptr_t)&_estack - (uintptr_t)&_sstack);
}

size_t m55_stack_peak_bytes(void)
{
  uintptr_t first_used = (uintptr_t)&_sstack;
  while (first_used < stack_fill_end &&
         *(const uint32_t *)first_used == STACK_CANARY)
  {
    first_used += sizeof(uint32_t);
  }
  if (first_used == stack_fill_end)
  {
    first_used = stack_fill_end;
  }
  return (size_t)((uintptr_t)&_estack - first_used);
}

size_t m55_heap_capacity_bytes(void)
{
  return (size_t)((uintptr_t)&_eheap - (uintptr_t)&_sheap);
}

size_t m55_heap_peak_bytes(void)
{
  if (heap_peak <= (uintptr_t)&_sheap)
  {
    return 0u;
  }
  return (size_t)(heap_peak - (uintptr_t)&_sheap);
}

extern int __real_main(void);

int __wrap_main(void)
{
  SCB_EnableICache();
  SCB_EnableDCache();

  if (HAL_Init() != HAL_OK)
  {
    Error_Handler();
  }
  benchmark_clock_config();

  __HAL_RCC_AXISRAM1_MEM_CLK_ENABLE();
  __HAL_RCC_AXISRAM2_MEM_CLK_ENABLE();
  __HAL_RCC_AXISRAM3_MEM_CLK_ENABLE();
  __HAL_RCC_AXISRAM4_MEM_CLK_ENABLE();
  __HAL_RCC_AXISRAM5_MEM_CLK_ENABLE();
  __HAL_RCC_AXISRAM6_MEM_CLK_ENABLE();

  /* AXISRAM2..6 can remain in shutdown after reset.  Enabling only the RCC
   * memory clock leaves AXISRAM3..6 reads undefined, which corrupts the malloc
   * arena used by the full-memory test layout.  Power the heap banks on through
   * their RAMCFG instances before the first allocation. */
  __HAL_RCC_RAMCFG_CLK_ENABLE();
  CLEAR_BIT(RAMCFG_SRAM3_AXI->CR, RAMCFG_CR_SRAMSD);
  CLEAR_BIT(RAMCFG_SRAM4_AXI->CR, RAMCFG_CR_SRAMSD);
  CLEAR_BIT(RAMCFG_SRAM5_AXI->CR, RAMCFG_CR_SRAMSD);
  CLEAR_BIT(RAMCFG_SRAM6_AXI->CR, RAMCFG_CR_SRAMSD);
  __DSB();
  __ISB();

  SysTick->CTRL = 0u;
  __disable_irq();
  benchmark_pmu_init();
#if defined(AIMER_BENCHMARK)
  benchmark_long_timer_init();
#endif

  printf("AIMer v3 on NUCLEO-N657X0-Q\n");
  printf("Cortex-M55 @ %lu Hz, MVE=%lu, I/D-cache=on\n",
         (unsigned long)SystemCoreClock,
         (unsigned long)SCB_GetMVEType());

#if defined(AIMER_BENCHMARK)
  benchmark_stack_canary_init();
#endif
  const int result = __real_main();
  printf("TEST_EXIT=%d\n", result);
  fflush(stdout);
  (void)semihosting_call(SEMIHOST_REPORT_EXCEPTION,
                         SEMIHOST_APPLICATION_EXIT);
  for (;;)
  {
    __WFI();
  }
}

void Error_Handler(void)
{
  static const char message[] = "STM32N657 platform initialization failed\n";
  for (size_t i = 0; i < sizeof(message) - 1; ++i)
  {
    (void)semihosting_call(SEMIHOST_SYS_WRITEC,
                           (uintptr_t)&message[i]);
  }
  __disable_irq();
  for (;;)
  {
    __WFI();
  }
}
