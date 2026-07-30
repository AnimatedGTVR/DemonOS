.section .text
.code64
.global _start
.type _start, @function
_start:
    lea demonx_service(%rip), %rdi
    lea demonx_reply(%rip), %rsi
    lea demonx_reply2(%rip), %rdx
    call demonx_client_main
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
demonx_reply2:
    .ascii "demonx.reply.2"
.section .note.GNU-stack,"",@progbits
