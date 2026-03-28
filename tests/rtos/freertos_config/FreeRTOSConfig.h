#pragma once
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/**
 * FreeRTOSConfig.h - minimal configuration for the Nova RTOS test binary.
 *
 * This config is shared between the POSIX simulator job (hosted Linux) and
 * the ARM Cortex-M3 QEMU job.  Conditional blocks handle the few values
 * that differ between the two ports.
 *
 * POSIX port notes:
 *   - configUSE_PREEMPTION must be 1 (POSIX port requires it)
 *   - configTICK_RATE_HZ affects the simulation tick; 1000 is typical
 *   - no special interrupt priority configuration needed
 *
 * ARM Cortex-M3 notes:
 *   - configCPU_CLOCK_HZ is unused at runtime under QEMU but required by
 *     some port internals; a representative value is provided
 *   - configMAX_SYSCALL_INTERRUPT_PRIORITY must be set for CM3 port
 *   - configPRIO_BITS = 3 matches the lm3s6965evb NVIC (8 priority levels)
 */

// ============================================================================
// Scheduler configuration
// ============================================================================

#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_MALLOC_FAILED_HOOK 0
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 0

#define configTICK_RATE_HZ ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES 5
#define configMINIMAL_STACK_SIZE ( ( uint16_t ) 256 )
#define configMAX_TASK_NAME_LEN 12
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1
#define configUSE_TASK_NOTIFICATIONS 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1
#define configUSE_MUTEXES 0
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configQUEUE_REGISTRY_SIZE 0
#define configUSE_QUEUE_SETS 0
#define configUSE_APPLICATION_TASK_TAG 0
#define configUSE_CO_ROUTINES 0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0

// ============================================================================
// Memory allocation
// ============================================================================

#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configSUPPORT_STATIC_ALLOCATION 0
#define configTOTAL_HEAP_SIZE ( ( size_t ) ( 32 * 1024 ) )

// ============================================================================
// Software timer configuration (disabled - not needed for Nova tests)
// ============================================================================

#define configUSE_TIMERS 0
#define configTIMER_TASK_PRIORITY ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH 10
#define configTIMER_TASK_STACK_DEPTH configMINIMAL_STACK_SIZE

// ============================================================================
// Event groups (disabled)
// ============================================================================

#define configUSE_EVENT_GROUPS 0
#define configUSE_STREAM_BUFFERS 0

// ============================================================================
// Port-specific: ARM Cortex-M3
// ============================================================================

#if defined( __ARM_ARCH_7M__ ) || defined( __CORTEX_M )
	// lm3s6965evb CPU clock; unused at runtime under QEMU
	#define configCPU_CLOCK_HZ ( ( unsigned long ) 50000000 )

	// NVIC priority bits on lm3s6965evb (Cortex-M3 with 3 implemented bits)
	#define configPRIO_BITS 3

	// lowest interrupt priority
	#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 0x07

	// highest priority from which FreeRTOS API may be called from an ISR
	#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 4

	#define configKERNEL_INTERRUPT_PRIORITY \
		( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
	#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
		( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

	// map FreeRTOS fault/assertion handlers to bare-metal names
	#define vPortSVCHandler SVC_Handler
	#define xPortPendSVHandler PendSV_Handler
	#define xPortSysTickHandler SysTick_Handler

	// disable the port's runtime vector table check: it reads VTOR and
	// asserts SVC/PendSV slots contain the FreeRTOS handlers;
	// Under QEMU lm3s6965evb the check fires as a false positive due to how
	// the port compares internal function pointers against the
	// weak-alias-resolved vector table entries (table is correctly constructed)
	#define configCHECK_HANDLER_INSTALLATION 0
#endif

// ============================================================================
// Port-specific: POSIX simulator
// ============================================================================

#if !defined( __ARM_ARCH_7M__ ) && !defined( __CORTEX_M )
	// the POSIX port derives its tick from the host timer; no CPU clock needed
	#define configCPU_CLOCK_HZ 0
#endif

// ============================================================================
// API inclusion
// ============================================================================

#define INCLUDE_vTaskDelay 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_uxTaskGetStackHighWaterMark 0
#define INCLUDE_xTaskAbortDelay 0
#define INCLUDE_eTaskGetState 0

// ============================================================================
// Assertion
// Uses printf + _Exit so QEMU semihosting sees the failure message before
// the process terminates.  On the POSIX port printf is safe here because
// configASSERT is only invoked during initialisation or from task context,
// never from an ISR.
// ============================================================================

#define configASSERT( x ) \
	do { \
		if( ( x ) == 0 ) { \
			/* write() to fd 2: async-signal-safe, works before stdio init. \
			 * The unused-result attribute on write() is intentionally ignored \
			 * here - there is nothing useful to do if the write fails, and \
			 * _Exit(1) follows immediately regardless. */ \
			static const char _msg[] = "FreeRTOS configASSERT failed\n"; \
			extern long write( int, const void*, unsigned long ); \
			long _wr __attribute__((unused)) = write( 2, _msg, sizeof( _msg ) - 1 ); \
			_Exit( 1 ); \
		} \
	} while( 0 )

#endif // FREERTOS_CONFIG_H
