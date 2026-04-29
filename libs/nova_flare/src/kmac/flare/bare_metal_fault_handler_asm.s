@ bare_metal_fault_handler_asm.s
@
@ Reads the active MSP, PSP, and LR into caller-supplied pointers.
@ Placed in a dedicated .s file so assembler directives here take full
@ effect without fighting compiler-driver flag-forwarding to GNU as.
@
@ Prototype (C linkage):
@   void flare_read_exception_regs( uint32_t* msp, uint32_t* psp, uint32_t* lr );
@
@ Arguments (AAPCS):
@   r0 = uint32_t* msp
@   r1 = uint32_t* psp
@   r2 = uint32_t* lr_out
@
@ Only compiled on ARM targets (CMakeLists guards inclusion).

    .arch   armv7-m
    .thumb
    .syntax unified

    .text
    .align  2
    .global flare_read_exception_regs
    .type   flare_read_exception_regs, %function

flare_read_exception_regs:
    mrs     r3, msp
    str     r3, [r0]        @ *msp = MSP
    mrs     r3, psp
    str     r3, [r1]        @ *psp = PSP
    str     lr, [r2]        @ *lr_out = LR (EXC_RETURN value)
    bx      lr

    .size   flare_read_exception_regs, . - flare_read_exception_regs
