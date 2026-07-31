.section .text
.code64
.global _start
.type _start, @function
_start:
    lea compositor_service(%rip), %rdi
    call counter_client_main
    mov %rax, %rdi
    mov $2, %eax
    int $0x80
1:  hlt
    jmp 1b
.section .rodata
compositor_service:
    .ascii "desktop.compositor"
.section .note.GNU-stack,"",@progbits
