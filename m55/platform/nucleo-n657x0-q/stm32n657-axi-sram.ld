/* SPDX-License-Identifier: MIT */

ENTRY(Reset_Handler)

_Min_Heap_Size = 0x1000;
_Min_Stack_Size = 0x20000;

/*
 * Debug/SRAM execution layout for end-to-end AIMer tests.
 *
 * Keep code at ST's verified AXISRAM2 FSBL address.  The original template
 * reserves 256 KiB for data at 0x341C0000.  Keep startup data and the stack in
 * that already-active window.  The malloc arena starts at 0x34200000 and may
 * use AXISRAM3..6 only after platform.c has enabled their clocks.
 */
MEMORY
{
  ROM     (xrw) : ORIGIN = 0x34180400, LENGTH = 255K
  BOOTRAM (xrw) : ORIGIN = 0x341C0000, LENGTH = 256K
  HEAPRAM (xrw) : ORIGIN = 0x34200000, LENGTH = 1792K
}

_estack = ORIGIN(BOOTRAM) + LENGTH(BOOTRAM);
_sstack = _estack - _Min_Stack_Size;

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(1024);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > ROM

  .text :
  {
    . = ALIGN(4);
    *(.text .text*)
    *(.glue_7 .glue_7t)
    *(.eh_frame)
    *(.RamFunc .RamFunc*)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } > ROM

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata .rodata*)
    . = ALIGN(4);
  } > ROM

  .ARM.extab (READONLY) :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } > ROM

  .ARM.exidx (READONLY) :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > ROM

  .preinit_array (READONLY) :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } > ROM

  .init_array (READONLY) :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } > ROM

  .fini_array (READONLY) :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } > ROM

  .gnu.sgstubs :
  {
    . = ALIGN(32);
    *(.gnu.sgstubs*)
    . = ALIGN(32);
  } > ROM

  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data .data*)
    . = ALIGN(4);
    _edata = .;
  } > BOOTRAM AT > ROM

  .noncacheable (NOLOAD) :
  {
    . = ALIGN(32);
    __snoncacheable = .;
    KEEP(*(.noncacheable))
    . = ALIGN(32);
    __enoncacheable = .;
  } > BOOTRAM

  .bss (NOLOAD) :
  {
    . = ALIGN(8);
    _sbss = .;
    __bss_start__ = .;
    *(.bss .bss*)
    *(COMMON)
    . = ALIGN(8);
    _ebss = .;
    __bss_end__ = .;
  } > BOOTRAM

  ._user_stack _sstack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . += _Min_Stack_Size;
    . = ALIGN(8);
  } > BOOTRAM

  ._user_heap (NOLOAD) :
  {
    . = ALIGN(8);
    _sheap = .;
    . += _Min_Heap_Size;
    . = ALIGN(8);
    _eheap = ORIGIN(HEAPRAM) + LENGTH(HEAPRAM);
  } > HEAPRAM

  ASSERT(_ebss <= _sstack, "BOOTRAM data overlaps the stack")

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
