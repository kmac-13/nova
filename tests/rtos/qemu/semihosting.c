/*
 * semihosting.c — semihosting exit helper
 *
 * On ARM targets running under QEMU with -semihosting, semihostingExit()
 * issues an ANGEL_SWI call (SYS_EXIT) that QEMU intercepts and uses as
 * the host process exit code.  This is the only reliable way to communicate
 * pass/fail from a bare-metal binary back to CI.
 *
 * On hosted (POSIX simulator) builds the semihosting mechanism is not used;
 * semihostingExit() is provided as a weak no-op so the same test source
 * compiles on both targets.  The test calls exit() directly on POSIX.
 *
 * References:
 *   ARM IHI0031F §8.3 — SYS_EXIT (0x18)
 *   QEMU semihosting: docs/about/deprecated.rst, hw/semihosting/arm-compat-semi.c
 */

#include "semihosting.h"

// ============================================================================
// ARM semihosting implementation
// ============================================================================

#if defined( __arm__ ) || defined( __ARM_ARCH )

// SYS_EXIT operation code (ARM semihosting spec §8.3)
#define SEMIHOSTING_SYS_EXIT 0x18UL

// ADP_Stopped_ApplicationExit — normal application exit reason
#define ADP_STOPPED_APPLICATION_EXIT 0x20026UL

typedef struct SemihostExitBlock
{
	unsigned long reason;
	unsigned long exitCode;
} SemihostExitBlock;

void semihostingExit( int exitCode )
{
	// the SYS_EXIT block must be in memory that QEMU can read, and
	// volatile prevents the compiler from eliding the struct before the
	// SWI is issued
	volatile SemihostExitBlock block =
	{
		.reason   = ADP_STOPPED_APPLICATION_EXIT,
		.exitCode = ( unsigned long ) exitCode
	};

	// issue ANGEL_SWI (SVC #0xAB on Thumb).  r0 = operation, r1 = &block.
	// this call does not return under QEMU; the infinite loop is a backstop
	// for hardware targets where semihosting is not connected
	__asm__ volatile
	(
		"mov r0, %[op]\n"
		"mov r1, %[blk]\n"
		"bkpt #0xAB \n"   // ANGEL_SWI Thumb encoding
		:
		: [op]  "r" ( SEMIHOSTING_SYS_EXIT ),
		  [blk] "r" ( &block )
		: "r0", "r1", "memory"
	);

	// should not reach here under QEMU; halt on hardware
	for( ;; )
	{
	}
}

// ============================================================================
// POSIX stub — normal process exit is used instead
// ============================================================================

#else // !__arm__

// provided so the same test source file compiles on the POSIX port
// (the test calls exit() directly; this is never invoked)
void semihostingExit( int exitCode )
{
	( void ) exitCode;
}

#endif // __arm__
