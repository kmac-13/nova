/*
 * semihosting.c - semihosting exit helper
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
 *   ARM IHI0031F §8.3 - SYS_EXIT (0x18)
 *   QEMU semihosting: docs/about/deprecated.rst, hw/semihosting/arm-compat-semi.c
 */

#include "semihosting.h"

// ============================================================================
// ARM semihosting implementation
// ============================================================================

#if defined( __arm__ ) || defined( __ARM_ARCH )

// SYS_EXIT operation code (ARM semihosting spec §8.3)
#define SEMIHOSTING_SYS_EXIT 0x18UL

// ADP_Stopped_ApplicationExit: normal exit reason, QEMU maps this to host exit(0)
#define ADP_STOPPED_APPLICATION_EXIT 0x20026UL

void semihostingExit( int exitCode )
{
	// For 32-bit ARM, SYS_EXIT (0x18) takes the reason code directly in r1,
	// not a pointer to a block.  QEMU maps ADP_Stopped_ApplicationExit to
	// host exit(0); any other reason code maps to host exit(1).
	// SYS_EXIT_EXTENDED (0x20) supports arbitrary exit codes via a block,
	// but requires probing for the extension first.  Since CI only needs
	// pass/fail, using the two standard reason codes is sufficient.
	const unsigned long reason = ( exitCode == 0 )
		? ADP_STOPPED_APPLICATION_EXIT  // -> QEMU host exit(0)
		: 0x20023UL;                    // ADP_Stopped_RunTimeError -> exit(1)

	__asm__ volatile
	(
		"mov r0, %[op]\n"
		"mov r1, %[rsn]\n"
		"bkpt #0xAB\n"
		:
		: [op]  "r" ( SEMIHOSTING_SYS_EXIT ),
		  [rsn] "r" ( reason )
		: "r0", "r1", "memory"
	);

	for( ;; ) {}
}

// ============================================================================
// POSIX stub - normal process exit is used instead
// ============================================================================

#else // !__arm__

// provided so the same test source file compiles on the POSIX port
// (the test calls exit() directly; this is never invoked)
void semihostingExit( int exitCode )
{
	( void ) exitCode;
}

#endif // __arm__
