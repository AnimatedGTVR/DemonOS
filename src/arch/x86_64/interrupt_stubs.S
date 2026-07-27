.section .text
.code64

.macro PUSH_GPRS
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
.endm

.macro POP_GPRS
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
.endm

.global interrupt_breakpoint_stub
.type interrupt_breakpoint_stub, @function
interrupt_breakpoint_stub:
    PUSH_GPRS
    call interrupt_breakpoint_handler
    POP_GPRS
    iretq
.size interrupt_breakpoint_stub, .-interrupt_breakpoint_stub

.global interrupt_page_fault_stub
.type interrupt_page_fault_stub, @function
interrupt_page_fault_stub:
    PUSH_GPRS
    mov %rsp, %rsi
    mov 120(%rsp), %rdi
    sub $8, %rsp
    call interrupt_page_fault_handler
1:
    cli
    hlt
    jmp 1b
.size interrupt_page_fault_stub, .-interrupt_page_fault_stub

.macro IRQ_STUB name, handler
.global \name
.type \name, @function
\name:
    PUSH_GPRS
    call \handler
    POP_GPRS
    iretq
.size \name, .-\name
.endm

IRQ_STUB interrupt_keyboard_stub, interrupt_keyboard_handler
IRQ_STUB interrupt_mouse_stub, interrupt_mouse_handler

.global interrupt_timer_stub
.type interrupt_timer_stub, @function
interrupt_timer_stub:
    PUSH_GPRS
    mov %rsp, %rdi
    call interrupt_timer_handler
    mov %rax, %rsp
    POP_GPRS
    iretq
.size interrupt_timer_stub, .-interrupt_timer_stub

.global interrupt_syscall_stub
.type interrupt_syscall_stub, @function
interrupt_syscall_stub:
    PUSH_GPRS
    mov %rsp, %rdi
    call syscall_dispatch
    test %rax, %rax
    jz 1f
    mov %rax, %rsp
    POP_GPRS
    iretq
1:
    mov user_kernel_rsp(%rip), %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    xor %eax, %eax
    ret
.size interrupt_syscall_stub, .-interrupt_syscall_stub

.global user_resume_ring3
.type user_resume_ring3, @function
user_resume_ring3:
    # Preserve the System V callee-saved kernel context. The user program owns
    # its GPR values and the exit syscall deliberately skips the user frame.
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, user_kernel_rsp(%rip)
    mov %rdi, %rsp
    POP_GPRS
    iretq
.size user_resume_ring3, .-user_resume_ring3

.section .bss
.align 8
.global user_kernel_rsp
user_kernel_rsp:
    .quad 0

.section .note.GNU-stack,"",@progbits
