OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    image PT_LOAD FLAGS(7);
}

/* The Quake engine's synthetic assets and the software renderer's 2MB
   surface cache exceed the legacy 0x300000..0x33d000 layout, so the core
   links at the isolated large-app base like the full Doom port. */
SECTIONS
{
    . = 0x20000000;
    .text : { *(.text .text.*) } :image
    .rodata : { *(.rodata .rodata.*) } :image
    .data : { *(.data .data.*) } :image
    .bss : { *(.bss .bss.* COMMON) } :image

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note .note.*) }
}
