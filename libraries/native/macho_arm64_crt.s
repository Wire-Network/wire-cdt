.global start
.global ____putc
.global __mmap
.global _setjmp
.global _longjmp

start:
   mov x29, sp
   ldr x0, [sp]
   add x1, sp, #8
   bl __wrap_main
   mov x16, #1
   svc #0x80

____putc:
   sub sp, sp, #16
   strb w0, [sp]
   mov x0, #1
   mov x1, sp
   mov x2, #1
   mov x16, #4
   svc #0x80
   add sp, sp, #16
   ret

__mmap:
   mov x0, #0
   /* 100 MiB initial WASM linear memory size. */
   movz x1, #0x640, lsl #16
   mov x2, #3
   mov x3, #0x1002
   mov x4, #-1
   mov x5, #0
   mov x16, #197
   svc #0x80
   ret

/* AAPCS64 callee-saved state: x19-x30, SP, and low 64 bits of d8-d15. */
_setjmp:
   stp x19, x20, [x0, #0]
   stp x21, x22, [x0, #16]
   stp x23, x24, [x0, #32]
   stp x25, x26, [x0, #48]
   stp x27, x28, [x0, #64]
   stp x29, x30, [x0, #80]
   mov x1, sp
   str x1, [x0, #96]
   stp d8, d9, [x0, #104]
   stp d10, d11, [x0, #120]
   stp d12, d13, [x0, #136]
   stp d14, d15, [x0, #152]
   mov x0, #0
   ret

_longjmp:
   mov x9, x0
   mov x2, #1
   cmp x1, #0
   csel x0, x1, x2, ne
   ldp x19, x20, [x9, #0]
   ldp x21, x22, [x9, #16]
   ldp x23, x24, [x9, #32]
   ldp x25, x26, [x9, #48]
   ldp x27, x28, [x9, #64]
   ldp x29, x30, [x9, #80]
   ldr x2, [x9, #96]
   ldp d8, d9, [x9, #104]
   ldp d10, d11, [x9, #120]
   ldp d12, d13, [x9, #136]
   ldp d14, d15, [x9, #152]
   mov sp, x2
   ret
