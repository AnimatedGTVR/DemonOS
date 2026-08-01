#include "wad.h"

#define WAD_HEADER_SIZE 12u
#define WAD_DIRECTORY_ENTRY_SIZE 16u

static uint32_t read_le32(const uint8_t bytes[4]) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8u) |
        ((uint32_t)bytes[2] << 16u) |
        ((uint32_t)bytes[3] << 24u);
}

static bool read_exact(struct demon_port_file *file, size_t offset,
                       uint8_t *bytes, size_t length) {
    return demon_port_seek(file, offset) &&
        demon_port_read(file, bytes, length) == length;
}

enum doom_wad_result doom_wad_validate(struct demon_port_file *file,
                                       struct doom_wad_info *info) {
    if (file == NULL || info == NULL) return DOOM_WAD_IO;
    if (file->size < WAD_HEADER_SIZE) return DOOM_WAD_TRUNCATED;
    uint8_t header[WAD_HEADER_SIZE];
    if (!read_exact(file, 0u, header, sizeof(header))) return DOOM_WAD_IO;
    const bool iwad = header[0] == 'I' && header[1] == 'W' &&
        header[2] == 'A' && header[3] == 'D';
    const bool pwad = header[0] == 'P' && header[1] == 'W' &&
        header[2] == 'A' && header[3] == 'D';
    if (!iwad && !pwad) return DOOM_WAD_BAD_MAGIC;

    const uint32_t lump_count = read_le32(&header[4]);
    const uint32_t directory_offset = read_le32(&header[8]);
    if ((size_t)directory_offset > file->size ||
        (size_t)lump_count > (file->size - (size_t)directory_offset) /
            WAD_DIRECTORY_ENTRY_SIZE)
        return DOOM_WAD_BAD_DIRECTORY;

    uint8_t entry[WAD_DIRECTORY_ENTRY_SIZE];
    for (uint32_t index = 0u; index < lump_count; ++index) {
        const size_t offset = (size_t)directory_offset +
            (size_t)index * WAD_DIRECTORY_ENTRY_SIZE;
        if (!read_exact(file, offset, entry, sizeof(entry))) return DOOM_WAD_IO;
        const uint32_t lump_offset = read_le32(&entry[0]);
        const uint32_t lump_size = read_le32(&entry[4]);
        if ((size_t)lump_offset > file->size ||
            (size_t)lump_size > file->size - (size_t)lump_offset)
            return DOOM_WAD_BAD_LUMP;
    }
    info->lump_count = lump_count;
    info->directory_offset = directory_offset;
    info->is_iwad = iwad;
    return DOOM_WAD_OK;
}
