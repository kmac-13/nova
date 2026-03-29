/*
 * startup_cortex_m3.c - minimal Cortex-M3 startup for mps2-an385 under QEMU
 *
 * Provides:
 *   - Initial stack pointer and vector table in the .isr_vector section
 *   - Reset_Handler: copies .data from flash, zeroes .bss, calls main()
 *   - Default_Handler: infinite loop backstop for unexpected exceptions
 *   - Weak aliases for every standard Cortex-M3 fault/IRQ slot
 *
 * All exception slots fall through to Default_Handler; none are overridden
 * (no OS present, no peripheral drivers needed for the test binary).
 *
 * This file is compiled only for the ARM cross-compile target.
 */

#include <stdint.h>
#include <string.h>

// provided by the linker script
extern uint32_t _estack;  // initial stack pointer (top of RAM)
extern uint32_t _sdata;   // .data destination start (RAM)
extern uint32_t _edata;   // .data destination end   (RAM)
extern uint32_t _sidata;  // .data source start      (flash LMA)
extern uint32_t _sbss;    // .bss start
extern uint32_t _ebss;    // .bss end

// forward declaration
int main( void );

// ============================================================================
// default exception handler - infinite loop,
// all unhandled vectors are aliased here
// ============================================================================

void Default_Handler( void )
{
	for( ;; )
	{
	}
}

// ============================================================================
// reset handler
// ============================================================================

void Reset_Handler( void )
{
	// copy .data initializers from flash (LMA) to RAM (VMA)
	uint32_t* src = &_sidata;
	uint32_t* dst = &_sdata;
	while ( dst < &_edata )
	{
		*dst++ = *src++;
	}

	// zero .bss
	dst = &_sbss;
	while ( dst < &_ebss )
	{
		*dst++ = 0;
	}

	// run C++ static constructors (.init_array section);
	// without this, objects with non-trivial constructors (e.g. std::atomic,
	// Nova's Logger<Tag>::_sink) are left uninitialised before main() runs
	extern void __libc_init_array( void );
	__libc_init_array();

	// call application entry point; should not return
	( void ) main();

	// Backstop if main() returns unexpectedly
	for( ;; )
	{
	}
}

// ============================================================================
// weak aliases for Cortex-M3 system exception slots
// FreeRTOS port replaces SVC_Handler, PendSV_Handler, SysTick_Handler
// ============================================================================

void NMI_Handler( void )         __attribute__( ( weak, alias( "Default_Handler" ) ) );
void HardFault_Handler( void )   __attribute__( ( weak, alias( "Default_Handler" ) ) );
void MemManage_Handler( void )   __attribute__( ( weak, alias( "Default_Handler" ) ) );
void BusFault_Handler( void )    __attribute__( ( weak, alias( "Default_Handler" ) ) );
void UsageFault_Handler( void )  __attribute__( ( weak, alias( "Default_Handler" ) ) );
void SVC_Handler( void )         __attribute__( ( weak, alias( "Default_Handler" ) ) );
void DebugMon_Handler( void )    __attribute__( ( weak, alias( "Default_Handler" ) ) );
void PendSV_Handler( void )      __attribute__( ( weak, alias( "Default_Handler" ) ) );
void SysTick_Handler( void )     __attribute__( ( weak, alias( "Default_Handler" ) ) );

