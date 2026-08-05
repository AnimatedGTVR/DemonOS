.section .text
.code64
.global _start
.type _start, @function
_start:
    lea demonx_service(%rip), %rdi
    lea demonx_reply(%rip), %rsi
    call demonx_main
    mov %rax, %rdi
    mov $2, %eax
    int $0x80
1:  hlt
    jmp 1b

.section .rodata
demonx_service:
    .ascii "demonx.display.0"
demonx_reply:
    .ascii "demonx.reply.1"
.section .note.GNU-stack,"",@progbits
