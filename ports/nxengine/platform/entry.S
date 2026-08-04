.section .text
.code64
.global _start
.type _start, @function
_start:
    /* Run global C++ constructors (.init_array) before main -- see the
       comment in linker.ld. Without this, static objects with
       non-trivial constructors (e.g. sprites.cpp's "static StringList
       sheetfiles;") are left as raw zeroed BSS instead of their
       constructed initial state. */
    lea __init_array_start(%rip), %rbx
    lea __init_array_end(%rip), %r12
1:  cmp %r12, %rbx
    jae 2f
    call *(%rbx)
    add $8, %rbx
    jmp 1b
2:  call nxengine_core_main
    mov %rax, %rdi
    mov $2, %eax
    int $0x80
1:  hlt
    jmp 1b
.section .note.GNU-stack,"",@progbits
