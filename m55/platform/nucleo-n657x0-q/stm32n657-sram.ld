/* SPDX-License-Identifier: MIT */

ENTRY(Reset_Handler)

_Min_Heap_Size = 0x1000;
_Min_Stack_Size = 0x10000;

/*
 * Phase-2 bring-up layout.  It deliberately matches ST's AXISRAM2 FSBL
 * template.  Larger AIMer parameter sets need a separately verified memory
 * layout before their end-to-end tests are enabled.
 */
MEMORY
{
  ROM (xrw) : ORIGIN = 0x34180400, LENGTH = 255K
  RAM (xrw) : ORIGIN = 0x341C0000, LENGTH = 256K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
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
  } > RAM AT > ROM

  .noncacheable (NOLOAD) :
  {
    . = ALIGN(32);
    __snoncacheable = .;
    KEEP(*(.noncacheable))
    . = ALIGN(32);
    __enoncacheable = .;
  } > RAM

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
  } > RAM

  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    _sheap = .;
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . += _Min_Heap_Size;
    . += _Min_Stack_Size;
    . = ALIGN(8);
  } > RAM

  _eheap = _sstack;

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
