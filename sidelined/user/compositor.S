.section .text
.code64
.global _start
.type _start, @function
_start:
    # The compositor no longer needs a boot-time fixed event-channel name: it
    # derives each window's event channel dynamically from that window's id
    # (see write_event_channel_name in compositor.mko).
    lea compositor_service(%rip), %rdi
    call compositor_main
    mov %rax, %rdi
    mov $2, %eax
    int $0x80
1:  hlt
    jmp 1b
.section .rodata
compositor_service:
    .ascii "desktop.compositor"
.section .note.GNU-stack,"",@progbits
