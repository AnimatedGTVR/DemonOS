.section .text
.code64
.global _start
.type _start, @function
_start:
    lea project_message(%rip), %rdi
    mov $(project_message_end-project_message), %rsi
    call project_main
    mov %rax, %rdi
    mov $2, %eax
    int $0x80
1:
    hlt
    jmp 1b

.section .rodata
project_message:
    .ascii "Hello from a portable MAKO project!\n"
project_message_end:

.section .note.GNU-stack,"",@progbits
