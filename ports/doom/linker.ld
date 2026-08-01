OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    image PT_LOAD FLAGS(7);
}

/* Large native ports live outside the legacy 0x300000..0x33d000 layout.
   The loader will map this image independently; PortKit's dynamic heap stays
   at 0x10000000 and existing applications retain their fixed addresses. */
SECTIONS
{
    . = 0x20000000;
    .text : { *(.text .text.*) } :image
    .rodata : { *(.rodata .rodata.*) } :image
    .data : { *(.data .data.*) } :image
    .bss : { *(.bss .bss.* COMMON) } :image

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note .note.*) }
}
