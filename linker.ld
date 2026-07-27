OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    . = 1M;
    kernel_start = .;

    .boot ALIGN(8) :
    {
        KEEP(*(.multiboot2))
        *(.boot.text)
    }

    .text ALIGN(4K) :
    {
        *(.text .text.*)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) :
    {
        *(.data .data.*)
    }

    .bss ALIGN(4K) (NOLOAD) :
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    kernel_end = ALIGN(4K);

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note .note.*)
    }
}
