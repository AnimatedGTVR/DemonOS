OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    image PT_LOAD FLAGS(7);
}

/* Same isolated large-app base as the Quake/Doom cores. */
SECTIONS
{
    . = 0x20000000;
    .text : { *(.text .text.*) } :image
    .rodata : { *(.rodata .rodata.*) } :image
    .data : { *(.data .data.*) } :image
    /* Global C++ constructors (static objects with non-trivial
       constructors, e.g. sprites.cpp's "static StringList sheetfiles;").
       entry.S walks this array before calling nxengine_core_main --
       without it, such objects are left as raw zeroed BSS instead of
       their constructed initial state. */
    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(.init_array .init_array.*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } :image
    .bss : { *(.bss .bss.* COMMON) } :image

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note .note.*) }
}
