#include "pe_reloc.h"

#define IMAGE_REL_BASED_ABSOLUTE 0u
#define IMAGE_REL_BASED_DIR64 10u
#define BLOCK_HEADER_SIZE 8u

static uint32_t read_le32_mem(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8u) |
        ((uint32_t)bytes[2] << 16u) |
        ((uint32_t)bytes[3] << 24u);
}

static uint16_t read_le16_mem(const uint8_t *bytes) {
    return (uint16_t)((uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u));
}

enum wine_pe_reloc_result wine_pe_apply_relocations(const struct wine_pe_image *image) {
    if (image == NULL) return WINE_PE_RELOC_UNSUPPORTED;
    if (image->info.reloc_directory_size == 0u) return WINE_PE_RELOC_NONE;

    const uint8_t *base = (const uint8_t *)(uintptr_t)image->base;
    const uint64_t delta = image->base - image->info.image_base;
    size_t consumed = 0u;
    while (consumed < image->info.reloc_directory_size) {
        const uint8_t *block_header = base + image->info.reloc_directory_rva + consumed;
        const uint32_t block_virtual_address = read_le32_mem(block_header + 0u);
        const uint32_t block_size = read_le32_mem(block_header + 4u);
        if (block_size < BLOCK_HEADER_SIZE) return WINE_PE_RELOC_UNSUPPORTED;

        const size_t entry_count = (block_size - BLOCK_HEADER_SIZE) / 2u;
        for (size_t i = 0u; i < entry_count; ++i) {
            const uint16_t entry =
                read_le16_mem(block_header + BLOCK_HEADER_SIZE + i * 2u);
            const uint32_t type = (uint32_t)(entry >> 12u);
            const uint32_t page_offset = (uint32_t)(entry & 0x0FFFu);
            if (type == IMAGE_REL_BASED_ABSOLUTE) continue; /* documented padding, no-op */
            if (type != IMAGE_REL_BASED_DIR64) return WINE_PE_RELOC_UNSUPPORTED;

            uint8_t *target = (uint8_t *)base + block_virtual_address + page_offset;
            uint64_t value = 0u;
            for (size_t byte = 0u; byte < 8u; ++byte)
                value |= (uint64_t)target[byte] << (byte * 8u);
            value += delta;
            for (size_t byte = 0u; byte < 8u; ++byte)
                target[byte] = (uint8_t)(value >> (byte * 8u));
        }
        consumed += block_size;
    }
    return WINE_PE_RELOC_OK;
}
