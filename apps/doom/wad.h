#ifndef DOOM_WAD_H
#define DOOM_WAD_H

#include <demon/portkit.h>
#include <stdbool.h>
#include <stdint.h>

struct doom_wad_info {
    uint32_t lump_count;
    uint32_t directory_offset;
    bool is_iwad;
};

enum doom_wad_result {
    DOOM_WAD_OK = 0,
    DOOM_WAD_IO,
    DOOM_WAD_TRUNCATED,
    DOOM_WAD_BAD_MAGIC,
    DOOM_WAD_BAD_DIRECTORY,
    DOOM_WAD_BAD_LUMP,
};

/* Validates the header, complete directory, and every lump's byte range.
   The file cursor is unspecified on return. No allocation is performed. */
enum doom_wad_result doom_wad_validate(struct demon_port_file *file,
                                       struct doom_wad_info *info);

#endif
