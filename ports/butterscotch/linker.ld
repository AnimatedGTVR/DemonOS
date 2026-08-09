OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    image PT_LOAD FLAGS(7);
}

/* Keep native game ports away from the legacy small-app mappings. The VM,
   decoded assets, and software framebuffer will grow into this address space
   in later stages without moving the executable or changing its ABI. */
SECTIONS
{
    . = 0x20000000;
    .text : { *(.text .text.*) } :image
    .rodata : { *(.rodata .rodata.*) } :image
    .data : { *(.data .data.*) } :image
    .bss : { *(.bss .bss.* COMMON) } :image

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note .note.*) }
}
