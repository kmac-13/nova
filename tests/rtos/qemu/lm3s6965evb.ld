/**
 * lm3s6965evb.ld — linker script for lm3s6965evb Cortex-M3 under QEMU
 *
 * Memory map (LM3S6965 datasheet, Table 2-4):
 *   Flash: 0x00000000, 256K  (QEMU lm3s6965evb models 256K flash)
 *   SRAM:  0x20000000, 64K
 *
 * The FreeRTOS heap (heap_4.c) carves from the ucHeap[] array in .bss,
 * sized by configTOTAL_HEAP_SIZE in FreeRTOSConfig.h (32K).  The remaining
 * RAM holds .data, .bss, and the MSP stack.
 *
 * Stack: placed at the top of RAM; _estack is loaded into the vector table
 * entry 0 so the Cortex-M3 core sets MSP on reset.  2K is ample for the
 * minimal test binary (FreeRTOS scheduler + one task + Nova stack builder).
 */

_stack_size = 0x800;  /* 2K MSP (initial/interrupt) stack */

MEMORY
{
    FLASH ( rx) : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

ENTRY( Reset_Handler )

SECTIONS
{
	/* ------------------------------------------------------------------ */
	/* vector table and code in flash */
	/* ------------------------------------------------------------------ */

	.isr_vector :
	{
		. = ALIGN( 4 );
		KEEP( *(.isr_vector) )
		. = ALIGN( 4 );
	} > FLASH

	.text :
	{
		. = ALIGN( 4 );
		*(.text)
		*(.text*)
		*(.rodata)
		*(.rodata*)
		*(.glue_7)
		*(.glue_7t)
		*(.eh_frame)

		KEEP( *(.init) )
		KEEP( *(.fini) )

		. = ALIGN( 4 );
		_etext = .;
	} > FLASH

	/* ARM unwinding tables (kept for completeness; unused without exceptions) */
	.ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
	.ARM       : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH

	/* constructors/destructors (newlib-nano calls __libc_init_array) */
	.preinit_array :
	{
		PROVIDE_HIDDEN( __preinit_array_start = . );
		KEEP( *(.preinit_array*) )
		PROVIDE_HIDDEN( __preinit_array_end = . );
	} > FLASH

	.init_array :
	{
		PROVIDE_HIDDEN( __init_array_start = . );
		KEEP( *(SORT(.init_array.*)) )
		KEEP( *(.init_array*) )
		PROVIDE_HIDDEN( __init_array_end = . );
	} > FLASH

	.fini_array :
	{
		PROVIDE_HIDDEN( __fini_array_start = . );
		KEEP( *(SORT(.fini_array.*)) )
		KEEP( *(.fini_array*) )
		PROVIDE_HIDDEN( __fini_array_end = . );
	} > FLASH

	/* ------------------------------------------------------------------ */
	/* initialised data: stored in flash (LMA), copied to RAM (VMA) */
	/* ------------------------------------------------------------------ */

	_sidata = LOADADDR( .data );

	.data :
	{
		. = ALIGN( 4 );
		_sdata = .;
		*(.data)
		*(.data*)
		. = ALIGN( 4 );
		_edata = .;
	} > RAM AT > FLASH

	/* ------------------------------------------------------------------ */
	/* zero-initialised data (BSS) in RAM */
	/* ------------------------------------------------------------------ */

	.bss :
	{
		. = ALIGN( 4 );
		_sbss = .;
		__bss_start__ = _sbss;
		*(.bss)
		*(.bss*)
		*(COMMON)
		. = ALIGN( 4 );
		_ebss = .;
		__bss_end__ = _ebss;
	} > RAM

	/* ------------------------------------------------------------------ */
	/* MSP stack reservation at the top of RAM */
	/* _estack is entry 0 of the vector table (initial stack pointer) */
	/* ------------------------------------------------------------------ */

	._user_heap_stack :
	{
		. = ALIGN( 8 );
		. = . + _stack_size;
		. = ALIGN( 8 );
	} > RAM

	_estack = ORIGIN( RAM ) + LENGTH( RAM );

	/* ------------------------------------------------------------------ */
	/* discard C++ exception handling tables (not needed without -fexceptions) */
	/* ------------------------------------------------------------------ */

	/DISCARD/ :
	{
		libc.a ( * )
		libm.a ( * )
		libgcc.a ( * )
	}
}
