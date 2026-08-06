OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    image PT_LOAD FLAGS(7);
}

/* Same isolated large-app base as the Quake/Doom/NXEngine cores
   (ports/nxengine/linker.ld) -- one RWX segment, no separate RX/RW split,
   since every large native port here already uses that shape successfully. */
SECTIONS
{
    . = 0x20000000;
    .text : { *(.text .text.*) } :image
    .rodata : { *(.rodata .rodata.*) } :image
    .data : { *(.data .data.*) } :image
    .bss : { *(.bss .bss.* COMMON) } :image

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note .note.*) *(.rela.*) *(.got .got.*) *(.dynamic) *(.dynsym) *(.dynstr) }
}
