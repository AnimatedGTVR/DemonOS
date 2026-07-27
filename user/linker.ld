OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    image PT_LOAD FLAGS(5);
}

SECTIONS
{
    . = 0x300000;
    .text :
    {
        *(.text .text.*)
        *(.rodata .rodata.*)
    } :image

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note .note.*)
    }
}
