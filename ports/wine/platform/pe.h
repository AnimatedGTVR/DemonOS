#ifndef WINE_PE_H
#define WINE_PE_H

#include <demon/portkit.h>
#include <stdbool.h>
#include <stdint.h>

/* W0 of docs/wine-port.md: a real PE/COFF header parser, not a loader.
   Scoped to exactly what this project can execute -- x86-64 (PE32+) only,
   matching DemonOS's own architecture; 32-bit PE (PE32) is deliberately out
   of scope rather than half-supported. See docs/wine-port.md for why this
   is the actual starting point instead of attempting to compile upstream
   Wine C source (which assumes a POSIX host this kernel does not provide
   and is unlikely to for a long time). */

struct wine_pe_info {
    uint16_t machine;                  /* IMAGE_FILE_MACHINE_AMD64 (0x8664) */
    uint16_t section_count;
    uint16_t optional_header_size;
    uint16_t characteristics;
    uint16_t optional_magic;           /* IMAGE_NT_OPTIONAL_HDR64_MAGIC (0x20b) */
    uint32_t entry_point_rva;
    uint64_t image_base;
    uint32_t size_of_image;
    uint32_t number_of_rva_and_sizes;
    uint32_t section_table_offset;     /* absolute file offset, for a future loader */
    /* Export/import data directories (entries 0 and 1 of the optional
       header's DataDirectory array) -- 0/0 (RVA and size) when absent, or
       when the optional header declares fewer than 16 bytes past the
       standard 112-byte fields (W0/W2's own minimal test fixtures use
       exactly 112 with no directories at all; W3's fixtures use the full
       240-byte standard size so these are populated). RVAs, not file
       offsets: resolving them needs a loaded image's base (W3,
       ports/wine/platform/pe_export.c/pe_import.c), not this file-only
       parser. */
    uint32_t export_directory_rva;
    uint32_t export_directory_size;
    uint32_t import_directory_rva;
    uint32_t import_directory_size;
    /* Base relocation directory (entry 5, IMAGE_DIRECTORY_ENTRY_BASERELOC)
       -- W4, ports/wine/platform/pe_reloc.c. Same absent-when-optional-
       header-is-minimal rule as export/import above. */
    uint32_t reloc_directory_rva;
    uint32_t reloc_directory_size;
};

enum wine_pe_result {
    WINE_PE_OK = 0,
    WINE_PE_IO,
    WINE_PE_TRUNCATED,
    WINE_PE_BAD_DOS_MAGIC,
    WINE_PE_BAD_PE_OFFSET,
    WINE_PE_BAD_PE_SIGNATURE,
    WINE_PE_UNSUPPORTED_MACHINE,
    WINE_PE_BAD_OPTIONAL_MAGIC,
    WINE_PE_BAD_SECTION_TABLE,
};

/* Validates the DOS header, PE signature, COFF header, and PE32+ optional
   header, and proves every section header's file offset+size stays inside
   the file -- the same "decode fixed-width LE fields explicitly, prove
   bounds before trusting them" discipline as apps/doom/wad.c. Does not
   load, map, or execute anything. The file cursor is unspecified on
   return. No allocation is performed. */
enum wine_pe_result wine_pe_validate(struct demon_port_file *file,
                                     struct wine_pe_info *info);

#endif
