.section .text
.code64
.global _start
.type _start, @function
_start:
    call demonx_xlib_test_main
    mov %rax, %rdi
    mov $2, %eax
    int $0x80
1:  hlt
    jmp 1b
.section .note.GNU-stack,"",@progbits
