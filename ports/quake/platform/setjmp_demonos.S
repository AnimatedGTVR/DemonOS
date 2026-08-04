/* setjmp_demonos.S -- freestanding x86-64 setjmp/longjmp for the Quake port.

   host.c uses setjmp(host_abortserver) around the frame loop and
   longjmp() from Host_Error; with no libc the ABI is provided here.  The
   jmp_buf layout is private to this file (glibc's opaque __jmp_buf_tag is
   large enough for the eight saved registers), so setjmp and longjmp are
   only ever paired with each other.
*/
.section .text
.code64

.global setjmp
.type setjmp, @function
setjmp:
    movq %rbx,  0(%rdi)
    movq %rbp,  8(%rdi)
    movq %r12, 16(%rdi)
    movq %r13, 24(%rdi)
    movq %r14, 32(%rdi)
    movq %r15, 40(%rdi)
    leaq 8(%rsp), %rdx
    movq %rdx, 48(%rdi)
    movq (%rsp), %rdx
    movq %rdx, 56(%rdi)
    xorl %eax, %eax
    ret
.size setjmp, .-setjmp

.global _setjmp
.type _setjmp, @function
.set _setjmp, setjmp

.global longjmp
.type longjmp, @function
longjmp:
    movl %esi, %eax
    testl %eax, %eax
    jnz 1f
    movl $1, %eax
1:
    movq  0(%rdi), %rbx
    movq  8(%rdi), %rbp
    movq 16(%rdi), %r12
    movq 24(%rdi), %r13
    movq 32(%rdi), %r14
    movq 40(%rdi), %r15
    movq 48(%rdi), %rsp
    movq 56(%rdi), %rdx
    jmp *%rdx
.size longjmp, .-longjmp

.section .note.GNU-stack,"",@progbits