/* lm3s6965evb peripheral IRQs (slots 16-58) - all defaulted */
void GPIOA_Handler( void )       __attribute__( ( weak, alias( "Default_Handler" ) ) );
void GPIOB_Handler( void )       __attribute__( ( weak, alias( "Default_Handler" ) ) );
void GPIOC_Handler( void )       __attribute__( ( weak, alias( "Default_Handler" ) ) );
void GPIOD_Handler( void )       __attribute__( ( weak, alias( "Default_Handler" ) ) );
void GPIOE_Handler( void )       __attribute__( ( weak, alias( "Default_Handler" ) ) );
void UART0_Handler( void )       __attribute__( ( weak, alias( "Default_Handler" ) ) );
void UART1_Handler( void )       __attribute__( ( weak, alias( "Default_Handler" ) ) );
void SSI0_Handler( void )        __attribute__( ( weak, alias( "Default_Handler" ) ) );
void I2C0_Handler( void )        __attribute__( ( weak, alias( "Default_Handler" ) ) );
void PWMFault_Handler( void )    __attribute__( ( weak, alias( "Default_Handler" ) ) );
void PWM0_Handler( void )        __attribute__( ( weak, alias( "Default_Handler" ) ) );
void PWM1_Handler( void )        __attribute__( ( weak, alias( "Default_Handler" ) ) );
void PWM2_Handler( void )        __attribute__( ( weak, alias( "Default_Handler" ) ) );
void QEI0_Handler( void )        __attribute__( ( weak, alias( "Default_Handler" ) ) );
void ADC0SS0_Handler( void )     __attribute__( ( weak, alias( "Default_Handler" ) ) );
void ADC0SS1_Handler( void )     __attribute__( ( weak, alias( "Default_Handler" ) ) );
void ADC0SS2_Handler( void )     __attribute__( ( weak, alias( "Default_Handler" ) ) );
void ADC0SS3_Handler( void )     __attribute__( ( weak, alias( "Default_Handler" ) ) );
void WDT_Handler( void )         __attribute__( ( weak, alias( "Default_Handler" ) ) );
void TIMER0A_Handler( void )     __attribute__( ( weak, alias( "Default_Handler" ) ) );
void TIMER0B_Handler( void )     __attribute__( ( weak, alias( "Default_Handler" ) ) );
void TIMER1A_Handler( void )     __attribute__( ( weak, alias( "Default_Handler" ) ) );
void TIMER1B_Handler( void )     __attribute__( ( weak, alias( "Default_Handler" ) ) );
void TIMER2A_Handler( void )     __attribute__( ( weak, alias( "Default_Handler" ) ) );
void TIMER2B_Handler( void )     __attribute__( ( weak, alias( "Default_Handler" ) ) );
void COMP0_Handler( void )       __attribute__( ( weak, alias( "Default_Handler" ) ) );
void COMP1_Handler( void )       __attribute__( ( weak, alias( "Default_Handler" ) ) );
void COMP2_Handler( void )       __attribute__( ( weak, alias( "Default_Handler" ) ) );
void SYSCTL_Handler( void )      __attribute__( ( weak, alias( "Default_Handler" ) ) );
void FLASH_Handler( void )       __attribute__( ( weak, alias( "Default_Handler" ) ) );
void GPIOF_Handler( void )       __attribute__( ( weak, alias( "Default_Handler" ) ) );
void GPIOG_Handler( void )       __attribute__( ( weak, alias( "Default_Handler" ) ) );
void GPIOH_Handler( void )       __attribute__( ( weak, alias( "Default_Handler" ) ) );
void UART2_Handler( void )       __attribute__( ( weak, alias( "Default_Handler" ) ) );
void SSI1_Handler( void )        __attribute__( ( weak, alias( "Default_Handler" ) ) );
void TIMER3A_Handler( void )     __attribute__( ( weak, alias( "Default_Handler" ) ) );
void TIMER3B_Handler( void )     __attribute__( ( weak, alias( "Default_Handler" ) ) );
void I2C1_Handler( void )        __attribute__( ( weak, alias( "Default_Handler" ) ) );
void QEI1_Handler( void )        __attribute__( ( weak, alias( "Default_Handler" ) ) );
void CAN0_Handler( void )        __attribute__( ( weak, alias( "Default_Handler" ) ) );
void CAN1_Handler( void )        __attribute__( ( weak, alias( "Default_Handler" ) ) );
void CAN2_Handler( void )        __attribute__( ( weak, alias( "Default_Handler" ) ) );
void Ethernet_Handler( void )    __attribute__( ( weak, alias( "Default_Handler" ) ) );
void Hibernate_Handler( void )   __attribute__( ( weak, alias( "Default_Handler" ) ) );

// ============================================================================
// vector table
// must be placed at 0x00000000 by the linker script (.isr_vector section),
// Cortex-M3 TRM §B1.5.3: entry 0 = initial SP, entry 1 = Reset_Handler
// ============================================================================

typedef void ( *VectorEntry )( void );

__attribute__( ( section( ".isr_vector" ) ) )
const VectorEntry g_vectorTable[] =
{
	// entry 0: initial stack pointer (cast from linker symbol address)
	( VectorEntry ) &_estack,

	// entry 1: reset handler
	Reset_Handler,

	// Cortex-M3 system exceptions (entries 2-15)
	NMI_Handler,
	HardFault_Handler,
	MemManage_Handler,
	BusFault_Handler,
	UsageFault_Handler,
	0,                   // reserved
	0,                   // reserved
	0,                   // reserved
	0,                   // reserved
	SVC_Handler,
	DebugMon_Handler,
	0,                   // reserved
	PendSV_Handler,
	SysTick_Handler,

	// lm3s6965evb peripheral IRQs (entries 16-58)
	GPIOA_Handler,
	GPIOB_Handler,
	GPIOC_Handler,
	GPIOD_Handler,
	GPIOE_Handler,
	UART0_Handler,
	UART1_Handler,
	SSI0_Handler,
	I2C0_Handler,
	PWMFault_Handler,
	PWM0_Handler,
	PWM1_Handler,
	PWM2_Handler,
	QEI0_Handler,
	ADC0SS0_Handler,
	ADC0SS1_Handler,
	ADC0SS2_Handler,
	ADC0SS3_Handler,
	WDT_Handler,
	TIMER0A_Handler,
	TIMER0B_Handler,
	TIMER1A_Handler,
	TIMER1B_Handler,
	TIMER2A_Handler,
	TIMER2B_Handler,
	COMP0_Handler,
	COMP1_Handler,
	COMP2_Handler,
	SYSCTL_Handler,
	FLASH_Handler,
	GPIOF_Handler,
	GPIOG_Handler,
	GPIOH_Handler,
	UART2_Handler,
	SSI1_Handler,
	TIMER3A_Handler,
	TIMER3B_Handler,
	I2C1_Handler,
	QEI1_Handler,
	CAN0_Handler,
	CAN1_Handler,
	CAN2_Handler,
	Ethernet_Handler,
	Hibernate_Handler,
};
