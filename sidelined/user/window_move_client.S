.section .text
.code64
.global _start
.type _start, @function
_start:
    # window_id=4 is a fixed test-harness constant: the boot test in
    # src/kernel.c spawns this client only after arranging for the window it
    # targets to have been created with a deterministic pid of 4 in the same
    # boot run. The compositor side of MOVE dispatch is fully dynamic (it
    # looks the id up in its live window table, see compositor.mko); only
    # this small harness binary's target is a build-time constant.
    lea compositor_service(%rip), %rdi
    mov $4, %esi
    call window_move_client_main
    mov %rax, %rdi
    mov $2, %eax
    int $0x80
1:  hlt
    jmp 1b
.section .rodata
compositor_service:
    .ascii "desktop.compositor"
.section .note.GNU-stack,"",@progbits
