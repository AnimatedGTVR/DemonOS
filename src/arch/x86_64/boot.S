.set MB2_MAGIC, 0xE85250D6
.set MB2_ARCH, 0
.set MB2_BOOT_MAGIC, 0x36D76289

.section .multiboot2, "a"
.align 8
mb2_header_start:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long mb2_header_end - mb2_header_start
    .long -(MB2_MAGIC + MB2_ARCH + (mb2_header_end - mb2_header_start))
    /* Prefer a small 32-bit linear framebuffer. Optional keeps text fallback. */
    .short 5
    .short 1
    .long 20
    .long 640
    .long 480
    .long 32
    .long 0
    .short 0
    .short 0
    .long 8
mb2_header_end:

.section .boot.text, "ax"
.code32
.global _start
.type _start, @function
_start:
    cli
    movl %eax, %ebp
    movl %ebx, %esi

    cmpl $MB2_BOOT_MAGIC, %eax
    jne boot_hang

    movl $page_tables_start, %ebx
    xorl %eax, %eax
    movl $((4096 * 3) / 4), %ecx
    movl %ebx, %edi
    rep stosl

    movl $pdpt_table, %eax
    orl $0x3, %eax
    movl %eax, pml4_table

    movl $pd_table, %eax
    orl $0x3, %eax
    movl %eax, pdpt_table

    xorl %ecx, %ecx
1:
    movl %ecx, %eax
    shll $21, %eax
    orl $0x83, %eax
    movl %eax, pd_table(,%ecx,8)
    movl $0, pd_table+4(,%ecx,8)
    incl %ecx
    cmpl $512, %ecx
    jne 1b

    movl %cr4, %eax
    orl $(1 << 5), %eax
    movl %eax, %cr4

    movl $pml4_table, %eax
    movl %eax, %cr3

    movl $0xC0000080, %ecx
    rdmsr
    orl $(1 << 8), %eax
    wrmsr

    lgdt gdt64_ptr
    movl %cr0, %eax
    orl $(1 << 31), %eax
    movl %eax, %cr0

    movl %ebp, %edi
    ljmpl $0x08, $long_mode_start

boot_hang:
    hlt
    jmp boot_hang

.code64
long_mode_start:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    leaq stack_top(%rip), %rsp
    andq $-16, %rsp
    xorq %rbp, %rbp

    /* EDI = Multiboot magic, ESI = physical address of the info structure. */
    call kernel_main

2:
    cli
    hlt
    jmp 2b
.size _start, . - _start

.align 8
gdt64:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF
    .quad 0x00AF92000000FFFF
gdt64_end:
gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .long gdt64

.section .bss
.align 4096
page_tables_start:
pml4_table:
    .skip 4096
pdpt_table:
    .skip 4096
pd_table:
    .skip 4096

.align 16
stack_bottom:
    .skip 32768
stack_top:
