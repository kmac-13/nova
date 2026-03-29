#pragma once
#ifndef NOVA_RTOS_SEMIHOSTING_H
#define NOVA_RTOS_SEMIHOSTING_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Exit via ARM semihosting SYS_EXIT on ARM/QEMU targets.
 *
 * Issues ANGEL_SWI (bkpt #0xAB) with the ADP_Stopped_ApplicationExit reason
 * block.  QEMU intercepts the breakpoint and uses @p exitCode as the host
 * process exit code, which GitHub Actions reads as the step result.
 *
 * On POSIX simulator builds this is a no-op stub; call exit() directly instead.
 *
 * Does not return on ARM under QEMU.  On bare hardware without a connected
 * debugger the implementation spins in an infinite loop.
 *
 * @param exitCode  0 = pass, non-zero = fail.
 */
void semihostingExit( int exitCode );

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // NOVA_RTOS_SEMIHOSTING_H
