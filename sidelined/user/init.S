.section .text
.code64
.global _start
.type _start, @function
_start:
    # RDI is the PID supplied by the kernel. The remaining arguments are
    # addresses inside this ELF image; all operating-system work happens in
    # the typed MKO user_main function.
    lea user_message(%rip), %rsi
    lea project_name(%rip), %rdx
    lea project_data(%rip), %rcx
    lea ipc_name(%rip), %r8
    lea ipc_message(%rip), %r9
    call user_main

    # user_main exits through MAKO-ABI and normally never returns. Keep this
    # tiny fallback so a future SDK regression terminates instead of running
    # into adjacent read-only data.
    mov %rax, %rdi
    mov $2, %eax
    int $0x80
1:
    hlt
    jmp 1b

.section .rodata
user_message:
    .ascii "Hello from an MKO ELF64 process!\n"
project_name:
    .ascii "project.mko"
project_data:
    .ascii "PORTABLE-PROJECT"
ipc_name:
    .ascii "desktop.test"
ipc_message:
    .ascii "IPC-OK!!"

.section .note.GNU-stack,"",@progbits
