.section .text
.code64
.global _start
.type _start, @function
_start:
    # window_id=4 is a fixed test-harness constant, same one baked into
    # window_move_client.S: the boot test arranges for a window with pid 4 to
    # exist before spawning this listener. The channel name matches the
    # compositor's deterministic "desktop.winN" scheme (see
    # write_event_channel_name in compositor.mko), derived generically from
    # whatever window id is currently focused -- only this harness binary's
    # own target window is a build-time constant.
    lea event_service(%rip), %rdi
    lea delivery_marker(%rip), %rsi
    mov $4, %edx
    call window_event_client_main
    mov %rax, %rdi
    mov $2, %eax
    int $0x80
1:  hlt
    jmp 1b
.section .rodata
event_service:
    .ascii "desktop.win4"
delivery_marker:
    .ascii "WINDOW_KEYBOARD_DELIVERY_OK\n"
.section .note.GNU-stack,"",@progbits
